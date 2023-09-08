//
// Created by ivan on 08.09.23.
//

#ifndef EMERGENCY_DELAY_QUEUE_H
#define EMERGENCY_DELAY_QUEUE_H

#include <pthread.h>
#include <stddef.h>
#include <stdbool.h>

enum {
    QUEUE_OVERFLOW_SKIP = 0,
    QUEUE_OVERFLOW_RESIZE = 1,
    QUEUE_OVERFLOW_LOOP_REPLACE = 2
};

typedef struct {
    int settings;

    pthread_mutex_t pop_lock;
    pthread_mutex_t push_lock;

    ssize_t start;
    ssize_t end;
    ssize_t size;
    char *buffer;
} queue_t;

typedef struct {
    size_t size;
} queue_element_t;

bool queue_init(queue_t *queue, ssize_t initial_capacity, int settings);
void queue_destroy(queue_t *queue);

ssize_t queue_free_space(queue_t *queue);
ssize_t queue_free_continuous_space(queue_t *queue);
bool queue_resize(queue_t *queue, ssize_t new_capacity, bool push_locked);

char *queue_push_acquire(queue_t *queue, ssize_t allocated_amount);
void queue_push_release(queue_t *queue);
char *queue_pop_acquire(queue_t *queue);
void queue_pop_release(queue_t *queue);
void queue_clear(queue_t *queue);

#endif //EMERGENCY_DELAY_QUEUE_H
