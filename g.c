#include "caesar.h"

#include <ctype.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdatomic.h>

#define NUM_THREADS 4

typedef struct future_t future_t;
struct future_t {
    char buf[BUF_SIZE + 1];
    future_t* next;
    uint32_t buf_len;
};

static pthread_t tid[NUM_THREADS] = { 0 };
static future_t* queue = NULL;
static bool running = 1;
static uint8_t offset = 0;

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
        f->next = NULL;

        lf_push(f);

        if (sz >= cap) {
            cap <<= 1;
            fs = realloc(fs, cap * sizeof(*fs));
        }
        fs[sz++] = f;
    }

    running = 0;

    for (uint32_t i = 0; i < NUM_THREADS; ++i) {
        pthread_join(tid[i], NULL);
    }

    for (uint32_t i = 0; i < sz; ++i) {
        fputs(fs[i]->buf, stdout);
        free(fs[i]);
    }

    free(fs);
}
