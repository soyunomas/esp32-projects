#include "probe_format.h"

#include <stdio.h>

static const char HEX[] = "0123456789ABCDEF";

static void append_char(char *destination,
                        size_t destination_size,
                        size_t *written,
                        char value)
{
    if (destination != NULL && destination_size > 0U &&
        *written < destination_size - 1U) {
        destination[*written] = value;
    }
    (*written)++;
}

size_t probe_json_escape_bytes(char *destination,
                               size_t destination_size,
                               const uint8_t *source,
                               size_t source_size)
{
    size_t written = 0U;
    if (source == NULL && source_size > 0U) {
        if (destination != NULL && destination_size > 0U) {
            destination[0] = '\0';
        }
        return 0U;
    }

    for (size_t index = 0U; index < source_size; ++index) {
        const uint8_t value = source[index];
        if (value == '"' || value == '\\') {
            append_char(destination, destination_size, &written, '\\');
            append_char(destination, destination_size, &written, (char)value);
        } else if (value >= 0x20U && value <= 0x7EU) {
            append_char(destination, destination_size, &written, (char)value);
        } else {
            const char escaped[] = {
                '\\', 'u', '0', '0', HEX[value >> 4U], HEX[value & 0x0FU],
            };
            for (size_t part = 0U; part < sizeof(escaped); ++part) {
                append_char(destination,
                            destination_size,
                            &written,
                            escaped[part]);
            }
        }
    }

    if (destination != NULL && destination_size > 0U) {
        const size_t terminator =
            written < destination_size ? written : destination_size - 1U;
        destination[terminator] = '\0';
    }
    return written;
}

void probe_bytes_to_hex(char *destination,
                        size_t destination_size,
                        const uint8_t *source,
                        size_t source_size)
{
    if (destination == NULL || destination_size == 0U) {
        return;
    }
    size_t written = 0U;
    if (source != NULL) {
        for (size_t index = 0U; index < source_size; ++index) {
            if (written + 2U >= destination_size) {
                break;
            }
            destination[written++] = HEX[source[index] >> 4U];
            destination[written++] = HEX[source[index] & 0x0FU];
        }
    }
    destination[written] = '\0';
}

void probe_format_bssid(char destination[18], const uint8_t bssid[6])
{
    if (destination == NULL) {
        return;
    }
    if (bssid == NULL) {
        destination[0] = '\0';
        return;
    }
    (void)snprintf(destination,
                   18U,
                   "%02X:%02X:%02X:%02X:%02X:%02X",
                   bssid[0],
                   bssid[1],
                   bssid[2],
                   bssid[3],
                   bssid[4],
                   bssid[5]);
}
