#include "csi_traffic.h"

#include <inttypes.h>
#include <stdatomic.h>

#include "esp_log.h"
#include "lwip/ip_addr.h"
#include "ping/ping_sock.h"

static const char *TAG = "csi_traffic";
static esp_ping_handle_t ping_session;
static atomic_bool running;
static atomic_uint_fast64_t replies;
static atomic_uint_fast64_t timeouts;
static uint32_t configured_interval_ms;
static uint32_t configured_payload_bytes;

static void on_ping_success(esp_ping_handle_t handle, void *argument)
{
    (void)handle;
    (void)argument;
    atomic_fetch_add(&replies, 1U);
}

static void on_ping_timeout(esp_ping_handle_t handle, void *argument)
{
    (void)handle;
    (void)argument;
    atomic_fetch_add(&timeouts, 1U);
}

static void on_ping_end(esp_ping_handle_t handle, void *argument)
{
    (void)handle;
    (void)argument;
    atomic_store(&running, false);
}

esp_err_t csi_traffic_start(esp_netif_t *netif,
                            uint32_t interval_ms,
                            uint32_t timeout_ms,
                            uint32_t payload_bytes)
{
    if (netif == NULL || interval_ms == 0U || timeout_ms == 0U ||
        payload_bytes == 0U || ping_session != NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_netif_ip_info_t ip_info;
    esp_err_t error = esp_netif_get_ip_info(netif, &ip_info);
    if (error != ESP_OK) {
        return error;
    }
    if (ip_info.gw.addr == 0U) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
    config.count = ESP_PING_COUNT_INFINITE;
    config.interval_ms = interval_ms;
    config.timeout_ms = timeout_ms;
    config.data_size = payload_bytes;
    config.interface = esp_netif_get_netif_impl_index(netif);
    ip_addr_set_ip4_u32(&config.target_addr, ip_info.gw.addr);

    const esp_ping_callbacks_t callbacks = {
        .cb_args = NULL,
        .on_ping_success = on_ping_success,
        .on_ping_timeout = on_ping_timeout,
        .on_ping_end = on_ping_end,
    };
    atomic_store(&replies, 0U);
    atomic_store(&timeouts, 0U);
    configured_interval_ms = interval_ms;
    configured_payload_bytes = payload_bytes;

    error = esp_ping_new_session(&config, &callbacks, &ping_session);
    if (error != ESP_OK) {
        ping_session = NULL;
        return error;
    }
    error = esp_ping_start(ping_session);
    if (error != ESP_OK) {
        esp_ping_delete_session(ping_session);
        ping_session = NULL;
        return error;
    }
    atomic_store(&running, true);
    ESP_LOGI(TAG,
             "Controlled CSI traffic started: interval=%" PRIu32
             " ms payload=%" PRIu32 " bytes",
             interval_ms,
             payload_bytes);
    return ESP_OK;
}

void csi_traffic_get_snapshot(csi_traffic_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    const uint64_t reply_count = atomic_load(&replies);
    const uint64_t timeout_count = atomic_load(&timeouts);
    *snapshot = (csi_traffic_snapshot_t) {
        .running = atomic_load(&running),
        .interval_ms = configured_interval_ms,
        .payload_bytes = configured_payload_bytes,
        .requests = reply_count + timeout_count,
        .replies = reply_count,
        .timeouts = timeout_count,
    };
}
