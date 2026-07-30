#include "motion_history.h"

#include <assert.h>

int main(void)
{
    motion_clock_t clock;
    motion_clock_clear(&clock);
    int64_t epoch = 0;
    assert(!motion_clock_now(&clock, 1000, &epoch));
    assert(!motion_clock_set(&clock, 1, 1000));
    assert(motion_clock_set(&clock, 1767225600, 10000));
    assert(motion_clock_now(&clock, 13500, &epoch));
    assert(epoch == 1767225603);
    assert(motion_clock_at(&clock, 5000, &epoch));
    assert(epoch == 1767225595);

    motion_history_t history;
    motion_history_init(&history);
    motion_history_start(&history, 10U, 1000, 300U, 250U, 900U);
    motion_history_update(&history, 11U, 450U, 1000U);
    motion_history_finish(&history, 12U, 3500);

    motion_history_event_t events[MOTION_HISTORY_CAPACITY];
    size_t count = motion_history_snapshot(
        &history, events, MOTION_HISTORY_CAPACITY);
    assert(count == 1U);
    assert(events[0].id == 1U);
    assert(events[0].start_scan_id == 10U);
    assert(events[0].end_scan_id == 12U);
    assert(events[0].peak_score_x100 == 450U);
    assert(events[0].coverage_permille == 1000U);
    assert(!events[0].active);
    assert(events[0].ended_monotonic_ms -
               events[0].started_monotonic_ms ==
           2500);

    for (uint32_t index = 0U;
         index < MOTION_HISTORY_CAPACITY + 5U;
         ++index) {
        motion_history_start(
            &history, 20U + index, 5000 + index * 10, 300U, 250U, 800U);
        motion_history_finish(
            &history, 20U + index, 5005 + index * 10);
    }
    count = motion_history_snapshot(
        &history, events, MOTION_HISTORY_CAPACITY);
    assert(count == MOTION_HISTORY_CAPACITY);
    assert(events[0].id == 7U);
    assert(events[count - 1U].id == MOTION_HISTORY_CAPACITY + 6U);
    return 0;
}
