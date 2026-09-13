#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "buildsys.h"
#include "cache.h"
#include "config.h"
#include "gitops.h"
#include "makeuser.h"
#include "net.h"
#include "prompt.h"
#include "release.h"
#include "repospec.h"
#include "search.h"
#include "show.h"
#include "util.h"

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

/* For a bare short name (no host), searches GitHub/GitLab/Codeberg and
 * either confirms the single match, offers a numbered picker for
 * multiple matches (grouped github -> gitlab -> codeberg, since that's
 * the order search_by_name already returns them in), or reports zero
 * matches. Returns a new REPOSPEC_FULL spec, or NULL if nothing was
 * resolved (already reported to the user; caller just returns). Always
 * frees bare_spec. */
static repospec_t *resolve_bare_name(repospec_t *bare_spec) {
    goget_config_t *cfg = config_load();
    search_result_list_t results = search_by_name(cfg, bare_spec->repo);
    config_free(cfg);

    if (results.count == 0) {
        printf("goget: no repository named '%s' found on GitHub, GitLab, or Codeberg.\n",
               bare_spec->repo);
        search_result_list_free(&results);
        repospec_free(bare_spec);
        return NULL;
    }

    size_t chosen;
    if (results.count == 1) {
        char msg[256];
        snprintf(msg, sizeof msg, "Found %s on %s -- proceed?", results.items[0].repo,
                 results.items[0].host);
        if (!prompt_yes_no(msg, 1)) {
            printf("goget: aborting.\n");
            search_result_list_free(&results);
            repospec_free(bare_spec);
            return NULL;
        }
        chosen = 0;
    } else {
        char **labels = xmalloc(results.count * sizeof(char *));
        for (size_t i = 0; i < results.count; i++) {
            size_t llen = strlen(results.items[i].owner) + strlen(results.items[i].repo) +
                           strlen(results.items[i].host) + 48;
            labels[i] = xmalloc(llen);
            snprintf(labels[i], llen, "%s/%s on %s (%ld stars)", results.items[i].owner,
                     results.items[i].repo, results.items[i].host, results.items[i].stars);
        }
        int idx = prompt_pick((const char *const *)labels, (int)results.count);
        for (size_t i = 0; i < results.count; i++) free(labels[i]);
        free(labels);

        if (idx < 0) {
            printf("goget: aborting.\n");
            search_result_list_free(&results);
            repospec_free(bare_spec);
            return NULL;
        }
        chosen = (size_t)idx;
    }

    repospec_t *resolved = xmalloc(sizeof(*resolved));
    resolved->kind = REPOSPEC_FULL;
    resolved->host = xstrdup(results.items[chosen].host);
    resolved->owner = xstrdup(results.items[chosen].owner);
    resolved->repo = xstrdup(results.items[chosen].repo);

    search_result_list_free(&results);
    repospec_free(bare_spec);
    return resolved;
}

/* Resolves a repo spec argument to a REPOSPEC_FULL spec, searching
 * GitHub/GitLab/Codeberg for bare short names. Returns NULL if
 * resolution isn't possible; caller should treat that as "already
 * reported, just return". */
static repospec_t *resolve_spec(const char *arg) {
    repospec_t *spec = repospec_parse(arg);
    if (!spec) {
        fprintf(stderr, "goget: could not parse repo spec '%s'\n", arg);
        return NULL;
    }
    if (spec->kind == REPOSPEC_BARE_NAME) {
        return resolve_bare_name(spec);
    }
    return spec;
}

/* Like resolve_spec, but also enforces the pkg.repos{} host allow-list --
 * used by build/fetch, which actually pull code/binaries from the
 * resolved host, but not by show, which is read-only inspection. Per
 * spec, this applies only to the explicitly-requested top-level package
 * (there's no transitive dependency resolution in goget to worry about
 * either way). */
static repospec_t *resolve_spec_checked(const char *arg) {
    repospec_t *spec = resolve_spec(arg);
    if (!spec) return NULL;

    goget_config_t *cfg = config_load();
    if (config_host_enabled(cfg, spec->host)) {
        config_free(cfg);
        return spec;
    }

    char msg[256];
    snprintf(msg, sizeof msg,
             "%s found on %s, which is disabled in your config. Allow this source?", spec->repo,
             spec->host);
    int choice = prompt_once_always_cancel(msg);

    if (choice == 'a') {
        config_set_host_enabled(cfg, spec->host, 1);
        config_save(cfg);
    }
    config_free(cfg);

    if (choice == 'c') {
        printf("goget: aborting.\n");
        repospec_free(spec);
        return NULL;
    }
    return spec; /* 'o' or 'a': proceed */
}

static int cmd_build(const char *arg);
static int cmd_fetch(const char *arg);

static int cmd_build(const char *arg) {
    repospec_t *spec = resolve_spec_checked(arg);
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
        free(cache_dir);
        char msg[256];
        snprintf(msg, sizeof msg,
                 "No source build available for %s. Install prebuilt binary instead?", spec->repo);
        int install_prebuilt = prompt_yes_no(msg, 1);
        repospec_free(spec);
        if (!install_prebuilt) {
            printf("goget: aborting.\n");
            return 1;
        }
        return cmd_fetch(arg);
    }

    printf("goget: detected %s build system for %s\n", buildsys_name(kind), spec->repo);

    char **use_args = NULL;
    size_t n_use_args = 0;
    if (kind == BUILD_CMAKE) {
        goget_config_t *cfg = config_load();
        use_args = config_build_use_cmake_args(cfg, spec->repo, &n_use_args);
        config_free(cfg);
        for (size_t i = 0; i < n_use_args; i++) {
            printf("goget: USE flag build option: %s\n", use_args[i]);
        }
    }

    rc = buildsys_build_and_install(kind, cache_dir, use_args, n_use_args);
    free(cache_dir);
    repospec_free(spec);
    for (size_t i = 0; i < n_use_args; i++) free(use_args[i]);
    free(use_args);

    if (rc != 0) {
        fprintf(stderr, "goget: build/install failed for %s\n", arg);
        return 1;
    }
    printf("goget: successfully built and installed %s\n", arg);
    return 0;
}

static int cmd_fetch(const char *arg) {
    repospec_t *spec = resolve_spec_checked(arg);
    if (!spec) return 1;

    int rc;
    if (strcmp(spec->host, "github.com") != 0) {
        printf("goget: fetch only checks GitHub Releases; %s is hosted on %s.\n", spec->repo,
               spec->host);
        rc = 1;
    } else {
        rc = release_fetch_and_install(spec->owner, spec->repo, spec->repo);
    }

    if (rc == 0) {
        printf("goget: successfully installed %s\n", spec->repo);
        repospec_free(spec);
        return 0;
    }
    if (rc == -1) {
        fprintf(stderr, "goget: fetch failed for %s\n", arg);
        repospec_free(spec);
        return 1;
    }

    /* rc == 1: no compatible release found. */
    char msg[256];
    snprintf(msg, sizeof msg,
             "No compatible prebuilt binary available for %s -- must build from source. Continue?",
             spec->repo);
    int do_build = prompt_yes_no(msg, 1);
    repospec_free(spec);
    if (!do_build) {
        printf("goget: aborting.\n");
        return 1;
    }
    return cmd_build(arg);
}

static int cmd_show(const char *arg) {
    repospec_t *spec = resolve_spec(arg);
    if (!spec) return 1;

    show_content_t content;
    int rc = show_fetch(spec->host, spec->owner, spec->repo, &content);
    if (rc == 1) {
        fprintf(stderr,
                "goget: show only supports github.com, gitlab.com, and codeberg.org; "
                "%s is hosted on %s.\n",
                spec->repo, spec->host);
        repospec_free(spec);
        return 1;
    }
    if (rc == -1) {
        fprintf(stderr, "goget: could not fetch README/build script for %s\n", spec->repo);
        repospec_free(spec);
        return 1;
    }

    if (content.readme == NULL && content.build_script == NULL) {
        fprintf(stderr, "goget: no README or recognized build script found for %s\n", spec->repo);
        show_content_free(&content);
        repospec_free(spec);
        return 1;
    }

    size_t needed = 128;
    if (content.readme) needed += strlen(content.readme) + 32;
    if (content.build_script) needed += strlen(content.build_script) + 64;
    char *combined = xmalloc(needed);
    combined[0] = '\0';

    if (content.readme) {
        strcat(combined, "=== README ===\n");
        strcat(combined, content.readme);
        strcat(combined, "\n\n");
    }
    if (content.build_script) {
        strcat(combined, "=== BUILD SCRIPT (");
        strcat(combined, content.build_script_label);
        strcat(combined, ") ===\n");
        strcat(combined, content.build_script);
        strcat(combined, "\n");
    }

    show_content_free(&content);
    repospec_free(spec);

    show_page(combined);
    free(combined);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    /* build's build-system-not-found fallback can call into cmd_fetch
     * internally, so curl needs to be ready regardless of which
     * top-level command was invoked; atexit avoids threading cleanup
     * through every return path in this dispatcher. */
    net_global_init();
    atexit(net_global_cleanup);

    const char *cmd = argv[1];

    if (strcmp(cmd, "build") == 0) {
        if (argc < 3) {
            fprintf(stderr, "usage: %s build <repo>\n", argv[0]);
            return 1;
        }
        return cmd_build(argv[2]);
    } else if (strcmp(cmd, "fetch") == 0) {
        if (argc < 3) {
            fprintf(stderr, "usage: %s fetch <repo>\n", argv[0]);
            return 1;
        }
        return cmd_fetch(argv[2]);
    } else if (strcmp(cmd, "show") == 0) {
        if (argc < 3) {
            fprintf(stderr, "usage: %s show <repo>\n", argv[0]);
            return 1;
        }
        return cmd_show(argv[2]);
    } else if (strcmp(cmd, "config") == 0) {
        goget_config_t *cfg = config_load();
        config_print(cfg);
        config_free(cfg);
        return 0;
    } else if (strcmp(cmd, "makeuser") == 0) {
        return makeuser_run();
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
