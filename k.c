#include "caesar.h"

#include <ctype.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdbool.h>
#include <stdlib.h>

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
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

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

        pthread_mutex_lock(&mutex);
        f->next = queue;
        queue = f;
        pthread_mutex_unlock(&mutex);
        pthread_cond_signal(&cond);

    }

    if (i < file_len) {
        future_t* f = malloc(sizeof(*f));
        f->buf = file_content + i;
        f->buf_len = file_len - i;
        f->next = NULL;

        pthread_mutex_lock(&mutex);
        f->next = queue;
        queue = f;
        pthread_mutex_unlock(&mutex);
        pthread_cond_signal(&cond);
    }

    running = 0;
    pthread_cond_broadcast(&cond);

    for (uint32_t i = 0; i < NUM_THREADS; ++i) {
        pthread_join(tid[i], NULL);
    }
    fputs(file_content, stdout);
    free(file_content);
}
