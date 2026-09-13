#ifndef GOGET_NET_H
#define GOGET_NET_H

int net_global_init(void);
void net_global_cleanup(void);

/* GETs url with optional extra_headers (a NULL-terminated array of
 * "Header: value" strings, or NULL for none). Returns the response body
 * as a newly-allocated NUL-terminated buffer (caller frees) regardless
 * of HTTP status -- even a 404 has a body worth inspecting (GitHub
 * returns a JSON error object on one). *out_http_status receives the
 * HTTP status code, or 0 if the request failed before getting a response
 * at all (DNS failure, connection refused, timeout, ...), in which case
 * an error is also printed to stderr. out_http_status may be NULL. */
char *net_get(const char *url, const char *const *extra_headers, long *out_http_status);

/* Downloads url to dest_path, streamed straight to disk (release assets
 * can be large). Returns 0 on success (HTTP 2xx), nonzero otherwise
 * (message printed to stderr). */
int net_download(const char *url, const char *dest_path);

#endif
