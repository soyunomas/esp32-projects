#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_netif.h"

typedef struct {
    bool running;
    uint32_t interval_ms;
    uint32_t payload_bytes;
    uint64_t requests;
    uint64_t replies;
    uint64_t timeouts;
} csi_traffic_snapshot_t;

esp_err_t csi_traffic_start(esp_netif_t *netif,
                            uint32_t interval_ms,
                            uint32_t timeout_ms,
                            uint32_t payload_bytes);

void csi_traffic_get_snapshot(csi_traffic_snapshot_t *snapshot);
