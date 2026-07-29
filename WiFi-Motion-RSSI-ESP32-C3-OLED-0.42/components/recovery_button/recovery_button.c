#include "recovery_button.h"

#include <string.h>

void recovery_button_init(recovery_button_t *button,
                          bool initially_pressed,
                          uint64_t hold_ms)
{
    if (button == NULL) {
        return;
    }
    memset(button, 0, sizeof(*button));
    button->hold_ms = hold_ms;
    button->previous_pressed = initially_pressed;
    button->ignore_until_release = initially_pressed;
}

recovery_button_action_t recovery_button_update(recovery_button_t *button,
                                                bool pressed,
                                                int64_t timestamp_ms)
{
    if (button == NULL || timestamp_ms < 0 || button->hold_ms == 0U) {
        return RECOVERY_BUTTON_NO_ACTION;
    }
    if (button->ignore_until_release) {
        button->previous_pressed = pressed;
        if (!pressed) {
            button->ignore_until_release = false;
        }
        return RECOVERY_BUTTON_NO_ACTION;
    }

    if (pressed && !button->previous_pressed) {
        button->pressed_at_ms = timestamp_ms;
        button->armed = false;
    }

    recovery_button_action_t action = RECOVERY_BUTTON_NO_ACTION;
    if (pressed && button->previous_pressed && !button->armed &&
        timestamp_ms >= button->pressed_at_ms &&
        (uint64_t)(timestamp_ms - button->pressed_at_ms) >= button->hold_ms) {
        button->armed = true;
        action = RECOVERY_BUTTON_ARMED;
    } else if (!pressed && button->previous_pressed) {
        if (button->armed ||
            (timestamp_ms >= button->pressed_at_ms &&
             (uint64_t)(timestamp_ms - button->pressed_at_ms) >=
                 button->hold_ms)) {
            action = RECOVERY_BUTTON_REQUESTED;
        } else {
            action = RECOVERY_BUTTON_SHORT_PRESS;
        }
        button->armed = false;
    }
    button->previous_pressed = pressed;
    return action;
}
