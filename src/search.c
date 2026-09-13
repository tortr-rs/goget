#include "search.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"
#include "net.h"
#include "util.h"

static void append_result(search_result_list_t *list, const char *host, const char *owner,
                            const char *repo, long stars) {
    list->items = xrealloc(list->items, (list->count + 1) * sizeof(search_result_t));
    list->items[list->count].host = xstrdup(host);
    list->items[list->count].owner = xstrdup(owner);
    list->items[list->count].repo = xstrdup(repo);
    list->items[list->count].stars = stars;
    list->count++;
}

static int cmp_stars_desc(const void *a, const void *b) {
    const search_result_t *ra = a;
    const search_result_t *rb = b;
    if (ra->stars > rb->stars) return -1;
    if (ra->stars < rb->stars) return 1;
    return 0;
}

static void sort_and_cap(search_result_list_t *list, size_t cap) {
    if (list->count > 1) {
        qsort(list->items, list->count, sizeof(search_result_t), cmp_stars_desc);
    }
    if (list->count > cap) {
        for (size_t i = cap; i < list->count; i++) {
            free(list->items[i].host);
            free(list->items[i].owner);
            free(list->items[i].repo);
        }
        list->count = cap;
    }
}

/* Moves src's items into dst (ownership transfer, no copying strings)
 * and resets src to empty. */
static void merge_into(search_result_list_t *dst, search_result_list_t *src) {
    if (src->count == 0) return;
    dst->items = xrealloc(dst->items, (dst->count + src->count) * sizeof(search_result_t));
    memcpy(dst->items + dst->count, src->items, src->count * sizeof(search_result_t));
    dst->count += src->count;
    free(src->items);
    src->items = NULL;
    src->count = 0;
}

static void fetch_github(const char *name, search_result_list_t *out) {
    char query[300];
    snprintf(query, sizeof query, "\"%s\" in:name", name);
    char *encoded_query = url_encode(query);

    size_t url_len = strlen("https://api.github.com/search/repositories?q=&sort=stars&order=desc") +
                       strlen(encoded_query) + 1;
    char *url = xmalloc(url_len);
    snprintf(url, url_len, "https://api.github.com/search/repositories?q=%s&sort=stars&order=desc",
             encoded_query);
    free(encoded_query);

    const char *headers[] = {"Accept: application/vnd.github+json", NULL};
    long status = 0;
    char *body = net_get(url, headers, &status);
    free(url);
    if (status < 200 || status >= 300) {
        free(body);
        return;
    }

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (root == NULL) return;

    cJSON *items = cJSON_GetObjectItemCaseSensitive(root, "items");
    if (cJSON_IsArray(items)) {
        cJSON *item;
        cJSON_ArrayForEach(item, items) {
            cJSON *jname = cJSON_GetObjectItemCaseSensitive(item, "name");
            cJSON *jowner = cJSON_GetObjectItemCaseSensitive(item, "owner");
            cJSON *jstars = cJSON_GetObjectItemCaseSensitive(item, "stargazers_count");
            if (!cJSON_IsString(jname) || !cJSON_IsObject(jowner)) continue;
            if (strcasecmp(jname->valuestring, name) != 0) continue;
            cJSON *jlogin = cJSON_GetObjectItemCaseSensitive(jowner, "login");
            if (!cJSON_IsString(jlogin)) continue;
            long stars = cJSON_IsNumber(jstars) ? (long)jstars->valuedouble : 0;
            append_result(out, "github.com", jlogin->valuestring, jname->valuestring, stars);
        }
    }
    cJSON_Delete(root);
}

static void fetch_gitlab(const char *name, search_result_list_t *out) {
    char *encoded = url_encode(name);
    size_t url_len =
        strlen("https://gitlab.com/api/v4/projects?search=&order_by=star_count&sort=desc&per_page=20") +
        strlen(encoded) + 1;
    char *url = xmalloc(url_len);
    snprintf(url, url_len,
             "https://gitlab.com/api/v4/projects?search=%s&order_by=star_count&sort=desc&per_page=20",
             encoded);
    free(encoded);

    long status = 0;
    char *body = net_get(url, NULL, &status);
    free(url);
    if (status < 200 || status >= 300) {
        free(body);
        return;
    }

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (root == NULL) return;
    if (!cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return;
    }

    cJSON *item;
    cJSON_ArrayForEach(item, root) {
        cJSON *jpath = cJSON_GetObjectItemCaseSensitive(item, "path");
        cJSON *jstars = cJSON_GetObjectItemCaseSensitive(item, "star_count");
        cJSON *jns = cJSON_GetObjectItemCaseSensitive(item, "namespace");
        if (!cJSON_IsString(jpath) || !cJSON_IsObject(jns)) continue;
        if (strcasecmp(jpath->valuestring, name) != 0) continue;
        cJSON *jfullpath = cJSON_GetObjectItemCaseSensitive(jns, "full_path");
        if (!cJSON_IsString(jfullpath)) continue;
        long stars = cJSON_IsNumber(jstars) ? (long)jstars->valuedouble : 0;
        append_result(out, "gitlab.com", jfullpath->valuestring, jpath->valuestring, stars);
    }
    cJSON_Delete(root);
}

static void fetch_codeberg(const char *name, search_result_list_t *out) {
    char *encoded = url_encode(name);
    size_t url_len = strlen("https://codeberg.org/api/v1/repos/search?q=&limit=20") + strlen(encoded) + 1;
    char *url = xmalloc(url_len);
    snprintf(url, url_len, "https://codeberg.org/api/v1/repos/search?q=%s&limit=20", encoded);
    free(encoded);

    long status = 0;
    char *body = net_get(url, NULL, &status);
    free(url);
    if (status < 200 || status >= 300) {
        free(body);
        return;
    }

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (root == NULL) return;

    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (cJSON_IsArray(data)) {
        cJSON *item;
        cJSON_ArrayForEach(item, data) {
            cJSON *jname = cJSON_GetObjectItemCaseSensitive(item, "name");
            cJSON *jstars = cJSON_GetObjectItemCaseSensitive(item, "stars_count");
            cJSON *jowner = cJSON_GetObjectItemCaseSensitive(item, "owner");
            if (!cJSON_IsString(jname) || !cJSON_IsObject(jowner)) continue;
            if (strcasecmp(jname->valuestring, name) != 0) continue;
            cJSON *jlogin = cJSON_GetObjectItemCaseSensitive(jowner, "login");
            if (!cJSON_IsString(jlogin)) continue;
            long stars = cJSON_IsNumber(jstars) ? (long)jstars->valuedouble : 0;
            append_result(out, "codeberg.org", jlogin->valuestring, jname->valuestring, stars);
        }
    }
    cJSON_Delete(root);
}

search_result_list_t search_by_name(const goget_config_t *cfg, const char *name) {
    search_result_list_t combined = {0};

    if (config_host_enabled(cfg, "github.com")) {
        search_result_list_t tmp = {0};
        fetch_github(name, &tmp);
        sort_and_cap(&tmp, 3);
        merge_into(&combined, &tmp);
    }
    if (config_host_enabled(cfg, "gitlab.com")) {
        search_result_list_t tmp = {0};
        fetch_gitlab(name, &tmp);
        sort_and_cap(&tmp, 3);
        merge_into(&combined, &tmp);
    }
    if (config_host_enabled(cfg, "codeberg.org")) {
        search_result_list_t tmp = {0};
        fetch_codeberg(name, &tmp);
        sort_and_cap(&tmp, 3);
        merge_into(&combined, &tmp);
    }

    return combined;
}

void search_result_list_free(search_result_list_t *list) {
    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i].host);
        free(list->items[i].owner);
        free(list->items[i].repo);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
}
