#include "compat.h"

#include <ctype.h>
#include <glob.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>

#include "proc.h"
#include "util.h"

arch_t host_arch(void) {
    struct utsname u;
    if (uname(&u) != 0) return ARCH_UNKNOWN;
    if (strcmp(u.machine, "x86_64") == 0) return ARCH_AMD64;
    if (strcmp(u.machine, "aarch64") == 0 || strcmp(u.machine, "arm64") == 0) return ARCH_ARM64;
    if (str_starts_with(u.machine, "arm")) return ARCH_ARM;
    if (strcmp(u.machine, "i386") == 0 || strcmp(u.machine, "i486") == 0 ||
        strcmp(u.machine, "i586") == 0 || strcmp(u.machine, "i686") == 0) {
        return ARCH_386;
    }
    return ARCH_UNKNOWN;
}

arch_t arch_from_asset_name(const char *name) {
    if (str_ci_contains(name, "aarch64") || str_ci_contains(name, "arm64")) return ARCH_ARM64;
    if (str_ci_contains(name, "x86_64") || str_ci_contains(name, "amd64")) return ARCH_AMD64;
    if (str_ci_contains(name, "armv7") || str_ci_contains(name, "armhf") ||
        str_ci_contains(name, "arm32")) {
        return ARCH_ARM;
    }
    if (str_ci_contains(name, "i386") || str_ci_contains(name, "i686") ||
        str_ci_contains(name, "386")) {
        return ARCH_386;
    }
    return ARCH_UNKNOWN;
}

const char *arch_name(arch_t a) {
    switch (a) {
        case ARCH_AMD64: return "amd64";
        case ARCH_ARM64: return "arm64";
        case ARCH_ARM: return "arm";
        case ARCH_386: return "386";
        default: return "unknown";
    }
}

int host_is_musl(void) {
    static const char *patterns[] = {
        "/lib/ld-musl-*.so.1",
        "/lib64/ld-musl-*.so.1",
        "/usr/lib/ld-musl-*.so.1",
    };
    for (size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); i++) {
        glob_t g;
        int rc = glob(patterns[i], 0, NULL, &g);
        int found = (rc == 0 && g.gl_pathc > 0);
        globfree(&g);
        if (found) return 1;
    }
    return 0;
}

char *host_glibc_version(void) {
    if (host_is_musl()) return NULL;

    char *const argv[] = {"ldd", "--version", NULL};
    int status;
    char *out = run_command_capture(argv, &status);

    char *newline = strchr(out, '\n');
    size_t linelen = newline != NULL ? (size_t)(newline - out) : strlen(out);
    char *line = xstrndup(out, linelen);
    free(out);

    char *last_space = strrchr(line, ' ');
    char *token = last_space != NULL ? last_space + 1 : line;

    char *result = NULL;
    if (isdigit((unsigned char)token[0])) {
        result = xstrdup(token);
    }
    free(line);
    return result;
}

binary_libc_t binary_libc_kind(const char *path) {
    /* Confirm this is actually a valid ELF file for this platform first.
     * Without this check, a non-ELF file (e.g. a FreeBSD .pkg, which is
     * a completely different archive format that happens to have no
     * recognized extension in our filters) makes `readelf -p .interp`
     * fail with empty output -- indistinguishable from a real
     * statically-linked ELF's empty .interp dump, which would otherwise
     * get misclassified as LIBC_STATIC and wrongly deemed compatible. */
    char *const hdr_argv[] = {"readelf", "-h", (char *)path, NULL};
    int hdr_status;
    char *hdr_out = run_command_capture(hdr_argv, &hdr_status);
    int is_elf = (hdr_status == 0 && strlen(hdr_out) > 0);
    if (!is_elf) {
        free(hdr_out);
        return LIBC_UNKNOWN;
    }

    /* Defense in depth beyond filename-based OS filtering: reject ELF
     * binaries whose OS/ABI header field names a non-Linux UNIX. Not
     * exhaustive -- many cross-platform toolchains (and Haiku, which has
     * no registered ELFOSABI value at all) leave this as the generic
     * "UNIX - System V" default regardless of actual target -- but it's
     * free to check since the header dump is already in hand. */
    static const char *foreign_abi_tokens[] = {
        "FreeBSD", "NetBSD", "OpenBSD", "Solaris", "HP-UX", "IRIX", "AIX", "Tru64",
    };
    for (size_t i = 0; i < sizeof(foreign_abi_tokens) / sizeof(foreign_abi_tokens[0]); i++) {
        if (str_ci_contains(hdr_out, foreign_abi_tokens[i])) {
            free(hdr_out);
            return LIBC_UNKNOWN;
        }
    }
    free(hdr_out);

    char *const argv[] = {"readelf", "-p", ".interp", (char *)path, NULL};
    int status;
    char *out = run_command_capture(argv, &status);

    binary_libc_t result;
    if (strlen(out) == 0) {
        /* Valid ELF with no .interp section dumped: statically linked,
         * so there's no dynamic libc dependency to check. */
        result = LIBC_STATIC;
    } else if (str_ci_contains(out, "musl")) {
        result = LIBC_MUSL;
    } else if (str_ci_contains(out, "ld-linux") || str_ci_contains(out, "ld.so") ||
               str_ci_contains(out, "ld64.so")) {
        result = LIBC_GLIBC;
    } else {
        result = LIBC_UNKNOWN;
    }
    free(out);
    (void)status;
    return result;
}

static long version_component(const char *v, int idx) {
    const char *p = v;
    for (int cur = 0; cur < idx; cur++) {
        p = strchr(p, '.');
        if (!p) return 0;
        p++;
    }
    return strtol(p, NULL, 10);
}

int compare_version(const char *a, const char *b) {
    for (int i = 0; i < 4; i++) {
        long va = version_component(a, i);
        long vb = version_component(b, i);
        if (va != vb) return va < vb ? -1 : 1;
    }
    return 0;
}

char *binary_min_glibc_version(const char *path) {
    char *const argv[] = {"objdump", "-T", (char *)path, NULL};
    int status;
    char *out = run_command_capture(argv, &status);

    char *best = NULL;
    const char *p = out;
    while ((p = strstr(p, "GLIBC_")) != NULL) {
        p += 6;
        const char *start = p;
        while (isdigit((unsigned char)*p) || *p == '.') p++;
        if (p > start) {
            char *ver = xstrndup(start, (size_t)(p - start));
            if (best == NULL || compare_version(ver, best) > 0) {
                free(best);
                best = ver;
            } else {
                free(ver);
            }
        }
    }
    free(out);
    return best;
}

int binary_is_compatible(const char *path, const char *asset_name) {
    arch_t asset_arch = arch_from_asset_name(asset_name);
    arch_t host = host_arch();
    if (asset_arch != ARCH_UNKNOWN && asset_arch != host) {
        return 0;
    }

    binary_libc_t kind = binary_libc_kind(path);
    if (kind == LIBC_STATIC) return 1;
    if (kind == LIBC_UNKNOWN) return 0;

    int musl_host = host_is_musl();
    if (kind == LIBC_MUSL) return musl_host ? 1 : 0;
    if (musl_host) return 0; /* kind == LIBC_GLIBC on a musl host */

    char *host_ver = host_glibc_version();
    char *bin_ver = binary_min_glibc_version(path);
    int compatible = 1;
    if (host_ver != NULL && bin_ver != NULL && compare_version(bin_ver, host_ver) > 0) {
        compatible = 0;
    }
    free(host_ver);
    free(bin_ver);
    return compatible;
}
