/*
 * ProFTPD - mod_conf_url URI implementation
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
 * Foundation, Inc., 51 Franklin Street, Suite 500, Boston, MA 02110-1335, USA.
 *
 * As a special exemption, TJ Saunders and other respective copyright holders
 * give permission to link this program with OpenSSL, and distribute the
 * resulting executable, without including the source code for OpenSSL in the
 * source distribution.
 */

#include "mod_conf_url.h"
#include "uri.h"

static const char *trace_channel = "conf_url";

static char *uri_parse_host(pool *p, const char *orig_uri, const char *uri,
    char **remaining) {
  char *host = NULL, *ptr = NULL;

  /* We have either of:
   *
   *  host<:...>
   *  [host]<:...>
   *
   * Look for an opening square bracket, to see if we have an IPv6 address
   * in the URI.
   */
  if (uri[0] == '[') {
    size_t urilen;

    ptr = strchr(uri + 1, ']');
    if (ptr == NULL) {
      /* If there is no ']', then it's a badly-formatted URI. */
      pr_log_debug(DEBUG0, MOD_CONF_URL_VERSION
        ": badly formatted IPv6 address in host info '%.200s'", orig_uri);
      errno = EINVAL;
      return NULL;
    }

    host = pstrndup(p, uri + 1, ptr - uri - 1);

    urilen = strlen(ptr);
    if (urilen > 0) {
      *remaining = ptr + 1;

    } else {
      *remaining = NULL;
    }

    return host;
  }

  ptr = strchr(uri + 1, ':');
  if (ptr == NULL) {
    /* IFF the host starts with a slash, THEN intepret the host as an
     * absolute path (e.g. for a local file).  Otherwise, look for a
     * slash as the start of a path in the URI.
     */

    if (uri[0] == '/') {
      *remaining = NULL;
      host = pstrdup(p, uri);

      return host;
    }

    ptr = strchr(uri, '/');
    if (ptr == NULL) {
      *remaining = NULL;
      host = pstrdup(p, uri);

      return host;
    }
  }

  *remaining = ptr;

  host = pstrndup(p, uri, ptr - uri);
  return host;
}

static int uri_parse_port(pool *p, const char *orig_uri, const char *uri,
    unsigned int *port, char **remaining) {
  register unsigned int i;
  char *ptr, *portspec;
  size_t portspeclen;

  /* Look for any possible trailing '/'. */
  ptr = strchr(uri, '/');
  if (ptr == NULL) {
    portspec = ((char *) uri) + 1;
    portspeclen = strlen(portspec);
    *remaining = NULL;

  } else {
    portspeclen = ptr - (uri + 1);

    *ptr = '\0';
    portspec = pstrndup(p, ((char *) uri) + 1, portspeclen);
    *ptr = '/';
    *remaining = ptr;
  }

  /* Ensure that only numeric characters appear in the portspec. */
  for (i = 0; i < portspeclen; i++) {
    if (PR_ISDIGIT((int) portspec[i]) == 0) {
      pr_log_debug(DEBUG2, MOD_CONF_URL_VERSION
        ": invalid character (%c) at index %d in port specification '%.200s'",
        portspec[i], i, portspec);
      errno = EINVAL;
      return -1;
    }
  }

  /* The above check will rule out any negative numbers, since it will reject
   * the minus character.  Thus we only need to check for a zero port, or a
   * number that's outside the 1-65535 range.
   */
  *port = atoi(portspec);
  if (*port == 0 ||
      *port >= 65536) {
    pr_log_debug(DEBUG2, MOD_CONF_URL_VERSION
      ": port specification '%.200s' yields invalid port number %d",
      portspec, *port);
    errno = EINVAL;
    return -1;
  }

  return 0;
}

/* Determine whether "username:password@" are present.  If so, then parse it
 * out, and return a pointer to the portion of the URI after the parsed-out
 * userinfo.
 */
static char *uri_parse_userinfo(pool *p, const char *orig_uri,
    const char *uri, char **username, char **password) {
  char *ptr, *ptr2, *rem_uri = NULL, *userinfo, *user = NULL, *passwd = NULL;

  /* We have either:
   *
   *  host<:...>
   *  [host]<:...>
   *
   * thus no user info, OR:
   *
   *  username:password@host...
   *  username:password@[host]...
   *  username:@host...
   *  username:pass@word@host...
   *  user@domain.com:pass@word@host...
   *
   * all of which have at least one occurrence of the '@' character.
   */

  ptr = strchr(uri, '@');
  if (ptr == NULL) {
    /* No '@' character at all?  No user info, then. */

    if (username != NULL) {
      *username = NULL;
    }

    if (password != NULL) {
      *password = NULL;
    }

    return pstrdup(p, uri);
  }

  /* To handle the case where the password field might itself contain an
   * '@' character, we first search from the end for '@'.  If found, then we
   * search for '@' from the beginning.  If also found, AND if both ocurrences
   * are the same, then we have a plain "username:password@" string.
   *
   * Note that we can handle '@' characters within passwords (or usernames),
   * but we currently cannot handle ':' characters within usernames.
   */

  ptr2 = strrchr(uri, '@');
  if (ptr2 != NULL) {
    if (ptr != ptr2) {
      /* Use the last found '@' as the delimiter. */
      ptr = ptr2;
    }
  }

  userinfo = pstrndup(p, uri, ptr - uri);
  rem_uri = ptr + 1;

  ptr = strchr(userinfo, ':');
  if (ptr == NULL) {
    if (username != NULL) {
      *username = NULL;
    }

    if (password != NULL) {
      *password = NULL;
    }

    return rem_uri;
  }

  user = pstrndup(p, userinfo, ptr - userinfo);
  if (username != NULL) {
    *username = user;
  }

  /* Watch for empty passwords. */
  if (*(ptr+1) == '\0') {
    passwd = pstrdup(p, "");

  } else {
    passwd = pstrdup(p, ptr + 1);
  }

  if (password != NULL) {
    *password = passwd;
  }

  return rem_uri;
}

static int uri_parse_kv(pool *p, const char *uri, char *kv, size_t kvlen,
  char **k, size_t *klen, char **v, size_t *vlen) {
  char *ptr;

  ptr = memchr(kv, '=', kvlen);
  if (ptr == NULL) {
    pr_log_debug(DEBUG1, MOD_CONF_URL_VERSION
      ": badly formatted query parameter '%.*s' in URI '%.200s'", (int) kvlen,
      kv, uri);
    errno = EINVAL;
    return -1;
  }

  *klen = ptr - kv;
  *k = pstrndup(p, kv, *klen);

  *vlen = kvlen - *klen - 1;
  *v = pstrndup(p, ptr + 1, *vlen);

  return 0;
}

static int uri_store_kv(pool *p, const char *uri, pr_table_t *params,
    char *k, size_t klen, char *v, size_t vlen) {
  int res;

  v = pstrndup(p, v, vlen);

  if (pr_table_count(params) == 0 ||
      pr_table_exists(params, k) == 0) {
    k = pstrndup(p, k, klen);
    res = pr_table_add(params, k, v, vlen);

  } else {
    res = pr_table_set(params, k, v, vlen);
  }

  if (res < 0) {
    int xerrno = errno;

    pr_log_debug(DEBUG0, MOD_CONF_URL_VERSION
      ": error stashing '%.*s=%.*s' from URI '%.200s': %s", (int) klen, k,
      (int) vlen, v, uri, strerror(xerrno));

    errno = xerrno;
    return -1;
  }

  pr_trace_msg(trace_channel, 9,
    "parsed parameter '%.*s', value '%.*s' from URI", (int) klen, k,
    (int) vlen, v);
  return 0;
}

static int uri_parse_params(pool *p, const char *orig_uri, const char *uri,
    pr_table_t *params) {
  int res;
  char *k, *v, *query_string, *ptr;
  size_t klen = 0, vlen = 0, kvlen, query_stringlen;
  pool *sub_pool;

  sub_pool = make_sub_pool(p);
  pr_pool_tag(sub_pool, "URI parameter pool");

  query_stringlen = strlen(uri);
  query_string = pstrndup(sub_pool, uri, query_stringlen);

  ptr = memchr(query_string, '&', query_stringlen);
  while (ptr != NULL) {
    pr_signals_handle();

    k = v = NULL;
    kvlen = vlen = 0;

    kvlen = ptr - query_string;
    res = uri_parse_kv(sub_pool, orig_uri, query_string, kvlen,
      &k, &klen, &v, &vlen);
    if (res < 0) {
      /* Malformed "key=val" string. */
      destroy_pool(sub_pool);
      errno = EINVAL;
      return -1;
    }

    res = uri_store_kv(p, orig_uri, params, k, klen, v, vlen);
    if (res < 0) {
      int xerrno = errno;

      destroy_pool(sub_pool);
      errno = xerrno;
      return -1;
    }

    query_string = ptr + 1;
    ptr = strchr(query_string, '&');
  }

  k = v = NULL;
  klen = vlen = 0;

  res = uri_parse_kv(sub_pool, orig_uri, query_string, query_stringlen,
    &k, &klen, &v, &vlen);
  if (res < 0) {
    /* Malformed "key=val" string. */
    destroy_pool(sub_pool);
    errno = EINVAL;
    return -1;
  }

  res = uri_store_kv(p, orig_uri, params, k, klen, v, vlen);
  if (res < 0) {
    int xerrno = errno;

    destroy_pool(sub_pool);
    errno = xerrno;
    return -1;
  }

  /* Warn about unknown/unsupported keys, but do NOT error on them! */

  destroy_pool(sub_pool);
  return 0;
}

int urlconf_uri_parse(pool *p, const char *orig_uri, char **scheme,
    char **host, unsigned int *port, char **path, char **username,
    char **password, pr_table_t *params) {
  register unsigned int i;
  pool *sub_pool;
  char *ptr, *ptr2 = NULL, *uri;
  size_t len;
  const char *supported_schemes[] = {
    "file://",
    "ftp://",
    "ftps://",
    "http://",
    "https://",
    NULL
  };
  int uses_supported_scheme = FALSE;

  if (p == NULL ||
      orig_uri == NULL ||
      scheme == NULL ||
      host == NULL ||
      port == NULL ||
      path == NULL ||
      username == NULL ||
      password == NULL ||
      params == NULL) {
    errno = EINVAL;
    return -1;
  }

  len = strlen(orig_uri);
  if (len < 7) {
    pr_log_debug(DEBUG0, MOD_CONF_URL_VERSION
      ": unknown/unsupported scheme in URI '%.200s' (URI too short)", orig_uri);
    errno = EINVAL;
    return -1;
  }

  for (i = 0; supported_schemes[i]; i++) {
    const char *sc;
    size_t sc_len;

    sc = supported_schemes[i];
    sc_len = strlen(sc);

    if (strncmp(orig_uri, sc, sc_len) == 0) {
      uses_supported_scheme = TRUE;
      *scheme = pstrdup(p, sc);
      break;
    }
  }

  if (uses_supported_scheme == FALSE) {
    pr_log_debug(DEBUG0, MOD_CONF_URL_VERSION
      ": unknown/unsupported scheme in URI '%.200s'", orig_uri);
    errno = EINVAL;
    return -1;
  }

  sub_pool = make_sub_pool(p);

  uri = pstrdup(sub_pool, orig_uri + 6);
  ptr = uri;

  /* Possible URIs at this point:
   *
   *  host:port/dbname?...
   *  host:port?...
   *  host:port
   *  host?...
   *  host
   *  username:password@host...
   *
   * And, in the case where 'host' is an IPv6 address:
   *
   *  [host]:port/dbname?...
   *  [host]:port?...
   *  [host]:port
   *  [host]?...
   *  [host]
   *  username:password@[host]...
   */

  ptr = strchr(uri, '?');
  if (ptr != NULL) {
    if (uri_parse_params(p, uri, ptr + 1, params) < 0) {
      int xerrno = errno;

      destroy_pool(sub_pool);
      errno = xerrno;
      return -1;
    }

    *ptr = '\0';
  }

  /* Note: Will we want/need to support URL-encoded characters in the future? */

  ptr = uri_parse_userinfo(sub_pool, uri, uri, username, password);

  *host = uri_parse_host(sub_pool, uri, ptr, &ptr2);
  if (*host == NULL) {
    int xerrno = errno;

    destroy_pool(sub_pool);
    errno = xerrno;
    return -1;
  }

  /* Optional port field present? */
  if (ptr2 != NULL) {
    ptr = strchr(ptr2, ':');
    if (ptr != NULL) {
      if (uri_parse_port(sub_pool, uri, ptr, port, &ptr2) < 0) {
        int xerrno = errno;

        destroy_pool(sub_pool);
        errno = xerrno;
        return -1;
      }
    }

    if (ptr2 != NULL) {
      *path = pstrdup(p, ptr2);
    }
  }

  destroy_pool(sub_pool);
  return 0;
}
