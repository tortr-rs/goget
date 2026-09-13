#ifndef GOGET_CACHE_H
#define GOGET_CACHE_H

#include "repospec.h"

/* Returns the heap-allocated cache directory for a fully-resolved repo
 * spec: $HOME/.cache/goget/src/<host>/<owner>/<repo> (owner may itself
 * contain '/' for nested GitLab-style groups, which just becomes deeper
 * nesting on disk). Does not create the directory. spec->kind must be
 * REPOSPEC_FULL. */
char *cache_repo_dir(const repospec_t *spec);

#endif
