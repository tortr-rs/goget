#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "buildsys.h"
#include "cache.h"
#include "gitops.h"
#include "repospec.h"

static void print_usage(const char *prog) {
    fprintf(stderr,
        "usage: %s <command> [args]\n"
        "commands:\n"
        "  build <repo>    build and install from source\n"
        "  fetch <repo>    install a prebuilt release binary\n"
        "  show <repo>     show README + build script\n"
        "  config          print current configuration\n"
        "  makeuser        create a new user account (requires root)\n",
        prog);
}

/* Development aid: parse a repo spec and print the result. build/fetch/show
 * call into this today since none of them are implemented yet; once they
 * are, this becomes the hidden `goget parse <spec>` command for testing
 * repospec.c in isolation. */
static void debug_print_spec(const char *arg) {
    repospec_t *spec = repospec_parse(arg);
    if (!spec) {
        printf("parse error: could not parse '%s'\n", arg);
        return;
    }
    if (spec->kind == REPOSPEC_BARE_NAME) {
        printf("kind=BARE_NAME repo=%s\n", spec->repo);
    } else {
        printf("kind=FULL host=%s owner=%s repo=%s\n", spec->host, spec->owner, spec->repo);
    }
    repospec_free(spec);
}

/* Resolves a repo spec argument to a REPOSPEC_FULL spec. Bare short names
 * need the GitHub/GitLab/Codeberg disambiguation search (not implemented
 * yet), so those are rejected for now with a clear message rather than
 * silently failing later. Returns NULL if resolution isn't possible;
 * caller should treat that as "already reported, just return". */
static repospec_t *resolve_spec(const char *arg) {
    repospec_t *spec = repospec_parse(arg);
    if (!spec) {
        fprintf(stderr, "goget: could not parse repo spec '%s'\n", arg);
        return NULL;
    }
    if (spec->kind == REPOSPEC_BARE_NAME) {
        fprintf(stderr,
                "goget: short-name search across GitHub/GitLab/Codeberg is "
                "not implemented yet.\n"
                "goget: please specify a full repo spec, e.g. "
                "github.com/owner/%s\n",
                spec->repo);
        repospec_free(spec);
        return NULL;
    }
    return spec;
}

static int cmd_build(const char *arg) {
    repospec_t *spec = resolve_spec(arg);
    if (!spec) return 1;

    char *cache_dir = cache_repo_dir(spec);
    char *clone_url = git_clone_url(spec);

    int rc = git_sync(clone_url, cache_dir);
    free(clone_url);
    if (rc != 0) {
        fprintf(stderr, "goget: failed to fetch source for %s\n", spec->repo);
        free(cache_dir);
        repospec_free(spec);
        return 1;
    }

    buildsys_kind_t kind = buildsys_detect(cache_dir);
    if (kind == BUILD_NONE) {
        /* Spec calls for prompting to fall back to a prebuilt binary here
         * (goget fetch), but fetch/the shared prompt module don't exist
         * yet -- wiring this up is the next increment. */
        fprintf(stderr,
                "goget: no recognized build system (CMake/Make/Autotools) "
                "found for %s.\n"
                "goget: prebuilt-binary fallback is not implemented yet.\n",
                spec->repo);
        free(cache_dir);
        repospec_free(spec);
        return 1;
    }

    printf("goget: detected %s build system for %s\n", buildsys_name(kind), spec->repo);
    rc = buildsys_build_and_install(kind, cache_dir, NULL, 0);
    free(cache_dir);
    repospec_free(spec);

    if (rc != 0) {
        fprintf(stderr, "goget: build/install failed for %s\n", arg);
        return 1;
    }
    printf("goget: successfully built and installed %s\n", arg);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "build") == 0) {
        if (argc < 3) {
            fprintf(stderr, "usage: %s build <repo>\n", argv[0]);
            return 1;
        }
        return cmd_build(argv[2]);
    } else if (strcmp(cmd, "fetch") == 0 || strcmp(cmd, "show") == 0) {
        if (argc < 3) {
            fprintf(stderr, "usage: %s %s <repo>\n", argv[0], cmd);
            return 1;
        }
        fprintf(stderr, "goget: '%s' is not implemented yet.\n", cmd);
        debug_print_spec(argv[2]);
        return 0;
    } else if (strcmp(cmd, "config") == 0) {
        fprintf(stderr, "goget: 'config' is not implemented yet.\n");
        return 0;
    } else if (strcmp(cmd, "makeuser") == 0) {
        fprintf(stderr, "goget: 'makeuser' is not implemented yet.\n");
        return 0;
    } else if (strcmp(cmd, "parse") == 0) {
        if (argc < 3) {
            fprintf(stderr, "usage: %s parse <spec>\n", argv[0]);
            return 1;
        }
        debug_print_spec(argv[2]);
        return 0;
    }

    fprintf(stderr, "goget: unknown command '%s'\n", cmd);
    print_usage(argv[0]);
    return 1;
}
