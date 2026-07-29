#include "recovery_button.h"

#include <stdio.h>

#define ASSERT_EQ(actual, expected) do { if ((actual) != (expected)) return 1; } while (0)

static int test_short_press(void)
{
    recovery_button_t button;
    recovery_button_init(&button, false, 5000U);
    ASSERT_EQ(recovery_button_update(&button, true, 1000),
              RECOVERY_BUTTON_NO_ACTION);
    ASSERT_EQ(recovery_button_update(&button, true, 2000),
              RECOVERY_BUTTON_NO_ACTION);
    ASSERT_EQ(recovery_button_update(&button, false, 2100),
              RECOVERY_BUTTON_SHORT_PRESS);
    return 0;
}

static int test_long_press_requires_release(void)
{
    recovery_button_t button;
    recovery_button_init(&button, false, 5000U);
    ASSERT_EQ(recovery_button_update(&button, true, 1000),
              RECOVERY_BUTTON_NO_ACTION);
    ASSERT_EQ(recovery_button_update(&button, true, 5999),
              RECOVERY_BUTTON_NO_ACTION);
    ASSERT_EQ(recovery_button_update(&button, true, 6000),
              RECOVERY_BUTTON_ARMED);
    ASSERT_EQ(recovery_button_update(&button, true, 7000),
              RECOVERY_BUTTON_NO_ACTION);
    ASSERT_EQ(recovery_button_update(&button, false, 7100),
              RECOVERY_BUTTON_REQUESTED);
    return 0;
}

static int test_boot_hold_is_ignored(void)
{
    recovery_button_t button;
    recovery_button_init(&button, true, 5000U);
    ASSERT_EQ(recovery_button_update(&button, true, 10000),
              RECOVERY_BUTTON_NO_ACTION);
    ASSERT_EQ(recovery_button_update(&button, false, 10100),
              RECOVERY_BUTTON_NO_ACTION);
    ASSERT_EQ(recovery_button_update(&button, true, 11000),
              RECOVERY_BUTTON_NO_ACTION);
    ASSERT_EQ(recovery_button_update(&button, false, 11100),
              RECOVERY_BUTTON_SHORT_PRESS);
    return 0;
}

int main(void)
{
    ASSERT_EQ(test_short_press(), 0);
    ASSERT_EQ(test_long_press_requires_release(), 0);
    ASSERT_EQ(test_boot_hold_is_ignored(), 0);
    puts("recovery button tests passed");
    return 0;
}
