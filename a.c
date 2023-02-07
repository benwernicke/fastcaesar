#include "caesar.h"

#include <ctype.h>

static uint8_t offset_from_key(int32_t key)
{
    key %= 26;
    return key < 0 ? 26 + key : key;
}

static uint8_t flip(uint8_t c, uint8_t o)
{
    if (isupper(c)) {
        return (c - 'A' + o) % 26 + 'A';
    } else if (islower(c)) {
        return (c - 'a' + o) % 26 + 'a';
    } else {
        return c;
    }
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
