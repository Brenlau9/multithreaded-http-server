#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "rwlock.h"

typedef struct rwlock {
    pthread_mutex_t lock;
    pthread_cond_t readers_available;
    pthread_cond_t writers_available;
    int num_readers;
    int num_writers;
    int num_readers_waiting;
    int num_writers_waiting;
    int n;
    int read_count;
    PRIORITY priority;
} rwlock;

rwlock_t *rwlock_new(PRIORITY p, uint32_t n) {
    rwlock_t *rwlock = calloc(1, sizeof(rwlock_t));
    pthread_mutex_init(&rwlock->lock, NULL);
    pthread_cond_init(&rwlock->readers_available, NULL);
    pthread_cond_init(&rwlock->writers_available, NULL);
    rwlock->num_readers = 0;
    rwlock->num_writers = 0;
    rwlock->num_readers_waiting = 0;
    rwlock->num_writers_waiting = 0;
    rwlock->n = n;
    rwlock->priority = p;
    rwlock->read_count = 0;
    return rwlock;
}
void rwlock_delete(rwlock_t **rw) {
    if (rw != NULL && *rw != NULL) {
        free(*rw);
        *rw = NULL;
    }
}
void reader_lock(rwlock_t *rw) {
    pthread_mutex_lock(&rw->lock);
    /*
    Wait if any of the following are true:
    1. The priority is writers and there are writers
    2. The priority is N_WAY, and there are writers OR there are more readers than the limit n OR if the read count exceeded n
    */
    while ((rw->priority == WRITERS && rw->num_writers > 0)
           || (rw->priority == N_WAY && (rw->num_writers > 0 || rw->read_count >= rw->n))) {
        rw->num_readers_waiting++;
        pthread_cond_wait(&rw->readers_available, &rw->lock);
        rw->num_readers_waiting--;
    }
    rw->num_readers++;
    rw->read_count++;
    pthread_mutex_unlock(&rw->lock);
}
void reader_unlock(rwlock_t *rw) {
    pthread_mutex_lock(&rw->lock);
    rw->num_readers--;
    //Broadcast readers first if priority is readers, otherwise signal to writers
    //Signal to writers first if priority is writers, otherwise broadcase to readers
    //For N-WAY, allow readers to go first if the read count has not exceeded n, otherwise allow writers to go.
    if (rw->priority == READERS) {
        if (rw->num_readers_waiting > 0) {
            pthread_cond_broadcast(&rw->readers_available);
        } else if (rw->num_writers_waiting > 0) {
            pthread_cond_signal(&rw->writers_available);
        }
    } else if (rw->priority == WRITERS) {
        if (rw->num_writers_waiting > 0) {
            pthread_cond_signal(&rw->writers_available);
        } else if (rw->num_readers_waiting > 0) {
            pthread_cond_broadcast(&rw->readers_available);
        }
    } else {
        if (rw->num_readers_waiting > 0 && rw->read_count < rw->n) {
            pthread_cond_broadcast(&rw->readers_available);
        } else if (rw->num_writers_waiting > 0) {
            pthread_cond_signal(&rw->writers_available);
        }
    }
    pthread_mutex_unlock(&rw->lock);
}
void writer_lock(rwlock_t *rw) {
    pthread_mutex_lock(&rw->lock);
    /*
    Wait if any of the following are true:
    1. The priority is readers and there are readers
    2. The priority is N_WAY, and there are readers OR there is already a writer in progress
    3. The priority is N_WAY, and the read count less than n (should let readers go)
    */
    while ((rw->priority == READERS && rw->num_readers > 0)
           || (rw->priority == N_WAY
               && (rw->num_readers > 0 || rw->num_writers > 0
                   || (rw->read_count < rw->n && rw->num_readers_waiting > 0)))
           || rw->num_writers > 0) {
        rw->num_writers_waiting++;
        pthread_cond_wait(&rw->writers_available, &rw->lock);
        rw->num_writers_waiting--;
    }
    rw->num_writers++;
    rw->read_count = 0;
    pthread_mutex_unlock(&rw->lock);
}
void writer_unlock(rwlock_t *rw) {
    pthread_mutex_lock(&rw->lock);
    rw->num_writers--;
    //Allow readers to go first if the priority is readers
    //Allow writers to go first if the priority is writers
    //If N_WAY, allow readers to go first if the read count is less than n, then writers
    if (rw->priority == READERS) {
        if (rw->num_readers_waiting > 0) {
            pthread_cond_broadcast(&rw->readers_available);
        } else if (rw->num_writers_waiting > 0) {
            pthread_cond_signal(&rw->writers_available);
        }
    } else if (rw->priority == WRITERS) {
        if (rw->num_writers_waiting > 0) {
            pthread_cond_signal(&rw->writers_available);
        } else if (rw->num_readers_waiting > 0) {
            pthread_cond_broadcast(&rw->readers_available);
        }
    } else {
        if (rw->num_readers_waiting > 0 && rw->read_count < rw->n) {
            pthread_cond_broadcast(&rw->readers_available);
        } else if (rw->num_writers_waiting > 0) {
            pthread_cond_signal(&rw->writers_available);
        }
    }
    pthread_mutex_unlock(&rw->lock);
}
