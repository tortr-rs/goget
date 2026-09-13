#include "gitops.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "proc.h"
#include "util.h"

char *git_clone_url(const repospec_t *spec) {
    size_t needed = strlen("https://") + strlen(spec->host) + 1 +
                     strlen(spec->owner) + 1 + strlen(spec->repo) +
                     strlen(".git") + 1;
    char *url = xmalloc(needed);
    snprintf(url, needed, "https://%s/%s/%s.git", spec->host, spec->owner, spec->repo);
    return url;
}

int git_sync(const char *clone_url, const char *dest_dir) {
    size_t marker_len = strlen(dest_dir) + strlen("/.git") + 1;
    char *git_marker = xmalloc(marker_len);
    snprintf(git_marker, marker_len, "%s/.git", dest_dir);
    int already_cloned = path_is_dir(git_marker);
    free(git_marker);

    if (already_cloned) {
        printf("goget: updating cached checkout at %s\n", dest_dir);
        char *const argv[] = {"git", "pull", "--ff-only", NULL};
        return run_command_in(dest_dir, argv);
    }

    char *parent = xstrdup(dest_dir);
    char *slash = strrchr(parent, '/');
    if (slash != NULL) {
        *slash = '\0';
        if (mkdir_p(parent) != 0) {
            perror("goget: could not create cache directory");
            free(parent);
            return -1;
        }
    }
    free(parent);

    printf("goget: cloning %s into %s\n", clone_url, dest_dir);
    char *const argv[] = {"git", "clone", (char *)clone_url, (char *)dest_dir, NULL};
    return run_command(argv);
}
