#define _POSIX_C_SOURCE 200809L

#ifdef _MSC_VER
#define strdup _strdup
#endif

#include "http.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_USER_AGENT "KaluaBilla/payload-dumper-ungo"
#define DEFAULT_TIMEOUT    600

typedef struct {
    char* url;
    CURL* curl;
    struct curl_slist* headers;
    uint64_t content_length;
    uint64_t bytes_downloaded;
    ziprand_http_config_t config;
} http_io_ctx_t;

typedef struct {
    uint8_t* buffer;
    size_t size;
    size_t written;
} curl_buffer_t;

static size_t http_write_callback(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t total_size = size * nmemb;
    curl_buffer_t* buf = userp;

    size_t to_copy = total_size;
    if (buf->written + to_copy > buf->size)
        to_copy = buf->size - buf->written;

    memcpy(buf->buffer + buf->written, contents, to_copy);
    buf->written += to_copy;

    return total_size;
}

ziprand_http_config_t ziprand_http_config_default(void)
{
    ziprand_http_config_t config;
    config.user_agent = NULL;
    config.verify_ssl = 1;
    config.timeout_seconds = DEFAULT_TIMEOUT;
    config.follow_redirects = 1;
    config.max_redirects = 10;
    config.verbose = 0;
    return config;
}

static int64_t http_read(void* ctx, uint64_t offset, void* buffer, size_t size)
{
    http_io_ctx_t* http = ctx;

    if (offset >= http->content_length)
        return 0;

    uint64_t remaining = http->content_length - offset;
    size_t to_read = size < remaining ? size : remaining;

    if (to_read == 0)
        return 0;

    char range[128];
    snprintf(range,
             sizeof(range),
             "%llu-%llu",
             (unsigned long long)offset,
             (unsigned long long)(offset + to_read - 1));

    curl_easy_setopt(http->curl, CURLOPT_RANGE, range);

    curl_buffer_t buf = {.buffer = buffer, .size = to_read, .written = 0};
    curl_easy_setopt(http->curl, CURLOPT_WRITEFUNCTION, http_write_callback);
    curl_easy_setopt(http->curl, CURLOPT_WRITEDATA, &buf);

    CURLcode res = curl_easy_perform(http->curl);
    if (res != CURLE_OK) {
        if (http->config.verbose) {
            fprintf(stderr, "HTTP request failed: %s\n", curl_easy_strerror(res));
        }
        return -1;
    }

    long http_code = 0;
    curl_easy_getinfo(http->curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 206 && http_code != 200) {
        if (http->config.verbose) {
            fprintf(stderr, "HTTP error: %ld\n", http_code);
        }
        return -1;
    }

    http->bytes_downloaded += buf.written;

    return buf.written;
}

static int64_t http_size(void* ctx)
{
    http_io_ctx_t* http = ctx;
    return http->content_length;
}

static void http_close(void* ctx)
{
    http_io_ctx_t* http = ctx;
    if (http->headers)
        curl_slist_free_all(http->headers);
    if (http->curl)
        curl_easy_cleanup(http->curl);
    free(http->url);
    free(http);
}

ziprand_io_t* ziprand_io_http_ex(const char* url, const ziprand_http_config_t* config)
{
    if (!url)
        return NULL;

    ziprand_http_config_t cfg;
    if (config) {
        cfg = *config;
    } else {
        cfg = ziprand_http_config_default();
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "Failed to initialize curl\n");
        return NULL;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);

    const char* ua = cfg.user_agent ? cfg.user_agent : DEFAULT_USER_AGENT;
    curl_easy_setopt(curl, CURLOPT_USERAGENT, ua);

    if (!cfg.verify_ssl) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    int timeout = cfg.timeout_seconds > 0 ? cfg.timeout_seconds : DEFAULT_TIMEOUT;
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)timeout);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);

    if (cfg.follow_redirects) {
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, (long)cfg.max_redirects);
    }

    if (cfg.verbose) {
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    }

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Accept: */*");
    headers = curl_slist_append(headers, "Accept-Encoding: identity");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_HEADER, 0L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "Failed to connect to %s: %s\n", url, curl_easy_strerror(res));
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return NULL;
    }

    curl_off_t content_length = 0;
    curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &content_length);

    if (content_length <= 0) {
        fprintf(stderr, "Could not determine content length for %s\n", url);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return NULL;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200 && http_code != 206) {
        fprintf(stderr, "HTTP error: %ld\n", http_code);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return NULL;
    }

    if (cfg.verbose) {
        printf("Remote file size: %.2f MB\n", content_length / (1024.0 * 1024.0));
        printf("User-Agent: %s\n", ua);
    }

    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 0L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    http_io_ctx_t* http = malloc(sizeof(http_io_ctx_t));
    if (!http) {
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return NULL;
    }

    http->url = strdup(url);
    http->curl = curl;
    http->headers = headers;
    http->content_length = (uint64_t)content_length;
    http->bytes_downloaded = 0;
    http->config = cfg;

    ziprand_io_t* io = malloc(sizeof(ziprand_io_t));
    if (!io) {
        free(http->url);
        free(http);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return NULL;
    }

    io->ctx = http;
    io->read = http_read;
    io->get_size = http_size;
    io->close = http_close;

    return io;
}

ziprand_io_t* ziprand_io_http(const char* url)
{
    return ziprand_io_http_ex(url, NULL);
}

uint64_t ziprand_http_get_bytes_downloaded(ziprand_io_t* io)
{
    if (!io || !io->ctx)
        return 0;

    http_io_ctx_t* http = io->ctx;
    if (http->curl && http->url) {
        return http->bytes_downloaded;
    }

    return 0;
}