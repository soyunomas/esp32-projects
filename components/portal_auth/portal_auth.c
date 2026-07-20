#include "portal_auth.h"

#include <string.h>

#include "esp_random.h"
#include "mbedtls/constant_time.h"
#include "mbedtls/md.h"
#include "nvs.h"

#define AUTH_NAMESPACE "portal_auth"
#define AUTH_BLOB_KEY "credential_v1"
#define AUTH_MAGIC 0x41555448UL
#define AUTH_VERSION 1U
#define AUTH_SALT_LENGTH 16U
#define AUTH_HASH_LENGTH 32U
#define AUTH_PBKDF2_ITERATIONS 20000U
#define AUTH_SESSION_IDLE_MS (30LL * 60LL * 1000LL)
#define AUTH_FAILURE_LIMIT 5U
#define AUTH_LOCKOUT_MS 30000LL

/* ESP-IDF still provides this PKCS#5 API when CONFIG_MBEDTLS_PKCS5_C is set,
 * although current Mbed TLS releases moved its declaration to a private
 * compatibility header. Keeping the exact public ABI here avoids depending
 * on an IDF-private include path. */
int mbedtls_pkcs5_pbkdf2_hmac_ext(mbedtls_md_type_t md_type,
                                  const unsigned char *password,
                                  size_t password_length,
                                  const unsigned char *salt,
                                  size_t salt_length,
                                  unsigned int iterations,
                                  uint32_t output_length,
                                  unsigned char *output);

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t iterations;
    uint8_t salt[AUTH_SALT_LENGTH];
    uint8_t hash[AUTH_HASH_LENGTH];
} stored_credential_t;

static stored_credential_t credential;
static uint8_t session_token[PORTAL_AUTH_SESSION_TOKEN_HEX_LENGTH / 2U];
static bool session_active;
static int64_t session_last_seen_ms;
static uint8_t failed_attempts;
static int64_t locked_until_ms;

static bool password_length_valid(const char *password)
{
    if (password == NULL) {
        return false;
    }
    const size_t length = strnlen(password,
                                  PORTAL_AUTH_PASSWORD_MAX_LENGTH + 1U);
    return length >= PORTAL_AUTH_PASSWORD_MIN_LENGTH &&
           length <= PORTAL_AUTH_PASSWORD_MAX_LENGTH;
}

static esp_err_t derive_password(const char *password,
                                 const uint8_t salt[AUTH_SALT_LENGTH],
                                 uint32_t iterations,
                                 uint8_t output[AUTH_HASH_LENGTH])
{
    if (!password_length_valid(password) || iterations == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    const int result = mbedtls_pkcs5_pbkdf2_hmac_ext(
        MBEDTLS_MD_SHA256,
        (const unsigned char *)password,
        strlen(password),
        salt,
        AUTH_SALT_LENGTH,
        iterations,
        AUTH_HASH_LENGTH,
        output);
    return result == 0 ? ESP_OK : ESP_FAIL;
}

static esp_err_t save_credential(const char *password)
{
    stored_credential_t candidate = {
        .magic = AUTH_MAGIC,
        .version = AUTH_VERSION,
        .size = sizeof(candidate),
        .iterations = AUTH_PBKDF2_ITERATIONS,
    };
    esp_fill_random(candidate.salt, sizeof(candidate.salt));
    esp_err_t error = derive_password(password,
                                      candidate.salt,
                                      candidate.iterations,
                                      candidate.hash);
    if (error != ESP_OK) {
        return error;
    }

    nvs_handle_t handle;
    error = nvs_open(AUTH_NAMESPACE, NVS_READWRITE, &handle);
    if (error != ESP_OK) {
        return error;
    }
    error = nvs_set_blob(handle, AUTH_BLOB_KEY, &candidate, sizeof(candidate));
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    nvs_close(handle);
    if (error == ESP_OK) {
        credential = candidate;
    }
    return error;
}

static bool credential_valid(const stored_credential_t *stored)
{
    return stored->magic == AUTH_MAGIC &&
           stored->version == AUTH_VERSION &&
           stored->size == sizeof(*stored) &&
           stored->iterations >= 10000U && stored->iterations <= 1000000U;
}

esp_err_t portal_auth_init(void)
{
    memset(&credential, 0, sizeof(credential));
    memset(session_token, 0, sizeof(session_token));
    session_active = false;
    session_last_seen_ms = 0;
    failed_attempts = 0U;
    locked_until_ms = 0;

    nvs_handle_t handle;
    esp_err_t error = nvs_open(AUTH_NAMESPACE, NVS_READONLY, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        return save_credential("admin");
    }
    if (error != ESP_OK) {
        return error;
    }
    size_t size = sizeof(credential);
    error = nvs_get_blob(handle, AUTH_BLOB_KEY, &credential, &size);
    nvs_close(handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        return save_credential("admin");
    }
    if (error != ESP_OK) {
        return error;
    }
    return size == sizeof(credential) && credential_valid(&credential)
        ? ESP_OK
        : ESP_ERR_INVALID_STATE;
}

static bool password_matches(const char *password)
{
    uint8_t candidate[AUTH_HASH_LENGTH];
    if (derive_password(password,
                        credential.salt,
                        credential.iterations,
                        candidate) != ESP_OK) {
        return false;
    }
    const bool matches = mbedtls_ct_memcmp(candidate,
                                           credential.hash,
                                           sizeof(candidate)) == 0;
    memset(candidate, 0, sizeof(candidate));
    return matches;
}

bool portal_auth_uses_default_password(void)
{
    return password_matches("admin");
}

static void token_to_hex(const uint8_t *token, char *hex)
{
    static const char digits[] = "0123456789abcdef";
    for (size_t index = 0U; index < sizeof(session_token); ++index) {
        hex[index * 2U] = digits[token[index] >> 4U];
        hex[index * 2U + 1U] = digits[token[index] & 0x0fU];
    }
    hex[PORTAL_AUTH_SESSION_TOKEN_HEX_LENGTH] = '\0';
}

static bool hex_to_token(const char *hex, uint8_t *token)
{
    if (hex == NULL ||
        strnlen(hex, PORTAL_AUTH_SESSION_TOKEN_HEX_LENGTH + 1U) !=
            PORTAL_AUTH_SESSION_TOKEN_HEX_LENGTH) {
        return false;
    }
    for (size_t index = 0U; index < sizeof(session_token); ++index) {
        unsigned value = 0U;
        for (size_t nibble = 0U; nibble < 2U; ++nibble) {
            const char character = hex[index * 2U + nibble];
            unsigned digit;
            if (character >= '0' && character <= '9') {
                digit = (unsigned)(character - '0');
            } else if (character >= 'a' && character <= 'f') {
                digit = (unsigned)(character - 'a') + 10U;
            } else {
                return false;
            }
            value = value * 16U + digit;
        }
        token[index] = (uint8_t)value;
    }
    return true;
}

portal_auth_login_result_t portal_auth_login(
    const char *username,
    const char *password,
    int64_t now_ms,
    char token_hex[PORTAL_AUTH_SESSION_TOKEN_HEX_LENGTH + 1U],
    uint32_t *retry_after_seconds)
{
    if (retry_after_seconds != NULL) {
        *retry_after_seconds = 0U;
    }
    if (now_ms < locked_until_ms) {
        if (retry_after_seconds != NULL) {
            *retry_after_seconds =
                (uint32_t)((locked_until_ms - now_ms + 999LL) / 1000LL);
        }
        return PORTAL_AUTH_LOGIN_LOCKED;
    }
    const bool valid_user = username != NULL &&
                            strcmp(username, PORTAL_AUTH_USERNAME) == 0;
    const bool valid_password = password_matches(password);
    if (!valid_user || !valid_password) {
        if (++failed_attempts >= AUTH_FAILURE_LIMIT) {
            failed_attempts = 0U;
            locked_until_ms = now_ms + AUTH_LOCKOUT_MS;
            if (retry_after_seconds != NULL) {
                *retry_after_seconds = AUTH_LOCKOUT_MS / 1000U;
            }
            return PORTAL_AUTH_LOGIN_LOCKED;
        }
        return PORTAL_AUTH_LOGIN_INVALID;
    }

    failed_attempts = 0U;
    locked_until_ms = 0;
    esp_fill_random(session_token, sizeof(session_token));
    session_active = true;
    session_last_seen_ms = now_ms;
    token_to_hex(session_token, token_hex);
    return PORTAL_AUTH_LOGIN_OK;
}

bool portal_auth_session_valid(const char *token_hex, int64_t now_ms)
{
    uint8_t candidate[sizeof(session_token)];
    if (!session_active || now_ms < session_last_seen_ms ||
        now_ms - session_last_seen_ms > AUTH_SESSION_IDLE_MS ||
        !hex_to_token(token_hex, candidate)) {
        session_active = false;
        return false;
    }
    const bool matches = mbedtls_ct_memcmp(candidate,
                                           session_token,
                                           sizeof(candidate)) == 0;
    memset(candidate, 0, sizeof(candidate));
    if (matches) {
        session_last_seen_ms = now_ms;
    }
    return matches;
}

void portal_auth_logout(void)
{
    memset(session_token, 0, sizeof(session_token));
    session_active = false;
    session_last_seen_ms = 0;
}

esp_err_t portal_auth_change_password(const char *current_password,
                                      const char *new_password,
                                      int64_t now_ms)
{
    (void)now_ms;
    if (!password_length_valid(new_password)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!password_matches(current_password)) {
        return ESP_ERR_INVALID_CRC;
    }
    const esp_err_t error = save_credential(new_password);
    if (error == ESP_OK) {
        portal_auth_logout();
    }
    return error;
}

esp_err_t portal_auth_erase(void)
{
    nvs_handle_t handle;
    esp_err_t error = nvs_open(AUTH_NAMESPACE, NVS_READWRITE, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        portal_auth_logout();
        return ESP_OK;
    }
    if (error != ESP_OK) {
        return error;
    }
    error = nvs_erase_key(handle, AUTH_BLOB_KEY);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        error = ESP_OK;
    }
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    nvs_close(handle);
    if (error == ESP_OK) {
        portal_auth_logout();
        memset(&credential, 0, sizeof(credential));
    }
    return error;
}
