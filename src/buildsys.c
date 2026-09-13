#include "buildsys.h"

#include <stdio.h>
#include <stdlib.h>

#include "proc.h"
#include "util.h"

buildsys_kind_t buildsys_detect(const char *dir) {
    char *path;

    path = path_join(dir, "CMakeLists.txt");
    int has_cmake = path_exists(path);
    free(path);
    if (has_cmake) return BUILD_CMAKE;

    path = path_join(dir, "configure");
    int has_configure = path_is_executable_file(path);
    free(path);
    if (has_configure) return BUILD_AUTOTOOLS;

    path = path_join(dir, "Makefile");
    int has_makefile = path_exists(path);
    free(path);
    if (has_makefile) return BUILD_MAKE;

    path = path_join(dir, "makefile");
    has_makefile = path_exists(path);
    free(path);
    if (has_makefile) return BUILD_MAKE;

    return BUILD_NONE;
}

const char *buildsys_name(buildsys_kind_t kind) {
    switch (kind) {
        case BUILD_CMAKE: return "CMake";
        case BUILD_MAKE: return "Make";
        case BUILD_AUTOTOOLS: return "Autotools";
        default: return "none";
    }
}

static int build_cmake(const char *dir, char **extra_flags, size_t n_extra) {
    char *builddir = path_join(dir, "build");

    size_t argc = 5 + n_extra + 1;
    char **argv = xmalloc(argc * sizeof(char *));
    size_t i = 0;
    argv[i++] = "cmake";
    argv[i++] = "-S";
    argv[i++] = (char *)dir;
    argv[i++] = "-B";
    argv[i++] = builddir;
    for (size_t j = 0; j < n_extra; j++) argv[i++] = extra_flags[j];
    argv[i++] = NULL;

    printf("goget: configuring with CMake...\n");
    int rc = run_command(argv);
    free(argv);
    if (rc != 0) {
        fprintf(stderr, "goget: cmake configure failed\n");
        free(builddir);
        return rc;
    }

    printf("goget: building...\n");
    char *const build_argv[] = {"cmake", "--build", builddir, NULL};
    rc = run_command(build_argv);
    if (rc != 0) {
        fprintf(stderr, "goget: cmake build failed\n");
        free(builddir);
        return rc;
    }

    printf("goget: installing (sudo cmake --install)...\n");
    char *const install_argv[] = {"sudo", "cmake", "--install", builddir, NULL};
    rc = run_command(install_argv);
    free(builddir);
    if (rc != 0) {
        fprintf(stderr, "goget: cmake --install failed\n");
    }
    return rc;
}

static int build_make(const char *dir) {
    printf("goget: building with make...\n");
    char *const make_argv[] = {"make", NULL};
    int rc = run_command_in(dir, make_argv);
    if (rc != 0) {
        fprintf(stderr, "goget: make failed\n");
        return rc;
    }

    printf("goget: installing (sudo make install)...\n");
    char *const install_argv[] = {"sudo", "make", "install", NULL};
    rc = run_command_in(dir, install_argv);
    if (rc != 0) {
        fprintf(stderr,
                "goget: 'make install' failed -- this project may not "
                "define an install target.\n");
    }
    return rc;
}

static int build_autotools(const char *dir) {
    printf("goget: running ./configure...\n");
    char *const configure_argv[] = {"./configure", NULL};
    int rc = run_command_in(dir, configure_argv);
    if (rc != 0) {
        fprintf(stderr, "goget: ./configure failed\n");
        return rc;
    }

    return build_make(dir);
}

int buildsys_build_and_install(buildsys_kind_t kind, const char *dir,
                                 char **extra_cmake_flags, size_t n_extra_flags) {
    switch (kind) {
        case BUILD_CMAKE:
            return build_cmake(dir, extra_cmake_flags, n_extra_flags);
        case BUILD_MAKE:
            return build_make(dir);
        case BUILD_AUTOTOOLS:
            return build_autotools(dir);
        default:
            fprintf(stderr, "goget: internal error: no build system to run\n");
            return -1;
    }
}
