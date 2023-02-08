#include "caesar.h"

#include <ctype.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdbool.h>
#include <stdlib.h>
#include <immintrin.h>

#define NUM_THREADS 4

typedef struct future_t future_t;
struct future_t {
    char buf[BUF_SIZE + 1];
    future_t* next;
    uint32_t buf_len;
};

static pthread_t tid[NUM_THREADS] = { 0 };
static future_t* queue = NULL;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static bool running = 1;
static uint8_t offset = 0;

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
    __m128i c = _mm_load_si128((__m128i*)cptr);

    // seperate char prefix and identifier
    __m128i prefix = _mm_and_si128(c, _mm_set1_epi8((uint8_t)0xE0));
    c = _mm_and_si128(c, _mm_set1_epi8((uint8_t)0x1F));

    __m128i alpha_mask = _mm_cmplt_epi8(c, _mm_set1_epi8(27));
    {
        __m128i z = _mm_cmpgt_epi8(c, _mm_set1_epi8(0));
        alpha_mask = _mm_and_si128(alpha_mask, z);
        __m128i lower = _mm_cmpeq_epi8(prefix, _mm_set1_epi8(0x60));
        __m128i upper = _mm_cmpeq_epi8(prefix, _mm_set1_epi8(0x40));
        __m128i t = _mm_or_si128(lower, upper);
        alpha_mask = _mm_and_si128(alpha_mask, t);
    }

    // caesar chiffre
    {
        c = _mm_add_epi8(c, _mm_set1_epi8(offset));

        // basicly c[x] % 26
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

static inline uint8_t flip(uint8_t c)
{
    if (!isalpha(c)) return c;
    return (((c & 0x1F) + offset - 1) % 26 + 1) | (c & 0xE0);
}

static void* work(void* a)
{
    future_t* f = NULL;
    while (1) {
        pthread_mutex_lock(&mutex);
        while (!queue && running) {
            pthread_cond_wait(&cond, &mutex);
        }
        if (queue) {
            f = queue;
            queue = f->next;
        } else if (!running) {
            pthread_mutex_unlock(&mutex);
            return NULL;
        }
        pthread_mutex_unlock(&mutex);

        uint32_t i = 0;
        for (; f->buf_len - i >= 32; i += 32) {
            flip_32((uint8_t*)f->buf + i);
        }
        if (f->buf_len - i >= 16) {
            flip_16((uint8_t*)f->buf + i);
            i += 16;
        }
        for (; i < f->buf_len; ++i) {
            f->buf[i] = flip(f->buf[i]);
        }
        f->buf[i] = 0;
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

    uint32_t cap = 16;
    uint32_t sz = 0;
    future_t** fs = calloc(16, sizeof(*fs));

    while (!feof(fp)) {
        future_t* f = malloc(sizeof(*f));
        f->buf_len = fread(f->buf, 1, sizeof(f->buf) - 1, fp);

        pthread_mutex_lock(&mutex);
        f->next = queue;
        queue = f;
        pthread_mutex_unlock(&mutex);
        pthread_cond_signal(&cond);

        if (sz >= cap) {
            cap <<= 1;
            fs = realloc(fs, cap * sizeof(*fs));
        }
        fs[sz++] = f;
    }

    running = 0;
    pthread_cond_broadcast(&cond);

    for (uint32_t i = 0; i < NUM_THREADS; ++i) {
        pthread_join(tid[i], NULL);
    }

    for (uint32_t i = 0; i < sz; ++i) {
        fputs(fs[i]->buf, stdout);
        free(fs[i]);
    }

    free(fs);
}
