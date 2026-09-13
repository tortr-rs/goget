#include "net.h"

#include <curl/curl.h>
#include <stdio.h>
#include <string.h>

#include "util.h"

int net_global_init(void) {
    return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK ? 0 : -1;
}

void net_global_cleanup(void) {
    curl_global_cleanup();
}

struct membuf {
    char *data;
    size_t len;
    size_t cap;
};

static size_t write_to_mem(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct membuf *mb = userp;
    if (mb->len + realsize + 1 > mb->cap) {
        size_t newcap = mb->cap ? mb->cap * 2 : 4096;
        while (newcap < mb->len + realsize + 1) newcap *= 2;
        mb->data = xrealloc(mb->data, newcap);
        mb->cap = newcap;
    }
    memcpy(mb->data + mb->len, contents, realsize);
    mb->len += realsize;
    mb->data[mb->len] = '\0';
    return realsize;
}

char *net_get(const char *url, const char *const *extra_headers, long *out_http_status) {
    if (out_http_status) *out_http_status = 0;

    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "goget: curl_easy_init failed\n");
        return xstrdup("");
    }

    struct membuf mb = {.data = xmalloc(1), .len = 0, .cap = 1};
    mb.data[0] = '\0';

    struct curl_slist *headers = NULL;
    if (extra_headers) {
        for (int i = 0; extra_headers[i] != NULL; i++) {
            headers = curl_slist_append(headers, extra_headers[i]);
        }
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_mem);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &mb);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "goget/0.1");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    if (headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(curl);
    long status = 0;
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    } else {
        fprintf(stderr, "goget: request to %s failed: %s\n", url, curl_easy_strerror(res));
    }

    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (out_http_status) *out_http_status = status;
    return mb.data;
}

int net_download(const char *url, const char *dest_path) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "goget: curl_easy_init failed\n");
        return -1;
    }

    FILE *f = fopen(dest_path, "wb");
    if (!f) {
        perror("goget: fopen");
        curl_easy_cleanup(curl);
        return -1;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "goget/0.1");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);

    CURLcode res = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    fclose(f);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        fprintf(stderr, "goget: download of %s failed: %s\n", url, curl_easy_strerror(res));
        return -1;
    }
    if (status < 200 || status >= 300) {
        fprintf(stderr, "goget: download of %s failed: HTTP %ld\n", url, status);
        return -1;
    }
    return 0;
}
