#ifndef GOGET_BUILDSYS_H
#define GOGET_BUILDSYS_H

#include <stddef.h>

typedef enum {
    BUILD_NONE,
    BUILD_CMAKE,
    BUILD_MAKE,
    BUILD_AUTOTOOLS,
} buildsys_kind_t;

/* Detects the build system in dir by checking for marker files, in this
 * priority order: CMakeLists.txt -> cmake; an executable "configure"
 * script -> autotools; a Makefile -> make. Returns BUILD_NONE if none of
 * those are present. */
buildsys_kind_t buildsys_detect(const char *dir);

const char *buildsys_name(buildsys_kind_t kind);

/* Builds and installs the project in dir using the given build system.
 * extra_cmake_flags (may be NULL if n_extra_flags is 0) are appended to
 * the cmake configure step only, for USE-flag-derived options like
 * -DENABLE_WAYLAND; other build systems ignore them for now.
 *
 * Install steps run under sudo, since they write outside the user's home
 * directory. Returns 0 on success. A failure at any step (including a
 * missing "install" target) is reported to stderr by the underlying tool
 * and this function returns nonzero -- it never silently treats a failed
 * install as success. */
int buildsys_build_and_install(buildsys_kind_t kind, const char *dir,
                                 char **extra_cmake_flags, size_t n_extra_flags);

#endif
