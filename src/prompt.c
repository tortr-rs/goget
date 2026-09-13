#include "prompt.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

/* The one place in goget that reads a line of interactive input. Every
 * prompt function below goes through this, never fgets/scanf directly. */
static int read_line(char *buf, size_t bufsize) {
    if (!fgets(buf, (int)bufsize, stdin)) {
        return 0;
    }
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') {
        buf[len - 1] = '\0';
    }
    return 1;
}

int prompt_yes_no(const char *message, int default_yes) {
    char line[64];
    for (;;) {
        printf("%s [%s] ", message, default_yes ? "Y/n" : "y/N");
        fflush(stdout);
        if (!read_line(line, sizeof line)) {
            printf("\n");
            return 0;
        }
        char *s = str_trim(line);
        if (*s == '\0') return default_yes;
        if (s[0] == 'y' || s[0] == 'Y') return 1;
        if (s[0] == 'n' || s[0] == 'N') return 0;
        printf("Please answer 'y' or 'n'.\n");
    }
}

int prompt_pick(const char *const *labels, int n_labels) {
    char line[64];
    for (;;) {
        for (int i = 0; i < n_labels; i++) {
            printf("  %d) %s\n", i + 1, labels[i]);
        }
        printf("Select 1-%d, or 'c' to cancel: ", n_labels);
        fflush(stdout);
        if (!read_line(line, sizeof line)) {
            printf("\n");
            return -1;
        }
        char *s = str_trim(line);
        if (*s == 'c' || *s == 'C') return -1;
        if (*s == '\0') {
            printf("Invalid selection.\n");
            continue;
        }
        char *endptr;
        long v = strtol(s, &endptr, 10);
        if (*endptr == '\0' && v >= 1 && v <= n_labels) {
            return (int)(v - 1);
        }
        printf("Invalid selection.\n");
    }
}

int prompt_once_always_cancel(const char *message) {
    char line[64];
    for (;;) {
        printf("%s [Once/Always/Cancel] ", message);
        fflush(stdout);
        if (!read_line(line, sizeof line)) {
            printf("\n");
            return 'c';
        }
        char *s = str_trim(line);
        if (*s != '\0') {
            char c = (char)tolower((unsigned char)s[0]);
            if (c == 'o') return 'o';
            if (c == 'a') return 'a';
            if (c == 'c') return 'c';
        }
        printf("Please choose Once, Always, or Cancel.\n");
    }
}

char *prompt_line(const char *message) {
    char line[256];
    printf("%s", message);
    fflush(stdout);
    if (!read_line(line, sizeof line)) {
        return NULL;
    }
    return xstrdup(str_trim(line));
}
