#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SCAN_SCHEDULER_MAX_CHANNELS 14U

typedef struct {
    uint8_t channels[SCAN_SCHEDULER_MAX_CHANNELS];
    size_t channel_count;
} scan_scheduler_t;

bool scan_scheduler_init(scan_scheduler_t *scheduler,
                         const uint8_t *channels,
                         size_t channel_count);
size_t scan_scheduler_channel_count(const scan_scheduler_t *scheduler);
uint8_t scan_scheduler_channel_at(const scan_scheduler_t *scheduler,
                                  size_t index);

#ifdef __cplusplus
}
#endif
