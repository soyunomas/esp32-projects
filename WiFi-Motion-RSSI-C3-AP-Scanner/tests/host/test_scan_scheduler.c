#include "scan_scheduler.h"

#include <assert.h>

int main(void)
{
    scan_scheduler_t scheduler;
    const uint8_t selected[] = {1U, 6U};
    assert(scan_scheduler_init(&scheduler, selected, 2U));
    assert(scan_scheduler_channel_count(&scheduler) == 2U);
    assert(scan_scheduler_channel_at(&scheduler, 0U) == 1U);
    assert(scan_scheduler_channel_at(&scheduler, 1U) == 6U);
    assert(scan_scheduler_channel_at(&scheduler, 2U) == 0U);

    const uint8_t all[] = {0U};
    assert(scan_scheduler_init(&scheduler, all, 1U));
    assert(scan_scheduler_channel_count(&scheduler) == 1U);
    assert(scan_scheduler_channel_at(&scheduler, 0U) == 0U);

    const uint8_t duplicate[] = {6U, 6U};
    assert(!scan_scheduler_init(&scheduler, duplicate, 2U));
    const uint8_t invalid_zero[] = {0U, 6U};
    assert(!scan_scheduler_init(&scheduler, invalid_zero, 2U));
    const uint8_t invalid_channel[] = {15U};
    assert(!scan_scheduler_init(&scheduler, invalid_channel, 1U));
    assert(!scan_scheduler_init(NULL, selected, 2U));
    assert(!scan_scheduler_init(&scheduler, NULL, 2U));
    assert(!scan_scheduler_init(&scheduler, selected, 0U));
    return 0;
}
