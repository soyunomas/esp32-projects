#include "app_dns.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DNS";
static int dns_sock = -1;
static TaskHandle_t dns_task_handle = NULL;

// IP del AP: 192.168.4.1
#define AP_IP_BYTE0 192
#define AP_IP_BYTE1 168
#define AP_IP_BYTE2 4
#define AP_IP_BYTE3 1

// Responde a cualquier consulta DNS con la IP del AP
static void dns_task(void *arg) {
    uint8_t rx_buf[128];
    uint8_t tx_buf[256];
    struct sockaddr_in client_addr;
    socklen_t addr_len;

    while (1) {
        addr_len = sizeof(client_addr);
        int len = recvfrom(dns_sock, rx_buf, sizeof(rx_buf), 0,
                           (struct sockaddr *)&client_addr, &addr_len);
        if (len < 12) continue;

        // Copiar la cabecera DNS de la consulta
        memcpy(tx_buf, rx_buf, len);

        // Flags: respuesta estándar, sin error
        tx_buf[2] = 0x81; // QR=1, Opcode=0, AA=1
        tx_buf[3] = 0x80; // RA=1, RCODE=0
        // ANCOUNT = 1
        tx_buf[6] = 0x00;
        tx_buf[7] = 0x01;

        int offset = len;

        // Puntero al nombre de la consulta (compresión DNS)
        tx_buf[offset++] = 0xC0;
        tx_buf[offset++] = 0x0C;

        // Tipo A (1)
        tx_buf[offset++] = 0x00;
        tx_buf[offset++] = 0x01;
        // Clase IN (1)
        tx_buf[offset++] = 0x00;
        tx_buf[offset++] = 0x01;
        // TTL = 60 segundos
        tx_buf[offset++] = 0x00;
        tx_buf[offset++] = 0x00;
        tx_buf[offset++] = 0x00;
        tx_buf[offset++] = 0x3C;
        // RDLENGTH = 4
        tx_buf[offset++] = 0x00;
        tx_buf[offset++] = 0x04;
        // IP del AP
        tx_buf[offset++] = AP_IP_BYTE0;
        tx_buf[offset++] = AP_IP_BYTE1;
        tx_buf[offset++] = AP_IP_BYTE2;
        tx_buf[offset++] = AP_IP_BYTE3;

        sendto(dns_sock, tx_buf, offset, 0,
               (struct sockaddr *)&client_addr, addr_len);
    }
}

void app_dns_start(void) {
    if (dns_sock >= 0) return;

    dns_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (dns_sock < 0) {
        ESP_LOGE(TAG, "Failed to create socket");
        return;
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    if (bind(dns_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind DNS socket");
        close(dns_sock);
        dns_sock = -1;
        return;
    }

    xTaskCreate(dns_task, "dns_srv", 4096, NULL, 5, &dns_task_handle);
    ESP_LOGI(TAG, "Captive portal DNS started");
}

void app_dns_stop(void) {
    if (dns_task_handle) {
        vTaskDelete(dns_task_handle);
        dns_task_handle = NULL;
    }
    if (dns_sock >= 0) {
        close(dns_sock);
        dns_sock = -1;
    }
    ESP_LOGI(TAG, "DNS stopped");
}
