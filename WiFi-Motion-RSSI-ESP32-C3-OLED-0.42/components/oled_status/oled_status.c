#include "oled_status.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_log.h"

#define OLED_I2C_PORT I2C_NUM_0
#define OLED_I2C_ADDRESS 0x3CU
#define OLED_SDA_GPIO 5
#define OLED_SCL_GPIO 6
#define OLED_WIDTH 72U
#define OLED_HEIGHT 40U
#define OLED_PAGES (OLED_HEIGHT / 8U)
#define OLED_X_OFFSET 28U
#define OLED_TIMEOUT_MS 100

static const char *TAG = "oled_status";
static i2c_master_bus_handle_t oled_bus;
static i2c_master_dev_handle_t oled_device;
static uint8_t framebuffer[OLED_WIDTH * OLED_PAGES];
static bool initialized;

/*
 * Readable 5x7 uppercase font. Each byte is one column and each bit is one
 * pixel from top to bottom. Unsupported characters are rendered as blanks.
 */
static const uint8_t font_5x7[59][5] = {
    ['!' - ' '] = {0x00, 0x00, 0x5F, 0x00, 0x00},
    ['-' - ' '] = {0x08, 0x08, 0x08, 0x08, 0x08},
    ['.' - ' '] = {0x00, 0x60, 0x60, 0x00, 0x00},
    ['/' - ' '] = {0x20, 0x10, 0x08, 0x04, 0x02},
    ['0' - ' '] = {0x3E, 0x51, 0x49, 0x45, 0x3E},
    ['1' - ' '] = {0x00, 0x42, 0x7F, 0x40, 0x00},
    ['2' - ' '] = {0x42, 0x61, 0x51, 0x49, 0x46},
    ['3' - ' '] = {0x21, 0x41, 0x45, 0x4B, 0x31},
    ['4' - ' '] = {0x18, 0x14, 0x12, 0x7F, 0x10},
    ['5' - ' '] = {0x27, 0x45, 0x45, 0x45, 0x39},
    ['6' - ' '] = {0x3C, 0x4A, 0x49, 0x49, 0x30},
    ['7' - ' '] = {0x01, 0x71, 0x09, 0x05, 0x03},
    ['8' - ' '] = {0x36, 0x49, 0x49, 0x49, 0x36},
    ['9' - ' '] = {0x06, 0x49, 0x49, 0x29, 0x1E},
    [':' - ' '] = {0x00, 0x36, 0x36, 0x00, 0x00},
    ['?' - ' '] = {0x02, 0x01, 0x51, 0x09, 0x06},
    ['A' - ' '] = {0x7E, 0x11, 0x11, 0x11, 0x7E},
    ['B' - ' '] = {0x7F, 0x49, 0x49, 0x49, 0x36},
    ['C' - ' '] = {0x3E, 0x41, 0x41, 0x41, 0x22},
    ['D' - ' '] = {0x7F, 0x41, 0x41, 0x22, 0x1C},
    ['E' - ' '] = {0x7F, 0x49, 0x49, 0x49, 0x41},
    ['F' - ' '] = {0x7F, 0x09, 0x09, 0x09, 0x01},
    ['G' - ' '] = {0x3E, 0x41, 0x49, 0x49, 0x7A},
    ['H' - ' '] = {0x7F, 0x08, 0x08, 0x08, 0x7F},
    ['I' - ' '] = {0x00, 0x41, 0x7F, 0x41, 0x00},
    ['J' - ' '] = {0x20, 0x40, 0x41, 0x3F, 0x01},
    ['K' - ' '] = {0x7F, 0x08, 0x14, 0x22, 0x41},
    ['L' - ' '] = {0x7F, 0x40, 0x40, 0x40, 0x40},
    ['M' - ' '] = {0x7F, 0x02, 0x0C, 0x02, 0x7F},
    ['N' - ' '] = {0x7F, 0x04, 0x08, 0x10, 0x7F},
    ['O' - ' '] = {0x3E, 0x41, 0x41, 0x41, 0x3E},
    ['P' - ' '] = {0x7F, 0x09, 0x09, 0x09, 0x06},
    ['Q' - ' '] = {0x3E, 0x41, 0x51, 0x21, 0x5E},
    ['R' - ' '] = {0x7F, 0x09, 0x19, 0x29, 0x46},
    ['S' - ' '] = {0x46, 0x49, 0x49, 0x49, 0x31},
    ['T' - ' '] = {0x01, 0x01, 0x7F, 0x01, 0x01},
    ['U' - ' '] = {0x3F, 0x40, 0x40, 0x40, 0x3F},
    ['V' - ' '] = {0x1F, 0x20, 0x40, 0x20, 0x1F},
    ['W' - ' '] = {0x3F, 0x40, 0x38, 0x40, 0x3F},
    ['X' - ' '] = {0x63, 0x14, 0x08, 0x14, 0x63},
    ['Y' - ' '] = {0x07, 0x08, 0x70, 0x08, 0x07},
    ['Z' - ' '] = {0x61, 0x51, 0x49, 0x45, 0x43},
};

static esp_err_t transmit(const uint8_t *data, size_t length)
{
    return i2c_master_transmit(oled_device, data, length, OLED_TIMEOUT_MS);
}

static esp_err_t send_commands(const uint8_t *commands, size_t count)
{
    uint8_t packet[32] = {0};
    if (commands == NULL || count == 0U || count >= sizeof(packet)) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(&packet[1], commands, count);
    return transmit(packet, count + 1U);
}

static void set_pixel(unsigned x, unsigned y)
{
    if (x < OLED_WIDTH && y < OLED_HEIGHT) {
        framebuffer[x + (y / 8U) * OLED_WIDTH] |=
            (uint8_t)(1U << (y % 8U));
    }
}

static void draw_text(unsigned row, const char *text)
{
    if (text == NULL || row >= 2U) {
        return;
    }
    size_t length = strnlen(text, 6U);
    const unsigned text_width = length == 0U ? 0U :
                                (unsigned)(length * 12U - 2U);
    unsigned x = (OLED_WIDTH - text_width) / 2U;
    const unsigned y = 3U + row * 20U;
    while (*text != '\0' && x + 10U <= OLED_WIDTH) {
        unsigned char character = (unsigned char)*text++;
        if (character >= 'a' && character <= 'z') {
            character = (unsigned char)(character - 'a' + 'A');
        }
        if (character >= ' ' && character <= 'Z') {
            const uint8_t *glyph = font_5x7[character - ' '];
            for (unsigned glyph_x = 0U; glyph_x < 5U; glyph_x++) {
                for (unsigned glyph_y = 0U; glyph_y < 7U; glyph_y++) {
                    if ((glyph[glyph_x] & (1U << glyph_y)) != 0U) {
                        set_pixel(x + glyph_x * 2U, y + glyph_y * 2U);
                        set_pixel(x + glyph_x * 2U + 1U,
                                  y + glyph_y * 2U);
                        set_pixel(x + glyph_x * 2U,
                                  y + glyph_y * 2U + 1U);
                        set_pixel(x + glyph_x * 2U + 1U,
                                  y + glyph_y * 2U + 1U);
                    }
                }
            }
        }
        x += 12U;
    }
}

static void text_chunk(const char *text, size_t offset, char output[7])
{
    output[0] = '\0';
    if (text != NULL) {
        const size_t length = strnlen(text, 18U);
        if (offset < length) {
            const size_t count = length - offset < 6U ?
                                 length - offset : 6U;
            memcpy(output, text + offset, count);
            output[count] = '\0';
        }
    }
}

static void address_tail(const char *address, char output[7])
{
    const char *tail = address != NULL ? strrchr(address, '.') : NULL;
    if (tail != NULL) {
        snprintf(output, 7U, "IP %s", tail + 1);
    } else {
        snprintf(output, 7U, "NO IP");
    }
}

static void compact_score(float score, char output[5])
{
    if (score < 0.0F) {
        score = 0.0F;
    } else if (score > 0.999F) {
        score = 0.999F;
    }
    const unsigned scaled = (unsigned)(score * 1000.0F + 0.5F);
    output[0] = '.';
    output[1] = (char)('0' + (scaled / 100U) % 10U);
    output[2] = (char)('0' + (scaled / 10U) % 10U);
    output[3] = (char)('0' + scaled % 10U);
    output[4] = '\0';
}

static esp_err_t flush(void)
{
    uint8_t data_packet[OLED_WIDTH + 1U];
    data_packet[0] = 0x40U;
    for (unsigned page = 0U; page < OLED_PAGES; page++) {
        const uint8_t position[] = {
            (uint8_t)(0xB0U | page),
            (uint8_t)(0x10U | (OLED_X_OFFSET >> 4U)),
            (uint8_t)(OLED_X_OFFSET & 0x0FU),
        };
        esp_err_t error = send_commands(position, sizeof(position));
        if (error != ESP_OK) {
            return error;
        }
        memcpy(&data_packet[1],
               &framebuffer[page * OLED_WIDTH],
               OLED_WIDTH);
        error = transmit(data_packet, sizeof(data_packet));
        if (error != ESP_OK) {
            return error;
        }
    }
    return ESP_OK;
}

static void show_lines(const char *line0, const char *line1)
{
    if (!initialized) {
        return;
    }
    memset(framebuffer, 0, sizeof(framebuffer));
    draw_text(0U, line0);
    draw_text(1U, line1);
    const esp_err_t error = flush();
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "OLED update failed: %s", esp_err_to_name(error));
    }
}

esp_err_t oled_status_init(void)
{
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = OLED_I2C_PORT,
        .sda_io_num = OLED_SDA_GPIO,
        .scl_io_num = OLED_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7U,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t error = i2c_new_master_bus(&bus_config, &oled_bus);
    if (error != ESP_OK) {
        return error;
    }
    error = i2c_master_probe(oled_bus, OLED_I2C_ADDRESS, OLED_TIMEOUT_MS);
    if (error != ESP_OK) {
        ESP_LOGE(TAG,
                 "No OLED at 0x%02X on SDA=%d SCL=%d",
                 OLED_I2C_ADDRESS,
                 OLED_SDA_GPIO,
                 OLED_SCL_GPIO);
        return error;
    }
    const i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = OLED_I2C_ADDRESS,
        .scl_speed_hz = 400000U,
    };
    error = i2c_master_bus_add_device(oled_bus,
                                      &device_config,
                                      &oled_device);
    if (error != ESP_OK) {
        return error;
    }

    /* SSD1306 72x40 EastRising sequence used by U8g2. */
    const uint8_t init_commands[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x27, 0xD3, 0x00, 0xAD, 0x30,
        0x8D, 0x14, 0x40, 0xA6, 0xA4, 0x20, 0x00, 0xA1, 0xC8,
        0xDA, 0x12, 0x81, 0xAF, 0xD9, 0x22, 0xDB, 0x20, 0x2E,
        0xAF,
    };
    error = send_commands(init_commands, sizeof(init_commands));
    if (error != ESP_OK) {
        return error;
    }
    initialized = true;
    memset(framebuffer, 0, sizeof(framebuffer));
    error = flush();
    if (error == ESP_OK) {
        ESP_LOGI(TAG,
                 "OLED ready: SSD1306 72x40, address=0x%02X, SDA=%d, SCL=%d",
                 OLED_I2C_ADDRESS,
                 OLED_SDA_GPIO,
                 OLED_SCL_GPIO);
    }
    return error;
}

bool oled_status_available(void)
{
    return initialized;
}

void oled_status_show_boot(void)
{
    show_lines("WIFI", "MOTION");
}

void oled_status_show_network(const char *ip_address,
                              bool recovery_mode,
                              const char *network_name)
{
    char tail[7];
    (void)network_name;
    address_tail(ip_address, tail);
    if (recovery_mode) {
        show_lines("SETUP", tail);
        return;
    }
    show_lines("WIFI", tail);
}

void oled_status_show_sample(const char *ip_address,
                             bool sample_ok,
                             int rssi_dbm,
                             bool calibrated,
                             bool motion,
                             float rssi_score,
                             float csi_score)
{
    static unsigned page_counter;
    char line0[7];
    char line1[7];
    char compact_rssi[5];
    char compact_csi[5];
    compact_score(rssi_score, compact_rssi);
    compact_score(csi_score, compact_csi);
    const char *state = !sample_ok ? "ERROR" :
                        !calibrated ? "CALIB" :
                        motion ? "MOTION" : "IDLE";
    const unsigned page = (page_counter++ / 4U) % 4U;
    if (page == 0U) {
        snprintf(line0, sizeof(line0), "%s", state);
        snprintf(line1, sizeof(line1), "R %d", rssi_dbm);
    } else if (page == 1U) {
        text_chunk(ip_address != NULL ? ip_address : "NO IP", 0U, line0);
        text_chunk(ip_address != NULL ? ip_address : "NO IP", 6U, line1);
    } else if (page == 2U) {
        snprintf(line0, sizeof(line0), "IP END");
        text_chunk(ip_address != NULL ? ip_address : "NO IP", 12U, line1);
        if (line1[0] == '\0') {
            snprintf(line1, sizeof(line1), "DONE");
        }
    } else {
        snprintf(line0, sizeof(line0), "R %.4s", compact_rssi);
        snprintf(line1, sizeof(line1), "C %.4s", compact_csi);
    }
    show_lines(line0, line1);
}
