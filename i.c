#include "caesar.h"

#include <ctype.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <immintrin.h>

#define NUM_THREADS 4

typedef struct future_t future_t;
struct future_t {
    char* buf;
    future_t* next;
    uint32_t buf_len;
};

static pthread_t tid[NUM_THREADS] = { 0 };
static future_t* queue = NULL;
static bool running = 1;
static uint8_t offset = 0;

static void flip_16(uint8_t* cptr)
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
        c = _mm_add_epi8(c, _mm_set1_epi8(offset - 1));

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

static inline future_t* lf_pop(void)
{
    future_t* new;
    future_t* old = atomic_load(&queue);
    do {
        if (!old) {
            return NULL;
        }
        new = old->next;
    } while (!atomic_compare_exchange_weak(&queue, &old, new));
    return old;
}

static inline void lf_push(future_t* new)
{
    future_t* old = atomic_load(&queue);
    do {
        new->next = old;
    } while (!atomic_compare_exchange_weak(&queue, &old, new));
}

static inline uint8_t flip(uint8_t c)
{
    if (!isalpha(c)) return c;
    return (((c & 0x1F) + offset - 1) % 26 + 1) | (c & 0xE0);
}

static void* work(void* a)
{
    future_t* f = NULL;
    while (1) {
        do {
            f = lf_pop();
        } while (!f && running);

        if (!f && !running) {
            return NULL;
        }

        uint32_t i = 0;
        for (; f->buf_len -i >= 16; i += 16) {
            flip_16((uint8_t*)f->buf + i);
        }
        for (; i < f->buf_len; ++i) {
            f->buf[i] = flip(f->buf[i]);
        }
        free(f);
    }
}

static uint8_t offset_from_key(int32_t key)
{
    key %= 26;
    return key < 0 ? 26 + key : key;
}


void caesar(FILE* fp, int32_t key)
{
    offset = offset_from_key(key);
    running = 1;

    for (uint32_t i = 0; i < NUM_THREADS; ++i) {
        pthread_create(&tid[i], NULL, work, NULL);
    }

    char* file_content = NULL;
    uint64_t file_len = 0;
    {
        fseek(fp, 0, SEEK_END);
        file_len = ftell(fp);
        rewind(fp);
        file_content = malloc(file_len + 1);
        file_content[file_len] = 0;
        fread(file_content, 1, file_len, fp);
    }

    uint64_t i = 0;
    for (;  i + BUF_SIZE < file_len; i += BUF_SIZE) {
        future_t* f = malloc(sizeof(*f));
        f->buf = file_content + i;
        f->buf_len = BUF_SIZE;
        f->next = NULL;
        lf_push(f);
    }

    if (i < file_len) {
        future_t* f = malloc(sizeof(*f));
        f->buf = file_content + i;
        f->buf_len = file_len - i;
        f->next = NULL;
        lf_push(f);
    }

    running = 0;

    for (uint32_t i = 0; i < NUM_THREADS; ++i) {
        pthread_join(tid[i], NULL);
    }
    fputs(file_content, stdout);
    free(file_content);
}
