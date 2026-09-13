#include <stdio.h>
#include <string.h>

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

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "build") == 0 || strcmp(cmd, "fetch") == 0 || strcmp(cmd, "show") == 0) {
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
