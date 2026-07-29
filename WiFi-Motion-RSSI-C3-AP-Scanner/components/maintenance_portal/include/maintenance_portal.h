#ifndef MAINTENANCE_PORTAL_H
#define MAINTENANCE_PORTAL_H

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "esp_wifi_types.h"
#include "multiref_detector.h"

esp_err_t maintenance_portal_start(const char *ap_ssid,
                                   const char *ap_password);
bool maintenance_portal_active(void);
void maintenance_portal_update_networks(
    const wifi_ap_record_t *records, size_t record_count);
void maintenance_portal_update_detector(
    uint32_t scan_id,
    const multiref_result_t *result);
bool maintenance_portal_take_calibration_request(void);
void maintenance_portal_update_calibration(
    bool running, uint16_t completed_scans, uint16_t target_scans);

#endif
