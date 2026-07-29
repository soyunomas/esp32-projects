#include "probe_format.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const uint8_t ssid[] = {'A', '"', '\\', 0x00U, 0xFFU};
    char escaped[64];
    const size_t required =
        probe_json_escape_bytes(escaped, sizeof(escaped), ssid, sizeof(ssid));
    assert(required == strlen("A\\\"\\\\\\u0000\\u00FF"));
    assert(strcmp(escaped, "A\\\"\\\\\\u0000\\u00FF") == 0);

    char truncated[5];
    assert(probe_json_escape_bytes(truncated,
                                   sizeof(truncated),
                                   ssid,
                                   sizeof(ssid)) == required);
    assert(truncated[sizeof(truncated) - 1U] == '\0');

    char hex[16];
    probe_bytes_to_hex(hex, sizeof(hex), ssid, sizeof(ssid));
    assert(strcmp(hex, "41225C00FF") == 0);

    const uint8_t bssid[] = {0x00U, 0x11U, 0x22U, 0xAAU, 0xBBU, 0xCCU};
    char bssid_text[18];
    probe_format_bssid(bssid_text, bssid);
    assert(strcmp(bssid_text, "00:11:22:AA:BB:CC") == 0);

    puts("probe_format tests passed");
    return 0;
}
