#ifndef GOGET_PROMPT_H
#define GOGET_PROMPT_H

/* Every interactive prompt in goget goes through this module, which reads
 * lines from stdin via one consistent fgets()-based path. That matters
 * because a single run can chain multiple prompts (e.g. "host disabled,
 * allow it?" followed later by "no compatible binary, build from source
 * instead?"), and they must not lose input the user already typed ahead
 * of a prompt appearing -- always reading through the same stdio stream
 * with the same read function guarantees that. */

/* Prints "message [Y/n] " (or "[y/N] " if default_yes is 0), reads a
 * line, and returns 1 for yes, 0 for no. Empty input takes the default.
 * Reprompts on unrecognized input; returns 0 if stdin hits EOF. */
int prompt_yes_no(const char *message, int default_yes);

/* Prints labels as a 1-based numbered list and reads a selection.
 * Returns the 0-based index chosen, or -1 if the user typed 'c'/'C' to
 * cancel or stdin hit EOF. Reprompts on invalid input. */
int prompt_pick(const char *const *labels, int n_labels);

/* Three-way prompt (config host allow-list override). Returns 'o', 'a',
 * or 'c'. Reprompts on invalid input; returns 'c' on EOF. */
int prompt_once_always_cancel(const char *message);

/* Prints message (no trailing newline added), reads one line, and
 * returns a heap-allocated trimmed copy (may be an empty string). Caller
 * frees. Returns NULL on EOF. */
char *prompt_line(const char *message);

#endif
