#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <stdbool.h>

/**
 * @brief Initialize HTTP client and start task
 * 
 * @param wifi_connected_ptr Pointer to WiFi connection status variable
 */
void http_client_init(bool *wifi_connected_ptr);

/**
 * @brief Fetch webpage from HTTP server
 * 
 * @param host Hostname or IP address of server (e.g., "example.com")
 * @param port Port number (typically 80 for HTTP)
 * @param path Path to resource (e.g., "/" for home page)
 */
void http_get_request(const char *host, int port, const char *path);

#endif
