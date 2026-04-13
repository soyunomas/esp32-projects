#pragma once

#include "esp_err.h"
#include <stddef.h>

#define LOG_BUFFER_SIZE 4096

esp_err_t log_buffer_init(void);
size_t log_buffer_read(char *dst, size_t dst_size);
