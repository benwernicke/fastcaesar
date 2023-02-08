#include "caesar.h"

#include <ctype.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <immintrin.h>

// fancy gcc branch prediction stuff
#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)

#define NUM_THREADS 4

typedef struct chunk_t chunk_t;
struct chunk_t {
    char buf[BUF_SIZE + 1];
    uint32_t buf_len;
    uint32_t id;
};

static pthread_t tid[NUM_THREADS] = { 0 };
static uint8_t offset = 0;
static FILE* file = NULL;

static chunk_t* reader(void)
{
    static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    static uint32_t chunk_id = 0;

    pthread_mutex_lock(&mutex);

    if (unlikely(feof(file))) {
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

    // This seems to be the better technique compared to another mutex here:
    // mutex in writer enforces some time offset between the workers, each task
    // will take roughly the same time tho. So a mutex here would be unnecessary
    // overhead. Very unlikely that multiple threads call this at the same time.
    // atomic spinlock based on the chunk_ids is sufficient
    //
    // Even if chunk_id wraps back to zero, shouldnt be a problem. By the time
    // it wraps. The original chunk with id zero must already be printed
    
    static uint32_t chunk_id = 0;
    uint32_t id = 0;

    do {
        id = atomic_load(&chunk_id);
    } while (id != chunk->id);

    fputs(chunk->buf, stdout);
    free(chunk);
    atomic_fetch_add(&chunk_id, 1);
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
    if (unlikely(!isalpha(c))) return c;
    return (((c & 0x1F) + offset - 1) % 26 + 1) | (c & 0xE0);
}

static void* work(void* a)
{
    chunk_t* chunk = NULL;
    for (;;) {
        chunk = reader();

        if (unlikely(!chunk)) {
            return NULL;
        }

        uint32_t i = 0;

        for (; chunk->buf_len - i >= 32; i += 32) {
            flip_32((uint8_t*)chunk->buf + i);
        }
        if (chunk->buf_len - i >= 16) {
            flip_16((uint8_t*)chunk->buf + i);
            i += 16;
        }

        for (; i < chunk->buf_len; ++i) {
            chunk->buf[i] = flip(chunk->buf[i]);
        }

        chunk->buf[chunk->buf_len] = 0;

        writer(chunk);
    }
    __builtin_unreachable();
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
