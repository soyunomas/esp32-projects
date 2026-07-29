#include "scan_scheduler.h"

#include <string.h>

bool scan_scheduler_init(scan_scheduler_t *scheduler,
                         const uint8_t *channels,
                         size_t channel_count)
{
    if (scheduler == NULL || channels == NULL || channel_count == 0U ||
        channel_count > SCAN_SCHEDULER_MAX_CHANNELS) {
        return false;
    }

    memset(scheduler, 0, sizeof(*scheduler));
    for (size_t index = 0U; index < channel_count; ++index) {
        const uint8_t channel = channels[index];
        if (channel > 14U || (channel == 0U && channel_count != 1U)) {
            return false;
        }
        for (size_t previous = 0U; previous < index; ++previous) {
            if (channels[previous] == channel) {
                return false;
            }
        }
        scheduler->channels[index] = channel;
    }
    scheduler->channel_count = channel_count;
    return true;
}

size_t scan_scheduler_channel_count(const scan_scheduler_t *scheduler)
{
    return scheduler != NULL ? scheduler->channel_count : 0U;
}

uint8_t scan_scheduler_channel_at(const scan_scheduler_t *scheduler,
                                  size_t index)
{
    if (scheduler == NULL || index >= scheduler->channel_count) {
        return 0U;
    }
    return scheduler->channels[index];
}
