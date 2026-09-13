#ifndef GOGET_UTIL_H
#define GOGET_UTIL_H

#include <stddef.h>

/* Fatal-on-OOM allocation helpers: goget is a short-lived CLI process,
 * so there is no sane recovery from malloc failure other than exiting. */
void *xmalloc(size_t size);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);

int str_ends_with(const char *s, const char *suffix);
int str_starts_with(const char *s, const char *prefix);

/* Returns a newly-allocated lowercased copy of s. */
char *str_to_lower_dup(const char *s);

/* Trims leading/trailing whitespace in place and returns the same pointer
 * advanced past leading whitespace (the caller must free the original
 * pointer separately if it was heap-allocated, or keep a copy). */
char *str_trim(char *s);

#endif
