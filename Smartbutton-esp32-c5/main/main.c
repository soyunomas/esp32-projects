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

// Definición estricta de pines
#define GPIO_BTN1 GPIO_NUM_4
#define GPIO_BTN2 GPIO_NUM_5

void app_main(void) {
    // ---------------------------------------------------------
    // 1. FASE CRÍTICA DE ARRANQUE (RTC DOMAIN)
    // ---------------------------------------------------------
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    uint64_t ext1_mask = esp_sleep_get_ext1_wakeup_status();
    uint64_t gpio_mask = esp_sleep_get_gpio_wakeup_status();
    uint64_t wakeup_pin_mask = ext1_mask | gpio_mask;

    // MAGIA: Configuramos los pines con Pull-Up ANTES de soltar el Hold del Deep Sleep.
    // Esto evita que queden flotando y permite una lectura física 100% fiable.
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_BTN1) | (1ULL << GPIO_BTN2),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    // Ahora sí devolvemos el control al procesador principal
    gpio_hold_dis(GPIO_BTN1);
    gpio_hold_dis(GPIO_BTN2);
    esp_rom_delay_us(2000); 

    int detected_btn = 0;
    bool is_deep_sleep_wake = (esp_reset_reason() == ESP_RST_DEEPSLEEP);

    if (is_deep_sleep_wake) {
        // Prioridad 1: Registro inmutable del hardware RTC (solo si causa es EXT1)
        if (cause == ESP_SLEEP_WAKEUP_EXT1) {
            if (ext1_mask & (1ULL << GPIO_BTN1)) detected_btn = 1;
            else if (ext1_mask & (1ULL << GPIO_BTN2)) detected_btn = 2;
        }
        // Fallback: read physical pin state
        if (detected_btn == 0) {
            if (gpio_get_level(GPIO_BTN1) == 0) detected_btn = 1;
            else if (gpio_get_level(GPIO_BTN2) == 0) detected_btn = 2;
        }
        g_wakeup_btn = detected_btn;
        ESP_LOGI("MAIN", "WAKEUP DETECTADO -> Cause: %d, Ext1: 0x%llx, Boton ID: %d", 
                 cause, (unsigned long long)ext1_mask, detected_btn);
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
                // CONDICIÓN CORREGIDA: Si la accion termina, nos dormimos SIEMPRE.
                if (bits & EVENT_HTTP_DONE) {
                    ESP_LOGI("MAIN", "Accion completada exitosamente. A dormir.");
                    ready_to_sleep = true;
                }
                
                // Timeout global de seguridad (30s por defecto)
                if (uptime_sec > (uint32_t)admin.wakeup_timeout_s) {
                    ESP_LOGW("MAIN", "Timeout de seguridad alcanzado. A dormir.");
                    ready_to_sleep = true;
                }
            } else {
                // Modo arranque frío: solo dormimos por timeout de configuración
                if (uptime_sec > (uint32_t)admin.config_awake_s) {
                    ESP_LOGI("MAIN", "Tiempo de configuracion agotado. A dormir.");
                    ready_to_sleep = true;
                }
            }

            // C) SECUENCIA DE APAGADO SEGURA
            if (ready_to_sleep) {
                ESP_LOGI("MAIN", "Iniciando secuencia de Deep Sleep...");
                
                // 1. Apagar LED explícitamente
                app_led_off();
                
                // 2. RETARDO CRÍTICO: Dar tiempo al FreeRTOS para enviar el dato por el cable WS2812
                vTaskDelay(pdMS_TO_TICKS(500)); 

                // 3. Configurar Wakeup
                const uint64_t gpio_mask = (1ULL << GPIO_BTN1) | (1ULL << GPIO_BTN2);
                esp_sleep_enable_ext1_wakeup_io(gpio_mask, ESP_EXT1_WAKEUP_ANY_LOW);
                
                // 4. Congelar los pines para evitar consumos y lecturas falsas
                gpio_hold_en(GPIO_BTN1);
                gpio_hold_en(GPIO_BTN2);

                // 5. Matar CPU
                esp_deep_sleep_start();
            }
        }
    }
}
