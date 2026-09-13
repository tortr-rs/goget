#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void *xmalloc(size_t size) {
    void *p = malloc(size);
    if (!p) {
        fprintf(stderr, "goget: out of memory\n");
        exit(1);
    }
    return p;
}

void *xrealloc(void *ptr, size_t size) {
    void *p = realloc(ptr, size);
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

int str_ci_contains(const char *haystack, const char *needle) {
    size_t hlen = strlen(haystack);
    size_t nlen = strlen(needle);
    if (nlen == 0) return 1;
    if (nlen > hlen) return 0;
    for (size_t i = 0; i + nlen <= hlen; i++) {
        size_t j = 0;
        for (; j < nlen; j++) {
            if (tolower((unsigned char)haystack[i + j]) != tolower((unsigned char)needle[j])) break;
        }
        if (j == nlen) return 1;
    }
    return 0;
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

int path_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

int path_is_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISDIR(st.st_mode);
}

int path_is_executable_file(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    if (!S_ISREG(st.st_mode)) return 0;
    return access(path, X_OK) == 0;
}

int mkdir_p(const char *path) {
    char *copy = xstrdup(path);
    size_t len = strlen(copy);
    /* Build up the path one component at a time, creating each as we go. */
    for (size_t i = 1; i <= len; i++) {
        if (copy[i] == '/' || copy[i] == '\0') {
            char save = copy[i];
            copy[i] = '\0';
            if (mkdir(copy, 0755) != 0 && errno != EEXIST) {
                int saved_errno = errno;
                free(copy);
                errno = saved_errno;
                return -1;
            }
            copy[i] = save;
        }
    }
    free(copy);
    return 0;
}

char *get_home_dir(void) {
    const char *home = getenv("HOME");
    if (home != NULL && *home != '\0') {
        return xstrdup(home);
    }
    struct passwd *pw = getpwuid(getuid());
    if (pw != NULL && pw->pw_dir != NULL) {
        return xstrdup(pw->pw_dir);
    }
    fprintf(stderr, "goget: cannot determine home directory\n");
    exit(1);
}

char *path_join(const char *a, const char *b) {
    size_t needed = strlen(a) + 1 + strlen(b) + 1;
    char *out = xmalloc(needed);
    snprintf(out, needed, "%s/%s", a, b);
    return out;
}

char *url_encode(const char *s) {
    static const char *hex = "0123456789ABCDEF";
    size_t len = strlen(s);
    char *out = xmalloc(len * 3 + 1);
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out[j++] = (char)c;
        } else {
            out[j++] = '%';
            out[j++] = hex[(c >> 4) & 0xF];
            out[j++] = hex[c & 0xF];
        }
    }
    out[j] = '\0';
    return out;
}
