#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MOTION_HISTORY_CAPACITY 128U

typedef struct {
    uint32_t id;
    uint32_t start_scan_id;
    uint32_t end_scan_id;
    int64_t started_monotonic_ms;
    int64_t ended_monotonic_ms;
    uint16_t trigger_score_x100;
    uint16_t start_score_x100;
    uint16_t peak_score_x100;
    uint16_t coverage_permille;
    bool active;
} motion_history_event_t;

typedef struct {
    motion_history_event_t events[MOTION_HISTORY_CAPACITY];
    size_t start;
    size_t count;
    uint32_t next_id;
} motion_history_t;

typedef struct {
    bool configured;
    int64_t epoch_seconds_at_sync;
    int64_t monotonic_ms_at_sync;
} motion_clock_t;

void motion_history_init(motion_history_t *history);
void motion_history_start(motion_history_t *history,
                          uint32_t scan_id,
                          int64_t monotonic_ms,
                          uint16_t score_x100,
                          uint16_t trigger_score_x100,
                          uint16_t coverage_permille);
void motion_history_update(motion_history_t *history,
                           uint32_t scan_id,
                           uint16_t score_x100,
                           uint16_t coverage_permille);
void motion_history_finish(motion_history_t *history,
                           uint32_t scan_id,
                           int64_t monotonic_ms);
size_t motion_history_snapshot(const motion_history_t *history,
                               motion_history_event_t *events,
                               size_t capacity);

void motion_clock_clear(motion_clock_t *clock);
bool motion_clock_set(motion_clock_t *clock,
                      int64_t epoch_seconds,
                      int64_t monotonic_ms);
bool motion_clock_now(const motion_clock_t *clock,
                      int64_t monotonic_ms,
                      int64_t *epoch_seconds);
bool motion_clock_at(const motion_clock_t *clock,
                     int64_t event_monotonic_ms,
                     int64_t *epoch_seconds);
