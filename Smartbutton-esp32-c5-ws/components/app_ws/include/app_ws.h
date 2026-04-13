#pragma once
#include "app_nvs.h"

// Envía mensaje WebSocket oneshot (conecta, envía, desconecta)
void app_ws_send_oneshot(int btn_id, button_config_t *cfg);

// Versión síncrona para testeo desde la web
int app_ws_test_sync(const button_config_t *cfg);
