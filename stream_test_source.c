//
/*
 * stream_test_source.c
 *
 *  Created on: Feb 14, 2019
 *      Author: heyes
 *      Based on ye olde blaster.c that has been kicking around for 25 years or more.

 * This version sends a compressed or uncompressed data payload using TCP to a target
 * host and port specified on the command line.
 *
 * The code loops sending a number of buffers and times how long that takes. This process is
 * repeated several times and the statistics stored. When all is done the statistics for the
 * total number of loops is converted into a mean and standard deviation. 
 *
 * Note: in this context a "loop" sends a single buffer. A "cycle" is a number of loops over 
 * which the statistics are summed. The -n option sets the number of loops per cycle and the 
 * -l option sets the total number of cycles in the test, both are mandatory.
 * 
 * Data is sent over the network in records, a header followed by a payload.
 * The header is defined in stream_tools.h :
 *
 * uint 32 - Total length of this recort in units of 4 bytes.
 * uint 32 - Uncompressed payload length.
 * uint 32 - Compressed payload length. If this equals the previous word paylod not compressed.
 * uint 32 - record number - wraps at 32 bit.
 * struct timespec - timestamp
 * uint 32 Payload
 *

 */

// Don't blame me for this list of includes, Eclipse generates it.
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <getopt.h>
#include <math.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

#include "stream_tools.h"

// We need to store the rates for each cycle. Set up a couple of arrays of doubles.
// The number of cycles is set by a command line option so malloc storage later.
int cycle_count = 0;
double *data_rates, *block_rates;

// We will set up a pool of reusable buffers. This is faster and safer than malloc.
stream_rb_t *free_buffer_queue;

// If we do compression the queued buffers may become corrupted. We keep a master copy
// so that we can use bcopy to refresh the buffers in the pool.
stream_buffer_t *master_data;

// Thread loops keep going until keep_going = 0
int keep_going = TRUE;

// Default host is localhost
char *target_host = "localhost";

int source_id = 0xC0DA0001;

// Default port is 5555
int target_port = 5555;

// socket to send on
int target_socket;

// By default we only send one buffer. Overridden by the -n option
int loops_per_cycle = 1;

// By default we calculate rates after every buffer.
int total_cycles = 1;

// Default to debug off. Each occurrence of -v on the command line increments do_debug.
int do_debug = 0;
int do_scan = 0;

// Default to only send 40 bytes.
int payload_length = 10;

void print_rate(struct timespec *tv, uint32_t length) {
    // local variables
    struct timespec tvEnd, tvDiff;
    double loop_rate, data_rate;
    // calculate rates
    clock_gettime(CLOCK_REALTIME, &tvEnd);
    time_subtract(&tvDiff, &tvEnd, tv);
    // only store 10000 data and block rate entries
    if (cycle_count % 10000 == 0) cycle_count = 0;
    block_rates[cycle_count]  = loop_rate = ((float) loops_per_cycle) /
                                            ((float) tvDiff.tv_sec + ((float) tvDiff.tv_nsec / 1000000000.0));
    data_rates[cycle_count++] = data_rate = ((float) length * loop_rate) / 1000000000.0;
    // print rates
    printf("buffer size = %d bytes, buffer rate = %.2f Hz, data rate = %.6f GByte/s\n", length, loop_rate, data_rate);
}

void print_final_stats() {
    // local variables
    double block_rate = 0.0, data_rate = 0.0, sd = 0.0;
    int i;
    // calculate summary rates
    for (i = 0; i < cycle_count; i++) {
        block_rate += block_rates[i];
        data_rate += data_rates[i];
    }
    block_rate /= cycle_count;
    data_rate /= cycle_count;
    for (i = 0; i < cycle_count; i++)
        sd += pow(data_rate - data_rates[i], 2);
    sd /= cycle_count;
    sd = sqrt(sd);
    printf("%.2f Hz, %.6f +/- %.2f GByte/s \n", block_rate, data_rate, sd);
}

// Give the poor user some help on command line options.
void print_options(char *pname) {
    printf("usage: %s [-vc] [-t target] [-f file] [-p port] [-n buffers] [-l loops] [-b bytes] [-r rate]\n", pname);
    printf("\t-v: verbose\n");
    printf("\t-c: compress data before send\n");
    printf("\t-t <target>: specify a host [default: \"localhost\"]\n");
    printf("\t-f <file>: read source data from a file [default: random data]\n");
    printf("\t-p <port>: specify a port [default: 5555]\n");
    printf("\t-n <buffers>: number of buffers per loop\n");
    printf("\t-l <loops>: total number of loops\n");
    printf("\t-b <bytes>: bytes per data packet\n");
    printf("\t-r <rate>: rate in kbyte/s\n");
    printf("\t-s: scan mode - divide b by \n");
}

typedef struct compression_stream {
    stream_rb_t *in_queue;
    stream_rb_t *out_queue;
} compression_stream_t;

/*void *compression_thread(void *arg) {
	compression_stream_t *cs = (compression_stream_t *) arg;

	while (keep_going) {
		uint8_t *in_data = (uint8_t *) stream_queue_get(cs->in_queue);
		uint8_t *buffer_out;
		uint32_t payload_length = stream_to_int(in_data);

		// We reserve 3 x 32-bit words to hold lengths - see later...
		if ((buffer_out = (uint8_t *) malloc(
				snappy_max_compressed_length(payload_length + 12) + 12)) == NULL) {
			printf("cannot allocate buffer of %d bytes\n",
					(int) snappy_max_compressed_length(payload_length + 12)
							+ 12);
			exit(1);
		}

		size_t out_length = snappy_max_compressed_length(payload_length);
		int res = snappy_compress(&env, (char *) &in_data[8],
				payload_length - 8, (char *) &buffer_out[8], &out_length);

		// Fill in the two length words..
		// 1) Current total length of the buffer
		// 2) Payload uncompressed length
		// 3) Payload compressed length.

		//printf("Snappy, before %p %d after %p %d\n", buffer, payload_length, buffer_out, out_length);
		if (res != 0) {
			printf("Snappy not Happy %d, to before %d after %d\n", res,
					payload_length, (int) out_length);
			exit(-res);
		}
	}
	return 0;
}*/

void *writer_thread(void *arg) {
    stream_rb_t *in = (stream_rb_t *) arg;
    uint32_t magic = CODA_MAGIC;
    /* The first thing down a newly opened socket is the magic number
     * The second thing down a newly opened socket is the unique ID of the sender.
     */
    int n = write(target_socket, &magic, 4);
    if (n != 4) {
        perror("write error or wrote less than 4 bytes: ");
        return NULL;
    }
    n = write(target_socket, &source_id, 4);
    if (n != 4) {
        perror("write error or wrote less than 4 bytes: ");
        return NULL;
    }
    // If both writes succeed then we assume all is well and start sending data.
    while (keep_going) {
        stream_buffer_t *buf = stream_queue_get(in);
        if (buf == (stream_buffer_t *) -1) break;
        // Total record length is always padded to 4 byte boundary
        int out_length = buf->total_length; // this is in bytes
        if (do_debug > 0) {
            printf("Writer for has data\n");
            print_data_hex((uint8_t *) buf, out_length);
        }
        int data_sent = 0;
        int try_send = out_length;
        while (data_sent < out_length) {
            if (do_debug > 1)
                printf("send remaining %d bytes of %d", try_send, out_length);
            int nSent = write(target_socket, buf + data_sent, try_send);
            if (nSent < 0) {
                perror("write error during data write: ");
                //print_data_hex((uint8_t *) buf, out_length);
                break;
            }
            if (nSent != try_send) {
                printf("Sent %d bytes out of %d\n", nSent, try_send);
            } 
            else
                break;
            data_sent += nSent;
            try_send -= nSent;
        }
        stream_queue_add(free_buffer_queue, buf);
    }
    printf("Send thread exits\n");
    return 0;
}

int main(int argc, char *argv[]) {
    // Define local variables
    struct sockaddr_in internet_address;
    struct timespec delay;
    delay.tv_nsec = delay.tv_sec = 0;
    char *data_file = NULL;
    //int do_compress = TRUE;
    // Get the command line arguments
    char opt;
    while ((opt = getopt(argc, argv, "svcf:r:i:h:o:p:n:b:l:")) != -1) {
        switch (opt) {
            case 's':
                // Turn on debugging and log to stdout
                do_scan = 1;
                printf("Scan mode turned on\n");
                break;
            case 'v':
                // Turn on debugging and log to stdout
                do_debug = 1;
                printf("Debug turned on\n");
                break;
            case 'c':
                // Turn on compression
                //do_compress = 1;
                printf("Compression turned on - but not implemented\n");
                break;
            case 'h':
                // Set the host for this source to send to
                printf("Host=%s\n", optarg);
                target_host = strdup(optarg);
                break;
            case 'i':
                // Set the host for this source to send to
                printf("ID=%s\n", optarg);
                source_id = strtol(optarg, NULL, 0);
                break;
            case 'f':
                // Set the host for this source to send to
                printf("Data Source File=%s\n", optarg);
                data_file = strdup(optarg);
                break;
            case 'p':
                // Send to this port.
            {
                char *err;
                target_port = strtol(optarg, &err, 10);
                // Catch common errors
                if ((err == optarg) ||
                    (*err != '\0') ||
                    (target_port < 1024) ||
                    (target_port > 65535)) {
                    printf("invalid port number = %s\n", optarg);
                    exit(0);
                }
                printf("send to port %d\n", target_port);
            }
                break;
            case 'n':
                // Send more than once and time each packet.
                loops_per_cycle = atoi(optarg);
                if (loops_per_cycle <= 0) {
                    printf("invalid loop count = %s\n", optarg);
                    exit(0);
                }
                printf("loop %d times\n", loops_per_cycle);
                break;
            case 'l':
                // Send more than once and time each packet.
                total_cycles = atoi(optarg);
                if (total_cycles <= 0) {
                    printf("invalid number of cycles = %s\n", optarg);
                    exit(0);
                }
                printf("Will measure rates %d times\n", total_cycles);
                break;
            case 'b':
                // payload_length is in words
                payload_length = atoi(optarg) / 4;
                if (payload_length <= 0) {
                    printf("invalid payload_length = %s, must be > 0.\n", optarg);
                    exit(0);
                }
                printf("send %d bytes per message\n", payload_length * 4);
                break;
            case 'r': {
                int rate = atoi(optarg);
                if (rate == 0) {
                    printf("invalid rate = %s.\n", optarg);
                    exit(0);
                }
                int brate = rate * 1000 / (4 * payload_length); // buffers per second
                delay.tv_sec = 0;
                delay.tv_nsec = 1000000000 / brate; // nanoseconds per buffer
                printf("send at %d kbytes per second, %d Hz\n", rate, brate);
                break;
            }
            default:
                print_options(argv[0]);
                return (0);
        }
    }
    // Grab a socket and tune it for performance.
    target_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (target_socket < 0) {
        printf("cannot open socket\n");
        exit(2);
    }
    // Size of socket's buffers
    int sockbufsize = 100000;
    printf("socket buffer size =%d\n", sockbufsize);
    // Set the socket buffer size
    if (setsockopt(target_socket, SOL_SOCKET, SO_SNDBUF, &sockbufsize, sizeof(sockbufsize)) < 0) {
        printf("setsockopt SO_SNDBUF failed\n");
        exit(1);
    }
    /*optval = 1;
     if (setsockopt(target_socket, IPPROTO_TCP, TCP_NODELAY, &optval,
     sizeof(optval)) < 0) {
     printf("setsockopt TCP_NODELAY failed\n");
     free(buffer);
     exit(1);
     }*/
    // hostdb entry for this target if hostname is given
    struct hostent *host_entry; 
    // Call gethostbyname() to convert string into host_entry from hostdb.
    host_entry = gethostbyname(target_host);
    // Set up the socket that we will use for sending.
    // This is an important step. We fill in parts of the address
    // parts that we do not touch must be empty.
    bzero((char *) &internet_address, sizeof(internet_address));
    // If host_entry is null then target_host was probably an IP address in dot notation.
    if (host_entry == NULL) {
        internet_address.sin_addr.s_addr = inet_addr(target_host);
        if (internet_address.sin_addr.s_addr == -1) {
            fprintf(stderr, "%s: unknown host\n", target_host);
            exit(2);
        }
    } 
    else {
        // Found argv[1] in hostdb!
        // Print the name from  host_entry
        printf(">>> hostname >%s<\n", host_entry->h_name);
        // copy the address
        bcopy(host_entry->h_addr, &internet_address.sin_addr, host_entry->h_length);
    }
    // put in the port
    internet_address.sin_port = htons(target_port);
    internet_address.sin_family = AF_INET;
    // Finally, attempt to connect our socket to the target host and port.
    if (connect(target_socket, (const struct sockaddr *) &internet_address, sizeof(internet_address)) < 0) {
        perror("connect");
        printf("connect failed: host %s port %d\n",
                inet_ntoa(internet_address.sin_addr), ntohs(internet_address.sin_port));
        exit(1);
    }
    printf("connected and preparing to send...\n");
    // Done setting up socket
    // Set up queues.
    printf("Creating buffer pool %d buffers\n", 4);
    // We are going to have a pool of four prefilled buffers created by copying one master.
    free_buffer_queue = stream_queue_create(4);
    // Allocate a stream_buffer to hold a master copy of the data.
    int request_length = (payload_length * 4) + sizeof(stream_buffer_t);
    request_length = ((request_length + 3) / 4) << 2;
    printf("Data buffers will be %d bytes long\n", request_length);
    if ((master_data = (stream_buffer_t *) malloc(request_length)) == NULL) {
        printf("cannot allocate buffer of %d bytes\n", request_length);
        exit(1);
    }
    // construct the master buffer protocol
    bzero(master_data, request_length);
    master_data->source_id = source_id;
    master_data->total_length = request_length; // total buffer length in bytes
    master_data->payload_length = payload_length * 4; // length of data payload in bytes
    master_data->compressed_length = payload_length * 4; // equals payload_length so uncompressed.
    master_data->magic = CODA_MAGIC;
    // We can fill the master copy from a file if one is specified, otherwise random numbers.
    // Was a data file specified on command line?
    int ix, of, cf;
    if (data_file == NULL) {
        printf("Filling data source buffer with random numbers\n");
        // No, so fill buffer with random words
        for (ix = 0; ix < payload_length; ix++)
            master_data->payload[ix] = rand();
    } 
    else {
        // Yes, so open data file
        printf("Filling data source buffer with %d bytes from file %s\n",
                (int) master_data->payload_length, data_file);
        of = open(data_file, O_RDONLY);
        if (!of) {printf("Error opening file %s\n", data_file); exit(-1);}
    }
    // Pop four copies of master_data on "free buffer" queue...
    for (ix = 0; ix < 4; ix++) {
        char *tmp = malloc(master_data->total_length);
        if (tmp == NULL) {
            printf("cannot allocate buffer of %d bytes\n", request_length);
            exit(-1);
        }
        bcopy(master_data, tmp, master_data->total_length);
        stream_queue_add(free_buffer_queue, tmp);
    }
    // We are going to time things to see how fast they are.
    struct timespec tvBegin, tvStartBlock;
    //snappy_init_env(&env);
    // Set tvBegin to the current time.
    clock_gettime(CLOCK_REALTIME, &tvBegin);
    clock_gettime(CLOCK_REALTIME, &tvStartBlock);
    // Set up the queues etc for the writer and compression threads.
    // We will have four data compression threads, create management structures
    int out_depth = 4;
    // compression_stream_t compressors[out_depth];
    struct ringBuffer *out_queue = stream_queue_create(out_depth);
    pthread_t writer_pthread_id;
    pthread_create(&writer_pthread_id, NULL, writer_thread, (void *) out_queue);
    /*if (do_compress) {
        int i;
        // Need some queues and some threads each with an in and out queue...
        for (i = 0; i < out_depth; i++) {
            compressors[i].in_queue = stream_queue_create(in_depth);
            compressors[i].out_queue = out_queue;
            pthread_t worker;
            pthread_create(&worker, NULL, compression_thread,
                    (void *) &compressors[i]);
        }
    }*/
    // Loop sending batches of buffers and measure rate between batches
    int buf_count;
    // save start time
    struct timespec time_then;
    bcopy(&tvStartBlock, &time_then, sizeof(struct timespec));
    uint32_t current_length = master_data->total_length / total_cycles;
    current_length = ((current_length + 3) / 4) << 2;
    // if there no data file, handle it
    if (data_file == NULL) {
        // store the data rates for total_cycles (-n) buffers
        data_rates  = (double *) malloc(total_cycles * sizeof(double));
        block_rates = (double *) malloc(total_cycles * sizeof(double));
        for (buf_count = 0; buf_count < total_cycles * loops_per_cycle; buf_count++) {
            // pop new free buffer off the queue
            stream_buffer_t *buf;
            // pull an "incoming" buffer off the queue
            buf = stream_queue_get(free_buffer_queue);
            buf->record_counter = buf_count;
            // acquire the clock time
            clock_gettime(CLOCK_REALTIME, &buf->timestamp);
            if (do_scan) buf->total_length = current_length;
            // Put it on the outgoing queue.
            stream_queue_add(out_queue, buf);
            // print rate diagnostics and handle the timing
            if ((buf_count != 0) && ((buf_count % loops_per_cycle) == 0)) {
                print_rate(&tvStartBlock, master_data->total_length);
                current_length += master_data->total_length / total_cycles;
                current_length = ((current_length + 3) / 4) << 2;
                if (do_scan && (current_length > master_data->total_length)) {
                    printf("doing silly break\n");
                    break;
                }
                clock_gettime(CLOCK_REALTIME, &tvStartBlock);
            }
            if (delay.tv_nsec > 0)
                nanosleep(&delay, NULL);
        }
        // print rate diagnostics
        if (buf_count > 1) print_rate(&tvStartBlock, master_data->total_length);
    } // no data file condition
    // if there is a data file, handle it
    if (data_file != NULL) {
        // store the data rates for only 10000 buffers
        data_rates  = (double *) malloc(10000*sizeof(double));
        block_rates = (double *) malloc(10000*sizeof(double));
        // local variables
        stream_buffer_t *fbuf;
        int nread, buf_cntr = 0;
        // pop new free buffer off the queue
        fbuf = stream_queue_get(free_buffer_queue);
        // read from file and assign payload to file buffer
        nread = read(of, fbuf->payload, (int) fbuf->payload_length);
        // sanity check that read size is identical to payload size
        if (nread != (int) fbuf->payload_length)
            printf("\nonly read %d of %d from data file\n", nread, (int) fbuf->payload_length);
        // loop over the data file
        while (nread != 0) {
            // acquire the clock time
            clock_gettime(CLOCK_REALTIME, &fbuf->timestamp);
            // add file buffer to queue and iterate record counter
            stream_queue_add(out_queue, fbuf);
            // increment the buffer counter
            buf_cntr++;
            fbuf->record_counter = buf_cntr;
            // print rate diagnostics
            if ((buf_cntr < 10) || (buf_cntr % 1000 == 0)) {
                printf("\nbuffer counter = %d, ", (int) fbuf->record_counter);
                print_rate(&tvStartBlock, master_data->total_length);
            }
            current_length += master_data->total_length / total_cycles;
            current_length = ((current_length + 3) / 4) << 2;
            clock_gettime(CLOCK_REALTIME, &tvStartBlock);
            // pop new free buffer off queue
            fbuf = stream_queue_get(free_buffer_queue);
            // read next chunk of file and assign it to the file buffer
            nread = read(of, fbuf->payload, (int) fbuf->payload_length);
        }
        // close the file when eof is reached
        if (nread == 0) {
            cf = close(of);
            if(cf == -1) {printf("Error while closing file %s\n", data_file); exit(-1);}
        }
    } // data file condition
    // tell the writer thread to quit
    stream_queue_add(out_queue, (stream_buffer_t *) -1);
    void *retval;
    pthread_join(writer_pthread_id, &retval);
    close(target_socket);
    // print average rates
    printf("\nAverage rates : ");
    print_final_stats();
    printf("\nDone testing!\n");
}

