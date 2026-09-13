#ifndef GOGET_CONFIG_H
#define GOGET_CONFIG_H

#include <stddef.h>

typedef struct {
    char *name;
    int on;
} use_flag_t;

typedef struct {
    char *package; /* NULL for the global pkg.USE block */
    use_flag_t *flags;
    size_t n_flags;
} use_block_t;

typedef struct {
    int github_on;
    int gitlab_on;
    int codeberg_on;
    use_block_t *use_blocks; /* one entry may have package == NULL (the
                                * global pkg.USE{} block); the rest are
                                * per-package pkg.<name>.USE{} overrides */
    size_t n_use_blocks;
} goget_config_t;

/* Loads ~/.config/goget/goget.conf, creating it with all-hosts-enabled
 * defaults if missing. Hard-errors (prints a message and exits(1)) on a
 * malformed file or an unrecognized pkg.<key>{} block key -- this is a
 * user-facing config file, so a typo should be loud, not silently
 * ignored. */
goget_config_t *config_load(void);
void config_free(goget_config_t *cfg);

/* host is a canonical hostname: "github.com", "gitlab.com", or
 * "codeberg.org". */
int config_host_enabled(const goget_config_t *cfg, const char *host);
void config_set_host_enabled(goget_config_t *cfg, const char *host, int enabled);

/* Rewrites the config file in canonical form from cfg's current state.
 * This is a full regeneration (the parser doesn't track original
 * formatting), so hand-added comments won't survive a save triggered by
 * "Always allow this host" -- acceptable for a config format this small. */
int config_save(const goget_config_t *cfg);

/* Resolves a USE flag's effective value for a package: a
 * pkg.<package>.USE{} override beats pkg.USE{} (global) beats
 * default-on. */
int config_use_flag(const goget_config_t *cfg, const char *package, const char *flag);

/* Prints the fully-resolved current configuration ("goget config"). */
void config_print(const goget_config_t *cfg);

/* Translates a package's effective USE flags into extra CMake -D
 * arguments via a small hardcoded per-package flag table (fastfetch is
 * the initial test case: wayland/x11/pulseaudio). Returns a
 * newly-allocated NULL-terminated array of newly-allocated strings
 * (caller frees each string, then the array); *out_count receives the
 * count (excluding the NULL terminator). A package absent from the
 * table, or a flag with no mapping for it, contributes nothing. */
char **config_build_use_cmake_args(const goget_config_t *cfg, const char *package, size_t *out_count);

#endif
