#include "cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

char *cache_repo_dir(const repospec_t *spec) {
    if (spec->kind != REPOSPEC_FULL) {
        fprintf(stderr, "goget: internal error: cache_repo_dir called on unresolved spec\n");
        exit(1);
    }

    char *home = get_home_dir();
    size_t needed = strlen(home) + strlen("/.cache/goget/src/") +
                     strlen(spec->host) + 1 + strlen(spec->owner) + 1 +
                     strlen(spec->repo) + 1;
    char *path = xmalloc(needed);
    snprintf(path, needed, "%s/.cache/goget/src/%s/%s/%s", home, spec->host,
              spec->owner, spec->repo);
    free(home);
    return path;
}
