/*
 * ProFTPD: mod_conf_url -- a module for reading configurations from URLs
 * Copyright (c) 2020 TJ Saunders
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307, USA.
 *
 * As a special exemption, TJ Saunders and other respective copyright holders
 * give permission to link this program with OpenSSL, and distribute the
 * resulting executable, without including the source code for OpenSSL in the
 * source distribution.
 *
 * This is mod_conf_url, contrib software for proftpd 1.2 and above.
 * For more information contact TJ Saunders <tj@castaglia.org>.
 *
 * -----DO NOT EDIT BELOW THIS LINE-----
 * $Archive: mod_conf_url.a$
 * $Libraries: -lcurl$
 */

#include "mod_conf_url.h"
#include "http.h"
#include "uri.h"

/* Fake fd number for FSIO needs. */
#define URLCONF_FILENO		7642

/* Default timeouts, in secs */
#define URLCONF_CONNECT_TIMEOUT	3UL
#define URLCONF_REQUEST_TIMEOUT	10UL

module conf_url_module;
pool *urlconf_pool = NULL;

static unsigned long urlconf_flags = 0UL;

/* List of URL schemes that we will support/honor. */
static const char *urlconf_schemes[] = {
  "https://",
  "http://",
  "ftps://",
  "ftp://",
  "file://",
  NULL
};

struct urlconf_data {
  pool *pool;
  int ftps;
  int ssl_verify;

  char *ptr, *buf;
  size_t bufsz, buflen;
};

static int use_tracing = FALSE;

static const char *trace_channel = "conf_url";

/* Prototypes */
static void urlconf_fs_register(pool *p);
static void urlconf_fs_unregister(void);

static int urlconf_scheme_supported(const char *path) {
  register unsigned int i;

  for (i = 0; urlconf_schemes[i]; i++) {
    const char *scheme;
    size_t scheme_len;

    scheme = urlconf_schemes[i];
    scheme_len = strlen(scheme);

    if (strncasecmp(path, scheme, scheme_len) == 0) {
      if (urlconf_flags & URLCONF_FL_CURL_NO_SSL) {
        if (strcmp(scheme, "https://") == 0 ||
            strcmp(scheme, "ftps://") == 0) {
          continue;
        }
      }

      return TRUE;
    }
  }

  return FALSE;
}

static int urlconf_update_uri(pool *p, char **uri, pr_table_t *params) {
  const void *key;
  char *new_uri = NULL, *ptr, *query = "";

  /* Change from a "ftps://" prefix -- which libcurl will interpret as
   * an implicit FTPS connection -- to "ftp://", with SSL support.
   */
  if (strncmp(*uri, "ftps://", 7) == 0) {
    size_t uri_len;

    uri_len = strlen(*uri);
    ptr = *uri + 4;

    /* We want to subtract 4 for the "ftps" prefix, BUT include the terminating
     * NUL, so we add 1 back.
     */
    uri_len -= 3;
    memmove(ptr - 1, ptr, uri_len);
  }

  ptr = strrchr(*uri, '?');
  if (ptr != NULL) {
    *ptr = '\0';
  }

  if (pr_table_count(params) == 0) {
    return 0;
  }

  pr_table_rewind(params);
  key = pr_table_next(params);
  while (key != NULL) {
    const void *val;

    pr_signals_handle();

    val = pr_table_get(params, (const char *) key, NULL);
    if (val != NULL) {
      query = pstrcat(p, query, *query ? "&" : "", key, "=", val, NULL);
    }

    key = pr_table_next(params);
  }

  new_uri = pstrcat(p, *uri, "?", query, NULL);
  *uri = new_uri;

  return 0;
}

static int urlconf_parse_uri(pool *p, char **uri, int *ftps, int *tracing,
    int *ssl_verify) {
  int res, xerrno;
  char *scheme = NULL, *host = NULL, *path = NULL, *username, *password;
  unsigned int port = 0;
  pr_table_t *params = NULL;
  const void *v;

  params = pr_table_alloc(p, 8);

  res = urlconf_uri_parse(p, *uri, &scheme, &host, &port, &path, &username,
    &password, params);
  if (res < 0) {
    xerrno = errno;

    pr_log_debug(DEBUG0, MOD_CONF_URL_VERSION
      ": failed parsing URI '%.200s': %s", *uri, strerror(xerrno));

    pr_table_free(params);
    errno = xerrno;
    return -1;
  }

  /* URLs using a scheme of "ftps://" need to be handled carefully, setting
   * all the proper libcurl options for forcing an explicit FTPS handshake.
   */
  if (strcmp(scheme, "ftps://") == 0) {
    *ftps = TRUE;
  }

  /* Remove any of our expected parameters from the table, after handling
   * them.  Afterward, rewrite the URL query parameters, having removed
   * ours.
   */

  v = pr_table_get(params, "tracing", NULL);
  if (v != NULL) {
    res = pr_str_is_boolean(v);
    if (res == TRUE) {
      *tracing = TRUE;
      pr_trace_use_stderr(*tracing);

      /* TODO: Make the trace level a param as well. */
      pr_trace_set_levels(trace_channel, 1, 20);
    }

    (void) pr_table_remove(params, "tracing", NULL);
  }

  v = pr_table_get(params, "ssl_verify", NULL);
  if (v != NULL) {
    res = pr_str_is_boolean(v);
    if (res == FALSE) {
      *ssl_verify = FALSE;
    }

    (void) pr_table_remove(params, "ssl_verify", NULL);
  }

  urlconf_update_uri(p, uri, params);
  return 0;
}

static size_t urlconf_data_cb(char *buf, size_t itemsz, size_t item_count,
    void *user_data) {
  struct urlconf_data *data;
  size_t bufsz;
  char *ptr;

  bufsz = itemsz * item_count;
  if (bufsz == 0) {
    return 0;
  }

  data = user_data;

  if (data->bufsz > 0) {
    ptr = data->buf;

    data->ptr = data->buf = palloc(data->pool, data->bufsz + bufsz);
    memcpy(data->buf, ptr, data->bufsz);

    ptr = data->buf + data->bufsz;
    data->buf += bufsz;

  } else {
    data->bufsz = data->buflen = bufsz;
    data->ptr = data->buf = palloc(data->pool, bufsz);

    ptr = data->buf;
  }

  memcpy(ptr, buf, bufsz);
  return bufsz;
}

static int urlconf_get_data(pool *p, void *http, const char *url,
    size_t (*resp_body)(char *, size_t, size_t, void *),
    void *user_data) {
  int res;
  long resp_code;
  const char *content_type = NULL;
  pr_table_t *headers;

  headers = urlconf_http_default_headers(p);
  res = urlconf_http_get(p, http, url, headers, resp_body, user_data,
    &resp_code, &content_type);
  if (res < 0) {
    return -1;
  }

  switch (resp_code) {
    case URLCONF_FILE_RESPONSE_CODE_OK:
    case URLCONF_FTP_RESPONSE_CODE_OK:
    case URLCONF_HTTP_RESPONSE_CODE_OK:
      break;

    case URLCONF_HTTP_RESPONSE_CODE_BAD_REQUEST:
      pr_trace_msg(trace_channel, 2,
        "received %ld response code for '%s' request", resp_code, url);
      errno = EINVAL;
      return -1;

    case URLCONF_FTP_RESPONSE_CODE_NOT_LOGGED_IN:
    case URLCONF_HTTP_RESPONSE_CODE_FORBIDDEN:
      pr_trace_msg(trace_channel, 2,
        "received %ld response code for '%s' request", resp_code, url);
      errno = EACCES;
      return -1;

    case URLCONF_FTP_RESPONSE_CODE_NOT_FOUND:
    case URLCONF_HTTP_RESPONSE_CODE_NOT_FOUND:
      pr_trace_msg(trace_channel, 2,
        "received %ld response code for '%s' request", resp_code, url);
      errno = ENOENT;
      return -1;

    default:
      pr_trace_msg(trace_channel, 2,
        "received %ld response code for '%s' request", resp_code, url);
      errno = EPERM;
      return -1;
  }

  return 0;
}

/* Construct the configuration file from the URL. */
static int urlconf_read_url(pool *p, pr_fh_t *fh, const char *url) {
  int res, xerrno;
  void *http;
  unsigned long http_flags;
  struct urlconf_data *data;

  data = fh->fh_data;

  http_flags = urlconf_flags;
  if (data->ftps) {
    http_flags |= URLCONF_FL_CURL_USE_SSL;
  }

  if (data->ssl_verify == FALSE) {
    http_flags |= URLCONF_FL_CURL_NO_VERIFY;
  }

  http = urlconf_http_alloc(p, URLCONF_CONNECT_TIMEOUT,
    URLCONF_REQUEST_TIMEOUT, http_flags);
  if (http == NULL) {
    return -1;
  }

  res = urlconf_get_data(p, http, url, urlconf_data_cb, fh->fh_data);
  xerrno = errno;

  urlconf_http_destroy(p, http);

  errno = xerrno;
  return res;
}

/* FSIO callbacks
 */

static void urlconf_set_stat(struct stat *st) {
  /* Set the mode, for file type checking. */
  st->st_mode = S_IFREG;

  /* Set a default "block size". */
  st->st_blksize = 8192;
}

static int urlconf_fsio_fstat(pr_fh_t *fh, int fd, struct stat *st) {
  if (fd == URLCONF_FILENO) {
    urlconf_set_stat(st);
    return 0;
  }

  return fstat(fd, st);
}

static int urlconf_fsio_lstat(pr_fs_t *fs, const char *path, struct stat *st) {
  /* Is this a path that we can use? */
  if (urlconf_scheme_supported(path) == TRUE) {
    urlconf_set_stat(st);
    return 0;
  }

  return lstat(path, st);
}

static int urlconf_fsio_stat(pr_fs_t *fs, const char *path, struct stat *st) {
  /* Is this a path that we can use? */
  if (urlconf_scheme_supported(path) == TRUE) {
    urlconf_set_stat(st);
    return 0;
  }

  return stat(path, st);
}

static int urlconf_fsio_open(pr_fh_t *fh, const char *path, int flags) {

  /* Is this a path that we can use? */
  if (urlconf_scheme_supported(path) == TRUE) {
    pool *p;
    char *url;
    struct urlconf_data *data;
    int ftps = FALSE, ssl_verify = TRUE;

    p = make_sub_pool(fh->fh_pool);
    pr_pool_tag(p, "URL Configuration Pool");
    data = pcalloc(p, sizeof(struct urlconf_data));
    data->pool = p;
    fh->fh_data = data;

    url = pstrdup(data->pool, path);
    pr_log_debug(DEBUG10, MOD_CONF_URL_VERSION ": opening path '%s'", url);

    /* Parse through the given URI, breaking out the needed pieces. */
    if (urlconf_parse_uri(data->pool, &url, &ftps, &use_tracing,
        &ssl_verify) < 0) {
      return -1;
    }

    data->ftps = ftps;
    data->ssl_verify = ssl_verify;

    if (urlconf_read_url(data->pool, fh, url) < 0) {
      return -1;
    }

    /* Return a fake file descriptor. */
    return URLCONF_FILENO;
  }

  /* Default normal open. */
  return open(path, flags, PR_OPEN_MODE);
}

static int urlconf_fsio_close(pr_fh_t *fh, int fd) {
  if (fd == URLCONF_FILENO) {
    return 0;
  }

  return close(fd);
}

static int urlconf_fsio_read(pr_fh_t *fh, int fd, char *buf, size_t buflen) {

  /* Make sure this filehandle is for this module before trying to use it. */
  if (fd == URLCONF_FILENO &&
      fh->fh_path != NULL &&
      urlconf_scheme_supported(fh->fh_path) == TRUE) {
    struct urlconf_data *data;

    data = fh->fh_data;

    if (data->buflen > 0) {
      size_t len;

      /* Read from our built-up buffer, until there are no more data to be
       * read.
       */

      len = data->buflen;
      if (len > buflen) {
        len = buflen;
      }

      memmove(buf, data->buf, len);
      data->buf += len;
      data->buflen -= len;

      return len;
    }

    return 0;
  }

  /* Default normal read. */
  return read(fd, buf, buflen);
}

/* Event handlers
 */

#if defined(PR_SHARED_MODULE)
static void urlconf_mod_unload_ev(const void *event_data, void *user_data) {
  if (strcmp((const char *) event_data, "mod_conf_url.c") != 0) {
    return;
  }

  /* Unregister ourselves from all events. */
  pr_event_unregister(&conf_url_module, NULL, NULL);
  urlconf_fs_unregister();
  urlconf_http_free();

  destroy_pool(urlconf_pool);
  urlconf_pool = NULL;
}
#endif /* PR_SHARED_MODULE */

static void urlconf_postparse_ev(const void *event_data, void *user_data) {
  urlconf_fs_unregister();

  if (use_tracing == TRUE) {
    pr_trace_set_levels(trace_channel, 0, 0);
    pr_trace_use_stderr(FALSE);
    use_tracing = FALSE;
  }
}

static void urlconf_restart_ev(const void *event_data, void *user_data) {
  /* Register the FSes.. */
  urlconf_fs_register(urlconf_pool);
}

/* Initialization functions
 */

static void urlconf_fs_register(pool *p) {
  register unsigned int i;

  /* Register FSes, with which we will watch for supported scheme URLs
   * being opened, and intercept them.
   */
  for (i = 0; urlconf_schemes[i]; i++) {
    pr_fs_t *fs = NULL;
    const char *scheme;

    scheme = urlconf_schemes[i];

    fs = pr_register_fs(p, "urlconf", scheme);
    if (fs == NULL) {
      pr_log_debug(DEBUG0, MOD_CONF_URL_VERSION
        ": error registering '%s' fs: %s", scheme, strerror(errno));
      return;
    }

    pr_log_debug(DEBUG10, MOD_CONF_URL_VERSION ": registered '%s' fs",
      scheme);

    /* Add the module's custom FS callbacks here. This module does not
     * provide callbacks for most of the operations.
     */
    fs->fstat = urlconf_fsio_fstat;
    fs->lstat = urlconf_fsio_lstat;
    fs->open = urlconf_fsio_open;
    fs->close = urlconf_fsio_close;
    fs->read = urlconf_fsio_read;
    fs->stat = urlconf_fsio_stat;

#if PROFTPD_VERSION_NUMBER >= 0x0001030603
    /* Tell the FSIO API that these are non-standard paths. */
    fs->non_std_path = TRUE;
#endif /* 1.3.6rc3 and later */
  }
}

static void urlconf_fs_unregister(void) {
  register unsigned int i;

  /* Unregister the registered FSes. */
  for (i = 0; urlconf_schemes[i]; i++) {
    const char *scheme;

    scheme = urlconf_schemes[i];

    if (pr_unregister_fs(scheme) < 0) {
      if (errno != ENOENT) {
        pr_log_debug(DEBUG0, MOD_CONF_URL_VERSION
          ": error unregistering '%s' fs: %s", scheme, strerror(errno));
      }

    } else {
      pr_log_debug(DEBUG8, MOD_CONF_URL_VERSION ": '%s' fs unregistered",
        scheme);
    }
  }
}

static int urlconf_init(void) {
  urlconf_pool = make_sub_pool(permanent_pool);
  pr_pool_tag(urlconf_pool, MOD_CONF_URL_VERSION);

  /* Register event handlers. */
#if defined(PR_SHARED_MODULE)
  pr_event_register(&conf_url_module, "core.module-unload",
    urlconf_mod_unload_ev, NULL);
#endif /* PR_SHARED_MODULE */
  pr_event_register(&conf_url_module, "core.postparse", urlconf_postparse_ev,
    NULL);
  pr_event_register(&conf_url_module, "core.restart", urlconf_restart_ev,
    NULL);

  urlconf_fs_register(urlconf_pool);
  urlconf_http_init(urlconf_pool, &urlconf_flags);

  return 0;
}

/* Module API tables
 */

module conf_url_module = {
  NULL, NULL,

  /* Module API version 2.0 */
  0x20,

  /* Module name */
  "conf_url",

  /* Module configuration handler table */
  NULL,

  /* Module command handler table */
  NULL,

  /* Module authentication handler table */
  NULL,

  /* Module initialization function */
  urlconf_init,

  /* Session initialization function */
  NULL,

  /* Module version */
  MOD_CONF_URL_VERSION
};
