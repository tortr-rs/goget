#include "repospec.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

static void strip_git_suffix(char *repo) {
    size_t len = strlen(repo);
    const char *suffix = ".git";
    size_t suflen = 4;
    if (len > suflen && strcmp(repo + len - suflen, suffix) == 0) {
        repo[len - suflen] = '\0';
    }
}

static int contains_dot(const char *start, const char *end) {
    for (const char *p = start; p < end; p++) {
        if (*p == '.') return 1;
    }
    return 0;
}

repospec_t *repospec_parse(const char *input) {
    char *trimmed = xstrdup(input);
    char *s = str_trim(trimmed);

    if (*s == '\0') {
        free(trimmed);
        return NULL;
    }
    for (char *p = s; *p; p++) {
        if (isspace((unsigned char)*p)) {
            free(trimmed);
            return NULL;
        }
    }

    char *host = NULL;
    char *rest = s;

    /* Scheme detection must happen before trailing-slash stripping, or a
     * degenerate input like "http://" strips down to "http:" and silently
     * misparses as a bare short name instead of a rejected empty path. */
    if (str_starts_with(s, "https://")) rest = s + 8;
    else if (str_starts_with(s, "http://")) rest = s + 7;
    else if (str_starts_with(s, "ssh://")) rest = s + 6;
    else if (str_starts_with(s, "git://")) rest = s + 6;

    int had_scheme = (rest != s);

    size_t rlen = strlen(rest);
    while (rlen > 0 && rest[rlen - 1] == '/') {
        rest[--rlen] = '\0';
    }
    if (rlen == 0) {
        free(trimmed);
        return NULL;
    }

    /* Detect scp-like ssh syntax: user@host:path (only meaningful when no
     * scheme was already stripped, since ssh:// URLs use user@host/path). */
    char *at = strchr(rest, '@');
    int is_scp_form = 0;
    if (at != NULL) {
        char *slash_after_at = strchr(at, '/');
        char *colon_after_at = strchr(at, ':');
        if (!had_scheme && colon_after_at &&
            (!slash_after_at || colon_after_at < slash_after_at)) {
            is_scp_form = 1;
        }
        rest = at + 1; /* skip "user@" either way */
    }

    char *path_start;

    if (is_scp_form) {
        char *colon = strchr(rest, ':');
        if (!colon) {
            free(trimmed);
            return NULL;
        }
        *colon = '\0';
        host = str_to_lower_dup(rest);
        path_start = colon + 1;
    } else {
        char *slash = strchr(rest, '/');
        if (!slash) {
            /* No '/' anywhere: only a bare short name if there was no
             * scheme/user@ involved (a URL with no path is malformed). */
            if (rest != s) {
                free(trimmed);
                return NULL;
            }
            char *repo = xstrdup(rest);
            strip_git_suffix(repo);
            free(trimmed);
            if (*repo == '\0') {
                free(repo);
                return NULL;
            }
            repospec_t *spec = xmalloc(sizeof(*spec));
            spec->kind = REPOSPEC_BARE_NAME;
            spec->host = NULL;
            spec->owner = NULL;
            spec->repo = repo;
            return spec;
        }

        if (rest != s) {
            /* Came from a URL scheme (or ssh://user@host/...): the segment
             * up to the first '/' is unambiguously the host. */
            *slash = '\0';
            host = str_to_lower_dup(rest);
            path_start = slash + 1;
        } else {
            /* Bare form with no scheme: "X/Y" or "X/Y/Z...". Heuristic:
             * if the first segment looks like a domain (contains a '.'),
             * treat it as host/owner/repo; otherwise assume it's a bare
             * "owner/repo" with no host and default to github.com. This
             * is a documented assumption, not part of the original spec,
             * which only calls out host/owner/repo and bare short names. */
            if (contains_dot(rest, slash)) {
                *slash = '\0';
                host = str_to_lower_dup(rest);
                path_start = slash + 1;
            } else {
                host = xstrdup("github.com");
                path_start = rest;
            }
        }
    }

    if (*path_start == '\0') {
        free(trimmed);
        free(host);
        return NULL;
    }

    char *last_slash = strrchr(path_start, '/');
    if (!last_slash) {
        /* Host is known but only one path segment given: not enough to
         * determine both owner and repo. */
        free(trimmed);
        free(host);
        return NULL;
    }
    *last_slash = '\0';

    char *owner = xstrdup(path_start);
    char *repo = xstrdup(last_slash + 1);
    strip_git_suffix(repo);

    free(trimmed);

    if (*owner == '\0' || *repo == '\0') {
        free(host);
        free(owner);
        free(repo);
        return NULL;
    }

    repospec_t *spec = xmalloc(sizeof(*spec));
    spec->kind = REPOSPEC_FULL;
    spec->host = host;
    spec->owner = owner;
    spec->repo = repo;
    return spec;
}

void repospec_free(repospec_t *spec) {
    if (!spec) return;
    free(spec->host);
    free(spec->owner);
    free(spec->repo);
    free(spec);
}
