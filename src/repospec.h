#ifndef GOGET_REPOSPEC_H
#define GOGET_REPOSPEC_H

/* A repo spec is either fully resolved (we know which host/owner/repo)
 * or a bare short name that still needs disambiguation via search
 * (see search.h) before it can be built/fetched/shown. */
typedef enum {
    REPOSPEC_FULL,
    REPOSPEC_BARE_NAME,
} repospec_kind_t;

typedef struct {
    repospec_kind_t kind;
    char *host;   /* lowercase, e.g. "github.com"; NULL if kind == REPOSPEC_BARE_NAME */
    char *owner;  /* NULL if kind == REPOSPEC_BARE_NAME */
    char *repo;   /* always set; ".git" suffix stripped */
} repospec_t;

/* Parses one of:
 *   - full URL:        https://github.com/owner/repo(.git)?
 *   - ssh spec:         git@github.com:owner/repo.git  or  ssh://git@host/owner/repo
 *   - bare host/owner/repo: github.com/owner/repo
 *   - bare owner/repo (no host): assumed to be github.com (documented assumption)
 *   - bare short name:  fastfetch  ->  REPOSPEC_BARE_NAME, needs search
 *
 * Returns a heap-allocated repospec_t on success, or NULL on malformed
 * input (empty string, embedded whitespace, etc). Caller owns the result
 * and must free it with repospec_free(). */
repospec_t *repospec_parse(const char *input);

void repospec_free(repospec_t *spec);

#endif
