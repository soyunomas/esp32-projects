#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RECOVERY_BUTTON_NO_ACTION = 0,
    RECOVERY_BUTTON_SHORT_PRESS,
    RECOVERY_BUTTON_ARMED,
    RECOVERY_BUTTON_REQUESTED,
} recovery_button_action_t;

typedef struct {
    uint64_t hold_ms;
    int64_t pressed_at_ms;
    bool previous_pressed;
    bool ignore_until_release;
    bool armed;
} recovery_button_t;

void recovery_button_init(recovery_button_t *button,
                          bool initially_pressed,
                          uint64_t hold_ms);
recovery_button_action_t recovery_button_update(recovery_button_t *button,
                                                bool pressed,
                                                int64_t timestamp_ms);

#ifdef __cplusplus
}
#endif
