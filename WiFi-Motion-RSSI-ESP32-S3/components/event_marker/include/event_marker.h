#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EVENT_MARKER_NO_TRANSITION = 0,
    EVENT_MARKER_STARTED = 1,
    EVENT_MARKER_FINISHED = -1,
} event_marker_transition_t;

typedef struct {
    bool active;
    bool previous_pressed;
    bool has_accepted_press;
    uint32_t event_id;
    int64_t last_accepted_press_ms;
    uint32_t debounce_ms;
} event_marker_t;

typedef struct {
    bool active;
    uint32_t event_id;
    event_marker_transition_t transition;
} event_marker_result_t;

void event_marker_init(event_marker_t *marker,
                       bool initial_pressed,
                       uint32_t debounce_ms);
event_marker_result_t event_marker_update(event_marker_t *marker,
                                          bool pressed,
                                          int64_t timestamp_ms);

#ifdef __cplusplus
}
#endif
