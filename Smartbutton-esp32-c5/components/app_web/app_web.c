#include "app_web.h"
#include "html_ui.h"
#include "esp_http_server.h"
#include "app_nvs.h"
#include "app_wifi.h"
#include "app_core.h"
#include "app_http.h"
#include "app_mqtt.h" // Include nuevo
#include "esp_log.h"
#include "cJSON.h"
#include "mbedtls/base64.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "app_btn_leds.h"
#include <string.h>

static const char *TAG = "WEB";

static bool check_auth(httpd_req_t *req) {
    char auth_hdr[192] = {0};
    if (httpd_req_get_hdr_value_str(req, "Authorization", auth_hdr, sizeof(auth_hdr)) != ESP_OK) {
        return false;
    }
    if (strncmp(auth_hdr, "Basic ", 6) != 0) return false;

    unsigned char decoded[128];
    size_t decoded_len = 0;
    if (mbedtls_base64_decode(decoded, sizeof(decoded) - 1, &decoded_len,
                              (unsigned char *)auth_hdr + 6, strlen(auth_hdr + 6)) != 0) {
        return false;
    }
    decoded[decoded_len] = 0;

    char *sep = strchr((char *)decoded, ':');
    if (!sep) return false;
    *sep = 0;
    const char *rx_user = (char *)decoded;
    const char *rx_pass = sep + 1;

    admin_config_t admin;
    app_nvs_get_admin(&admin);

    return (strcmp(rx_user, admin.user) == 0 && strcmp(rx_pass, admin.pass) == 0);
}

static esp_err_t send_auth_required(httpd_req_t *req) {
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"SmartButton\"");
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "Login required", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t captive_redirect_handler(httpd_req_t *req) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t root_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, index_html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t verify_get_handler(httpd_req_t *req) {
    if (!check_auth(req)) return send_auth_required(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t scan_get_handler(httpd_req_t *req) {
    if (!check_auth(req)) return send_auth_required(req);
    cJSON *arr = app_wifi_scan();
    char *json = cJSON_PrintUnformatted(arr);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    free(json);
    cJSON_Delete(arr);
    return ESP_OK;
}

static esp_err_t wifi_post_handler(httpd_req_t *req) {
    if (!check_auth(req)) return send_auth_required(req);
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = 0;

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send(req, "{\"ok\":false}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
    cJSON *pass = cJSON_GetObjectItem(root, "pass");

    if (cJSON_IsString(ssid) && ssid->valuestring[0]) {
        const char *p = (cJSON_IsString(pass)) ? pass->valuestring : "";
        app_nvs_save_wifi(ssid->valuestring, p);
        ESP_LOGI(TAG, "WiFi config saved: %s", ssid->valuestring);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
        cJSON_Delete(root);
        vTaskDelay(pdMS_TO_TICKS(1500));
        esp_restart();
        return ESP_OK;
    }

    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":false}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t mqtt_get_handler(httpd_req_t *req) {
    if (!check_auth(req)) return send_auth_required(req);
    mqtt_config_t cfg;
    app_nvs_get_mqtt(&cfg);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "host", cfg.host);
    cJSON_AddNumberToObject(root, "port", cfg.port);
    cJSON_AddStringToObject(root, "user", cfg.user);
    // No enviamos el pass real por seguridad, solo indicativo
    cJSON_AddStringToObject(root, "pass", ""); 
    cJSON_AddStringToObject(root, "clid", cfg.client_id);
    cJSON_AddBoolToObject(root, "en", cfg.enabled);

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t mqtt_post_handler(httpd_req_t *req) {
    if (!check_auth(req)) return send_auth_required(req);
    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = 0;

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send(req, "{\"ok\":false}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    mqtt_config_t cfg;
    app_nvs_get_mqtt(&cfg);

    cJSON *host = cJSON_GetObjectItem(root, "host");
    cJSON *port = cJSON_GetObjectItem(root, "port");
    cJSON *user = cJSON_GetObjectItem(root, "user");
    cJSON *pass = cJSON_GetObjectItem(root, "pass");
    cJSON *clid = cJSON_GetObjectItem(root, "clid");
    cJSON *en = cJSON_GetObjectItem(root, "en");

    if (cJSON_IsString(host)) strlcpy(cfg.host, host->valuestring, sizeof(cfg.host));
    if (cJSON_IsNumber(port)) cfg.port = port->valueint;
    if (cJSON_IsString(user)) strlcpy(cfg.user, user->valuestring, sizeof(cfg.user));
    if (cJSON_IsString(pass) && strlen(pass->valuestring) > 0) strlcpy(cfg.pass, pass->valuestring, sizeof(cfg.pass));
    if (cJSON_IsString(clid)) strlcpy(cfg.client_id, clid->valuestring, sizeof(cfg.client_id));
    if (cJSON_IsBool(en)) cfg.enabled = cJSON_IsTrue(en);

    app_nvs_save_mqtt(&cfg);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t btn_post_handler(httpd_req_t *req) {
    if (!check_auth(req)) return send_auth_required(req);
    int total = req->content_len;
    if (total <= 0 || total >= 2048) {
        httpd_resp_set_status(req, "413 Payload Too Large");
        httpd_resp_send(req, "{\"ok\":false}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    char buf[2048];
    int received = 0;
    while (received < total) {
        int ret = httpd_req_recv(req, buf + received, total - received);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) continue;
            return ESP_FAIL;
        }
        received += ret;
    }
    buf[received] = 0;

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send(req, "{\"ok\":false}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    cJSON *jid = cJSON_GetObjectItem(root, "id");
    cJSON *jatype = cJSON_GetObjectItem(root, "atype");
    cJSON *jtarget = cJSON_GetObjectItem(root, "target"); // URL or Topic
    cJSON *jmethod = cJSON_GetObjectItem(root, "method");
    cJSON *jpayload = cJSON_GetObjectItem(root, "payload");
    cJSON *jtimeout = cJSON_GetObjectItem(root, "timeout");
    cJSON *jcooldown = cJSON_GetObjectItem(root, "cooldown");
    cJSON *jnocache = cJSON_GetObjectItem(root, "nocache");
    cJSON *jname = cJSON_GetObjectItem(root, "name");

    if (cJSON_IsNumber(jid) && cJSON_IsString(jtarget)) {
        button_config_t cfg = {0};
        if (cJSON_IsString(jname)) strlcpy(cfg.name, jname->valuestring, sizeof(cfg.name));
        cfg.action_type = cJSON_IsNumber(jatype) ? jatype->valueint : 0;
        strlcpy(cfg.target, jtarget->valuestring, sizeof(cfg.target));
        cfg.method = cJSON_IsNumber(jmethod) ? jmethod->valueint : 0;
        if (cJSON_IsString(jpayload)) {
            strlcpy(cfg.payload, jpayload->valuestring, sizeof(cfg.payload));
        }
        cfg.timeout_ms = cJSON_IsNumber(jtimeout) ? jtimeout->valueint : 5000;
        if (cfg.timeout_ms < 1000) cfg.timeout_ms = 1000;
        if (cfg.timeout_ms > 30000) cfg.timeout_ms = 30000;

        cfg.cooldown_ms = cJSON_IsNumber(jcooldown) ? jcooldown->valueint : 2000;
        if (cfg.cooldown_ms < 500) cfg.cooldown_ms = 500;
        if (cfg.cooldown_ms > 60000) cfg.cooldown_ms = 60000;

        cfg.no_cache = cJSON_IsBool(jnocache) ? cJSON_IsTrue(jnocache) : false;

        app_nvs_save_button(jid->valueint, &cfg);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    } else {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"ok\":false}", HTTPD_RESP_USE_STRLEN);
    }

    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t btn_get_handler(httpd_req_t *req) {
    if (!check_auth(req)) return send_auth_required(req);
    char param[8] = {0};
    if (httpd_req_get_url_query_str(req, param, sizeof(param)) != ESP_OK) {
        httpd_resp_send(req, "{}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    char val[4] = {0};
    httpd_query_key_value(param, "id", val, sizeof(val));
    int btn_id = atoi(val);

    button_config_t cfg = {0};
    cJSON *root = cJSON_CreateObject();

    if (app_nvs_get_button_config(btn_id, &cfg) == ESP_OK) {
        cJSON_AddStringToObject(root, "name", cfg.name);
        cJSON_AddNumberToObject(root, "atype", cfg.action_type);
        cJSON_AddStringToObject(root, "target", cfg.target);
        cJSON_AddNumberToObject(root, "method", cfg.method);
        cJSON_AddStringToObject(root, "payload", cfg.payload);
        cJSON_AddNumberToObject(root, "timeout", cfg.timeout_ms > 0 ? cfg.timeout_ms : 5000);
        cJSON_AddNumberToObject(root, "cooldown", cfg.cooldown_ms > 0 ? cfg.cooldown_ms : 2000);
        cJSON_AddBoolToObject(root, "nocache", cfg.no_cache);
    }

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t test_post_handler(httpd_req_t *req) {
    if (!check_auth(req)) return send_auth_required(req);
    int total = req->content_len;
    if (total <= 0 || total >= 2048) {
        httpd_resp_set_status(req, "413 Payload Too Large");
        httpd_resp_send(req, "{\"ok\":false,\"status\":-1}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    char buf[2048];
    int received = 0;
    while (received < total) {
        int ret = httpd_req_recv(req, buf + received, total - received);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) continue;
            return ESP_FAIL;
        }
        received += ret;
    }
    buf[received] = 0;

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send(req, "{\"ok\":false,\"status\":-1}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    cJSON *jatype = cJSON_GetObjectItem(root, "atype");
    cJSON *jtarget = cJSON_GetObjectItem(root, "target");
    cJSON *jmethod = cJSON_GetObjectItem(root, "method");
    cJSON *jpayload = cJSON_GetObjectItem(root, "payload");
    cJSON *jtimeout = cJSON_GetObjectItem(root, "timeout");
    cJSON *jnocache = cJSON_GetObjectItem(root, "nocache");

    cJSON *jid = cJSON_GetObjectItem(root, "id");
    int test_btn_id = cJSON_IsNumber(jid) ? jid->valueint : 0;
    if (test_btn_id >= 1 && test_btn_id <= 2) {
        app_btn_leds_on(test_btn_id);
    }

    button_config_t cfg = {0};
    cfg.action_type = cJSON_IsNumber(jatype) ? jatype->valueint : 0;
    if (cJSON_IsString(jtarget)) strlcpy(cfg.target, jtarget->valuestring, sizeof(cfg.target));
    cfg.method = cJSON_IsNumber(jmethod) ? jmethod->valueint : 0;
    if (cJSON_IsString(jpayload)) strlcpy(cfg.payload, jpayload->valuestring, sizeof(cfg.payload));
    cfg.timeout_ms = cJSON_IsNumber(jtimeout) ? jtimeout->valueint : 5000;
    cfg.no_cache = cJSON_IsBool(jnocache) ? cJSON_IsTrue(jnocache) : false;

    int status = -1;
    if (cfg.action_type == 1) {
        // MQTT Test
        mqtt_config_t mcfg;
        app_nvs_get_mqtt(&mcfg);
        status = app_mqtt_test_sync(&mcfg, &cfg);
    } else {
        // HTTP Test - Map target to url for http client
        strlcpy(cfg.target, jtarget->valuestring, sizeof(cfg.target)); 
        // Nota: app_http_test_sync espera que 'url' esté en cfg.url (que es el primer campo del struct legacy)
        // En nuestro nuevo struct, target es el 2do campo, pero app_http.c usa button_config_t antiguo? 
        // NO, app_http.c incluye app_nvs.h que YA HEMOS ACTUALIZADO.
        // Pero espera cfg->url, y nosotros renombramos a cfg->target.
        // Solución: En app_http.c hay que cambiar cfg->url por cfg->target.
        // O mejor: cfg->target es char[512], compatible.
        
        // HACK: app_http_test_sync se recompilará usando el nuevo header.
        // Necesitamos asegurar que app_http.c use 'target' en vez de 'url'.
        // Ver cambio en app_http.c
        status = app_http_test_sync(&cfg);
    }

    if (test_btn_id >= 1 && test_btn_id <= 2) {
        app_btn_leds_off(test_btn_id);
    }

    cJSON *resp = cJSON_CreateObject();
    bool success = (status >= 200 && status < 300);
    cJSON_AddBoolToObject(resp, "ok", success);
    cJSON_AddNumberToObject(resp, "status", status);
    if (cfg.action_type == 1) {
        if (status == -2) cJSON_AddStringToObject(resp, "msg", "MQTT no habilitado");
        else if (status == -1) cJSON_AddStringToObject(resp, "msg", "Error conexión broker");
        else if (status == 504) cJSON_AddStringToObject(resp, "msg", "Timeout publicación");
    }
    char *json = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    free(json);
    cJSON_Delete(resp);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t netinfo_get_handler(httpd_req_t *req) {
    if (!check_auth(req)) return send_auth_required(req);
    cJSON *info = app_wifi_get_netinfo();
    char *json = cJSON_PrintUnformatted(info);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    free(json);
    cJSON_Delete(info);
    return ESP_OK;
}

static esp_err_t admin_get_handler(httpd_req_t *req) {
    if (!check_auth(req)) return send_auth_required(req);
    admin_config_t admin;
    app_nvs_get_admin(&admin);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "user", admin.user);
    cJSON_AddNumberToObject(root, "reset_time", admin.reset_time_ms / 1000);
    cJSON_AddStringToObject(root, "ap_ssid", admin.ap_ssid);
    cJSON_AddStringToObject(root, "ap_pass", admin.ap_pass);
    cJSON_AddBoolToObject(root, "pure_client", admin.pure_client);
    cJSON_AddBoolToObject(root, "deep_sleep", admin.deep_sleep);
    cJSON_AddNumberToObject(root, "sta_max_retries", admin.sta_max_retries);
    cJSON_AddNumberToObject(root, "ap_channel", admin.ap_channel);
    cJSON_AddNumberToObject(root, "wakeup_timeout", admin.wakeup_timeout_s);
    cJSON_AddNumberToObject(root, "config_awake", admin.config_awake_s);
    cJSON_AddNumberToObject(root, "debounce", admin.debounce_ms);
    
    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t admin_post_handler(httpd_req_t *req) {
    if (!check_auth(req)) return send_auth_required(req);
    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = 0;

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send(req, "{\"ok\":false}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    cJSON *juser = cJSON_GetObjectItem(root, "user");
    cJSON *jpass = cJSON_GetObjectItem(root, "pass");
    cJSON *jreset = cJSON_GetObjectItem(root, "reset_time");
    cJSON *jap_ssid = cJSON_GetObjectItem(root, "ap_ssid");
    cJSON *jap_pass = cJSON_GetObjectItem(root, "ap_pass");
    cJSON *jpure = cJSON_GetObjectItem(root, "pure_client");
    cJSON *jdeep = cJSON_GetObjectItem(root, "deep_sleep");
    cJSON *jretries = cJSON_GetObjectItem(root, "sta_max_retries");
    cJSON *jchan = cJSON_GetObjectItem(root, "ap_channel");
    cJSON *jwktout = cJSON_GetObjectItem(root, "wakeup_timeout");
    cJSON *jcfgawake = cJSON_GetObjectItem(root, "config_awake");
    cJSON *jdebounce = cJSON_GetObjectItem(root, "debounce");

    admin_config_t updated;
    app_nvs_get_admin(&updated);

    if (cJSON_IsString(juser) && juser->valuestring[0]) strlcpy(updated.user, juser->valuestring, sizeof(updated.user));
    if (cJSON_IsString(jpass) && jpass->valuestring[0]) strlcpy(updated.pass, jpass->valuestring, sizeof(updated.pass));
    if (cJSON_IsNumber(jreset)) updated.reset_time_ms = jreset->valueint * 1000;
    if (cJSON_IsString(jap_ssid)) strlcpy(updated.ap_ssid, jap_ssid->valuestring, sizeof(updated.ap_ssid));
    if (cJSON_IsString(jap_pass)) strlcpy(updated.ap_pass, jap_pass->valuestring, sizeof(updated.ap_pass));
    if (cJSON_IsBool(jpure)) updated.pure_client = cJSON_IsTrue(jpure);
    if (cJSON_IsBool(jdeep)) updated.deep_sleep = cJSON_IsTrue(jdeep);
    if (cJSON_IsNumber(jretries)) updated.sta_max_retries = jretries->valueint;
    if (cJSON_IsNumber(jchan)) updated.ap_channel = jchan->valueint;
    if (cJSON_IsNumber(jwktout)) updated.wakeup_timeout_s = jwktout->valueint;
    if (cJSON_IsNumber(jcfgawake)) updated.config_awake_s = jcfgawake->valueint;
    if (cJSON_IsNumber(jdebounce)) updated.debounce_ms = jdebounce->valueint;

    if (updated.reset_time_ms < 3000) updated.reset_time_ms = 3000;
    if (updated.reset_time_ms > 60000) updated.reset_time_ms = 60000;
    if (updated.sta_max_retries < 1) updated.sta_max_retries = 1;
    if (updated.sta_max_retries > 20) updated.sta_max_retries = 20;
    if (updated.ap_channel < 1) updated.ap_channel = 1;
    if (updated.ap_channel > 13) updated.ap_channel = 13;
    if (updated.wakeup_timeout_s < 10) updated.wakeup_timeout_s = 10;
    if (updated.wakeup_timeout_s > 120) updated.wakeup_timeout_s = 120;
    if (updated.config_awake_s < 30) updated.config_awake_s = 30;
    if (updated.config_awake_s > 600) updated.config_awake_s = 600;
    if (updated.debounce_ms < 50) updated.debounce_ms = 50;
    if (updated.debounce_ms > 500) updated.debounce_ms = 500;

    app_nvs_save_admin(&updated);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);

    cJSON_Delete(root);

    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
    return ESP_OK;
}

static esp_err_t factory_reset_post_handler(httpd_req_t *req) {
    if (!check_auth(req)) return send_auth_required(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    vTaskDelay(pdMS_TO_TICKS(1000));
    app_nvs_clear_all();
    esp_restart();
    return ESP_OK;
}

static esp_err_t ota_post_handler(httpd_req_t *req) {
    if (!check_auth(req)) return send_auth_required(req);

    esp_ota_handle_t update_handle = 0;
    const esp_partition_t *update_partition = NULL;
    char buf[1024];
    int remaining = req->content_len;

    update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        ESP_LOGE(TAG, "OTA partition not found");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
    if (err != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, buf, (remaining > sizeof(buf)) ? sizeof(buf) : remaining);
        if (recv_len <= 0) {
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) continue;
            esp_ota_end(update_handle);
            return ESP_FAIL;
        }

        err = esp_ota_write(update_handle, buf, recv_len);
        if (err != ESP_OK) {
            esp_ota_end(update_handle);
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        remaining -= recv_len;
    }

    err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

void app_web_start(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 30; // Aumentado para handlers extra
    config.stack_size = 8192;
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t uri_root = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler };
        httpd_register_uri_handler(server, &uri_root);

        httpd_uri_t uri_verify = { .uri = "/api/verify", .method = HTTP_GET, .handler = verify_get_handler };
        httpd_register_uri_handler(server, &uri_verify);

        httpd_uri_t uri_scan = { .uri = "/api/scan", .method = HTTP_GET, .handler = scan_get_handler };
        httpd_register_uri_handler(server, &uri_scan);

        httpd_uri_t uri_wifi = { .uri = "/api/wifi", .method = HTTP_POST, .handler = wifi_post_handler };
        httpd_register_uri_handler(server, &uri_wifi);
        
        // MQTT Handlers
        httpd_uri_t uri_mqtt_get = { .uri = "/api/mqtt", .method = HTTP_GET, .handler = mqtt_get_handler };
        httpd_register_uri_handler(server, &uri_mqtt_get);
        
        httpd_uri_t uri_mqtt_post = { .uri = "/api/mqtt", .method = HTTP_POST, .handler = mqtt_post_handler };
        httpd_register_uri_handler(server, &uri_mqtt_post);

        httpd_uri_t uri_btn_post = { .uri = "/api/btn", .method = HTTP_POST, .handler = btn_post_handler };
        httpd_register_uri_handler(server, &uri_btn_post);

        httpd_uri_t uri_btn_get = { .uri = "/api/btn", .method = HTTP_GET, .handler = btn_get_handler };
        httpd_register_uri_handler(server, &uri_btn_get);

        httpd_uri_t uri_test = { .uri = "/api/test", .method = HTTP_POST, .handler = test_post_handler };
        httpd_register_uri_handler(server, &uri_test);

        httpd_uri_t uri_netinfo = { .uri = "/api/netinfo", .method = HTTP_GET, .handler = netinfo_get_handler };
        httpd_register_uri_handler(server, &uri_netinfo);

        httpd_uri_t uri_admin_get = { .uri = "/api/admin", .method = HTTP_GET, .handler = admin_get_handler };
        httpd_register_uri_handler(server, &uri_admin_get);

        httpd_uri_t uri_admin_post = { .uri = "/api/admin", .method = HTTP_POST, .handler = admin_post_handler };
        httpd_register_uri_handler(server, &uri_admin_post);

        httpd_uri_t uri_freset = { .uri = "/api/factory_reset", .method = HTTP_POST, .handler = factory_reset_post_handler };
        httpd_register_uri_handler(server, &uri_freset);

        httpd_uri_t uri_ota = { .uri = "/api/ota", .method = HTTP_POST, .handler = ota_post_handler };
        httpd_register_uri_handler(server, &uri_ota);

        const char *captive_uris[] = {
            "/generate_204", "/gen_204", "/hotspot-detect.html",
            "/connecttest.txt", "/redirect", "/canonical.html"
        };
        for (int i = 0; i < sizeof(captive_uris) / sizeof(captive_uris[0]); i++) {
            httpd_uri_t uri = { .uri = captive_uris[i], .method = HTTP_GET, .handler = captive_redirect_handler };
            httpd_register_uri_handler(server, &uri);
        }

        ESP_LOGI(TAG, "Web Server Started");
    }
}
