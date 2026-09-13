#ifndef GOGET_UTIL_H
#define GOGET_UTIL_H

#include <stddef.h>

/* Fatal-on-OOM allocation helpers: goget is a short-lived CLI process,
 * so there is no sane recovery from malloc failure other than exiting. */
void *xmalloc(size_t size);
void *xrealloc(void *ptr, size_t size);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);

int str_ends_with(const char *s, const char *suffix);
int str_starts_with(const char *s, const char *prefix);
/* Case-insensitive substring test, since asset/version strings from
 * external APIs and tool output show up in inconsistent case. */
int str_ci_contains(const char *haystack, const char *needle);

/* Returns a newly-allocated lowercased copy of s. */
char *str_to_lower_dup(const char *s);

/* Trims leading/trailing whitespace in place and returns the same pointer
 * advanced past leading whitespace (the caller must free the original
 * pointer separately if it was heap-allocated, or keep a copy). */
char *str_trim(char *s);

int path_exists(const char *path);
int path_is_dir(const char *path);
int path_is_executable_file(const char *path);

/* mkdir -p equivalent: creates path and all missing parent directories
 * with mode 0755. Returns 0 on success (including "already exists"), -1
 * with errno set on failure. */
int mkdir_p(const char *path);

/* Returns a newly-allocated copy of the current user's home directory,
 * from $HOME or, failing that, the passwd database. Exits fatally if
 * neither source works, since goget cannot function without it. */
char *get_home_dir(void);

/* Returns a newly-allocated "a/b". */
char *path_join(const char *a, const char *b);

/* Percent-encodes s for use as a single URL query-parameter value
 * (unreserved chars per RFC 3986 pass through unescaped). */
char *url_encode(const char *s);

#endif
