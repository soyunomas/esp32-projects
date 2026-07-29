#ifndef PROBE_CONFIG_H
#define PROBE_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#include "reference_selector.h"

#define PROBE_CONFIG_MAGIC 0x50434647U
#define PROBE_CONFIG_VERSION 2U
#define PROBE_CONFIG_MAX_SSIDS 8U
#define PROBE_CONFIG_ADMIN_USER_MAX_LENGTH 16U
#define PROBE_CONFIG_ADMIN_PASSWORD_MAX_LENGTH 32U
#define PROBE_CONFIG_DEFAULT_ADMIN_USER "admin"
#define PROBE_CONFIG_DEFAULT_ADMIN_PASSWORD "admin"

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
    uint32_t crc32;
} probe_config_blob_t;

void probe_config_set_defaults(probe_config_blob_t *config);
void probe_config_finalize(probe_config_blob_t *config);
bool probe_config_validate(const probe_config_blob_t *config);
probe_config_status_t probe_config_load(probe_config_blob_t *config);
probe_config_status_t probe_config_save(probe_config_blob_t *config);

#endif
