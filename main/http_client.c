#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "http_client.h"

static const char *TAG = "http_client";
static bool *wifi_status_ptr = NULL;

#define HTTP_RECV_BUFFER_SIZE 1024

/**
 * @brief Perform HTTP GET request
 */
void http_get_request(const char *host, int port, const char *path)
{
    char recv_buf[HTTP_RECV_BUFFER_SIZE];
    int sock = -1;
    struct sockaddr_in dest_addr;
    
    ESP_LOGI(TAG, "Starting HTTP GET request to http://%s:%d%s", host, port, path);
    
    /* Get IP address of the host */
    struct hostent *server = gethostbyname(host);
    if (server == NULL) {
        ESP_LOGE(TAG, "DNS lookup failed for host: %s", host);
        return;
    }
    
    /* Create socket */
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create socket: errno %d", errno);
        return;
    }
    ESP_LOGI(TAG, "Socket created");
    
    /* Set up destination address */
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);
    memcpy(&dest_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    
    ESP_LOGI(TAG, "Connecting to %s:%d...", host, port);
    
    /* Connect to server */
    int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket connect failed: errno %d", errno);
        close(sock);
        return;
    }
    ESP_LOGI(TAG, "Connected to server");
    
    /* Prepare HTTP GET request */
    char request[512];
    int len = snprintf(request, sizeof(request),
                      "GET %s HTTP/1.1\r\n"
                      "Host: %s\r\n"
                      "User-Agent: ESP32-HTTP-Client\r\n"
                      "Connection: close\r\n"
                      "\r\n",
                      path, host);
    
    /* Send HTTP request */
    err = send(sock, request, len, 0);
    if (err < 0) {
        ESP_LOGE(TAG, "Failed to send request: errno %d", errno);
        close(sock);
        return;
    }
    ESP_LOGI(TAG, "HTTP request sent (%d bytes)", err);
    ESP_LOGI(TAG, "--- HTTP REQUEST ---");
    ESP_LOGI(TAG, "%s", request);
    ESP_LOGI(TAG, "--------------------");
    
    /* Receive HTTP response */
    ESP_LOGI(TAG, "--- HTTP RESPONSE ---");
    int total_received = 0;
    while (1) {
        int received = recv(sock, recv_buf, sizeof(recv_buf) - 1, 0);
        if (received < 0) {
            ESP_LOGE(TAG, "Receive failed: errno %d", errno);
            break;
        } else if (received == 0) {
            ESP_LOGI(TAG, "Connection closed by server");
            break;
        } else {
            recv_buf[received] = 0;
            printf("%s", recv_buf);
            total_received += received;
        }
    }
    ESP_LOGI(TAG, "---------------------");
    ESP_LOGI(TAG, "Total received: %d bytes", total_received);
    
    /* Close socket */
    close(sock);
    ESP_LOGI(TAG, "Socket closed");
}

/**
 * @brief HTTP client task - periodically fetches webpage when WiFi is connected
 */
static void http_client_task(void *pvParameters)
{
    const char *HOST = "example.com";
    const int PORT = 80;
    const char *PATH = "/";
    const int REQUEST_INTERVAL_SEC = 30;
    
    ESP_LOGI(TAG, "HTTP client task started");
    
    while (1) {
        /* Check if WiFi is connected */
        if (wifi_status_ptr != NULL && *wifi_status_ptr) {
            ESP_LOGI(TAG, "WiFi is connected, performing HTTP request...");
            http_get_request(HOST, PORT, PATH);
            
            /* Wait before next request */
            ESP_LOGI(TAG, "Waiting %d seconds before next request...", REQUEST_INTERVAL_SEC);
            vTaskDelay((REQUEST_INTERVAL_SEC * 1000) / portTICK_PERIOD_MS);
        } else {
            ESP_LOGI(TAG, "WiFi not connected, waiting...");
            vTaskDelay(5000 / portTICK_PERIOD_MS);
        }
    }
}

/**
 * @brief Initialize HTTP client
 */
void http_client_init(bool *wifi_connected_ptr)
{
    wifi_status_ptr = wifi_connected_ptr;
    
    /* Create HTTP client task */
    xTaskCreate(http_client_task, "http_client_task", 4096, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "HTTP client initialized");
}
