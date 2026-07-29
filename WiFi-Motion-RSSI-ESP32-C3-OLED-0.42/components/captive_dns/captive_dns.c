#include "captive_dns.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#define DNS_PORT 53
#define DNS_PACKET_MAX_LENGTH 512U

static const char *TAG = "captive_dns";
static int dns_socket = -1;
static TaskHandle_t dns_task_handle;
static uint8_t portal_ipv4[4];

static void dns_task(void *argument)
{
    (void)argument;
    uint8_t query[DNS_PACKET_MAX_LENGTH];
    uint8_t response[DNS_PACKET_MAX_LENGTH];
    for (;;) {
        struct sockaddr_storage client = {0};
        socklen_t client_length = sizeof(client);
        const int received = recvfrom(dns_socket,
                                      query,
                                      sizeof(query),
                                      0,
                                      (struct sockaddr *)&client,
                                      &client_length);
        if (received <= 0) {
            continue;
        }
        const size_t response_length = captive_dns_build_a_response(
            query,
            (size_t)received,
            portal_ipv4,
            response,
            sizeof(response));
        if (response_length > 0U) {
            (void)sendto(dns_socket,
                         response,
                         response_length,
                         0,
                         (const struct sockaddr *)&client,
                         client_length);
        }
    }
}

esp_err_t captive_dns_start(esp_netif_t *ap_netif)
{
    if (dns_socket >= 0) {
        return ESP_OK;
    }
    if (ap_netif == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_netif_ip_info_t ip_info;
    esp_err_t error = esp_netif_get_ip_info(ap_netif, &ip_info);
    if (error != ESP_OK) {
        return error;
    }
    const uint32_t address = ntohl(ip_info.ip.addr);
    portal_ipv4[0] = (uint8_t)(address >> 24U);
    portal_ipv4[1] = (uint8_t)(address >> 16U);
    portal_ipv4[2] = (uint8_t)(address >> 8U);
    portal_ipv4[3] = (uint8_t)address;

    dns_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (dns_socket < 0) {
        return ESP_FAIL;
    }
    const struct sockaddr_in address_config = {
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(dns_socket,
             (const struct sockaddr *)&address_config,
             sizeof(address_config)) != 0) {
        close(dns_socket);
        dns_socket = -1;
        return ESP_FAIL;
    }
    if (xTaskCreate(dns_task,
                    "captive_dns",
                    3072,
                    NULL,
                    4,
                    &dns_task_handle) != pdPASS) {
        close(dns_socket);
        dns_socket = -1;
        dns_task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG,
             "Captive DNS ready at %u.%u.%u.%u",
             portal_ipv4[0], portal_ipv4[1], portal_ipv4[2], portal_ipv4[3]);
    return ESP_OK;
}

void captive_dns_stop(void)
{
    if (dns_task_handle != NULL) {
        vTaskDelete(dns_task_handle);
        dns_task_handle = NULL;
    }
    if (dns_socket >= 0) {
        close(dns_socket);
        dns_socket = -1;
    }
    memset(portal_ipv4, 0, sizeof(portal_ipv4));
}
