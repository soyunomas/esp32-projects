#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PORTAL_AUTH_USERNAME "admin"
#define PORTAL_AUTH_PASSWORD_MIN_LENGTH 5U
#define PORTAL_AUTH_PASSWORD_MAX_LENGTH 64U
#define PORTAL_AUTH_SESSION_TOKEN_HEX_LENGTH 64U

typedef enum {
    PORTAL_AUTH_LOGIN_OK = 0,
    PORTAL_AUTH_LOGIN_INVALID,
    PORTAL_AUTH_LOGIN_LOCKED,
    PORTAL_AUTH_LOGIN_ERROR,
} portal_auth_login_result_t;

esp_err_t portal_auth_init(void);
bool portal_auth_uses_default_password(void);
portal_auth_login_result_t portal_auth_login(const char *username,
                                             const char *password,
                                             int64_t now_ms,
                                             char token_hex[
                                                 PORTAL_AUTH_SESSION_TOKEN_HEX_LENGTH + 1U],
                                             uint32_t *retry_after_seconds);
bool portal_auth_session_valid(const char *token_hex, int64_t now_ms);
void portal_auth_logout(void);
esp_err_t portal_auth_change_password(const char *current_password,
                                      const char *new_password,
                                      int64_t now_ms);
esp_err_t portal_auth_erase(void);

#ifdef __cplusplus
}
#endif
