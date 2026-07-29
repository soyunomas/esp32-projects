#include "captive_dns.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#define DNS_PORT 53
#define DNS_PACKET_MAX_LENGTH 512U
#define DNS_HEADER_LENGTH 12U
#define DNS_ANSWER_LENGTH 16U

static int dns_socket = -1;
static uint8_t portal_ipv4[4];

static uint16_t read_u16(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8U) | bytes[1]);
}

static size_t build_response(const uint8_t *query,
                             size_t query_length,
                             uint8_t *response,
                             size_t response_capacity)
{
    if (query_length < DNS_HEADER_LENGTH ||
        (query[2] & 0x80U) != 0U ||
        read_u16(&query[4]) == 0U) {
        return 0U;
    }
    size_t cursor = DNS_HEADER_LENGTH;
    while (cursor < query_length) {
        const uint8_t label_length = query[cursor++];
        if (label_length == 0U) {
            break;
        }
        if (label_length > 63U ||
            cursor + label_length > query_length) {
            return 0U;
        }
        cursor += label_length;
    }
    if (cursor + 4U > query_length) {
        return 0U;
    }
    const size_t question_end = cursor + 4U;
    const size_t response_length =
        question_end + DNS_ANSWER_LENGTH;
    if (response_length > response_capacity) {
        return 0U;
    }
    memcpy(response, query, question_end);
    response[2] = 0x81U;
    response[3] = 0x80U;
    response[6] = 0x00U;
    response[7] = 0x01U;
    memset(&response[8], 0, 4U);
    const uint8_t answer[DNS_ANSWER_LENGTH] = {
        0xc0U, 0x0cU, 0x00U, 0x01U, 0x00U, 0x01U,
        0x00U, 0x00U, 0x00U, 0x3cU, 0x00U, 0x04U,
        portal_ipv4[0], portal_ipv4[1],
        portal_ipv4[2], portal_ipv4[3],
    };
    memcpy(&response[question_end], answer, sizeof(answer));
    return response_length;
}

static void dns_task(void *argument)
{
    (void)argument;
    uint8_t query[DNS_PACKET_MAX_LENGTH];
    uint8_t response[DNS_PACKET_MAX_LENGTH];
    for (;;) {
        struct sockaddr_storage client = {0};
        socklen_t client_length = sizeof(client);
        const int received = recvfrom(
            dns_socket,
            query,
            sizeof(query),
            0,
            (struct sockaddr *)&client,
            &client_length);
        if (received <= 0) {
            continue;
        }
        const size_t response_length =
            build_response(query,
                           (size_t)received,
                           response,
                           sizeof(response));
        if (response_length > 0U) {
            (void)sendto(
                dns_socket,
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
                    3072U,
                    NULL,
                    4U,
                    NULL) != pdPASS) {
        close(dns_socket);
        dns_socket = -1;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
