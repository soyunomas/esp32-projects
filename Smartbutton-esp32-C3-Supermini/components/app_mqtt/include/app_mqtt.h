#pragma once
#include "app_nvs.h"
#include "stdbool.h"

// Dispara el envío de mensaje MQTT (conecta, publica, desconecta)
void app_mqtt_publish_oneshot(int btn_id, button_config_t *btn_cfg);

// Versión síncrona para testeo
int app_mqtt_test_sync(const mqtt_config_t *mcfg, const button_config_t *bcfg);
