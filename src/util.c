#include "util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *xmalloc(size_t size) {
    void *p = malloc(size);
    if (!p) {
        fprintf(stderr, "goget: out of memory\n");
        exit(1);
    }
    return p;
}

char *xstrdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *copy = xmalloc(len);
    memcpy(copy, s, len);
    return copy;
}

char *xstrndup(const char *s, size_t n) {
    char *copy = xmalloc(n + 1);
    memcpy(copy, s, n);
    copy[n] = '\0';
    return copy;
}

int str_ends_with(const char *s, const char *suffix) {
    size_t slen = strlen(s);
    size_t suflen = strlen(suffix);
    if (suflen > slen) return 0;
    return strcmp(s + slen - suflen, suffix) == 0;
}

int str_starts_with(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

char *str_to_lower_dup(const char *s) {
    size_t len = strlen(s);
    char *copy = xmalloc(len + 1);
    for (size_t i = 0; i < len; i++) {
        copy[i] = (char)tolower((unsigned char)s[i]);
    }
    copy[len] = '\0';
    return copy;
}

char *str_trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return s;
}
