#include "caesar.h"

#include <ctype.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <immintrin.h>

#define NUM_THREADS 4

typedef struct chunk_t chunk_t;
struct chunk_t {
    char buf[BUF_SIZE + 1];
    uint32_t buf_len;
    uint32_t id;
    chunk_t* next;
};

static pthread_t tid[NUM_THREADS] = { 0 };
static uint8_t offset = 0;
static FILE* file = NULL;

static chunk_t* reader(void)
{
    static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    static uint32_t chunk_id = 0;

    pthread_mutex_lock(&mutex);

    if (feof(file)) {
        pthread_mutex_unlock(&mutex);
        return NULL;
    }

    chunk_t* chunk = malloc(sizeof(*chunk));
    chunk->buf_len = fread(chunk->buf, 1, BUF_SIZE, file);
    chunk->id = chunk_id++;

    pthread_mutex_unlock(&mutex);
    return chunk;
}

static void writer(chunk_t* chunk)
{
    static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    static uint32_t chunk_id = 0;
    static chunk_t* list = NULL;

    pthread_mutex_lock(&mutex);

    if (chunk->id == chunk_id) {
        fputs(chunk->buf, stdout);
        free(chunk);
        chunk_id++;

        while (list && list->id == chunk_id) {
            fputs(list->buf, stdout);
            chunk_t* tmp = list;
            list = list->next;
            free(tmp);
            chunk_id++;
        }
    } else {
        chunk_t** curr = &list;
        while (*curr && (*curr)->id < chunk->id) {
            curr = &(*curr)->next;
        }
        chunk->next = *curr;
        *curr = chunk;
    }

    pthread_mutex_unlock(&mutex);
}

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

static inline uint8_t flip(uint8_t c)
{
    if (!isalpha(c)) return c;
    return (((c & 0x1F) + offset - 1) % 26 + 1) | (c & 0xE0);
}

static void* work(void* a)
{
    chunk_t* chunk = NULL;
    while (1) {
        chunk = reader();

        if (!chunk) {
            return NULL;
        }

        uint32_t i = 0;
        for (; chunk->buf_len - i >= 16; i += 16) {
            flip_16((uint8_t*)chunk->buf + i);
        }
        for (; i < chunk->buf_len; ++i) {
            chunk->buf[i] = flip(chunk->buf[i]);
        }

        chunk->buf[chunk->buf_len] = 0;

        writer(chunk);
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
    file = fp;

    for (uint32_t i = 0; i < NUM_THREADS; ++i) {
        pthread_create(&tid[i], NULL, work, NULL);
    }

    for (uint32_t i = 0; i < NUM_THREADS; ++i) {
        pthread_join(tid[i], NULL);
    }
}
