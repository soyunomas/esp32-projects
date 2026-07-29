#ifndef CAPTIVE_DNS_H
#define CAPTIVE_DNS_H

#include "esp_err.h"
#include "esp_netif.h"

esp_err_t captive_dns_start(esp_netif_t *ap_netif);

#endif
