#include "event_marker.h"

#include <string.h>

void event_marker_init(event_marker_t *marker,
                       bool initial_pressed,
                       uint32_t debounce_ms)
{
    if (marker == NULL) {
        return;
    }
    memset(marker, 0, sizeof(*marker));
    marker->previous_pressed = initial_pressed;
    marker->debounce_ms = debounce_ms;
}

event_marker_result_t event_marker_update(event_marker_t *marker,
                                          bool pressed,
                                          int64_t timestamp_ms)
{
    event_marker_result_t result = {0};
    if (marker == NULL) {
        return result;
    }

    const bool falling_edge = pressed && !marker->previous_pressed;
    marker->previous_pressed = pressed;
    const bool debounce_elapsed =
        !marker->has_accepted_press ||
        (timestamp_ms >= marker->last_accepted_press_ms &&
         (uint64_t)(timestamp_ms - marker->last_accepted_press_ms) >=
             marker->debounce_ms);

    if (falling_edge && debounce_elapsed) {
        marker->has_accepted_press = true;
        marker->last_accepted_press_ms = timestamp_ms;
        marker->active = !marker->active;
        if (marker->active) {
            marker->event_id++;
            result.transition = EVENT_MARKER_STARTED;
        } else {
            result.transition = EVENT_MARKER_FINISHED;
        }
    }

    result.active = marker->active;
    result.event_id = marker->event_id;
    return result;
}
