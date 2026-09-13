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

#endif
