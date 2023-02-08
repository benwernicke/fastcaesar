#include "caesar.h"

#include <ctype.h>
#include <stdbool.h>
#include <immintrin.h>

static _Alignas(32) char buf[BUF_SIZE + 1] = { 0 };
static uint32_t buf_len = 0;

static bool get_buf(FILE* f) { return (buf_len = fread(buf, 1, BUF_SIZE, f)) > 0; }

static uint8_t offset_from_key(int32_t key)
{
    key %= 26;
    return key < 0 ? 26 + key : key;
}

static void flip_32(uint8_t* cptr, uint8_t o)
{
    __m256i orig = _mm256_load_si256((__m256i*)cptr);
    
    __m256i prefix = _mm256_and_si256(orig, _mm256_set1_epi8((uint8_t)0xE0));
    __m256i c      = _mm256_and_si256(orig, _mm256_set1_epi8((uint8_t)0x1F));

    __m256i alpha_mask;
    {
        __m256i lower = _mm256_cmpeq_epi8(prefix, _mm256_set1_epi8(0x60));
        __m256i upper = _mm256_cmpeq_epi8(prefix, _mm256_set1_epi8(0x40));
        alpha_mask = _mm256_or_si256(lower, upper);

        lower = _mm256_cmpgt_epi8(c, _mm256_set1_epi8(0));
        upper = _mm256_cmpgt_epi8(c, _mm256_set1_epi8(26));
        upper = _mm256_xor_si256(upper, _mm256_set1_epi8((uint8_t)0xFF));

        upper = _mm256_and_si256(upper, lower);
        alpha_mask = _mm256_and_si256(alpha_mask, upper);
    }

    {
        __m256i not_alpha_mask = _mm256_xor_si256(alpha_mask, _mm256_set1_epi8((uint8_t)0xFF));
        orig = _mm256_and_si256(orig, not_alpha_mask);
    }

    c = _mm256_add_epi8(c, _mm256_set1_epi8(o));
    __m256i m = _mm256_cmpgt_epi8(c, _mm256_set1_epi8(26));
    m = _mm256_and_si256(m, _mm256_set1_epi8(26));
    c = _mm256_sub_epi8(c, m);
    c = _mm256_or_si256(c, prefix);
    c = _mm256_and_si256(c, alpha_mask);
    orig = _mm256_or_si256(c, orig);
    _mm256_store_si256((__m256i*)cptr, orig);
}

static void flip_16(uint8_t* cptr, uint8_t o)
{
    __m128i c = _mm_load_si128((__m128i*)cptr);

    // seperate char prefix and identifier
    __m128i prefix = _mm_and_si128(c, _mm_set1_epi8((uint8_t)0xE0));
    c = _mm_and_si128(c, _mm_set1_epi8((uint8_t)0x1F));

    __m128i alpha_mask = _mm_cmplt_epi8(c, _mm_set1_epi8(27));
    {
        __m128i lower = _mm_cmpeq_epi8(prefix, _mm_set1_epi8(0x60));
        __m128i upper = _mm_cmpeq_epi8(prefix, _mm_set1_epi8(0x40));
        __m128i t = _mm_or_si128(lower, upper);
        alpha_mask = _mm_and_si128(alpha_mask, t);
    }

    // caesar chiffre
    {
        // raw char values 0 <= c[x] <= 25 + o
        c = _mm_add_epi8(c, _mm_set1_epi8(o));

        {
            __m128i m = _mm_cmpgt_epi8(c, _mm_set1_epi8(26));
            m = _mm_and_si128(_mm_set1_epi8(26), m);
            c = _mm_sub_epi8(c, m);
        }
        // add prefix back to c
        c = _mm_or_si128(c, prefix);
    }

    _mm_maskmoveu_si128(c, alpha_mask, (char*)cptr);
}

static uint8_t flip_1(uint8_t c, uint8_t o)
{
    if (!isalpha(c)) return c;
    return (((c & 0x1F) + o - 1) % 26 + 1) | (c & 0xE0);
}

void caesar(FILE* fp, int32_t key)
{
    uint8_t o = offset_from_key(key);
    while (get_buf(fp)) {
        uint32_t i = 0;
        for (; buf_len -i >= 32; i += 32) {
            flip_32((uint8_t*)buf + i, o);
        }
        if (buf_len - i >= 16) {
            flip_16((uint8_t*)buf + i, o);
            i += 16;
        }
        for (; i < buf_len; ++i) {
            buf[i] = flip_1(buf[i], o);
        }
        buf[i] = 0;
        fputs(buf, stdout);
    }
}
