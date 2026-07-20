#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mbedtls/md.h"
#include "nvs.h"
#include "portal_auth.h"

#define MOCK_BLOB_MAX 128U

static uint8_t stored_blob[MOCK_BLOB_MAX];
static size_t stored_blob_size;
static bool stored_blob_present;
static uint8_t random_counter = 1U;

void esp_fill_random(void *buffer, size_t length)
{
    uint8_t *bytes = buffer;
    for (size_t index = 0U; index < length; ++index) {
        bytes[index] = random_counter++;
    }
}

int mbedtls_ct_memcmp(const void *left, const void *right, size_t length)
{
    const uint8_t *left_bytes = left;
    const uint8_t *right_bytes = right;
    uint8_t difference = 0U;
    for (size_t index = 0U; index < length; ++index) {
        difference |= left_bytes[index] ^ right_bytes[index];
    }
    return difference;
}

int mbedtls_pkcs5_pbkdf2_hmac_ext(mbedtls_md_type_t md_type,
                                  const unsigned char *password,
                                  size_t password_length,
                                  const unsigned char *salt,
                                  size_t salt_length,
                                  unsigned int iterations,
                                  uint32_t output_length,
                                  unsigned char *output)
{
    assert(md_type == MBEDTLS_MD_SHA256);
    uint32_t state = iterations;
    for (size_t index = 0U; index < password_length; ++index) {
        state = state * 33U + password[index];
    }
    for (size_t index = 0U; index < salt_length; ++index) {
        state = state * 33U + salt[index];
    }
    for (uint32_t index = 0U; index < output_length; ++index) {
        state = state * 1103515245U + 12345U;
        output[index] = (uint8_t)(state >> 16U);
    }
    return 0;
}

esp_err_t nvs_open(const char *namespace_name,
                   int open_mode,
                   nvs_handle_t *handle)
{
    assert(strcmp(namespace_name, "portal_auth") == 0);
    if (open_mode == NVS_READONLY && !stored_blob_present) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    *handle = 1;
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle)
{
    assert(handle == 1);
}

esp_err_t nvs_get_blob(nvs_handle_t handle,
                       const char *key,
                       void *value,
                       size_t *length)
{
    assert(handle == 1);
    assert(strcmp(key, "credential_v1") == 0);
    if (!stored_blob_present) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    assert(*length >= stored_blob_size);
    memcpy(value, stored_blob, stored_blob_size);
    *length = stored_blob_size;
    return ESP_OK;
}

esp_err_t nvs_set_blob(nvs_handle_t handle,
                       const char *key,
                       const void *value,
                       size_t length)
{
    assert(handle == 1);
    assert(strcmp(key, "credential_v1") == 0);
    assert(length <= sizeof(stored_blob));
    memcpy(stored_blob, value, length);
    stored_blob_size = length;
    stored_blob_present = true;
    return ESP_OK;
}

esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key)
{
    assert(handle == 1);
    assert(strcmp(key, "credential_v1") == 0);
    if (!stored_blob_present) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    stored_blob_present = false;
    stored_blob_size = 0U;
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    assert(handle == 1);
    return ESP_OK;
}

static void test_default_login_lockout_and_expiry(void)
{
    assert(portal_auth_init() == ESP_OK);
    assert(portal_auth_uses_default_password());

    char token[PORTAL_AUTH_SESSION_TOKEN_HEX_LENGTH + 1U];
    uint32_t retry_after = 0U;
    for (int attempt = 0; attempt < 4; ++attempt) {
        assert(portal_auth_login("admin", "wrong", 1000,
                                 token, &retry_after) ==
               PORTAL_AUTH_LOGIN_INVALID);
    }
    assert(portal_auth_login("admin", "wrong", 1000,
                             token, &retry_after) == PORTAL_AUTH_LOGIN_LOCKED);
    assert(retry_after == 30U);
    assert(portal_auth_login("admin", "admin", 30000,
                             token, &retry_after) == PORTAL_AUTH_LOGIN_LOCKED);
    assert(portal_auth_login("admin", "admin", 31001,
                             token, &retry_after) == PORTAL_AUTH_LOGIN_OK);
    assert(portal_auth_session_valid(token, 31001));

    char altered[sizeof(token)];
    memcpy(altered, token, sizeof(altered));
    altered[0] = altered[0] == '0' ? '1' : '0';
    assert(!portal_auth_session_valid(altered, 31002));
    assert(portal_auth_session_valid(token, 31001 + 30LL * 60LL * 1000LL));
    assert(!portal_auth_session_valid(token,
                                      31002 + 2LL * 30LL * 60LL * 1000LL));
}

static void test_password_change_persistence_and_session_invalidation(void)
{
    char token[PORTAL_AUTH_SESSION_TOKEN_HEX_LENGTH + 1U];
    uint32_t retry_after = 0U;
    assert(portal_auth_login("admin", "admin", 2000000,
                             token, &retry_after) == PORTAL_AUTH_LOGIN_OK);
    assert(portal_auth_change_password("wrong", "new-password", 2000001) ==
           ESP_ERR_INVALID_CRC);
    assert(portal_auth_change_password("admin", "tiny", 2000001) ==
           ESP_ERR_INVALID_ARG);
    assert(portal_auth_change_password("admin", "new-password", 2000001) ==
           ESP_OK);
    assert(!portal_auth_session_valid(token, 2000002));
    assert(!portal_auth_uses_default_password());
    assert(portal_auth_login("admin", "admin", 2000003,
                             token, &retry_after) == PORTAL_AUTH_LOGIN_INVALID);
    assert(portal_auth_login("admin", "new-password", 2000004,
                             token, &retry_after) == PORTAL_AUTH_LOGIN_OK);

    assert(portal_auth_init() == ESP_OK);
    assert(portal_auth_login("admin", "new-password", 2000005,
                             token, &retry_after) == PORTAL_AUTH_LOGIN_OK);
    assert(portal_auth_change_password("new-password", "admin", 2000006) ==
           ESP_OK);
    assert(portal_auth_uses_default_password());
}

int main(void)
{
    test_default_login_lockout_and_expiry();
    test_password_change_persistence_and_session_invalidation();
    puts("portal_auth tests passed");
    return 0;
}
