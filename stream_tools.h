/*
* stream_tools.h
*
*  Created on: Feb 21, 2019
*      Author: heyes
*/

#include <stddef.h>
#include <stdint.h>
#include <assert.h>
#include <time.h>
#include <stdbool.h>

#ifndef STREAM_TOOLS_H_
#define STREAM_TOOLS_H_

#define FALSE 0
#define TRUE 1

#define CODA_MAGIC 0xC0DA2019

#define STREAM_FORMAT 0x0101

typedef struct stream_buffer {
    uint32_t source_id;
    uint32_t total_length;
    uint32_t payload_length;
    uint32_t compressed_length;
    uint32_t magic;
    uint32_t format_version;
    bool     end_of_file;
    uint64_t record_counter;
    struct timespec timestamp;
    uint32_t payload[];
} stream_buffer_t;

uint32_t stream_to_int(uint8_t *buf);

void int_to_stream(uint8_t *buf, uint32_t data);

void print_data_hex(uint8_t *buf, int len);

// Set the value of result to the difference between t1 and t2.
// Return 1 if the difference is negative, otherwise 0.

int time_subtract(struct timespec *result, struct timespec *t2,
                  struct timespec *t1);

// Print a time value.
void time_print(struct timespec *tv);

typedef struct ringBuffer {
    char name[64];
    char max_length;
    void **buffer;
    uint64_t writePosition;
    uint64_t readPosition;
    size_t size;
} stream_rb_t;

//init
struct ringBuffer *stream_queue_create(int size);

//producer
void stream_queue_add(struct ringBuffer *buf, void *newValue);

//consumer
void *stream_queue_get(struct ringBuffer *buf);

void stream_queue_destroy(struct ringBuffer *buf);

#endif /* STREAM_TOOLS_H_ */
