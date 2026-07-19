#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "motion_detector.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t sample_interval_ms;
    motion_detector_config_t detector;
} app_runtime_config_t;

esp_err_t app_config_init(void);
void app_config_defaults(app_runtime_config_t *config);
esp_err_t app_config_load(app_runtime_config_t *config);
esp_err_t app_config_save(const app_runtime_config_t *config);
esp_err_t app_config_erase(void);
bool app_config_valid(const app_runtime_config_t *config);

#ifdef __cplusplus
}
#endif
