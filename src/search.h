#ifndef GOGET_SEARCH_H
#define GOGET_SEARCH_H

#include <stddef.h>

#include "config.h"

typedef struct {
    char *host;
    char *owner;
    char *repo;
    long stars;
} search_result_t;

typedef struct {
    search_result_t *items;
    size_t count;
} search_result_list_t;

/* Searches GitHub, GitLab, and Codeberg (skipping any disabled in cfg)
 * for repos whose name exactly matches `name`, case-insensitively.
 * Results are capped at 3 per host, sorted by stars descending within
 * each host, and ordered github -> gitlab -> codeberg overall. A
 * network/API error on one host yields zero results for that host
 * rather than aborting the whole search (GitHub search runs
 * unauthenticated for now, so it's the most likely to be rate-limited). */
search_result_list_t search_by_name(const goget_config_t *cfg, const char *name);
void search_result_list_free(search_result_list_t *list);

#endif
