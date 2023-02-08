#include "caesar.h"

#include <ctype.h>
#include <stdbool.h>
#include <immintrin.h>
#include <pthread.h>
#include <stdlib.h>

#define NUM_THREADS 4

// fancy gcc branch prediction stuff
#define likely(x)   __builtin_expect((x),1)
#define unlikely(x) __builtin_expect((x),0)

static uint8_t offset = 0;

typedef struct chunk_t chunk_t;
struct chunk_t {
    char* b;
    uint32_t l;
};

static pthread_t tid[NUM_THREADS] = { 0 };
static chunk_t chunk[NUM_THREADS] = { 0 };

static uint8_t offset_from_key(int32_t key)
{
    key %= 26;
    return key < 0 ? 26 + key : key;
}

static void flip_32(uint8_t* cptr)
{
    __m256i orig = _mm256_loadu_si256((__m256i*)cptr);
    
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

    c = _mm256_add_epi8(c, _mm256_set1_epi8(offset));
    __m256i m = _mm256_cmpgt_epi8(c, _mm256_set1_epi8(26));
    m = _mm256_and_si256(m, _mm256_set1_epi8(26));
    c = _mm256_sub_epi8(c, m);
    c = _mm256_or_si256(c, prefix);
    c = _mm256_and_si256(c, alpha_mask);
    orig = _mm256_or_si256(c, orig);
    _mm256_storeu_si256((__m256i*)cptr, orig);
}

static void flip_16(uint8_t* cptr)
{
    __m128i c = _mm_loadu_si128((__m128i*)cptr);

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
        c = _mm_add_epi8(c, _mm_set1_epi8(offset));

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

static uint8_t flip_1(uint8_t c)
{
    if (!isalpha(c)) return c;
    return (((c & 0x1F) + offset - 1) % 26 + 1) | (c & 0xE0);
}

static void* work(void* a)
{
    chunk_t* chunk = (chunk_t*) a;

    uint32_t i = 0;
    for (; chunk->l - i >= 32; i += 32) {
        flip_32((uint8_t*)chunk->b + i);
    }
    if (chunk->l - i >= 16) {
        flip_16((uint8_t*)chunk->b + i);
        i += 16;
    }
    for (; i < chunk->l; ++i) {
        chunk->b[i] = flip_1(chunk->b[i]);
    }
    return NULL;
}

void caesar(FILE* fp, int32_t key)
{
    offset = offset_from_key(key);

    char* file_content;
    uint64_t file_len;

    {
        fseek(fp, 0, SEEK_END);
        file_len = ftell(fp);
        rewind(fp);
        file_content = malloc(file_len + 1);
        file_content[file_len] = 0;
        fread(file_content, 1, file_len, fp);
    }

    {
        uint64_t chunk_size = file_len / NUM_THREADS;
        for (uint32_t i = 0; i < NUM_THREADS; ++i) {
            chunk[i].b = file_content + chunk_size * i;
            chunk[i].l = chunk_size;
        }
        chunk[NUM_THREADS - 1].l = file_len - chunk_size * (NUM_THREADS - 1);

        for (uint32_t i = 0; i < NUM_THREADS; ++i) {
            pthread_create(&tid[i], NULL, work, &chunk[i]);
        }

        for (uint32_t i = 0; i < NUM_THREADS; ++i) {
            pthread_join(tid[i], NULL);
        }
    }

    fputs(file_content, stdout);
    free(file_content);
}
