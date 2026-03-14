#pragma once
void app_buttons_init(void);

// Permite a main.c disparar un evento síncrono al despertar del deepsleep
// protegiendo la acción con el temporizador "cooldown" interno
void app_buttons_simulate_press(int btn_id);
