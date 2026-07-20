#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "telegram_notifier.h"

int main(void)
{
    assert(telegram_notifier_token_valid("123456789:AbCdEf_123456-xyz"));
    assert(!telegram_notifier_token_valid(NULL));
    assert(!telegram_notifier_token_valid("123456789"));
    assert(!telegram_notifier_token_valid("bot123:AbCdEf_123456"));
    assert(!telegram_notifier_token_valid("123456789:bad/path"));
    assert(!telegram_notifier_token_valid("123456789:bad?query"));

    assert(telegram_notifier_chat_id_valid("123456789"));
    assert(telegram_notifier_chat_id_valid("-1001234567890"));
    assert(!telegram_notifier_chat_id_valid(NULL));
    assert(!telegram_notifier_chat_id_valid(""));
    assert(!telegram_notifier_chat_id_valid("-"));
    assert(!telegram_notifier_chat_id_valid("12 34"));
    assert(!telegram_notifier_chat_id_valid("@channel"));

    char oversized[TELEGRAM_CHAT_ID_MAX_LENGTH + 2U];
    memset(oversized, '1', sizeof(oversized) - 1U);
    oversized[sizeof(oversized) - 1U] = '\0';
    assert(!telegram_notifier_chat_id_valid(oversized));

    puts("telegram validation tests passed");
    return 0;
}
