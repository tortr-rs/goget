#include "makeuser.h"

#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "proc.h"
#include "prompt.h"
#include "util.h"

/* Best-effort wipe before freeing a password buffer. A plain memset
 * right before free() can legally be optimized away by the compiler
 * since the buffer is "dead" afterward; routing through a volatile
 * pointer defeats that. Not a cryptographic guarantee (the string could
 * still have been copied elsewhere, e.g. into libc's own internals), but
 * better than leaving a plaintext password sitting in freed heap memory
 * for longer than necessary. */
static void secure_clear(char *s) {
    if (!s) return;
    volatile char *p = (volatile char *)s;
    while (*p) *p++ = '\0';
}

static int group_exists(const char *name) {
    return getgrnam(name) != NULL;
}

/* Reads a line with terminal echo disabled, falling back to a plain read
 * if stdin isn't a tty (e.g. piped input during testing) rather than
 * failing outright. Never prints asterisks or any other stand-in for the
 * typed characters -- genuinely suppresses echo via termios, matching
 * how passwd/sudo prompts behave. */
static char *read_masked_line(const char *prompt_msg) {
    printf("%s", prompt_msg);
    fflush(stdout);

    struct termios oldt;
    int have_tty = (tcgetattr(STDIN_FILENO, &oldt) == 0);

    if (have_tty) {
        struct termios newt = oldt;
        newt.c_lflag &= ~((tcflag_t)ECHO);
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &newt);
    }

    char buf[256];
    char *result = NULL;
    if (fgets(buf, sizeof buf, stdin)) {
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';
        result = xstrdup(buf);
    }

    if (have_tty) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &oldt);
    }
    printf("\n"); /* the Enter keypress's newline was never echoed either */
    return result;
}

/* Resolves which group grants admin/sudo privileges on this system.
 * Only one of wheel/sudo existing is used automatically; if both exist
 * or neither does, the operator running makeuser is asked (a runtime
 * ambiguity, not something a fixed heuristic should silently guess). */
static char *detect_admin_group(void) {
    int has_wheel = group_exists("wheel");
    int has_sudo = group_exists("sudo");

    if (has_wheel && !has_sudo) return xstrdup("wheel");
    if (has_sudo && !has_wheel) return xstrdup("sudo");

    if (has_wheel && has_sudo) {
        printf("Both 'wheel' and 'sudo' admin groups exist on this system.\n");
        const char *labels[] = {"wheel", "sudo"};
        int idx = prompt_pick(labels, 2);
        if (idx < 0) return NULL;
        return xstrdup(labels[idx]);
    }

    printf("Neither 'wheel' nor 'sudo' admin group exists on this system.\n");
    char *name = prompt_line("Admin group name to use (created if it doesn't exist): ");
    if (name == NULL || *name == '\0') {
        free(name);
        return NULL;
    }
    return name;
}

int makeuser_run(void) {
    if (geteuid() != 0) {
        fprintf(stderr, "goget: makeuser must be run as root.\n");
        return 1;
    }

    char *username = prompt_line("Username: ");
    if (username == NULL || *username == '\0') {
        fprintf(stderr, "goget: no username given, aborting.\n");
        free(username);
        return 1;
    }

    char *password = NULL;
    for (int attempt = 0; attempt < 3 && password == NULL; attempt++) {
        char *p1 = read_masked_line("Password: ");
        char *p2 = read_masked_line("Confirm password: ");
        if (p1 != NULL && p2 != NULL && *p1 != '\0' && strcmp(p1, p2) == 0) {
            password = p1;
            secure_clear(p2);
            free(p2);
        } else {
            printf("Passwords did not match (or were empty); try again.\n");
            secure_clear(p1);
            free(p1);
            secure_clear(p2);
            free(p2);
        }
    }
    if (password == NULL) {
        fprintf(stderr, "goget: too many failed password attempts, aborting.\n");
        free(username);
        return 1;
    }

    char *group = detect_admin_group();
    int add_to_admin = 0;
    if (group != NULL) {
        char msg[160];
        snprintf(msg, sizeof msg, "Add '%s' to the admin group (%s)?", username, group);
        add_to_admin = prompt_yes_no(msg, 0);
    }

    char *const useradd_argv[] = {"useradd", "-m", username, NULL};
    int rc = run_command(useradd_argv);
    if (rc != 0) {
        fprintf(stderr, "goget: useradd failed for '%s'\n", username);
        secure_clear(password);
        free(password);
        free(username);
        free(group);
        return 1;
    }

    /* chpasswd (not passwd) because it takes "user:password" over stdin
     * non-interactively; plain passwd on many distros (including this
     * one) has no --stdin option and expects an interactive terminal. */
    size_t input_len = strlen(username) + 1 + strlen(password) + 2;
    char *chpasswd_input = xmalloc(input_len);
    snprintf(chpasswd_input, input_len, "%s:%s\n", username, password);
    secure_clear(password);
    free(password);

    char *const chpasswd_argv[] = {"chpasswd", NULL};
    rc = run_command_with_stdin(chpasswd_argv, chpasswd_input);
    secure_clear(chpasswd_input);
    free(chpasswd_input);
    if (rc != 0) {
        fprintf(stderr, "goget: setting password failed for '%s'\n", username);
        free(username);
        free(group);
        return 1;
    }

    if (add_to_admin && group != NULL) {
        if (!group_exists(group)) {
            char *const groupadd_argv[] = {"groupadd", group, NULL};
            run_command(groupadd_argv); /* usermod below fails loudly if this didn't work */
        }
        char *const usermod_argv[] = {"usermod", "-aG", group, username, NULL};
        rc = run_command(usermod_argv);
        if (rc != 0) {
            fprintf(stderr, "goget: usermod failed to add '%s' to group '%s'\n", username, group);
            free(username);
            free(group);
            return 1;
        }
    }

    printf("goget: created user '%s'%s\n", username, add_to_admin ? " (admin)" : "");
    free(username);
    free(group);
    return 0;
}
