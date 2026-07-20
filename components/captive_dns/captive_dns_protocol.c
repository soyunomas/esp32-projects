#include "captive_dns.h"

#include <string.h>

#define DNS_HEADER_LENGTH 12U
#define DNS_ANSWER_LENGTH 16U

static uint16_t read_u16(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8U) | bytes[1]);
}

size_t captive_dns_build_a_response(const uint8_t *query,
                                    size_t query_length,
                                    const uint8_t ipv4[4],
                                    uint8_t *response,
                                    size_t response_capacity)
{
    if (query == NULL || ipv4 == NULL || response == NULL ||
        query_length < DNS_HEADER_LENGTH ||
        (query[2] & 0x80U) != 0U || read_u16(&query[4]) == 0U) {
        return 0U;
    }

    size_t cursor = DNS_HEADER_LENGTH;
    while (cursor < query_length) {
        const uint8_t label_length = query[cursor++];
        if (label_length == 0U) {
            break;
        }
        if (label_length > 63U || cursor + label_length > query_length) {
            return 0U;
        }
        cursor += label_length;
    }
    if (cursor + 4U > query_length) {
        return 0U;
    }
    const size_t question_end = cursor + 4U;
    const size_t response_length = question_end + DNS_ANSWER_LENGTH;
    if (response_length > response_capacity) {
        return 0U;
    }

    memcpy(response, query, question_end);
    response[2] = 0x81U;
    response[3] = 0x80U;
    response[4] = 0x00U;
    response[5] = 0x01U;
    response[6] = 0x00U;
    response[7] = 0x01U;
    response[8] = 0x00U;
    response[9] = 0x00U;
    response[10] = 0x00U;
    response[11] = 0x00U;

    uint8_t *answer = &response[question_end];
    const uint8_t fixed_answer[DNS_ANSWER_LENGTH] = {
        0xc0U, 0x0cU,
        0x00U, 0x01U,
        0x00U, 0x01U,
        0x00U, 0x00U, 0x00U, 0x3cU,
        0x00U, 0x04U,
        ipv4[0], ipv4[1], ipv4[2], ipv4[3],
    };
    memcpy(answer, fixed_answer, sizeof(fixed_answer));
    return response_length;
}
