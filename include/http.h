#ifndef ZIPRAND_HTTP_H
#define ZIPRAND_HTTP_H

#include <stdint.h>
#include <ziprand.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * HTTP configuration options
 */
typedef struct {
    const char* user_agent; /* Custom User-Agent (NULL for default) */
    int verify_ssl;         /* 1 = verify SSL, 0 = skip verification */
    int timeout_seconds;    /* Connection timeout (0 = default 600s) */
    int follow_redirects;   /* 1 = follow, 0 = don't follow */
    int max_redirects;      /* Maximum redirect hops (default 10) */
    int verbose;            /* 1 = print debug info, 0 = quiet */
} ziprand_http_config_t;

/**
 * Create default HTTP configuration
 */
ziprand_http_config_t ziprand_http_config_default(void);

/**
 * Create HTTP I/O interface with custom configuration
 * @param url URL to access
 * @param config HTTP configuration (NULL for defaults)
 * @return I/O interface or NULL on error
 */
ziprand_io_t* ziprand_io_http_ex(const char* url, const ziprand_http_config_t* config);

/**
 * Create HTTP I/O interface with defaults
 * @param url URL to access
 * @return I/O interface or NULL on error
 */
ziprand_io_t* ziprand_io_http(const char* url);

/**
 * Get total bytes downloaded for bandwidth tracking
 * @param io HTTP I/O interface
 * @return Total bytes downloaded, or 0 if not HTTP
 */
uint64_t ziprand_http_get_bytes_downloaded(ziprand_io_t* io);

#ifdef __cplusplus
}
#endif

#endif /* ZIPRAND_HTTP_H */