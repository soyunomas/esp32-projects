#include "probe_config.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "nvs.h"
#include "reference_store.h"

static unsigned char stored[sizeof(probe_config_blob_t)];
static size_t stored_size;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t mode;
    uint8_t ssid_count;
    reference_ssid_t ssids[PROBE_CONFIG_MAX_SSIDS];
    char admin_username[PROBE_CONFIG_ADMIN_USER_MAX_LENGTH + 1U];
    char admin_password[PROBE_CONFIG_ADMIN_PASSWORD_MAX_LENGTH + 1U];
    uint32_t crc32;
} probe_config_v2_blob_t;

esp_err_t nvs_open(const char *namespace_name,
                   int open_mode,
                   nvs_handle_t *handle)
{
    (void)namespace_name;
    (void)open_mode;
    *handle = 1U;
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle)
{
    (void)handle;
}

esp_err_t nvs_get_blob(nvs_handle_t handle,
                       const char *key,
                       void *value,
                       size_t *length)
{
    (void)handle;
    (void)key;
    if (stored_size == 0U) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if (value == NULL) {
        *length = stored_size;
        return ESP_OK;
    }
    if (*length < stored_size) {
        return ESP_FAIL;
    }
    memcpy(value, stored, stored_size);
    *length = stored_size;
    return ESP_OK;
}

esp_err_t nvs_set_blob(nvs_handle_t handle,
                       const char *key,
                       const void *value,
                       size_t length)
{
    (void)handle;
    (void)key;
    assert(length <= sizeof(stored));
    memcpy(stored, value, length);
    stored_size = length;
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    (void)handle;
    return ESP_OK;
}

int main(void)
{
    probe_config_blob_t saved;
    probe_config_set_defaults(&saved);
    saved.mode = PROBE_CONFIG_MODE_MANUAL;
    saved.ssid_count = 1U;
    saved.ssids[0].bytes[0] = 'N';
    saved.ssids[0].length = 1U;
    memcpy(saved.admin_username, "nvs-user", 9U);
    memcpy(saved.admin_password, "nvs-password", 13U);
    saved.trigger_consecutive = 3U;
    saved.calibration_scans = 40U;
    assert(probe_config_save(&saved) == PROBE_CONFIG_OK);

    probe_config_blob_t loaded = {0};
    assert(probe_config_load(&loaded) == PROBE_CONFIG_OK);
    assert(loaded.trigger_consecutive == 3U);
    assert(loaded.calibration_scans == 40U);
    assert(loaded.ssids[0].bytes[0] == 'N');
    assert(strcmp(loaded.admin_username, "nvs-user") == 0);

    probe_config_v2_blob_t old = {0};
    old.magic = PROBE_CONFIG_MAGIC;
    old.version = PROBE_CONFIG_PREVIOUS_VERSION;
    old.mode = PROBE_CONFIG_MODE_MANUAL;
    old.ssid_count = 1U;
    old.ssids[0].bytes[0] = 'L';
    old.ssids[0].length = 1U;
    memcpy(old.admin_username, "legacy", 7U);
    memcpy(old.admin_password, "legacy-password", 16U);
    old.crc32 = reference_store_crc32(
        &old, offsetof(probe_config_v2_blob_t, crc32));
    memcpy(stored, &old, sizeof(old));
    stored_size = sizeof(old);

    assert(probe_config_load(&loaded) == PROBE_CONFIG_OK);
    assert(stored_size == sizeof(probe_config_blob_t));
    assert(loaded.version == PROBE_CONFIG_VERSION);
    assert(loaded.trigger_consecutive == 1U);
    assert(loaded.calibration_scans == 25U);
    assert(loaded.ssids[0].bytes[0] == 'L');
    assert(strcmp(loaded.admin_username, "legacy") == 0);
    assert(strcmp(loaded.admin_password, "legacy-password") == 0);
    return 0;
}
