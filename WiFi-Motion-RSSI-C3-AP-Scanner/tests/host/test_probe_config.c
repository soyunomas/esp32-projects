#include "probe_config.h"

#include <assert.h>

static probe_config_blob_t automatic(void)
{
    probe_config_blob_t config;
    probe_config_set_defaults(&config);
    return config;
}

int main(void)
{
    probe_config_blob_t config = automatic();
    assert(probe_config_validate(&config));

    config.mode = PROBE_CONFIG_MODE_MANUAL;
    config.ssid_count = 1U;
    config.ssids[0].bytes[0] = 'A';
    config.ssids[0].length = 1U;
    probe_config_finalize(&config);
    assert(probe_config_validate(&config));

    config.ssids[1] = config.ssids[0];
    config.ssid_count = 2U;
    probe_config_finalize(&config);
    assert(!probe_config_validate(&config));

    config = automatic();
    config.crc32 ^= 1U;
    assert(!probe_config_validate(&config));

    config = automatic();
    config.version++;
    assert(!probe_config_validate(&config));

    config = automatic();
    config.admin_password[0] = '\0';
    probe_config_finalize(&config);
    assert(!probe_config_validate(&config));

    config = automatic();
    config.mode = PROBE_CONFIG_MODE_MANUAL;
    config.ssid_count = PROBE_CONFIG_MAX_SSIDS;
    for (uint8_t index = 0U;
         index < PROBE_CONFIG_MAX_SSIDS;
         ++index) {
        config.ssids[index].bytes[0] = (uint8_t)('A' + index);
        config.ssids[index].length = 1U;
    }
    probe_config_finalize(&config);
    assert(probe_config_validate(&config));
    return 0;
}
