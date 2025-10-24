#ifndef BUFFER_H
#define BUFFER_H

#include <linux/semaphore.h>   // For kernel semaphores
#include <linux/mutex.h>       // For kernel mutexes
#include <linux/time64.h>      // For time structures in the kernel
#include <linux/types.h>       // For basic types like bool
#include <linux/atomic.h>      // For atomic_t
#include <linux/delay.h>       // For fsleep
#include <linux/random.h>      // For get_random_bytes
#include "config.h"

// Kernel equivalent of time_t is struct timespec64
struct array_item {
    int input_id;
    int priority;
    int sleep_interval; 	// sleep time before processing this entry
    int book_id;
};

// Use kernel semaphores for circular buffer
struct circular_buffer {
    struct array_item items[BUFFER_SIZE];
    int read, write, count;	// index of read and write; count=how many entrys in buffer
    struct semaphore mutex;   	// Semaphore for mutual exclusion
    struct semaphore full;    	// Semaphore for tracking filled slots
    struct semaphore empty;   	// Semaphore for tracking empty slots
};

// Use kernel mutex for inventory_array
struct inventory_array {
    struct array_item items[INV_ARRAY_SIZE];
    int count;
    struct mutex mutex;       // Kernel mutex for inventory array
};

// Declare functions (syscalls)
void array_init(struct inventory_array *inv_arrays);
void array_cleanup(struct inventory_array *inv_arrays);
void producer_function(void *arg);
void consumer_function(void *arg);
void auditor_function(void *arg);

#endif
