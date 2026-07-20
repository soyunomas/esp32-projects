#include "captive_dns.h"

#include <stdio.h>
#include <string.h>

#define ASSERT_TRUE(condition) do { if (!(condition)) return 1; } while (0)

int main(void)
{
    const uint8_t query[] = {
        0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x07, 'e', 'x', 'a', 'm', 'p', 'l', 'e',
        0x03, 'c', 'o', 'm', 0x00,
        0x00, 0x01, 0x00, 0x01,
    };
    const uint8_t ip[] = {192, 168, 4, 1};
    uint8_t response[128] = {0};
    const size_t length = captive_dns_build_a_response(
        query, sizeof(query), ip, response, sizeof(response));
    ASSERT_TRUE(length == sizeof(query) + 16U);
    ASSERT_TRUE(response[0] == 0x12U && response[1] == 0x34U);
    ASSERT_TRUE(response[2] == 0x81U && response[3] == 0x80U);
    ASSERT_TRUE(response[6] == 0x00U && response[7] == 0x01U);
    ASSERT_TRUE(memcmp(&response[length - 4U], ip, sizeof(ip)) == 0);

    ASSERT_TRUE(captive_dns_build_a_response(
        query, 11U, ip, response, sizeof(response)) == 0U);
    uint8_t malformed[sizeof(query)];
    memcpy(malformed, query, sizeof(query));
    malformed[12] = 64U;
    ASSERT_TRUE(captive_dns_build_a_response(
        malformed, sizeof(malformed), ip, response, sizeof(response)) == 0U);
    ASSERT_TRUE(captive_dns_build_a_response(
        query, sizeof(query), ip, response, 20U) == 0U);

    puts("captive DNS tests passed");
    return 0;
}
