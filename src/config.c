#include "config.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

static const char *DEFAULT_CONFIG =
    "pkg.repos{github.on gitlab.on codeberg.on}\n";

static char *get_config_dir(void) {
    char *home = get_home_dir();
    char *dir = path_join(home, ".config/goget");
    free(home);
    return dir;
}

static char *get_config_path(void) {
    char *dir = get_config_dir();
    char *path = path_join(dir, "goget.conf");
    free(dir);
    return path;
}

static char *read_whole_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long size = ftell(f);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    char *buf = xmalloc((size_t)size + 1);
    size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[got] = '\0';
    return buf;
}

static void die_config(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "goget: config error: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

/* Parses one whitespace-separated "<name>.on" / "<name>.off" token.
 * Returns a heap-allocated name and sets *on; hard-errors on anything
 * else, since this is a small fixed grammar and a typo should be loud. */
static char *parse_flag_token(const char *token, int *on) {
    size_t len = strlen(token);
    if (len > 3 && strcmp(token + len - 3, ".on") == 0) {
        *on = 1;
        return xstrndup(token, len - 3);
    }
    if (len > 4 && strcmp(token + len - 4, ".off") == 0) {
        *on = 0;
        return xstrndup(token, len - 4);
    }
    die_config("expected '<name>.on' or '<name>.off', got '%s'", token);
    return NULL; /* unreachable */
}

static void parse_block_body(const char *body, use_flag_t **out_flags, size_t *out_count,
                               int is_repos_block, goget_config_t *cfg) {
    use_flag_t *flags = NULL;
    size_t count = 0;

    char *copy = xstrdup(body);
    char *saveptr = NULL;
    char *tok = strtok_r(copy, " \t\r\n", &saveptr);
    while (tok != NULL) {
        int on;
        char *name = parse_flag_token(tok, &on);

        if (is_repos_block) {
            if (strcmp(name, "github") == 0) cfg->github_on = on;
            else if (strcmp(name, "gitlab") == 0) cfg->gitlab_on = on;
            else if (strcmp(name, "codeberg") == 0) cfg->codeberg_on = on;
            else die_config("unrecognized host '%s' in pkg.repos{}", name);
            free(name);
        } else {
            flags = xrealloc(flags, (count + 1) * sizeof(*flags));
            flags[count].name = name;
            flags[count].on = on;
            count++;
        }

        tok = strtok_r(NULL, " \t\r\n", &saveptr);
    }
    free(copy);

    *out_flags = flags;
    *out_count = count;
}

static goget_config_t *parse_config(const char *content) {
    goget_config_t *cfg = xmalloc(sizeof(*cfg));
    cfg->github_on = 1;
    cfg->gitlab_on = 1;
    cfg->codeberg_on = 1;
    cfg->use_blocks = NULL;
    cfg->n_use_blocks = 0;

    const char *p = content;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p == '#') {
            while (*p && *p != '\n') p++;
            continue;
        }
        if (*p == '\0') break;

        if (strncmp(p, "pkg.", 4) != 0) {
            die_config("expected a 'pkg.<key>{...}' block, found: '%.20s'", p);
        }
        p += 4;

        const char *key_start = p;
        while (*p && *p != '{' && !isspace((unsigned char)*p)) p++;
        char *key = xstrndup(key_start, (size_t)(p - key_start));

        while (*p && isspace((unsigned char)*p)) p++;
        if (*p != '{') {
            die_config("expected '{' after 'pkg.%s'", key);
        }
        p++;

        const char *body_start = p;
        while (*p && *p != '}') p++;
        if (*p != '}') {
            die_config("unterminated block 'pkg.%s{'", key);
        }
        char *body = xstrndup(body_start, (size_t)(p - body_start));
        p++;

        if (strcmp(key, "repos") == 0) {
            use_flag_t *unused_flags;
            size_t unused_count;
            parse_block_body(body, &unused_flags, &unused_count, 1, cfg);
        } else if (strcmp(key, "USE") == 0 || str_ends_with(key, ".USE")) {
            char *package = strcmp(key, "USE") == 0 ? NULL : xstrndup(key, strlen(key) - 4);
            use_flag_t *flags;
            size_t count;
            parse_block_body(body, &flags, &count, 0, cfg);

            cfg->use_blocks = xrealloc(cfg->use_blocks, (cfg->n_use_blocks + 1) * sizeof(use_block_t));
            cfg->use_blocks[cfg->n_use_blocks].package = package;
            cfg->use_blocks[cfg->n_use_blocks].flags = flags;
            cfg->use_blocks[cfg->n_use_blocks].n_flags = count;
            cfg->n_use_blocks++;
        } else {
            die_config("unrecognized config block key 'pkg.%s'", key);
        }

        free(key);
        free(body);
    }

    return cfg;
}

goget_config_t *config_load(void) {
    char *path = get_config_path();
    char *content = read_whole_file(path);

    if (content == NULL) {
        char *dir = get_config_dir();
        if (mkdir_p(dir) != 0) {
            perror("goget: could not create config directory");
            free(dir);
            free(path);
            exit(1);
        }
        free(dir);

        FILE *f = fopen(path, "w");
        if (f == NULL) {
            perror("goget: could not create config file");
            free(path);
            exit(1);
        }
        fputs(DEFAULT_CONFIG, f);
        fclose(f);
        content = xstrdup(DEFAULT_CONFIG);
    }
    free(path);

    goget_config_t *cfg = parse_config(content);
    free(content);
    return cfg;
}

void config_free(goget_config_t *cfg) {
    if (!cfg) return;
    for (size_t i = 0; i < cfg->n_use_blocks; i++) {
        free(cfg->use_blocks[i].package);
        for (size_t j = 0; j < cfg->use_blocks[i].n_flags; j++) {
            free(cfg->use_blocks[i].flags[j].name);
        }
        free(cfg->use_blocks[i].flags);
    }
    free(cfg->use_blocks);
    free(cfg);
}

int config_host_enabled(const goget_config_t *cfg, const char *host) {
    if (strcmp(host, "github.com") == 0) return cfg->github_on;
    if (strcmp(host, "gitlab.com") == 0) return cfg->gitlab_on;
    if (strcmp(host, "codeberg.org") == 0) return cfg->codeberg_on;
    return 1; /* hosts outside the three goget.conf tracks are unaffected by the allow-list */
}

void config_set_host_enabled(goget_config_t *cfg, const char *host, int enabled) {
    if (strcmp(host, "github.com") == 0) cfg->github_on = enabled;
    else if (strcmp(host, "gitlab.com") == 0) cfg->gitlab_on = enabled;
    else if (strcmp(host, "codeberg.org") == 0) cfg->codeberg_on = enabled;
}

int config_save(const goget_config_t *cfg) {
    char *path = get_config_path();
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        perror("goget: could not write config file");
        free(path);
        return -1;
    }
    free(path);

    fprintf(f, "pkg.repos{github.%s gitlab.%s codeberg.%s}\n", cfg->github_on ? "on" : "off",
            cfg->gitlab_on ? "on" : "off", cfg->codeberg_on ? "on" : "off");

    for (size_t i = 0; i < cfg->n_use_blocks; i++) {
        const use_block_t *b = &cfg->use_blocks[i];
        if (b->package == NULL) {
            fprintf(f, "pkg.USE{");
        } else {
            fprintf(f, "pkg.%s.USE{", b->package);
        }
        for (size_t j = 0; j < b->n_flags; j++) {
            fprintf(f, "%s%s.%s", j > 0 ? " " : "", b->flags[j].name, b->flags[j].on ? "on" : "off");
        }
        fprintf(f, "}\n");
    }

    fclose(f);
    return 0;
}

int config_use_flag(const goget_config_t *cfg, const char *package, const char *flag) {
    for (size_t i = 0; i < cfg->n_use_blocks; i++) {
        if (cfg->use_blocks[i].package != NULL && strcmp(cfg->use_blocks[i].package, package) == 0) {
            for (size_t j = 0; j < cfg->use_blocks[i].n_flags; j++) {
                if (strcmp(cfg->use_blocks[i].flags[j].name, flag) == 0) {
                    return cfg->use_blocks[i].flags[j].on;
                }
            }
        }
    }
    for (size_t i = 0; i < cfg->n_use_blocks; i++) {
        if (cfg->use_blocks[i].package == NULL) {
            for (size_t j = 0; j < cfg->use_blocks[i].n_flags; j++) {
                if (strcmp(cfg->use_blocks[i].flags[j].name, flag) == 0) {
                    return cfg->use_blocks[i].flags[j].on;
                }
            }
        }
    }
    return 1; /* default-on */
}

void config_print(const goget_config_t *cfg) {
    printf("pkg.repos{github.%s gitlab.%s codeberg.%s}\n", cfg->github_on ? "on" : "off",
           cfg->gitlab_on ? "on" : "off", cfg->codeberg_on ? "on" : "off");

    for (size_t i = 0; i < cfg->n_use_blocks; i++) {
        const use_block_t *b = &cfg->use_blocks[i];
        if (b->package == NULL) {
            printf("pkg.USE{");
        } else {
            printf("pkg.%s.USE{", b->package);
        }
        for (size_t j = 0; j < b->n_flags; j++) {
            printf("%s%s.%s", j > 0 ? " " : "", b->flags[j].name, b->flags[j].on ? "on" : "off");
        }
        printf("}\n");
    }
}

typedef struct {
    const char *flag;
    const char *cmake_options[2];
} flag_mapping_t;

typedef struct {
    const char *package;
    const flag_mapping_t *mappings;
    size_t n_mappings;
} package_flag_table_t;

static const flag_mapping_t FASTFETCH_MAPPINGS[] = {
    {"wayland", {"ENABLE_WAYLAND", NULL}},
    {"x11", {"ENABLE_XCB_RANDR", "ENABLE_XRANDR"}},
    {"pulseaudio", {"ENABLE_PULSE", NULL}},
};

static const package_flag_table_t PACKAGE_FLAG_TABLES[] = {
    {"fastfetch", FASTFETCH_MAPPINGS, sizeof(FASTFETCH_MAPPINGS) / sizeof(FASTFETCH_MAPPINGS[0])},
};

char **config_build_use_cmake_args(const goget_config_t *cfg, const char *package, size_t *out_count) {
    const package_flag_table_t *table = NULL;
    for (size_t i = 0; i < sizeof(PACKAGE_FLAG_TABLES) / sizeof(PACKAGE_FLAG_TABLES[0]); i++) {
        if (strcmp(PACKAGE_FLAG_TABLES[i].package, package) == 0) {
            table = &PACKAGE_FLAG_TABLES[i];
            break;
        }
    }

    char **args = NULL;
    size_t count = 0;
    if (table != NULL) {
        for (size_t i = 0; i < table->n_mappings; i++) {
            const flag_mapping_t *m = &table->mappings[i];
            int on = config_use_flag(cfg, package, m->flag);
            for (int j = 0; j < 2 && m->cmake_options[j] != NULL; j++) {
                args = xrealloc(args, (count + 1) * sizeof(char *));
                size_t len = strlen("-D") + strlen(m->cmake_options[j]) + strlen("=OFF") + 1;
                char *arg = xmalloc(len);
                snprintf(arg, len, "-D%s=%s", m->cmake_options[j], on ? "ON" : "OFF");
                args[count++] = arg;
            }
        }
    }

    args = xrealloc(args, (count + 1) * sizeof(char *));
    args[count] = NULL;
    *out_count = count;
    return args;
}
