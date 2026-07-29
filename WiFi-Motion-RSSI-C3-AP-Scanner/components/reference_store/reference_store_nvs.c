#include "reference_store.h"

#include <string.h>

#include "esp_err.h"
#include "nvs.h"

#define REFERENCE_STORE_NAMESPACE "probe_refs"
#define REFERENCE_STORE_KEY "selection"

reference_store_status_t reference_store_load(reference_store_blob_t *blob)
{
    if (blob == NULL) {
        return REFERENCE_STORE_INVALID;
    }
    nvs_handle_t handle;
    esp_err_t error =
        nvs_open(REFERENCE_STORE_NAMESPACE, NVS_READONLY, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        return REFERENCE_STORE_NOT_FOUND;
    }
    if (error != ESP_OK) {
        return REFERENCE_STORE_IO_ERROR;
    }
    size_t size = sizeof(*blob);
    memset(blob, 0, sizeof(*blob));
    error = nvs_get_blob(handle, REFERENCE_STORE_KEY, blob, &size);
    nvs_close(handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        return REFERENCE_STORE_NOT_FOUND;
    }
    if (error != ESP_OK || size != sizeof(*blob)) {
        return error == ESP_OK ? REFERENCE_STORE_INVALID
                               : REFERENCE_STORE_IO_ERROR;
    }
    return reference_store_validate(blob) ? REFERENCE_STORE_OK
                                          : REFERENCE_STORE_INVALID;
}

reference_store_status_t reference_store_save(reference_store_blob_t *blob)
{
    if (blob == NULL) {
        return REFERENCE_STORE_INVALID;
    }
    reference_store_finalize(blob);
    if (!reference_store_validate(blob)) {
        return REFERENCE_STORE_INVALID;
    }
    nvs_handle_t handle;
    esp_err_t error =
        nvs_open(REFERENCE_STORE_NAMESPACE, NVS_READWRITE, &handle);
    if (error != ESP_OK) {
        return REFERENCE_STORE_IO_ERROR;
    }
    error = nvs_set_blob(handle, REFERENCE_STORE_KEY, blob, sizeof(*blob));
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    nvs_close(handle);
    return error == ESP_OK ? REFERENCE_STORE_OK
                           : REFERENCE_STORE_IO_ERROR;
}

reference_store_status_t reference_store_erase(void)
{
    nvs_handle_t handle;
    esp_err_t error =
        nvs_open(REFERENCE_STORE_NAMESPACE, NVS_READWRITE, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        return REFERENCE_STORE_OK;
    }
    if (error != ESP_OK) {
        return REFERENCE_STORE_IO_ERROR;
    }
    error = nvs_erase_key(handle, REFERENCE_STORE_KEY);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        error = ESP_OK;
    }
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    nvs_close(handle);
    return error == ESP_OK ? REFERENCE_STORE_OK
                           : REFERENCE_STORE_IO_ERROR;
}
