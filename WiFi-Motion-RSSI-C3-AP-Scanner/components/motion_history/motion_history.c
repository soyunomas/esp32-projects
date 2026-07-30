#include "motion_history.h"

#include <string.h>

#define MOTION_CLOCK_MIN_EPOCH 946684800LL
#define MOTION_CLOCK_MAX_EPOCH 4102444799LL

static motion_history_event_t *latest(motion_history_t *history)
{
    if (history == NULL || history->count == 0U) {
        return NULL;
    }
    const size_t index =
        (history->start + history->count - 1U) %
        MOTION_HISTORY_CAPACITY;
    return &history->events[index];
}

void motion_history_init(motion_history_t *history)
{
    if (history != NULL) {
        memset(history, 0, sizeof(*history));
        history->next_id = 1U;
    }
}

void motion_history_start(motion_history_t *history,
                          uint32_t scan_id,
                          int64_t monotonic_ms,
                          uint16_t score_x100,
                          uint16_t trigger_score_x100,
                          uint16_t coverage_permille)
{
    if (history == NULL || scan_id == 0U || monotonic_ms < 0) {
        return;
    }
    motion_history_event_t *current = latest(history);
    if (current != NULL && current->active) {
        return;
    }
    size_t index;
    if (history->count < MOTION_HISTORY_CAPACITY) {
        index =
            (history->start + history->count) %
            MOTION_HISTORY_CAPACITY;
        history->count++;
    } else {
        index = history->start;
        history->start =
            (history->start + 1U) % MOTION_HISTORY_CAPACITY;
    }
    motion_history_event_t *event = &history->events[index];
    memset(event, 0, sizeof(*event));
    event->id = history->next_id++;
    if (history->next_id == 0U) {
        history->next_id = 1U;
    }
    event->start_scan_id = scan_id;
    event->started_monotonic_ms = monotonic_ms;
    event->trigger_score_x100 = trigger_score_x100;
    event->start_score_x100 = score_x100;
    event->peak_score_x100 = score_x100;
    event->coverage_permille = coverage_permille;
    event->active = true;
}

void motion_history_update(motion_history_t *history,
                           uint32_t scan_id,
                           uint16_t score_x100,
                           uint16_t coverage_permille)
{
    motion_history_event_t *event = latest(history);
    if (event == NULL || !event->active || scan_id == 0U) {
        return;
    }
    event->end_scan_id = scan_id;
    if (score_x100 > event->peak_score_x100) {
        event->peak_score_x100 = score_x100;
    }
    event->coverage_permille = coverage_permille;
}

void motion_history_finish(motion_history_t *history,
                           uint32_t scan_id,
                           int64_t monotonic_ms)
{
    motion_history_event_t *event = latest(history);
    if (event == NULL || !event->active || scan_id == 0U ||
        monotonic_ms < event->started_monotonic_ms) {
        return;
    }
    event->end_scan_id = scan_id;
    event->ended_monotonic_ms = monotonic_ms;
    event->active = false;
}

size_t motion_history_snapshot(const motion_history_t *history,
                               motion_history_event_t *events,
                               size_t capacity)
{
    if (history == NULL || events == NULL || capacity == 0U) {
        return 0U;
    }
    const size_t count =
        history->count < capacity ? history->count : capacity;
    const size_t first =
        (history->start + history->count - count) %
        MOTION_HISTORY_CAPACITY;
    for (size_t index = 0U; index < count; ++index) {
        events[index] =
            history->events[
                (first + index) % MOTION_HISTORY_CAPACITY];
    }
    return count;
}

void motion_clock_clear(motion_clock_t *clock)
{
    if (clock != NULL) {
        memset(clock, 0, sizeof(*clock));
    }
}

bool motion_clock_set(motion_clock_t *clock,
                      int64_t epoch_seconds,
                      int64_t monotonic_ms)
{
    if (clock == NULL || epoch_seconds < MOTION_CLOCK_MIN_EPOCH ||
        epoch_seconds > MOTION_CLOCK_MAX_EPOCH || monotonic_ms < 0) {
        return false;
    }
    clock->configured = true;
    clock->epoch_seconds_at_sync = epoch_seconds;
    clock->monotonic_ms_at_sync = monotonic_ms;
    return true;
}

bool motion_clock_at(const motion_clock_t *clock,
                     int64_t event_monotonic_ms,
                     int64_t *epoch_seconds)
{
    if (clock == NULL || !clock->configured ||
        event_monotonic_ms < 0 || epoch_seconds == NULL) {
        return false;
    }
    *epoch_seconds =
        clock->epoch_seconds_at_sync +
        (event_monotonic_ms - clock->monotonic_ms_at_sync) / 1000;
    return true;
}

bool motion_clock_now(const motion_clock_t *clock,
                      int64_t monotonic_ms,
                      int64_t *epoch_seconds)
{
    return motion_clock_at(clock, monotonic_ms, epoch_seconds);
}
