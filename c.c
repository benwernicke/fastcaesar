#include "caesar.h"

#include <ctype.h>
#include <stdbool.h>

static char buf[1025] = { 0 };
static uint32_t buf_len = 0;

static bool get_buf(FILE* f) { return (buf_len = fread(buf, 1, 1024, f)) > 0; }

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
    while (get_buf(fp)) {
        uint32_t i = 0;
        for (; i < buf_len; ++i) {
            buf[i] = flip(buf[i], o);
        }
        buf[i] = 0;
        fputs(buf, stdout);
    }
    putc('\n', stdout);
}
