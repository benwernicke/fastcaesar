#include "caesar.h"

#include <ctype.h>
#include <stdbool.h>
#include <immintrin.h>

static char buf[1025] = { 0 };
static uint32_t buf_len = 0;

static bool get_buf(FILE* f) { return (buf_len = fread(buf, 1, 1024, f)) > 0; }

static uint8_t offset_from_key(int32_t key)
{
    key %= 26;
    return key < 0 ? 26 + key : key;
}

// for debugging
/*static void dump_vec(__m128i v)*/
/*{*/
    /*printf("%u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u\n", */
            /*((uint8_t*)&v)[0],*/
            /*((uint8_t*)&v)[1],*/
            /*((uint8_t*)&v)[2],*/
            /*((uint8_t*)&v)[3],*/
            /*((uint8_t*)&v)[4],*/
            /*((uint8_t*)&v)[5],*/
            /*((uint8_t*)&v)[6],*/
            /*((uint8_t*)&v)[7],*/
            /*((uint8_t*)&v)[8],*/
            /*((uint8_t*)&v)[9],*/
            /*((uint8_t*)&v)[10],*/
            /*((uint8_t*)&v)[11],*/
            /*((uint8_t*)&v)[12],*/
            /*((uint8_t*)&v)[13],*/
            /*((uint8_t*)&v)[14],*/
            /*((uint8_t*)&v)[15]*/
        /*);*/
/*}*/

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
        c = _mm_add_epi8(c, _mm_set1_epi8(o - 1));

        // basicly c[x] % 26
        {
            __m128i m = _mm_cmpgt_epi8(c, _mm_set1_epi8(25));
            m = _mm_and_si128(_mm_set1_epi8(26), m);
            c = _mm_sub_epi8(c, m);
        }
        c = _mm_add_epi8(c, _mm_set1_epi8(1));

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
        for (; buf_len -i >= 16; i += 16) {
            flip_16((uint8_t*)buf + i, o);
        }
        for (; i < buf_len; ++i) {
            buf[i] = flip_1(buf[i], o);
        }
        buf[i] = 0;
        fputs(buf, stdout);
    }
    putc('\n', stdout);
}
