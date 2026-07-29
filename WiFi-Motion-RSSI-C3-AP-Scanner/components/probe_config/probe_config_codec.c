#include "probe_config.h"

#include <stddef.h>
#include <string.h>

#include "reference_store.h"

void probe_config_set_defaults(probe_config_blob_t *config)
{
    if (config == NULL) {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->mode = PROBE_CONFIG_MODE_AUTOMATIC;
    memcpy(config->admin_username,
           PROBE_CONFIG_DEFAULT_ADMIN_USER,
           sizeof(PROBE_CONFIG_DEFAULT_ADMIN_USER));
    memcpy(config->admin_password,
           PROBE_CONFIG_DEFAULT_ADMIN_PASSWORD,
           sizeof(PROBE_CONFIG_DEFAULT_ADMIN_PASSWORD));
    probe_config_finalize(config);
}

void probe_config_finalize(probe_config_blob_t *config)
{
    if (config == NULL) {
        return;
    }
    config->magic = PROBE_CONFIG_MAGIC;
    config->version = PROBE_CONFIG_VERSION;
    config->crc32 = reference_store_crc32(
        config, offsetof(probe_config_blob_t, crc32));
}

bool probe_config_validate(const probe_config_blob_t *config)
{
    if (config == NULL || config->magic != PROBE_CONFIG_MAGIC ||
        config->version != PROBE_CONFIG_VERSION ||
        config->mode > PROBE_CONFIG_MODE_MANUAL ||
        config->ssid_count > PROBE_CONFIG_MAX_SSIDS) {
        return false;
    }
    const size_t username_length =
        strnlen(config->admin_username,
                PROBE_CONFIG_ADMIN_USER_MAX_LENGTH + 1U);
    const size_t password_length =
        strnlen(config->admin_password,
                PROBE_CONFIG_ADMIN_PASSWORD_MAX_LENGTH + 1U);
    if (username_length == 0U ||
        username_length > PROBE_CONFIG_ADMIN_USER_MAX_LENGTH ||
        password_length < 4U ||
        password_length > PROBE_CONFIG_ADMIN_PASSWORD_MAX_LENGTH) {
        return false;
    }
    if ((config->mode == PROBE_CONFIG_MODE_AUTOMATIC &&
         config->ssid_count != 0U) ||
        (config->mode == PROBE_CONFIG_MODE_MANUAL &&
         config->ssid_count == 0U)) {
        return false;
    }
    for (uint8_t index = 0U; index < config->ssid_count; ++index) {
        if (config->ssids[index].length == 0U ||
            config->ssids[index].length >
                REFERENCE_SELECTOR_SSID_MAX_LENGTH) {
            return false;
        }
        for (uint8_t other = (uint8_t)(index + 1U);
             other < config->ssid_count;
             ++other) {
            if (config->ssids[index].length ==
                    config->ssids[other].length &&
                memcmp(config->ssids[index].bytes,
                       config->ssids[other].bytes,
                       config->ssids[index].length) == 0) {
                return false;
            }
        }
    }
    return config->crc32 ==
           reference_store_crc32(
               config, offsetof(probe_config_blob_t, crc32));
}
