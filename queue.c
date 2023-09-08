//
// Created by ivan on 08.09.23.
//

#include <stdlib.h>
#include "queue.h"
#include "c23_compat.h"

// TODO: may be dangerous, implying that sizeof(void*) is a power of two
#define QUEUE_SIZE_ALIGN(s) ((s + sizeof(void *) - 1) & ~(sizeof(void *) - 1))
#define QUEUE_GET_OVERFLOW_BEHAVIOR(queue) (queue->settings & (QUEUE_OVERFLOW_LOOP_REPLACE | QUEUE_OVERFLOW_RESIZE))

bool queue_init(queue_t *queue, ssize_t initial_capacity, int settings) {
    if (queue == nullptr)
        return false;

    queue->settings = settings;

    queue->start = 0;
    queue->end = 0;
    queue->size = QUEUE_SIZE_ALIGN(initial_capacity);
    queue->buffer = malloc(queue->size * sizeof(char)); // TODO: I hope that there won't be any alignment issues... :/
    if (queue->buffer == nullptr)
        return false;

    if (pthread_mutex_init(&queue->pop_lock, NULL) != 0
        || pthread_mutex_init(&queue->push_lock, NULL) != 0)
        return false;

    return true;
}

void queue_destroy(queue_t *queue) {
    if (queue == nullptr)
        return;

    queue_clear(queue);

    if (queue->buffer != nullptr) {
        free(queue->buffer);
        queue->buffer = nullptr;
    }

    // TODO: undefined behavior when the queue is still used by the other threads
    pthread_mutex_destroy(&queue->pop_lock);
    pthread_mutex_destroy(&queue->push_lock);
}

ssize_t queue_free_space(queue_t *queue) {
    if (queue == nullptr)
        return 0;

    if (queue->start <= queue->end)
        return queue->size - queue->end + queue->start;
    else
        return queue->start - queue->end;
}

ssize_t queue_free_continuous_space(queue_t *queue) {
    if (queue == nullptr)
        return 0;

    if (queue->start <= queue->end)
        return queue->size - queue->end;
    else
        return queue->start - queue->end;
}

bool queue_resize(queue_t *queue, ssize_t new_capacity, bool push_locked) {
    if (queue == nullptr || queue->buffer == nullptr)
        return false;

    new_capacity = QUEUE_SIZE_ALIGN(new_capacity);

    if (new_capacity == queue->size)
        return true;

    pthread_mutex_lock(&queue->pop_lock);
    if (!push_locked)
        pthread_mutex_lock(&queue->push_lock);
    if (new_capacity < queue->size) {
        // TODO: shrink
        return false;
    } else {
        char *new_buffer = realloc(queue->buffer, new_capacity);
        if (new_buffer == nullptr)
            return false;

        queue->buffer = new_buffer;
    }
    queue->size = new_capacity;
    pthread_mutex_unlock(&queue->pop_lock);
    if (!push_locked)
        pthread_mutex_unlock(&queue->push_lock);

    return true;
}

char *queue_push_acquire(queue_t *queue, ssize_t amount) {
    if (queue == nullptr || queue->buffer == nullptr)
        return nullptr;

    pthread_mutex_lock(&queue->push_lock);

    amount = QUEUE_SIZE_ALIGN(amount);
    ssize_t allocated_amount = amount + sizeof(queue_element_t); // TODO: clang-tidy warning???
    if (queue_free_continuous_space(queue) < allocated_amount) {
        const int settings = QUEUE_GET_OVERFLOW_BEHAVIOR(queue);
        if ((settings & QUEUE_OVERFLOW_RESIZE) > 0 &&
            queue->start <= queue->end) { // TODO: there will be other edge cases but ATM I CBA
            if (!queue_resize(queue, allocated_amount + queue->end, true))
                return nullptr;
        } else if ((settings & QUEUE_OVERFLOW_LOOP_REPLACE) > 0) {
            // TODO:
            (void) allocated_amount;
            return nullptr;
        } else // (settings == QUEUE_OVERFLOW_SKIP)
            return nullptr;
    }

    char *p = queue->buffer + queue->end;
    queue_element_t *qep = (queue_element_t *) p;
    qep->size = amount;
    p += sizeof(queue_element_t);

    queue->end += allocated_amount;

    return p;
}

void queue_push_release(queue_t *queue) {
    if (queue == nullptr)
        return;

    pthread_mutex_unlock(&queue->push_lock);
}

char *queue_pop_acquire(queue_t *queue) {
    if (queue == nullptr || queue->buffer == nullptr)
        return nullptr;

    if (queue->start == queue->end)
        return nullptr;

    pthread_mutex_lock(&queue->pop_lock);
    char *p = queue->buffer + queue->start + sizeof(queue_element_t);

    return p;
}

void queue_pop_release(queue_t *queue) {
    if (queue == nullptr)
        return;

    queue_element_t *qep = (queue_element_t *)(queue->buffer + queue->start);
    queue->start += qep->size + sizeof(queue_element_t);
    if (queue->start == queue->end)
        queue->start = queue->end = 0;

    pthread_mutex_unlock(&queue->pop_lock);
}

void queue_clear(queue_t *queue) {
    if (queue == nullptr)
        return;

    pthread_mutex_lock(&queue->pop_lock);
    pthread_mutex_lock(&queue->push_lock);

    queue->start = queue->end = 0;

    pthread_mutex_unlock(&queue->pop_lock);
    pthread_mutex_unlock(&queue->push_lock);
}