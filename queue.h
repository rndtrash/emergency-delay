//
// Created by ivan on 08.09.23.
//

#ifndef EMERGENCY_DELAY_QUEUE_H
#define EMERGENCY_DELAY_QUEUE_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>

#include "c23_compat.h"

// TODO: may be dangerous, implying that sizeof(void*) is a power of two
#define QUEUE_SIZE_ALIGN(s, t) ((s + sizeof(t) - 1) & ~(sizeof(t) - 1))

typedef enum {
    QUEUE_OVERFLOW_SKIP = 0,
    QUEUE_OVERFLOW_RESIZE = 1,
    QUEUE_OVERFLOW_LOOP_REPLACE = 2
} queue_overflow_behavior_t;

__attribute__((aligned(sizeof(ssize_t))))
typedef struct {
    ssize_t size;
    ssize_t offset;
    bool continued;
} queue_item_header_t;

#define QUEUE_ITEM_SIZE (2048)
#define QUEUE_ITEM_BUFFER_SIZE (QUEUE_ITEM_SIZE - (ssize_t)sizeof(queue_item_header_t))

typedef struct {
    queue_item_header_t header;
    char buffer[QUEUE_ITEM_BUFFER_SIZE];
} queue_item_t;

typedef struct {
    queue_overflow_behavior_t overflow_behavior;

    pthread_mutex_t pop_lock;
    pthread_mutex_t push_lock;

    /**
     * The start of the queue in chunks
     */
    _Atomic ssize_t start;
    /**
     * The end of the queue in chunks
     */
    _Atomic ssize_t end;
    /**
     * The size of the queue in chunks
     */
    _Atomic ssize_t size;
    queue_item_t *buffer;
} queue_t;

/**
 * Initialize the queue structure and allocate a queue buffer.
 *
 * @param [in] queue a pointer to the queue structure
 * @param [in] initial_capacity initial size of the queue buffer in bytes
 * @param [in] overflow_behavior what to do if the queue buffer is exhausted
 * @returns true if succeeded, false if failed
 */
bool queue_init(queue_t *queue, ssize_t initial_capacity, queue_overflow_behavior_t overflow_behavior);

/**
 * Wait for the threads to finish and deallocate the queue buffer.
 *
 * @note This function does not free the queue structure itself.
 * @param [in] queue a pointer to the queue
 */
void queue_destroy(queue_t *queue);

/**
 * Does the queue contain any element?
 *
 * @param [in] queue a pointer to the queue
 * @returns true if the queue is empty, false otherwise
 */
bool queue_is_empty(const queue_t *queue);

/**
 * Get the space available in the queue.
 *
 * @param [in] queue a pointer to the queue
 * @returns Amount of free space in the queue in bytes
 */
ssize_t queue_free_space(const queue_t *queue);

/**
 * Get the size of the queue buffer
 *
 * @param [in] queue a pointer to the queue
 * @returns Size of the buffer in bytes
 */
ssize_t queue_size(const queue_t *queue);

/**
 * Extend or shrink the queue buffer while keeping the queue items in tact.
 *
 * @param [in] queue a pointer to the queue
 * @param [in] new_capacity the new capacity of the queue buffer in bytes
 * @returns true if succeeded, false if failed
 */
bool queue_resize(queue_t *queue, ssize_t new_capacity);

/**
 * Adds a new item with data from the source buffer to the end of the queue.
 *
 * @param [in] queue a pointer to the queue
 * @param [in] size the source buffer's size in bytes
 * @param [in] buffer the source buffer
 * @returns true if succeeded, false if failed
 */
NODISCARD bool queue_push(queue_t *queue, ssize_t size, const char *buffer);

/**
 * Gets the size of the first element in the queue.
 *
 * @param [in] queue a pointer to the queue
 * @returns A negative number if the queue is empty, or the first item's size.
 */
ssize_t queue_peek_size(queue_t *queue);

/**
 * Puts data from the first queue item into the buffer, then removes said item.
 *
 * @param [in] queue a pointer to the queue
 * @param [in] size the target buffer's size in bytes
 * @param [out] buffer the target buffer
 * @param [out] written A positive number or zero that indicates the amount of written bytes,
 * or a negative number that indicates the amount of bytes that haven't been written
 * if the buffer is smaller than the data
 * @returns true if succeeded, false if failed
 */
NODISCARD bool queue_pop(queue_t *queue, ssize_t size, char *buffer, ssize_t *written);

/**
 * Removes all the items from the queue
 *
 * @param [in] queue a pointer to the queue
 */
void queue_clear(queue_t *queue);

#endif //EMERGENCY_DELAY_QUEUE_H
