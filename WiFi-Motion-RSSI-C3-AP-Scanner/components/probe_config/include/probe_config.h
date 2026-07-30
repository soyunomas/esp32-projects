#ifndef PROBE_CONFIG_H
#define PROBE_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "reference_selector.h"

#define PROBE_CONFIG_MAGIC 0x50434647U
#define PROBE_CONFIG_VERSION 3U
#define PROBE_CONFIG_PREVIOUS_VERSION 2U
#define PROBE_CONFIG_MAX_SSIDS 8U
#define PROBE_CONFIG_ADMIN_USER_MAX_LENGTH 16U
#define PROBE_CONFIG_ADMIN_PASSWORD_MAX_LENGTH 32U
#define PROBE_CONFIG_DEFAULT_ADMIN_USER "admin"
#define PROBE_CONFIG_DEFAULT_ADMIN_PASSWORD "admin"

#define PROBE_CONFIG_TRIGGER_SCORE_SENSITIVE_X100 200U
#define PROBE_CONFIG_TRIGGER_SCORE_BALANCED_X100 250U
#define PROBE_CONFIG_TRIGGER_SCORE_CONSERVATIVE_X100 350U
#define PROBE_CONFIG_TRIGGER_CONSECUTIVE_MIN 1U
#define PROBE_CONFIG_TRIGGER_CONSECUTIVE_MAX 3U
#define PROBE_CONFIG_SCAN_DELAY_FAST_MS 250U
#define PROBE_CONFIG_SCAN_DELAY_NORMAL_MS 500U
#define PROBE_CONFIG_SCAN_DELAY_SLOW_MS 1000U
#define PROBE_CONFIG_MOTION_DURATION_SHORT_S 2U
#define PROBE_CONFIG_MOTION_DURATION_NORMAL_S 4U
#define PROBE_CONFIG_MOTION_DURATION_LONG_S 8U
#define PROBE_CONFIG_CALIBRATION_FAST_SCANS 15U
#define PROBE_CONFIG_CALIBRATION_NORMAL_SCANS 25U
#define PROBE_CONFIG_CALIBRATION_PRECISE_SCANS 40U

typedef enum {
    PROBE_CONFIG_MODE_AUTOMATIC = 0,
    PROBE_CONFIG_MODE_MANUAL = 1,
} probe_config_mode_t;

typedef enum {
    PROBE_CONFIG_OK = 0,
    PROBE_CONFIG_NOT_FOUND,
    PROBE_CONFIG_INVALID,
    PROBE_CONFIG_IO_ERROR,
} probe_config_status_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t mode;
    uint8_t ssid_count;
    reference_ssid_t ssids[PROBE_CONFIG_MAX_SSIDS];
    char admin_username[PROBE_CONFIG_ADMIN_USER_MAX_LENGTH + 1U];
    char admin_password[PROBE_CONFIG_ADMIN_PASSWORD_MAX_LENGTH + 1U];
    uint16_t trigger_score_x100;
    uint16_t inter_scan_delay_ms;
    uint16_t calibration_scans;
    uint8_t trigger_consecutive;
    uint8_t motion_duration_seconds;
    uint32_t crc32;
} probe_config_blob_t;

void probe_config_set_defaults(probe_config_blob_t *config);
void probe_config_finalize(probe_config_blob_t *config);
bool probe_config_validate(const probe_config_blob_t *config);
bool probe_config_repair(probe_config_blob_t *config);
bool probe_config_decode(const void *data,
                         size_t size,
                         probe_config_blob_t *config,
                         bool *changed);
uint16_t probe_config_release_score_x100(uint16_t trigger_score_x100);
uint8_t probe_config_clear_consecutive(uint8_t duration_seconds,
                                       uint32_t scan_cycle_ms);
probe_config_status_t probe_config_load(probe_config_blob_t *config);
probe_config_status_t probe_config_save(probe_config_blob_t *config);

#endif
