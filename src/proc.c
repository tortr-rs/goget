#include "proc.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include "util.h"

int run_command_in(const char *cwd, char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("goget: fork");
        return -1;
    }

    if (pid == 0) {
        if (cwd != NULL && chdir(cwd) != 0) {
            perror("goget: chdir");
            _exit(127);
        }
        execvp(argv[0], argv);
        fprintf(stderr, "goget: failed to run '%s': %m\n", argv[0]);
        _exit(127);
    }

    int status;
    if (waitpid(pid, &status, 0) < 0) {
        perror("goget: waitpid");
        return -1;
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        fprintf(stderr, "goget: '%s' was killed by signal %d\n", argv[0], WTERMSIG(status));
    }
    return -1;
}

int run_command(char *const argv[]) {
    return run_command_in(NULL, argv);
}

char *run_command_capture(char *const argv[], int *out_status) {
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        perror("goget: pipe");
        if (out_status) *out_status = -1;
        return xstrdup("");
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("goget: fork");
        close(pipefd[0]);
        close(pipefd[1]);
        if (out_status) *out_status = -1;
        return xstrdup("");
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        execvp(argv[0], argv);
        _exit(127);
    }

    close(pipefd[1]);

    size_t cap = 4096, len = 0;
    char *buf = xmalloc(cap);
    ssize_t n;
    while ((n = read(pipefd[0], buf + len, cap - len - 1)) > 0) {
        len += (size_t)n;
        if (len + 1 >= cap) {
            cap *= 2;
            buf = xrealloc(buf, cap);
        }
    }
    close(pipefd[0]);
    buf[len] = '\0';

    int status;
    waitpid(pid, &status, 0);
    if (out_status) {
        *out_status = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
    return buf;
}

int run_command_with_stdin(char *const argv[], const char *input) {
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        perror("goget: pipe");
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("goget: fork");
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        close(pipefd[1]);
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);
        execvp(argv[0], argv);
        fprintf(stderr, "goget: failed to run '%s': %m\n", argv[0]);
        _exit(127);
    }

    close(pipefd[0]);
    size_t len = strlen(input);
    size_t written = 0;
    while (written < len) {
        ssize_t n = write(pipefd[1], input + written, len - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        written += (size_t)n;
    }
    close(pipefd[1]);

    int status;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
