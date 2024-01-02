//
// Created by ivan on 08.09.23.
//

#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "queue.h"
#include "c23_compat.h"

#define MIN(x, y) (((x) < (y)) ? (x) : (y))

bool queue_init(queue_t *queue, const ssize_t initial_capacity, const queue_overflow_behavior_t overflow_behavior) {
    if (queue == nullptr)
        return false;

    bzero(queue, sizeof(*queue));

    queue->overflow_behavior = overflow_behavior;
    queue->start = 0;
    queue->end = 0;
    queue->size = (initial_capacity + QUEUE_ITEM_BUFFER_SIZE - 1) / QUEUE_ITEM_BUFFER_SIZE;
    queue->buffer = malloc(queue->size * QUEUE_ITEM_SIZE);
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

    // TODO: undefined behavior when the queue is still being used by the other threads
    assert(pthread_mutex_destroy(&queue->pop_lock) == 0);
    assert(pthread_mutex_destroy(&queue->push_lock) == 0);
}

static ssize_t queue_free_space_chunks(const queue_t *queue) {
    if (queue == nullptr)
        return 0;

    if (queue->start <= queue->end)
        return queue->size - queue->end + queue->start;
    return queue->start - queue->end;
}

bool queue_is_empty(const queue_t *queue) {
    return queue->start == queue->end;
}

ssize_t queue_free_space(const queue_t *queue) {
    return queue_free_space_chunks(queue) * QUEUE_ITEM_BUFFER_SIZE;
}

ssize_t queue_size(const queue_t *queue) {
    return queue->size * QUEUE_ITEM_BUFFER_SIZE;
}

static bool queue_resize_internal(queue_t *queue, ssize_t new_capacity, const bool push_locked) {
    if (queue == nullptr || queue->buffer == nullptr)
        return false;

    new_capacity = (new_capacity + QUEUE_ITEM_BUFFER_SIZE - 1) / QUEUE_ITEM_BUFFER_SIZE;

    if (new_capacity == queue->size)
        return true;

    bool success = false;

    pthread_mutex_lock(&queue->pop_lock);
    if (!push_locked)
        pthread_mutex_lock(&queue->push_lock);

    if (new_capacity < queue->size) {
        // TODO: shrink
        goto fail;
    } else {
        queue_item_t *new_buffer = realloc(queue->buffer, new_capacity * QUEUE_ITEM_SIZE);
        if (new_buffer == nullptr)
            goto fail;

        if (queue->start > queue->end) {
            // Rearrange the items if they loop around
            ssize_t free_item_space = new_capacity - queue->size;
            ssize_t take_from_end = MIN(queue->end, free_item_space);
            memcpy(&new_buffer[queue->size], &new_buffer[0], take_from_end * QUEUE_ITEM_SIZE);
            memcpy(&new_buffer[0], &new_buffer[free_item_space], (queue->end - take_from_end) * QUEUE_ITEM_SIZE);
            queue->end = queue->size + take_from_end;
        }

        queue->buffer = new_buffer;
    }
    queue->size = new_capacity;
    success = true;

fail:
    pthread_mutex_unlock(&queue->pop_lock);
    if (!push_locked)
        pthread_mutex_unlock(&queue->push_lock);

    return success;
}

bool queue_resize(queue_t *queue, const ssize_t new_capacity) {
    return queue_resize_internal(queue, new_capacity, false);
}

NODISCARD

static bool queue_push_chunk(queue_t *queue, const ssize_t size, const bool continued, const char *buffer) {
    if (queue_free_space_chunks(queue) == 0)
        // TODO: handle the overflow gracefully with queue->overflow_behavior
        return false;

    assert(size <= QUEUE_ITEM_BUFFER_SIZE);

    const ssize_t index = queue->end;
    queue->end++;
    if (queue->end >= queue->size)
        queue->end -= queue->size;

    queue_item_t *item = &queue->buffer[index];
    item->header.offset = 0;
    item->header.size = size;
    item->header.continued = continued;

    memcpy(item->buffer, buffer, size);

    return true;
}

NODISCARD

bool queue_push(queue_t *queue, ssize_t size, const char *buffer) {
    if (queue == nullptr || queue->buffer == nullptr || buffer == nullptr)
        return false;

    bool success = false;

    pthread_mutex_lock(&queue->push_lock);

    const ssize_t chunks = (size + QUEUE_ITEM_BUFFER_SIZE - 1) / QUEUE_ITEM_BUFFER_SIZE;
    const ssize_t end_initial = queue->end;
    for (int i = 0; i < chunks && size > 0; i++) {
        const ssize_t size_to_push = MIN(QUEUE_ITEM_BUFFER_SIZE, size);
        const bool has_more_chunks = i != chunks - 1;
        if (!queue_push_chunk(queue, size_to_push, has_more_chunks, buffer)) {
            // Undo all the chunks
            queue->end = end_initial;
            goto fail;
        }

        size -= size_to_push;
        buffer += size_to_push;
    }
    success = true;

fail:
    pthread_mutex_unlock(&queue->push_lock);

    return success;
}

NODISCARD

bool queue_pop(queue_t *queue, ssize_t size, char *buffer, ssize_t *written) {
    if (queue == nullptr || queue->buffer == nullptr || buffer == nullptr || written == nullptr || size == 0)
        return false;

    if (queue->start == queue->end)
        return false;

    pthread_mutex_lock(&queue->pop_lock);

    bool has_more_chunks = false;
    ssize_t chunks_size = 0;
    ssize_t pop_size = 0;
    do {
        queue_item_t *item = &queue->buffer[queue->start];
        const ssize_t size_to_pop = MIN(item->header.size, size);

        memcpy(buffer, item->buffer + item->header.offset, size_to_pop);

        size -= size_to_pop;
        chunks_size += item->header.size;
        pop_size += size_to_pop;

        item->header.offset = size_to_pop;
        item->header.size -= item->header.offset;
        assert(item->header.size >= 0);

        has_more_chunks = item->header.continued;

        // If the item was fully copied
        if (item->header.size == 0) {
            queue->start++;
            if (queue->start >= queue->size)
                queue->start -= queue->size;
        }

        if (has_more_chunks)
            assert(queue_free_space_chunks(queue) > 0);
    } while (size > 0 && queue_free_space_chunks(queue) > 0 && has_more_chunks);

    // Count how many chunks have we missed
    ssize_t start_temp = queue->start;
    while (has_more_chunks) {
        const queue_item_t *item = &queue->buffer[start_temp];
        assert(item->header.offset == 0);
        chunks_size += item->header.size;
        has_more_chunks = item->header.continued;

        start_temp++;
        if (start_temp >= queue->size)
            start_temp -= queue->size;

        if (has_more_chunks
            && (start_temp <= queue->end
                    ? queue->size - queue->end + start_temp
                    : start_temp - queue->end) <= 0)
            has_more_chunks = false;

        if (has_more_chunks)
            assert(queue_free_space_chunks(queue) > 0);
    }

    if (pop_size == chunks_size)
        *written = pop_size;
    else
        *written = pop_size - chunks_size;

    pthread_mutex_unlock(&queue->pop_lock);

    return true;
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
