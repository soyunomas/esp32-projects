#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_netif.h"

#ifdef __cplusplus
extern "C" {
#endif

size_t captive_dns_build_a_response(const uint8_t *query,
                                    size_t query_length,
                                    const uint8_t ipv4[4],
                                    uint8_t *response,
                                    size_t response_capacity);

esp_err_t captive_dns_start(esp_netif_t *ap_netif);
void captive_dns_stop(void);

#ifdef __cplusplus
}
#endif
