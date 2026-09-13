#ifndef GOGET_CHECKSUM_H
#define GOGET_CHECKSUM_H

#include <stddef.h>

typedef struct {
    const char *name;
    const char *url;
} checksum_candidate_t;

typedef enum {
    CHECKSUM_OK,       /* a checksum file was found and it matched */
    CHECKSUM_MISMATCH, /* a checksum file was found but did NOT match -- hard fail */
    CHECKSUM_MISSING,  /* no checksum file found (or it had no entry for
                         * this asset) -- warn and continue */
} checksum_result_t;

/* True if name looks like a checksum manifest (*.sha256, *.sha256sum,
 * or containing "checksums"/"sha256sums"). */
int checksum_is_checksum_filename(const char *name);

/* Looks for a checksum-style asset among candidates, downloads it, finds
 * the entry for asset_name within it (or, for a single-hash-only file,
 * takes it unconditionally), and compares against the actual sha256 of
 * the local file at local_path (via the system `sha256sum`). */
checksum_result_t checksum_verify(const checksum_candidate_t *candidates, size_t n_candidates,
                                    const char *asset_name, const char *local_path);

#endif
