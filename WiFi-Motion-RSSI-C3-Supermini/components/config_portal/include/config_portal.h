#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_config.h"
#include "esp_err.h"
#include "motion_detector.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int64_t uptime_ms;
    int rssi_dbm;
    bool sample_ok;
    uint8_t channel;
    char bssid[18];
    motion_detector_result_t detector;
    motion_detector_result_t csi_detector;
    uint64_t queries;
    uint64_t samples_ok;
    uint64_t read_errors;
    uint64_t schedule_misses;
    uint64_t disconnects;
    uint64_t reconnects;
} config_portal_diagnostics_t;

esp_err_t config_portal_start(const app_runtime_config_t *effective_config,
                              bool provisioning_mode);
void config_portal_update_diagnostics(
    const config_portal_diagnostics_t *diagnostics);
bool config_portal_take_calibration_request(int64_t timestamp_ms);
bool config_portal_wifi_scan_active(int64_t timestamp_ms);

#ifdef __cplusplus
}
#endif
