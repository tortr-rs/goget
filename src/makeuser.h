#ifndef GOGET_MAKEUSER_H
#define GOGET_MAKEUSER_H

/* Interactively creates a new user account: prompts for username,
 * masked password (twice, to confirm), and whether to add them to the
 * system's admin group (wheel or sudo, auto-detected; the operator is
 * asked if both or neither exist). Wraps useradd + chpasswd + usermod
 * -aG <group>. Must be run as root; fails immediately and clearly
 * otherwise. Returns 0 on success. */
int makeuser_run(void);

#endif
