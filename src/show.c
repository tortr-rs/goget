#include "show.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "cJSON.h"
#include "net.h"
#include "util.h"

static char *url_encode_slashes(const char *s) {
    size_t len = strlen(s);
    char *out = xmalloc(len * 3 + 1);
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '/') {
            out[j++] = '%';
            out[j++] = '2';
            out[j++] = 'F';
        } else {
            out[j++] = s[i];
        }
    }
    out[j] = '\0';
    return out;
}

static char *fetch_json_field_str(const char *url, const char *field) {
    long status = 0;
    char *body = net_get(url, NULL, &status);
    if (status < 200 || status >= 300) {
        free(body);
        return NULL;
    }
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (root == NULL) return NULL;
    cJSON *f = cJSON_GetObjectItemCaseSensitive(root, field);
    char *result = cJSON_IsString(f) ? xstrdup(f->valuestring) : NULL;
    cJSON_Delete(root);
    return result;
}

static char *get_default_branch(const char *host, const char *owner, const char *repo) {
    char *url = NULL;
    char *result = NULL;

    if (strcmp(host, "github.com") == 0) {
        size_t len = strlen("https://api.github.com/repos//") + strlen(owner) + strlen(repo) + 1;
        url = xmalloc(len);
        snprintf(url, len, "https://api.github.com/repos/%s/%s", owner, repo);
        result = fetch_json_field_str(url, "default_branch");
    } else if (strcmp(host, "gitlab.com") == 0) {
        char *combined = xmalloc(strlen(owner) + 1 + strlen(repo) + 1);
        snprintf(combined, strlen(owner) + 1 + strlen(repo) + 1, "%s/%s", owner, repo);
        char *encoded = url_encode_slashes(combined);
        free(combined);
        size_t len = strlen("https://gitlab.com/api/v4/projects/") + strlen(encoded) + 1;
        url = xmalloc(len);
        snprintf(url, len, "https://gitlab.com/api/v4/projects/%s", encoded);
        free(encoded);
        result = fetch_json_field_str(url, "default_branch");
    } else if (strcmp(host, "codeberg.org") == 0) {
        size_t len = strlen("https://codeberg.org/api/v1/repos//") + strlen(owner) + strlen(repo) + 1;
        url = xmalloc(len);
        snprintf(url, len, "https://codeberg.org/api/v1/repos/%s/%s", owner, repo);
        result = fetch_json_field_str(url, "default_branch");
    }

    free(url);
    return result;
}

static char *build_raw_url(const char *host, const char *owner, const char *repo,
                             const char *branch, const char *path) {
    size_t len = strlen(host) + strlen(owner) + strlen(repo) + strlen(branch) + strlen(path) + 64;
    char *url = xmalloc(len);
    if (strcmp(host, "github.com") == 0) {
        snprintf(url, len, "https://raw.githubusercontent.com/%s/%s/%s/%s", owner, repo, branch, path);
    } else if (strcmp(host, "gitlab.com") == 0) {
        snprintf(url, len, "https://gitlab.com/%s/%s/-/raw/%s/%s", owner, repo, branch, path);
    } else if (strcmp(host, "codeberg.org") == 0) {
        snprintf(url, len, "https://codeberg.org/%s/%s/raw/branch/%s/%s", owner, repo, branch, path);
    } else {
        url[0] = '\0';
    }
    return url;
}

static char *try_fetch_raw(const char *host, const char *owner, const char *repo,
                             const char *branch, const char *path) {
    char *url = build_raw_url(host, owner, repo, branch, path);
    long status = 0;
    char *body = net_get(url, NULL, &status);
    free(url);
    if (status < 200 || status >= 300) {
        free(body);
        return NULL;
    }
    return body;
}

int show_fetch(const char *host, const char *owner, const char *repo, show_content_t *out) {
    out->readme = NULL;
    out->build_script = NULL;
    out->build_script_label = NULL;

    if (strcmp(host, "github.com") != 0 && strcmp(host, "gitlab.com") != 0 &&
        strcmp(host, "codeberg.org") != 0) {
        return 1;
    }

    char *branch = get_default_branch(host, owner, repo);
    if (branch == NULL) {
        fprintf(stderr, "goget: could not determine default branch for %s/%s on %s\n", owner, repo, host);
        return -1;
    }

    static const char *readme_candidates[] = {"README.md", "README", "README.rst", "README.txt", NULL};
    for (int i = 0; readme_candidates[i] != NULL && out->readme == NULL; i++) {
        out->readme = try_fetch_raw(host, owner, repo, branch, readme_candidates[i]);
    }

    /* Same priority order as buildsys_detect(). */
    static const char *build_candidates[] = {"CMakeLists.txt", "configure", "Makefile", NULL};
    for (int i = 0; build_candidates[i] != NULL && out->build_script == NULL; i++) {
        out->build_script = try_fetch_raw(host, owner, repo, branch, build_candidates[i]);
        if (out->build_script != NULL) out->build_script_label = build_candidates[i];
    }

    free(branch);
    return 0;
}

void show_content_free(show_content_t *content) {
    free(content->readme);
    free(content->build_script);
}

static void write_all(int fd, const char *buf, size_t len) {
    size_t written = 0;
    while (written < len) {
        ssize_t n = write(fd, buf + written, len - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        written += (size_t)n;
    }
}

void show_page(const char *text) {
    const char *pager = getenv("PAGER");
    if (pager == NULL || *pager == '\0') pager = "less";

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        perror("goget: pipe");
        fputs(text, stdout);
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("goget: fork");
        close(pipefd[0]);
        close(pipefd[1]);
        fputs(text, stdout);
        return;
    }

    if (pid == 0) {
        close(pipefd[1]);
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);
        execlp(pager, pager, NULL);
        fprintf(stderr, "goget: failed to run pager '%s': %m\n", pager);
        _exit(127);
    }

    close(pipefd[0]);
    write_all(pipefd[1], text, strlen(text));
    close(pipefd[1]);

    int status;
    waitpid(pid, &status, 0);
}
