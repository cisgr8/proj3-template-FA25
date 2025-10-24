#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/kernel.h>
#include <errno.h>
#include "config.h"

#define SYS_array_init       548
#define SYS_array_cleanup    549
#define SYS_producer_function 550
#define SYS_consumer_function 551
#define SYS_auditor_function  552

void *producer_thread_fn(void *arg)
{
    long ret;
    printf("[Producer %ld] started.\n", (long)arg);

    ret = syscall(SYS_producer_function);
    if (ret != 0) {
        printf("[Producer %ld] syscall producer_function failed: ret=%ld, errno=%d\n",
               (long)arg, ret, errno);
    } else {
        printf("[Producer %ld] completed successfully.\n", (long)arg);
    }

    return NULL;
}

void *consumer_thread_fn(void *arg)
{
    long ret;
    printf("[Consumer %ld] started.\n", (long)arg);

    ret = syscall(SYS_consumer_function);
    if (ret != 0) {
        printf("[Consumer %ld] syscall consumer_function failed: ret=%ld, errno=%d\n",
               (long)arg, ret, errno);
    } else {
        printf("[Consumer %ld] completed successfully.\n", (long)arg);
    }

    return NULL;
}

void *auditor_thread_fn(void *arg)
{
    long ret;
    printf("[Auditor] started.\n");

    ret = syscall(SYS_auditor_function);
    if (ret != 0) {
        printf("[Auditor] syscall consumer_function failed: ret=%ld, errno=%d\n",
               ret, errno);
    } else {
        printf("[Auditor] completed successfully.\n");
    }

    return NULL;
}

int main()
{
    pthread_t producer_threads[NUM_PRODUCERS];
    pthread_t consumer_threads[NUM_CONSUMERS];
	pthread_t auditor_thread;
    long ret;
    int i;

    // buffer initialize
    printf("Initializing arrays (syscall array_init)...\n");
    ret = syscall(SYS_array_init);
    if (ret != 0) {
        printf("Error: array_init failed (ret=%ld, errno=%d)\n", ret, errno);
        return -1;
    }
    printf("Arrays initialized successfully.\n\n");

    // create producer threads
    for (i = 0; i < NUM_PRODUCERS; i++) {
        if (pthread_create(&producer_threads[i], NULL, producer_thread_fn, (void*)(long)(i+1)) != 0) {
            printf("Error: failed to create producer thread %d\n", i+1);
            return -1;
        } else {
            printf("Successfully created producer thread %d\n", i+1);
        }
    }
    printf("\n");

    // create consumer threads
    for (i = 0; i < NUM_CONSUMERS; i++) {
        if (pthread_create(&consumer_threads[i], NULL, consumer_thread_fn, (void*)(long)(i+1)) != 0) {
            printf("Error: failed to create consumer thread %d\n", i+1);
            return -1;
        } else {
            printf("Successfully created consumer thread %d\n", i+1);
        }
    }
    printf("\n");

    // wait until producer threads are finished
    for (i = 0; i < NUM_PRODUCERS; i++) {
        pthread_join(producer_threads[i], NULL);
    }
    printf("All producer threads have finished.\n\n");

    // wait until consumer threads are finished
    for (i = 0; i < NUM_CONSUMERS; i++) {
        pthread_join(consumer_threads[i], NULL);
    }
    printf("All consumer threads have finished.\n\n");
	
	//Auditor thread, runs after everything else finishes
	if (pthread_create(&auditor_thread, NULL, auditor_thread_fn, NULL) != 0) {
        printf("Error: failed to create auditor thread\n");
        return -1;
    } else {
        printf("Successfully created auditor thread\n");
    }

    // wait until auditor thread is finished
    pthread_join(auditor_thread, NULL);
    printf("Auditor thread has finished.\n\n");

    // clean up
    printf("Cleaning up arrays (syscall array_cleanup)...\n");
    ret = syscall(SYS_array_cleanup);
    if (ret != 0) {
        printf("Error: array_cleanup failed (ret=%ld, errno=%d)\n", ret, errno);
        return -1;
    }
    printf("Arrays cleaned up successfully.\n\n");

    return 0;
}

