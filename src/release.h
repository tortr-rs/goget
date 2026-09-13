#ifndef GOGET_RELEASE_H
#define GOGET_RELEASE_H

/* Attempts to install a compatible prebuilt release binary for a
 * fully-resolved repo. Fetch is GitHub-Releases-specific (per spec) --
 * gitlab/codeberg repos can still be built from source via `goget
 * build`, just not fetched as prebuilt binaries.
 *
 * Tries every plausible Linux release asset (matching this host's
 * architecture first, then arch-unnamed assets) until one passes the
 * compatibility check (architecture, musl-vs-glibc, glibc version), then
 * verifies its checksum if a manifest is present, then installs it.
 *
 * Returns:
 *    0  success
 *    1  no compatible release found -- caller should offer the
 *       "must build from source" fallback prompt
 *   -1  hard failure (checksum mismatch, network/API error, install
 *       step failed) -- caller should abort, not fall back silently
 */
int release_fetch_and_install(const char *owner, const char *repo, const char *short_name);

#endif
