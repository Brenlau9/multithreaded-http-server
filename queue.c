#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <pthread.h>
#include "queue.h"

typedef struct queue {
    int in;
    int out;
    int num_elem;
    int size;
    void **arr;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} queue;
queue_t *queue_new(int size) {
    queue_t *Q;
    Q = calloc(1, sizeof(queue_t));
    Q->arr = calloc(size, sizeof(void *));
    Q->in = 0;
    Q->out = 0;
    Q->num_elem = 0;
    Q->size = size;
    pthread_mutex_init(&Q->mutex, NULL);
    pthread_cond_init(&Q->not_empty, NULL);
    pthread_cond_init(&Q->not_full, NULL);
    return Q;
}
void queue_delete(queue_t **q) {
    if (q != NULL && *q != NULL) {
        if ((*q)->arr != NULL) {
            free((*q)->arr);
        }
        free(*q);
        *q = NULL;
    }
}
bool queue_push(queue_t *q, void *elem) {
    if (q != NULL) {
        pthread_mutex_lock(&q->mutex);
        while (q->num_elem == q->size) {
            pthread_cond_wait(&q->not_full, &q->mutex);
        }
        q->arr[q->in] = elem;
        q->in = (q->in + 1) % q->size;
        q->num_elem++;
        pthread_cond_signal(&q->not_empty);
        pthread_mutex_unlock(&q->mutex);
        return true;
    } else {
        return false;
    }
}
bool queue_pop(queue_t *q, void **elem) {
    if (q != NULL) {
        pthread_mutex_lock(&q->mutex);
        while (q->num_elem == 0) {
            pthread_cond_wait(&q->not_empty, &q->mutex);
        }
        *elem = q->arr[q->out];
        q->out = (q->out + 1) % q->size;
        q->num_elem--;
        pthread_cond_signal(&q->not_full);
        pthread_mutex_unlock(&q->mutex);
        return true;
    } else {
        return false;
    }
}
