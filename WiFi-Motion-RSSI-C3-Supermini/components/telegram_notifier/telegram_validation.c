#include "telegram_notifier.h"

#include <ctype.h>
#include <string.h>

bool telegram_notifier_token_valid(const char *token)
{
    if (token == NULL) {
        return false;
    }
    const size_t length = strnlen(token, TELEGRAM_TOKEN_MAX_LENGTH + 1U);
    if (length < 12U || length > TELEGRAM_TOKEN_MAX_LENGTH) {
        return false;
    }
    bool colon_seen = false;
    size_t suffix_length = 0U;
    for (size_t index = 0U; index < length; ++index) {
        const unsigned char character = (unsigned char)token[index];
        if (!colon_seen && character == ':') {
            if (index == 0U) {
                return false;
            }
            colon_seen = true;
            continue;
        }
        if (!colon_seen) {
            if (!isdigit(character)) {
                return false;
            }
        } else {
            if (!(isalnum(character) || character == '_' || character == '-')) {
                return false;
            }
            ++suffix_length;
        }
    }
    return colon_seen && suffix_length >= 8U;
}

bool telegram_notifier_chat_id_valid(const char *chat_id)
{
    if (chat_id == NULL) {
        return false;
    }
    const size_t length = strnlen(chat_id, TELEGRAM_CHAT_ID_MAX_LENGTH + 1U);
    if (length == 0U || length > TELEGRAM_CHAT_ID_MAX_LENGTH) {
        return false;
    }
    size_t index = chat_id[0] == '-' ? 1U : 0U;
    if (index == length) {
        return false;
    }
    for (; index < length; ++index) {
        if (!isdigit((unsigned char)chat_id[index])) {
            return false;
        }
    }
    return true;
}
