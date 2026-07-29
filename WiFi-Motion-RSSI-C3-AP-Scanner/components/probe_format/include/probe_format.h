#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

size_t probe_json_escape_bytes(char *destination,
                               size_t destination_size,
                               const uint8_t *source,
                               size_t source_size);
void probe_bytes_to_hex(char *destination,
                        size_t destination_size,
                        const uint8_t *source,
                        size_t source_size);
void probe_format_bssid(char destination[18], const uint8_t bssid[6]);

#ifdef __cplusplus
}
#endif
