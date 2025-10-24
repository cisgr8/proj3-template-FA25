#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/semaphore.h>
#include <linux/mutex.h>
#include <linux/slab.h>       // for kmalloc, kzalloc, kfree
#include <linux/fs.h>         // For filp_open, kernel_read, kernel_write
#include <linux/uaccess.h>    // For accessing user space
#include <linux/time64.h>     // For time in kernel
#include <linux/atomic.h>     // for atomic_t
#include "buffer.h"

#define LINE_BUFFER_SIZE 512	//max 512 for reading each line in the input file

// a mutex for output log
static DEFINE_MUTEX(output_log_mutex);

static struct file *shared_file = NULL;     // shared pointer for input file(3 producers)
static struct semaphore file_sem;           // semaphore for input file

struct circular_buffer *circ_buffer;
struct inventory_array *inv_arrays;
atomic_t producers_running;   		// how many active producers

//1-5 consumer process data to inventory array and quit, 
//the last consumer processes data and prints all content in inventory_arrays to output log
static atomic_t consumers_remaining;

static void random_sleep(int delay)
{
	char rand;
	get_random_bytes(&rand, sizeof(rand));
	fsleep(delay * (((unsigned char)rand)%16));
}

SYSCALL_DEFINE0(array_init)
{
    int i;

    // allocate memory for the circular buffer
    circ_buffer = kzalloc(sizeof(struct circular_buffer), GFP_KERNEL);
    if (!circ_buffer) {
        printk(KERN_ERR "Failed to allocate memory for circ_buffer\n");
        return -ENOMEM;
    }

    // allocate memory for inventory arrays
    inv_arrays = kzalloc(sizeof(struct inventory_array) * NUM_BOOKS, GFP_KERNEL);
    if (!inv_arrays) {
        printk(KERN_ERR "Failed to allocate memory for inv_arrays\n");
        kfree(circ_buffer);
        return -ENOMEM;
    }

    // initialize the mutexes and semaphores
    sema_init(&circ_buffer->mutex, 1);
    sema_init(&circ_buffer->full, 0);
    sema_init(&circ_buffer->empty, BUFFER_SIZE);
    circ_buffer->read = 0;
    circ_buffer->write = 0;
    circ_buffer->count = 0;

    // initialize inv_arrays
    for (i = 0; i < NUM_BOOKS; i++) {
        inv_arrays[i].count = 0;
		//TODO: initialize mutex for each inventory array
        //Comment to test conenction to github connections
        mutex_init(&inv_arrays[i].mutex);
    }

    // open input file
    shared_file = filp_open("input.txt", O_RDONLY, 0);
    if (IS_ERR(shared_file)) {
        printk(KERN_ERR "Failed to open input.txt in array_init\n");
        kfree(inv_arrays);
        kfree(circ_buffer);
        return PTR_ERR(shared_file);
    }

    // TODO: initialize input file semaphore
    sema_init(&file_sem, 1);

    // set producer_count = 3, consumer_count = 4
    atomic_set(&producers_running, NUM_PRODUCERS);
    atomic_set(&consumers_remaining, NUM_CONSUMERS);

    printk(KERN_INFO "Array initialized successfully\n");
    return 0;
}

SYSCALL_DEFINE0(array_cleanup)
{
    int i;

    // TODO: destroy mutexes for inventory arrays
	for(i = 0; i < NUM_BOOKS; i++){
        mutex_destroy(&inv_arrays[i].mutex); //Destory Mutex for each inventory arrays
    }

    //If file opened and the shared_file pointer isn't empty, close the file safely
    if (shared_file && !IS_ERR(shared_file)) {
        filp_close(shared_file, NULL);
        shared_file = NULL;
    }

    // free memory
    if (circ_buffer) {
        kfree(circ_buffer);
    }
    if (inv_arrays) {
        kfree(inv_arrays);
    }

    printk(KERN_INFO "Buffer cleaned up successfully\n");
    return 0;
}

SYSCALL_DEFINE0(producer_function)
{
    struct file *log_file;
    struct array_item item;
    char line[LINE_BUFFER_SIZE];//each line in input file
    ssize_t bytes_read;
    char c;						//how many characters we have read
    char log_msg[256];			//put characters to a line
    int idx, num, line_count = 0;
	struct timespec64 timestamp;

    printk(KERN_INFO "Producer function started\n");

    // open log file, O_APPEND=>write from the end of this file. 
    // and it is atomic, so we don't need to lock the producer log file.
    log_file = filp_open("producer_log.txt", O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (IS_ERR(log_file)) {
        printk(KERN_ERR "Failed to open producer_log.txt\n");
        return PTR_ERR(log_file);
    }
    printk(KERN_INFO "Producer log file opened successfully\n");

    while (1) { //continue reading from input file line by line untill end of the input file
        
		//TODO: semaphore operation
        down(&file_sem);

		//read a whole line 
        idx = 0;
        while (idx < LINE_BUFFER_SIZE - 1) {
            bytes_read = kernel_read(shared_file, &c, 1, &shared_file->f_pos);
            if (bytes_read <= 0)
                break;
            line[idx++] = c;
            if (c == '\n')
                break;
        }
        line[idx] = '\0'; //ending sign \0

		//TODO: semaphore operation
        up(&file_sem);

        if (bytes_read <= 0) {
            printk(KERN_INFO "Producer: End of input.txt or error\n");
            break;
        }

        line_count++;
        printk(KERN_INFO "Processing line %d: %s", line_count, line);

        // parse this line
        num = sscanf(line, "%d %d %d %d",
                     &item.input_id, &item.priority,
                     &item.sleep_interval, &item.book_id);
        if (num != 4) {
            printk(KERN_ERR "Invalid input format: %s", line);
            continue;
        }

        ktime_get_real_ts64(&timestamp);

        //write to circular buffer
        down(&circ_buffer->empty);//wait for empty slot
        down(&circ_buffer->mutex);//lock circular buffer

        circ_buffer->items[circ_buffer->write] = item;
        circ_buffer->write = (circ_buffer->write + 1) % BUFFER_SIZE;
        circ_buffer->count++;

        up(&circ_buffer->mutex);
        up(&circ_buffer->full);

		mutex_lock(&output_log_mutex);
        // write log
        snprintf(log_msg, sizeof(log_msg),
                 "Producer: Added entry %d [Priority: %d, Time: %llu, Book ID: %d]\n",
                 item.input_id, item.priority,
                 (unsigned long long)timestamp.tv_sec, item.book_id);
        kernel_write(log_file, log_msg, strlen(log_msg), &log_file->f_pos);
        mutex_unlock(&output_log_mutex);

        // simulate CPU processing
		random_sleep(item.sleep_interval);
    }

    //One producer is finished, producer count-=1
    atomic_dec(&producers_running);

    printk(KERN_INFO "Producer finished processing %d lines\n", line_count);

    filp_close(log_file, NULL);
    return 0;
}

SYSCALL_DEFINE0(consumer_function)
{
	//TODO: Open output_log.txt

    while (1) {
		if (down_trylock(&circ_buffer->full) != 0) { //if no full slot
            // if no full slot and no active producer, then consumer quit
            if (atomic_read(&producers_running) == 0) { 
                break;
            } else {
                msleep(10);
                continue;
            }
        }

        //TODO:
		//Remove next book from circular buffer and print to log - similar but opposite of producer
		//The full mutex and exit condition are taken care of above
		//At the appropriate Inventory index: Lock mutex, create entry for book, increment count, unlock mutex, write to log
    }
	//TODO: Close log and return
}

SYSCALL_DEFINE0(auditor_function)
{
	//TODO:
	//Open output_log.txt and lock mutex
	//Print all structs of each priority level, starting at priority 0 and going up
	//Use inv_arrays.count to avoid printing empty indices
	//Close log, unlock mutex, and return
}
