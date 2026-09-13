#include "checksum.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "net.h"
#include "proc.h"
#include "util.h"

int checksum_is_checksum_filename(const char *name) {
    if (str_ends_with(name, ".sha256") || str_ends_with(name, ".sha256sum")) return 1;
    if (str_ci_contains(name, "checksums") || str_ci_contains(name, "sha256sums")) return 1;
    return 0;
}

/* Parses a checksum manifest looking for asset_name's entry. Handles both
 * the multi-entry "<hash>  <filename>" format (checksums.txt/SHA256SUMS,
 * one line per released file, filename possibly prefixed with '*' for
 * sha256sum's binary mode) and the single-entry "<hash>" format some
 * projects use for a per-asset "<assetname>.sha256" file. */
static char *find_expected_hash(const char *content, const char *asset_name) {
    char *copy = xstrdup(content);
    char *saveptr = NULL;
    char *line = strtok_r(copy, "\n", &saveptr);

    char *matched = NULL;
    char *single_token = NULL;
    int line_count = 0;
    int multi_token_line_seen = 0;

    while (line != NULL) {
        char *l = str_trim(line);
        if (*l != '\0') {
            line_count++;
            char *space = l;
            while (*space && !isspace((unsigned char)*space)) space++;
            char *hash_tok = xstrndup(l, (size_t)(space - l));

            while (*space && isspace((unsigned char)*space)) space++;
            if (*space != '\0') {
                multi_token_line_seen = 1;
                char *fname = space;
                if (*fname == '*') fname++;
                char *base = strrchr(fname, '/');
                base = base ? base + 1 : fname;
                if (strcmp(base, asset_name) == 0 && matched == NULL) {
                    matched = hash_tok;
                } else {
                    free(hash_tok);
                }
            } else if (single_token == NULL) {
                single_token = hash_tok;
            } else {
                free(hash_tok);
            }
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }
    free(copy);

    if (matched != NULL) {
        free(single_token);
        return matched;
    }
    if (!multi_token_line_seen && line_count == 1 && single_token != NULL) {
        return single_token;
    }
    free(single_token);
    return NULL;
}

static char *compute_sha256_hex(const char *path) {
    char *const argv[] = {"sha256sum", (char *)path, NULL};
    int status;
    char *out = run_command_capture(argv, &status);
    if (status != 0) {
        free(out);
        return NULL;
    }
    char *space = strchr(out, ' ');
    size_t len = space ? (size_t)(space - out) : strlen(out);
    char *hash = xstrndup(out, len);
    free(out);
    return hash;
}

checksum_result_t checksum_verify(const checksum_candidate_t *candidates, size_t n_candidates,
                                    const char *asset_name, const char *local_path) {
    const checksum_candidate_t *found = NULL;
    for (size_t i = 0; i < n_candidates; i++) {
        if (checksum_is_checksum_filename(candidates[i].name)) {
            found = &candidates[i];
            break;
        }
    }
    if (found == NULL) return CHECKSUM_MISSING;

    char *content = net_get(found->url, NULL, NULL);
    if (content == NULL || *content == '\0') {
        free(content);
        return CHECKSUM_MISSING;
    }

    char *expected = find_expected_hash(content, asset_name);
    free(content);
    if (expected == NULL) return CHECKSUM_MISSING;

    char *actual = compute_sha256_hex(local_path);
    if (actual == NULL) {
        free(expected);
        return CHECKSUM_MISSING;
    }

    int match = (strcasecmp(expected, actual) == 0);
    free(expected);
    free(actual);
    return match ? CHECKSUM_OK : CHECKSUM_MISMATCH;
}
