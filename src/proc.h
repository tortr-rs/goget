#ifndef GOGET_PROC_H
#define GOGET_PROC_H

/* Runs an external program via fork+execvp, inheriting stdin/stdout/stderr
 * (so interactive prompts like sudo's password prompt, or git's progress
 * output, work normally). argv must be NULL-terminated, argv[0] the
 * program name/path. No shell is involved, so no argument is ever subject
 * to shell interpretation.
 *
 * cwd may be NULL to inherit the caller's working directory.
 *
 * Returns the child's exit status (0-255) on normal exit, or -1 if the
 * child was killed by a signal or fork/exec itself failed (a message is
 * printed to stderr in the exec-failure case). */
int run_command_in(const char *cwd, char *const argv[]);
int run_command(char *const argv[]);

/* Runs argv (no shell, argv[0] found via PATH) and captures its stdout
 * into a newly-allocated NUL-terminated buffer, discarding stderr to
 * /dev/null. Sets *out_status to the child's exit status (or -1, same
 * meaning as run_command's return). Returns the captured buffer (never
 * NULL, may be empty "") -- caller must free it. Used for parsing tool
 * output (ldd --version, objdump -T, readelf), not for anything
 * interactive. */
char *run_command_capture(char *const argv[], int *out_status);

/* Runs argv, feeding `input` to its stdin (then closing it) instead of
 * inheriting the caller's stdin. Used for chpasswd, which reads
 * "user:password\n" from stdin rather than taking it as an argument (an
 * argument would leak the password via /proc/<pid>/cmdline). Returns the
 * same exit-status convention as run_command. */
int run_command_with_stdin(char *const argv[], const char *input);

#endif
