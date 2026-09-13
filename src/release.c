#include "release.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cJSON.h"
#include "checksum.h"
#include "compat.h"
#include "net.h"
#include "proc.h"
#include "util.h"

typedef struct {
    char *name;
    char *url;
} asset_t;

static void assets_free(asset_t *assets, size_t count) {
    for (size_t i = 0; i < count; i++) {
        free(assets[i].name);
        free(assets[i].url);
    }
    free(assets);
}

/* Excludes assets for other operating systems by name. This is
 * necessarily a blocklist (we can't enumerate every possible Linux asset
 * name up front), so an unrecognized OS name could slip through -- but
 * that's caught later regardless: binary_libc_kind() rejects anything
 * that isn't a valid Linux ELF as LIBC_UNKNOWN, which binary_is_compatible
 * treats as incompatible. This filter just avoids wasting a download on
 * the obvious cases. */
static int looks_like_other_os(const char *name) {
    static const char *other_os_tokens[] = {
        "darwin", "macos",     "osx",    "apple",    "ios",      "windows",
        "win32",  "win64",     "freebsd", "openbsd", "netbsd",   "dragonfly",
        "solaris", "illumos",  "aix",    "wasm",     "wasi",     "android",
        "haiku",  "redox",     "serenity", "plan9",  "hpux",     "irix",
        "sunos",  "minix",     "hurd",
    };
    for (size_t i = 0; i < sizeof(other_os_tokens) / sizeof(other_os_tokens[0]); i++) {
        if (str_ci_contains(name, other_os_tokens[i])) return 1;
    }
    return str_ends_with(name, ".exe") || str_ends_with(name, ".msi") ||
           str_ends_with(name, ".dmg") || str_ends_with(name, ".app");
}

static int looks_like_package_format(const char *name) {
    return str_ends_with(name, ".deb") || str_ends_with(name, ".rpm") ||
           str_ends_with(name, ".pkg") || str_ends_with(name, ".apk");
}

static int is_archive_name(const char *name) {
    return str_ends_with(name, ".tar.gz") || str_ends_with(name, ".tgz") ||
           str_ends_with(name, ".tar.xz") || str_ends_with(name, ".txz") ||
           str_ends_with(name, ".tar.bz2") || str_ends_with(name, ".tbz2") ||
           str_ends_with(name, ".zip");
}

static int extract_archive(const char *archive_path, const char *dest_dir, const char *asset_name) {
    if (str_ends_with(asset_name, ".zip")) {
        char *const argv[] = {"unzip", "-q", "-o", (char *)archive_path, "-d", (char *)dest_dir, NULL};
        return run_command(argv);
    }
    /* GNU tar auto-detects gzip/xz/bzip2 compression from -xf alone. */
    char *const argv[] = {"tar", "-xf", (char *)archive_path, "-C", (char *)dest_dir, NULL};
    return run_command(argv);
}

static void remove_dir_recursive(const char *dir) {
    char *const argv[] = {"rm", "-rf", (char *)dir, NULL};
    run_command(argv);
}

/* Recursively searches dir for the binary to install, per the priority
 * order in the spec: a same-named file under a bin/ path, then a
 * same-named file with the executable bit set, then any same-named file
 * (handles archives with multiple files sharing a name, e.g. a binary
 * and a same-named bash-completion script). If nothing shares the
 * repo's name, falls back to the sole executable regular file in the
 * tree, if there's exactly one -- a documented extra heuristic beyond
 * the spec, for archives that don't name the binary after the repo. */
typedef struct {
    const char *wanted;
    char *bin_match;
    char *exec_match;
    char *any_match;
    char *lone_exec;
    int exec_count;
} binary_search_t;

static void binary_search_walk(const char *dir, binary_search_t *ctx) {
    DIR *d = opendir(dir);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;

        char *full = path_join(dir, ent->d_name);
        struct stat st;
        if (lstat(full, &st) != 0) {
            free(full);
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            binary_search_walk(full, ctx);
            free(full);
            continue;
        }
        if (!S_ISREG(st.st_mode)) {
            free(full);
            continue;
        }

        int is_wanted = (strcmp(ent->d_name, ctx->wanted) == 0);
        int is_exec = (access(full, X_OK) == 0);

        if (is_wanted) {
            if (ctx->bin_match == NULL && strstr(full, "/bin/") != NULL) {
                ctx->bin_match = xstrdup(full);
            }
            if (ctx->exec_match == NULL && is_exec) {
                ctx->exec_match = xstrdup(full);
            }
            if (ctx->any_match == NULL) {
                ctx->any_match = xstrdup(full);
            }
        }
        if (is_exec) {
            ctx->exec_count++;
            free(ctx->lone_exec);
            ctx->lone_exec = (ctx->exec_count == 1) ? xstrdup(full) : NULL;
        }
        free(full);
    }
    closedir(d);
}

static char *pick_binary_in_dir(const char *root, const char *wanted_name) {
    binary_search_t ctx = {.wanted = wanted_name};
    binary_search_walk(root, &ctx);

    char *result = NULL;
    if (ctx.bin_match) result = ctx.bin_match;
    else if (ctx.exec_match) result = ctx.exec_match;
    else if (ctx.any_match) result = ctx.any_match;
    else if (ctx.exec_count == 1) result = ctx.lone_exec;

    if (result != ctx.bin_match) free(ctx.bin_match);
    if (result != ctx.exec_match) free(ctx.exec_match);
    if (result != ctx.any_match) free(ctx.any_match);
    if (result != ctx.lone_exec) free(ctx.lone_exec);
    return result;
}

typedef enum { TRY_SUCCESS, TRY_SKIP, TRY_HARD_FAIL } try_result_t;

/* Downloads, extracts (if needed), picks a binary out of, and
 * compat-checks one candidate asset. On TRY_SUCCESS, *out_binary_path
 * and *out_workdir are set; the caller must install from binary_path and
 * then remove workdir. Any other outcome cleans up after itself. */
static try_result_t try_asset(asset_t *assets, size_t count, size_t idx, const char *short_name,
                                char **out_binary_path, char **out_workdir) {
    asset_t *a = &assets[idx];

    char tmpl[] = "/tmp/goget-fetch-XXXXXX";
    char *workdir = xstrdup(tmpl);
    if (mkdtemp(workdir) == NULL) {
        perror("goget: mkdtemp");
        free(workdir);
        return TRY_SKIP;
    }

    char *download_path = path_join(workdir, a->name);
    printf("goget: downloading %s...\n", a->name);
    if (net_download(a->url, download_path) != 0) {
        free(download_path);
        remove_dir_recursive(workdir);
        free(workdir);
        return TRY_SKIP;
    }

    char *binary_path;
    if (is_archive_name(a->name)) {
        char *extract_dir = path_join(workdir, "extracted");
        mkdir_p(extract_dir);
        int rc = extract_archive(download_path, extract_dir, a->name);
        free(download_path);
        if (rc != 0) {
            fprintf(stderr, "goget: failed to extract %s\n", a->name);
            free(extract_dir);
            remove_dir_recursive(workdir);
            free(workdir);
            return TRY_SKIP;
        }
        char *picked = pick_binary_in_dir(extract_dir, short_name);
        free(extract_dir);
        if (picked == NULL) {
            fprintf(stderr, "goget: could not find a '%s' binary inside %s\n", short_name, a->name);
            remove_dir_recursive(workdir);
            free(workdir);
            return TRY_SKIP;
        }
        binary_path = picked;
    } else {
        binary_path = download_path;
    }

    if (!binary_is_compatible(binary_path, a->name)) {
        printf("goget: %s is not compatible with this host, skipping\n", a->name);
        free(binary_path);
        remove_dir_recursive(workdir);
        free(workdir);
        return TRY_SKIP;
    }

    checksum_candidate_t *cands = xmalloc(count * sizeof(checksum_candidate_t));
    for (size_t i = 0; i < count; i++) {
        cands[i].name = assets[i].name;
        cands[i].url = assets[i].url;
    }
    checksum_result_t cres = checksum_verify(cands, count, a->name, binary_path);
    free(cands);

    if (cres == CHECKSUM_MISMATCH) {
        fprintf(stderr, "goget: checksum verification FAILED for %s -- aborting.\n", a->name);
        free(binary_path);
        remove_dir_recursive(workdir);
        free(workdir);
        return TRY_HARD_FAIL;
    } else if (cres == CHECKSUM_MISSING) {
        printf("goget: warning: no checksum file found for %s; continuing without verification\n", a->name);
    } else {
        printf("goget: checksum verified OK for %s\n", a->name);
    }

    *out_binary_path = binary_path;
    *out_workdir = workdir;
    return TRY_SUCCESS;
}

int release_fetch_and_install(const char *owner, const char *repo, const char *short_name) {
    size_t url_len = strlen("https://api.github.com/repos///releases/latest") + strlen(owner) +
                       strlen(repo) + 1;
    char *url = xmalloc(url_len);
    snprintf(url, url_len, "https://api.github.com/repos/%s/%s/releases/latest", owner, repo);

    const char *headers[] = {"Accept: application/vnd.github+json", NULL};
    long status = 0;
    char *body = net_get(url, headers, &status);
    free(url);

    if (status == 0) {
        fprintf(stderr, "goget: network error contacting the GitHub releases API\n");
        free(body);
        return -1;
    }
    if (status == 404) {
        free(body);
        return 1;
    }
    if (status < 200 || status >= 300) {
        fprintf(stderr, "goget: GitHub releases API returned HTTP %ld for %s/%s\n", status, owner, repo);
        free(body);
        return -1;
    }

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (root == NULL) {
        fprintf(stderr, "goget: could not parse GitHub releases response\n");
        return -1;
    }

    cJSON *assets_json = cJSON_GetObjectItemCaseSensitive(root, "assets");
    if (!cJSON_IsArray(assets_json) || cJSON_GetArraySize(assets_json) == 0) {
        cJSON_Delete(root);
        return 1;
    }

    size_t n = (size_t)cJSON_GetArraySize(assets_json);
    asset_t *assets = xmalloc(n * sizeof(asset_t));
    size_t count = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, assets_json) {
        cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "name");
        cJSON *dl_url = cJSON_GetObjectItemCaseSensitive(item, "browser_download_url");
        if (cJSON_IsString(name) && cJSON_IsString(dl_url)) {
            assets[count].name = xstrdup(name->valuestring);
            assets[count].url = xstrdup(dl_url->valuestring);
            count++;
        }
    }
    cJSON_Delete(root);

    arch_t host = host_arch();
    size_t *order = xmalloc((count > 0 ? count : 1) * sizeof(size_t));
    size_t n_order = 0;

    for (size_t i = 0; i < count; i++) {
        if (checksum_is_checksum_filename(assets[i].name)) continue;
        if (looks_like_other_os(assets[i].name)) continue;
        if (looks_like_package_format(assets[i].name)) continue;
        if (arch_from_asset_name(assets[i].name) == host) order[n_order++] = i;
    }
    for (size_t i = 0; i < count; i++) {
        if (checksum_is_checksum_filename(assets[i].name)) continue;
        if (looks_like_other_os(assets[i].name)) continue;
        if (looks_like_package_format(assets[i].name)) continue;
        if (arch_from_asset_name(assets[i].name) == ARCH_UNKNOWN) order[n_order++] = i;
    }

    int result = 1;
    for (size_t k = 0; k < n_order && result == 1; k++) {
        char *binary_path = NULL;
        char *workdir = NULL;
        try_result_t rc = try_asset(assets, count, order[k], short_name, &binary_path, &workdir);

        if (rc == TRY_HARD_FAIL) {
            result = -1;
        } else if (rc == TRY_SUCCESS) {
            char *dest = path_join("/usr/local/bin", short_name);
            printf("goget: installing to %s (sudo install)...\n", dest);
            char *const argv[] = {"sudo", "install", "-Dm755", binary_path, dest, NULL};
            int install_rc = run_command(argv);
            free(dest);
            free(binary_path);
            remove_dir_recursive(workdir);
            free(workdir);
            if (install_rc == 0) {
                result = 0;
            } else {
                fprintf(stderr, "goget: install step failed\n");
                result = -1;
            }
        }
        /* TRY_SKIP: loop continues to the next candidate. */
    }

    assets_free(assets, count);
    free(order);
    return result;
}
