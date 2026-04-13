#include "app_core.h"
#include "app_nvs.h"
#include "app_wifi.h"
#include "app_web.h"
#include "app_buttons.h"
#include "app_led.h"
#include "app_dns.h"
#include "app_http.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"

volatile int g_wakeup_btn = 0;

#define NUM_BUTTONS 5
static const gpio_num_t GPIO_BTN[NUM_BUTTONS] = {
    GPIO_NUM_4, GPIO_NUM_5, GPIO_NUM_6, GPIO_NUM_7, GPIO_NUM_10
};

void app_main(void) {
    // ---------------------------------------------------------
    // 1. FASE CRÍTICA DE ARRANQUE
    // ---------------------------------------------------------
    
    // ESP-IDF 6+: Devuelve un bitmask con todas las causas de wakeup detectadas
    uint64_t causes = esp_sleep_get_wakeup_causes();

    // ESP32-C3: Configuramos los pines con Pull-Up ANTES de leer
    uint64_t btn_mask = 0;
    for (int i = 0; i < NUM_BUTTONS; i++) {
        btn_mask |= (1ULL << GPIO_BTN[i]);
    }
    gpio_config_t io_conf = {
        .pin_bit_mask = btn_mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    for (int i = 0; i < NUM_BUTTONS; i++) {
        gpio_hold_dis(GPIO_BTN[i]);
    }
    esp_rom_delay_us(2000); 

    int detected_btn = 0;
    bool is_deep_sleep_wake = (esp_reset_reason() == ESP_RST_DEEPSLEEP);

    if (is_deep_sleep_wake) {
        // ESP32-C3: No tiene EXT1, usamos GPIO wakeup.
        // Verificamos si ESP_SLEEP_WAKEUP_GPIO está en el bitmask
        if (causes & (1ULL << ESP_SLEEP_WAKEUP_GPIO)) {
            for (int i = 0; i < NUM_BUTTONS; i++) {
                if (gpio_get_level(GPIO_BTN[i]) == 0) { detected_btn = i + 1; break; }
            }
        }
        // Fallback: leer estado físico
        if (detected_btn == 0) {
            for (int i = 0; i < NUM_BUTTONS; i++) {
                if (gpio_get_level(GPIO_BTN[i]) == 0) { detected_btn = i + 1; break; }
            }
        }
        g_wakeup_btn = detected_btn;
        ESP_LOGI("MAIN", "WAKEUP DETECTADO -> Causes: %llu, Boton ID: %d", (unsigned long long)causes, detected_btn);
    }

    // ---------------------------------------------------------
    // 2. INICIALIZACIÓN DEL SISTEMA
    // ---------------------------------------------------------
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    app_nvs_init();
    app_event_group = xEventGroupCreate();
    
    app_set_state_callback(app_led_update_state);
    
    app_led_init();
    app_buttons_init();

    nvs_wifi_config_t conf;
    bool configured = app_nvs_get_wifi_config(&conf);

    admin_config_t admin;
    app_nvs_get_admin(&admin);

    if (admin.deep_sleep && !is_deep_sleep_wake) {
        ESP_LOGI("MAIN", "Arranque frio. Manteniendo despierto %d seg para config.", admin.config_awake_s);
    }

    if (!configured) {
        app_set_state(STATE_NO_CONFIG);
        app_wifi_start_ap();
        app_dns_start();
    } else {
        app_set_state(STATE_CONNECTING);
        app_wifi_start_sta();
    }
    app_web_start();

    uint32_t uptime_sec = 0;
    bool action_triggered = false;

    // ---------------------------------------------------------
    // 3. BUCLE PRINCIPAL (SUPERLOOP)
    // ---------------------------------------------------------
    while (1) {
        EventBits_t bits = xEventGroupWaitBits(app_event_group, EVENT_HTTP_DONE, pdTRUE, pdFALSE, pdMS_TO_TICKS(1000));
        uptime_sec++;

        if (app_get_state() == STATE_FACTORY_RESET) {
            app_nvs_clear_all();
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
        }

        if (admin.deep_sleep && configured) {

            // A) Disparo Diferido: Esperamos a que la WiFi conecte
            if (is_deep_sleep_wake && detected_btn > 0 && !action_triggered) {
                if (app_get_state() == STATE_NORMAL) { 
                    ESP_LOGI("MAIN", "WiFi OK. Ejecutando accion Boton %d", detected_btn);
                    app_buttons_simulate_press(detected_btn);
                    g_wakeup_btn = 0;
                    action_triggered = true;
                } else if (uptime_sec > 15) {
                   ESP_LOGE("MAIN", "WiFi Timeout (15s). Abortando auto-ejecucion.");
                   action_triggered = true; 
                }
            }

            // B) Decisión de Dormir
            bool ready_to_sleep = false;

            if (is_deep_sleep_wake) {
                if (bits & EVENT_HTTP_DONE) {
                    ESP_LOGI("MAIN", "Accion completada exitosamente. A dormir.");
                    ready_to_sleep = true;
                }
                
                if (uptime_sec > (uint32_t)admin.wakeup_timeout_s) {
                    ESP_LOGW("MAIN", "Timeout de seguridad alcanzado. A dormir.");
                    ready_to_sleep = true;
                }
            } else {
                if (uptime_sec > (uint32_t)admin.config_awake_s) {
                    ESP_LOGI("MAIN", "Tiempo de configuracion agotado. A dormir.");
                    ready_to_sleep = true;
                }
            }

            // C) SECUENCIA DE APAGADO SEGURA
            if (ready_to_sleep) {
                ESP_LOGI("MAIN", "Iniciando secuencia de Deep Sleep...");
                
                // 1. Apagar LED
                app_led_off();
                vTaskDelay(pdMS_TO_TICKS(100));

                // 2. Configurar GPIO Wakeup (ESP32-C3: API actualizada para ESP-IDF 6.0+)
                esp_sleep_enable_gpio_wakeup_on_hp_periph_powerdown(btn_mask, ESP_GPIO_WAKEUP_GPIO_LOW);
                
                // 3. Congelar pines para evitar consumos y lecturas falsas
                for (int i = 0; i < NUM_BUTTONS; i++) {
                    gpio_hold_en(GPIO_BTN[i]);
                }

                // 4. Dormir
                esp_deep_sleep_start();
            }
        }
    }
}
