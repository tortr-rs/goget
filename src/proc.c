#include "proc.h"

#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>

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
