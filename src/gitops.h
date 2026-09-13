#ifndef GOGET_GITOPS_H
#define GOGET_GITOPS_H

#include "repospec.h"

/* Returns a heap-allocated https clone URL for a fully-resolved spec:
 * "https://<host>/<owner>/<repo>.git". goget always clones over https
 * (even if the original spec was an ssh:// or git@ URL) since it has no
 * SSH key/agent setup step of its own and https works for any public
 * repo with no auth required. */
char *git_clone_url(const repospec_t *spec);

/* Clones dest_dir fresh if it doesn't already exist (creating parent
 * directories as needed), or runs a fast-forward-only pull if it's
 * already a git checkout. Returns 0 on success, nonzero on failure (git
 * itself already printed the reason to stderr). */
int git_sync(const char *clone_url, const char *dest_dir);

#endif
