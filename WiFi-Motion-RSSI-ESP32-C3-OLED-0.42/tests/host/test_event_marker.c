#include "event_marker.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    event_marker_t marker;
    event_marker_init(&marker, false, 250U);

    event_marker_result_t result = event_marker_update(&marker, false, 0);
    assert(!result.active);
    assert(result.transition == EVENT_MARKER_NO_TRANSITION);

    result = event_marker_update(&marker, true, 1000);
    assert(result.active);
    assert(result.event_id == 1U);
    assert(result.transition == EVENT_MARKER_STARTED);

    result = event_marker_update(&marker, false, 1010);
    assert(result.active);
    result = event_marker_update(&marker, true, 1100);
    assert(result.active);
    assert(result.transition == EVENT_MARKER_NO_TRANSITION);

    result = event_marker_update(&marker, false, 1200);
    assert(result.active);
    result = event_marker_update(&marker, true, 1300);
    assert(!result.active);
    assert(result.event_id == 1U);
    assert(result.transition == EVENT_MARKER_FINISHED);

    event_marker_init(&marker, true, 250U);
    result = event_marker_update(&marker, true, 0);
    assert(!result.active);
    result = event_marker_update(&marker, false, 100);
    assert(!result.active);
    result = event_marker_update(&marker, true, 500);
    assert(result.active);
    assert(result.event_id == 1U);

    puts("event_marker tests passed");
    return 0;
}
