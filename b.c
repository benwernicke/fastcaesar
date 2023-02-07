#include "caesar.h"

#include <ctype.h>

static uint8_t offset_from_key(int32_t key)
{
    key %= 26;
    return key < 0 ? 26 + key : key;
}

static uint8_t flip(uint8_t c, uint8_t o)
{
    if (!isalpha(c)) return c;
    return (((c & 0x1F) + o - 1) % 26 + 1) | (c & 0xE0);
}

void caesar(FILE* fp, int32_t key)
{
    uint8_t o = offset_from_key(key);
    char c;
    while ((c = getc(fp)) != EOF) {
        putc(flip(c, o), stdout);
    }
    putc('\n', stdout);
}
