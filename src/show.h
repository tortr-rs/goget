#ifndef GOGET_SHOW_H
#define GOGET_SHOW_H

typedef struct {
    char *readme;             /* NULL if no README variant was found */
    char *build_script;       /* NULL if no recognized build-system file was found */
    const char *build_script_label; /* e.g. "CMakeLists.txt"; static string, not owned */
} show_content_t;

/* Fetches just the README and a recognized build-system marker file for
 * a fully-resolved repo, via each host's raw-file API -- no clone, and
 * nothing touches goget's cache. Only github.com, gitlab.com, and
 * codeberg.org are supported (the same three hosts goget.conf's
 * pkg.repos allow-list covers).
 *
 * Returns 0 on success (individual fields may still be NULL if that
 * particular file wasn't found in the repo), 1 if host isn't one of the
 * three supported hosts, -1 on a hard network/API error. */
int show_fetch(const char *host, const char *owner, const char *repo, show_content_t *out);

void show_content_free(show_content_t *content);

/* Pipes text through $PAGER (falling back to "less") via fork+exec --
 * never reimplements paging itself. */
void show_page(const char *text);

#endif
