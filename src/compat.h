#ifndef GOGET_COMPAT_H
#define GOGET_COMPAT_H

/* Canonical architecture buckets, so "x86_64"/"amd64" and
 * "aarch64"/"arm64" (different projects name the same thing
 * differently) compare equal. */
typedef enum {
    ARCH_UNKNOWN,
    ARCH_AMD64,
    ARCH_ARM64,
    ARCH_ARM,
    ARCH_386,
} arch_t;

arch_t host_arch(void);

/* Best-effort guess at the architecture a release asset's filename
 * refers to, from common naming conventions. ARCH_UNKNOWN if nothing
 * recognizable is found (caller should then not reject on arch alone). */
arch_t arch_from_asset_name(const char *name);

const char *arch_name(arch_t a);

/* True if this host uses musl libc (detected via /lib/ld-musl-*.so.1). */
int host_is_musl(void);

/* Heap-allocated "major.minor" for the host's glibc, or NULL if the host
 * is musl or the version couldn't be determined. */
char *host_glibc_version(void);

typedef enum {
    LIBC_STATIC,  /* no dynamic interpreter -- libc compatibility doesn't apply */
    LIBC_GLIBC,
    LIBC_MUSL,
    LIBC_UNKNOWN, /* has an interpreter, but not one we recognize */
} binary_libc_t;

binary_libc_t binary_libc_kind(const char *path);

/* Heap-allocated "major.minor" for the minimum glibc version a
 * glibc-linked binary requires (the highest GLIBC_x.y symbol version it
 * references, via `objdump -T`), or NULL if none is found. */
char *binary_min_glibc_version(const char *path);

/* Numeric "major.minor[.patch...]" comparison, like strcmp's sign
 * convention. */
int compare_version(const char *a, const char *b);

/* Full compatibility check: architecture match, musl-vs-glibc mismatch,
 * and (if both glibc) minimum version. asset_name is used only to guess
 * the asset's architecture from its filename. An unrecognized dynamic
 * linker is treated as incompatible -- a wrongly-installed broken binary
 * is worse than an unnecessary source build. Returns 1 if compatible, 0
 * otherwise. */
int binary_is_compatible(const char *path, const char *asset_name);

#endif
