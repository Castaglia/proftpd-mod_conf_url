/*
 * ProFTPD - mod_conf_url HTTP requests
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

#ifndef MOD_CONF_URL_HTTP_H
#define MOD_CONF_URL_HTTP_H

/* HTTP headers */
#define URLCONF_HTTP_HEADER_ACCEPT			"Accept"
#define URLCONF_HTTP_HEADER_CACHE_CONTROL		"Cache-Control"
#define URLCONF_HTTP_HEADER_CONTENT_LEN			"Content-Length"
#define URLCONF_HTTP_HEADER_CONTENT_TYPE		"Content-Type"
#define URLCONF_HTTP_HEADER_DATE			"Date"
#define URLCONF_HTTP_HEADER_EXPECT			"Expect"
#define URLCONF_HTTP_HEADER_EXPIRES			"Expires"
#define URLCONF_HTTP_HEADER_HOST			"Host"
#define URLCONF_HTTP_HEADER_LAST_MODIFIED		"Last-Modified"
#define URLCONF_HTTP_HEADER_USER_AGENT			"User-Agent"

/* FTP response codes */
#define URLCONF_FTP_RESPONSE_CODE_OK			226L
#define URLCONF_FTP_RESPONSE_CODE_NOT_LOGGED_IN		530L
#define URLCONF_FTP_RESPONSE_CODE_NOT_FOUND		550L

/* HTTP response codes */
#define URLCONF_HTTP_RESPONSE_CODE_OK			200L
#define URLCONF_HTTP_RESPONSE_CODE_NO_CONTENT		204L
#define URLCONF_HTTP_RESPONSE_CODE_PARTIAL_CONTENT	206L

#define URLCONF_HTTP_RESPONSE_CODE_BAD_REQUEST		400L
#define URLCONF_HTTP_RESPONSE_CODE_UNAUTHORIZED		401L
#define URLCONF_HTTP_RESPONSE_CODE_FORBIDDEN		403L
#define URLCONF_HTTP_RESPONSE_CODE_NOT_FOUND		404L
#define URLCONF_HTTP_RESPONSE_CODE_METHOD_NOT_ALLOWED	405L
#define URLCONF_HTTP_RESPONSE_CODE_PRECONDITION_FAILED	412L
#define URLCONF_HTTP_RESPONSE_CODE_TOO_MANY_REQUESTS	429L

#define URLCONF_HTTP_RESPONSE_CODE_INTERNAL_SERVER_ERROR	500L
#define URLCONF_HTTP_RESPONSE_CODE_BAD_GATEWAY			502L
#define URLCONF_HTTP_RESPONSE_CODE_SERVICE_UNAVAIL		503L
#define URLCONF_HTTP_RESPONSE_CODE_GATEWAY_TIMEOUT		504L

/* HTTP content types */
#define URLCONF_HTTP_CONTENT_TYPE_TEXT_PLAIN		"text/plain"

void *urlconf_http_alloc(pool *p, unsigned long max_connect_secs,
  unsigned long max_request_secs, unsigned long flags);
int urlconf_http_destroy(pool *p, void *http);

/* Return a table populated with the default request headers: Accept,
 * User-Agent, etc.
 */
pr_table_t *urlconf_http_default_headers(pool *p);

int urlconf_http_get(pool *p, void *http, const char *url, pr_table_t *headers,
  size_t (*resp_body)(char *, size_t, size_t, void *), void *user_data,
  long *resp_code, const char **content_type);

/* API lifetime functions, for mod_conf_url use only. */
int urlconf_http_init(pool *p, unsigned long *feature_flags);
int urlconf_http_free(void);

#endif /* MOD_CONF_URL_HTTP_H */
