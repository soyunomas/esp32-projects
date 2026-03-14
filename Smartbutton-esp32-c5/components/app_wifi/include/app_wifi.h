#pragma once
#include "cJSON.h"

void app_wifi_start_sta(void);
void app_wifi_start_ap(void);
void app_wifi_switch_to_ap(void);
cJSON *app_wifi_scan(void);
cJSON *app_wifi_get_netinfo(void);
