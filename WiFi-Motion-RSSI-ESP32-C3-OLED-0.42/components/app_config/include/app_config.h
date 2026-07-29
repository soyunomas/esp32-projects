#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "motion_detector.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_CONFIG_SCHEMA_VERSION 4U
#define APP_CONFIG_WIFI_SSID_MAX_LENGTH 32U
#define APP_CONFIG_WIFI_PASSWORD_MAX_LENGTH 64U

typedef enum {
    APP_DETECTION_SOURCE_RSSI = 0,
    APP_DETECTION_SOURCE_CSI,
    APP_DETECTION_SOURCE_BOTH,
    APP_DETECTION_SOURCE_COUNT,
} app_detection_source_t;

typedef struct {
    char wifi_ssid[APP_CONFIG_WIFI_SSID_MAX_LENGTH + 1U];
    char wifi_password[APP_CONFIG_WIFI_PASSWORD_MAX_LENGTH + 1U];
    uint32_t sample_interval_ms;
    app_detection_source_t detection_source;
    motion_sensitivity_profile_t sensitivity_profile;
    motion_detector_config_t detector;
} app_runtime_config_t;

const char *app_detection_source_name(app_detection_source_t source);
bool app_detection_source_parse(const char *name,
                                app_detection_source_t *source);
bool app_detection_source_active(app_detection_source_t source,
                                 bool rssi_active,
                                 bool csi_active);

esp_err_t app_config_init(void);
void app_config_defaults(app_runtime_config_t *config);
esp_err_t app_config_load(app_runtime_config_t *config);
esp_err_t app_config_save(const app_runtime_config_t *config);
esp_err_t app_config_erase(void);
bool app_config_valid(const app_runtime_config_t *config);

#ifdef __cplusplus
}
#endif
