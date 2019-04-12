/*
 * stream_tools.c
 *
 *  Created on: Feb 21, 2019
 *      Author: heyes
 */

#include "stream_tools.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

uint32_t stream_to_int(uint8_t *buf) {
    uint32_t data = (((uint32_t) buf[3]) << 24) + (((uint32_t) buf[2]) << 16)
                    + (((uint32_t) buf[1]) << 8) + ((uint32_t) buf[0]);
    return data;
}

void int_to_stream(uint8_t *buf, uint32_t data) {
    buf[0] = data & 0xff;
    buf[1] = (data >> 8) & 0xff;
    buf[2] = (data >> 16) & 0xff;
    buf[3] = (data >> 24) & 0xff;
}

void print_data_hex(uint8_t *buf, int len) {
    int i;
    printf("Hex dump of buffer %p\n0000: ", buf);
    for (i = 0; i < len; i++) {
        printf("%02x", buf[i]);

        if ((i + 1) % 32 == 0)
            printf("\n%04d: ", i);
        else if ((i + 1) % 4 == 0)
            printf(" ");
    }
    printf("\n------\n");

}

// Set the value of result to the difference between t1 and t2.
// Return 1 if the difference is negative, otherwise 0.

int time_subtract(struct timespec *result, struct timespec *t2,
                  struct timespec *t1) {
    long int nano = 1000000000;
    long int diff = (t2->tv_nsec + nano * t2->tv_sec)
                    - (t1->tv_nsec + nano * t1->tv_sec);
    result->tv_sec = diff / nano;
    result->tv_nsec = diff % nano;

    return (diff < 0);
}

// Print a time value.
void time_print(struct timespec *tv) {
    char buffer[30];
    time_t curtime;

    printf("%ld.%09d", (long) tv->tv_sec, (int) tv->tv_nsec);
    curtime = tv->tv_sec;
    strftime(buffer, 30, "%m-%d-%Y  %T", localtime(&curtime));
    printf(" = %s.%09d\n", buffer, (int) tv->tv_nsec);
}

struct ringBuffer *stream_queue_create(int length) {
    //create the ring buffer
    struct ringBuffer *buf = calloc(1, sizeof(struct ringBuffer));
    buf->buffer = calloc(length, sizeof(void *));
    buf->size = length;
    return buf;
}

//producer
void stream_queue_add(struct ringBuffer *buf, void *newValue) {
    uint64_t writepos = __sync_fetch_and_add(&buf->writePosition, 1)
                        % buf->size;

    //spin lock until buffer space available
    while (!__sync_bool_compare_and_swap(&(buf->buffer[writepos]), NULL,
                                         newValue))
        if (usleep(10) < 0) usleep(10);

    //sem_post(buf->semaphore);
}

//consumer
void *stream_queue_get(struct ringBuffer *buf) {

    //sem_wait(buf->semaphore);
    //spin lock until buffer space available
    void *value = NULL;
    while ((value = buf->buffer[buf->readPosition % buf->size]) == NULL) {
        if (usleep(10) < 0) break;
    }
    buf->buffer[buf->readPosition % buf->size] = NULL;
    buf->readPosition++;

    return value;
}

void stream_queue_destroy(struct ringBuffer *buf) {
    free(buf->buffer);
    free(buf);
}
