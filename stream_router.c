//  Multithreaded Stream Router

#include <arpa/inet.h>
#include <assert.h>
#include <getopt.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
// #include <mpi.h>

#include "zmq.h"
#include "czmq.h"

#include "stream_tools.h"

int do_delay = 0;
int do_debug = 0;
int do_stats = 0;
int worker_threads = 1;
int keep_going = 1;
int zmq_mode = 0;
int mpi_mode = 0;
int server_socket;
char *publisher = "tcp://*:5556";
void *out_queue;
void *publish_socket;

typedef struct worker_thread_context {
    char name[64];
    int socket;
    pthread_t thread;
    void *zmq_context;
} worker_thread_context_t;

void buf_free(void *buf) {
    // Buffer was allocated with Malloc(), free it with free()
    if (do_debug > 0)
        printf("call free\n");
    free(buf);
}

void *output_thread(void *arg) {
    printf("Output thread starts -------\n");
    while (keep_going) {
        stream_buffer_t *buf = stream_queue_get(out_queue);
        if (buf == NULL)
            break;
        if (zmq_mode) {
            int n = zmq_send(publish_socket, buf, buf->total_length, 0);
            if (n == -1) {
                perror("zmq_send error");
                break;
            }
        }
        // Done with this buffer
        free(buf);
    }
    printf("Output thread ends -------\n");
    return (NULL);
}

void *worker_routine(void *arg) {
    worker_thread_context_t *ctx = arg;
    ctx->thread = pthread_self();
    int looping = 1;
    uint32_t magic, source_id;
    uint64_t data_counter, loop_counter;
    struct timespec tStart, tEnd, tDiff;
    // First thing on the socket is the magic number
    read(ctx->socket, &magic, 4);
    if (magic != CODA_MAGIC) {
        printf("*** Spurious connect attempt *** : magic read %08x\n", magic);
    }
    else {
        // Second thing on the thread is the source ID
        int nr = read(ctx->socket, &source_id, 4);
        if (nr != 4) {
            printf("expected ID but only read %d bytes\n", nr);
            return (NULL);
        }
        sprintf(ctx->name, "%08X", source_id);
        printf("Worker thread %s starts -------\n", ctx->name);
        // If we ever exit the loop and buf != NULL then we must free it.
        stream_buffer_t *buf = NULL;
        data_counter = 0;
        loop_counter = 0;
        clock_gettime(CLOCK_REALTIME, &tStart);
        while (looping && keep_going) {
            // stream_buffer_t *buf = (stream_buffer_t *) stream_queue_get(ctx->queue);
            // Read the ID from the block header.
            int nread;
            uint32_t block_length;
            uint8_t *lptr;
            // Read the ID
            nread = 0;
            lptr = (uint8_t *) &source_id;
            if (do_debug > 0)
                printf("Read the ID - 4 bytes \n");
            while (nread < 4) {
                int n = read(ctx->socket, lptr + nread, 4 - nread);
                if (n <= 0) {
                    looping = 0;
                    break;
                }
                nread += n;
            }
            if (!looping || !keep_going)
                break;
            if (do_debug > 0)
                printf(" \tID = %08X\n", source_id);
            // Read the length
            nread = 0;
            lptr = (uint8_t *) &block_length;
            if (do_debug > 0)
                printf("read overall length\n");
            while (nread < 4) {
                int n = read(ctx->socket, lptr + nread, 4 - nread);
                if (n <= 0) {
                    looping = 0;
                    break;
                }
                nread += n;
            }
            if (!looping || !keep_going)
                break;
            if (do_debug > 0)
                printf("\tlength = %d\n", block_length);
            // Here we take ownership of memory so we have to free it somewhere.
            buf = (stream_buffer_t *) malloc(block_length);
            buf->total_length = block_length;
            buf->source_id = source_id;
            nread = 8;
            uint8_t *bptr = (uint8_t *) buf;
            while (nread < block_length) {
                int n = read(ctx->socket, bptr + nread, block_length - nread);
                if (n <= 0) {
                    looping = 0;
                    // Here we free the buffer id something goes wrong
                    break;
                }
                nread += n;
            }
            if (!looping || !keep_going)
                break;
            // Handle statistics...
            data_counter += nread;
            loop_counter++;
            clock_gettime(CLOCK_REALTIME, &tEnd);
            time_subtract(&tDiff, &tEnd, &tStart);
            double tDiffDouble = ((float) tDiff.tv_sec) + ((float) tDiff.tv_nsec / 1000000000.0);
            if (do_stats && (tDiffDouble > 10.0)) {
                double loop_rate, data_rate;
                loop_rate = ((float) loop_counter) / tDiffDouble;
                data_rate = ((float) data_counter) / (tDiffDouble * 1000000000.0); // GByte/s
                printf("ID %08X - buffer rate %.2f Hz, data rate %.6f GByte/s \n",
                        buf->source_id, loop_rate, data_rate);
                data_counter = 0;
                loop_counter = 0;
                clock_gettime(CLOCK_REALTIME, &tStart);
            }
            if (do_debug > 0)
                printf("read %d bytes of data\n", nread);
            if (buf->magic != CODA_MAGIC)
                printf("Magic number error %08x should be %08x\n", buf->magic, CODA_MAGIC);
            if (do_debug > 1)
                print_data_hex((uint8_t *) buf, buf->total_length);
            if (do_debug > 0)
                printf("read ID from block header %08X\n", buf->source_id);
            if (buf->source_id != source_id) {
                printf("*** ID from block header %08X != ID from connect %08X\n", buf->source_id, source_id);
                break;
            }
            if (do_debug > 0)
                printf("Add buffer to output stream\n");
            // we give up ownership of the buffer
            stream_queue_add(out_queue, buf);
            buf = NULL;
        }
        if (buf != NULL) {
            printf("Worker thread %s left the main thread and buf != NULL, so free(buf)\n", ctx->name);
            free(buf);
        }
    }
    printf("Worker thread %s ends -------\n", ctx->name);
    shutdown(ctx->socket, SHUT_RDWR);
    free(ctx);
    return 0;
}

void cc_handler(int signum) {
    keep_going = 0;
    close(server_socket);
    shutdown(server_socket, SHUT_RDWR);
    printf ("\nStream shutdown due to signal %s\n", sys_siglist[signum]);
}

// Give the poor user some help on command line options.
void print_options(char *pname) {
    printf("usage:\t%s [-v] [-t target] [-p port] [-u url]\n", pname);
    printf("\t-v: verbose\n");
    printf("\t-z: use zmq for output\n");
    //printf("\t-m: use mpi for output\n");
    printf("\t-s: print statistics every 10s\n");
    printf("\t-p <port>: specify a port [default: 5555]\n");
    printf("\t-u <url>: specify url to publish on [default: tcp://*:5556]\n");
}

int main(int argc, char **argv) {
    // default address to listen to
    printf("%s starts\n", argv[0]);
    int target_port = 5555;
    printf("Initializing -------\n");
    if (argc >1) {
        printf("\tExecuting with command line options\n\t");
        char opt;
        while ((opt = getopt(argc, argv, "mzvp:su:")) != -1) {
            switch (opt) {
                case 'v':
                    // Log to stdout
                    //zsys_set_logstream(stdout);
                    do_debug++;
                    break;
                case 'z':
                    zmq_mode = 1;
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
                        printf("%s exits\n", argv[0]);
                        exit(0);
                    }
                }
                    break;
                case 's':
                    // Send to this port.
                    do_stats = 1;
                    break;
                case 'u':
                    publisher = strdup(optarg);
                    break;
                default:
                    print_options(argv[0]);
                    printf("%s exits\n", argv[0]);
                    return (0);
            }

        }
    }
    else printf("\tExecuting with no command line options\n\t   Using default settings\n");
    printf("TCP stream input port %d\n\t", target_port);
    if (!zmq_mode) printf("NOT Publishing using ZMQ\n\t");
    else printf("Publishing using ZMQ using URL %s\n\t", publisher);
    if (!do_stats) printf("NOT ");
    printf("printing stats every 10 seconds.\n");
    printf("-------\n\n");
    //  Socket to receive from to sources
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        printf("%s exits -> ", argv[0]);
        perror("Can't open socket\n");
        exit(1);
    }
    struct sockaddr_in sin;
    bzero(&sin, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    sin.sin_port = htons(target_port);
    if (bind(server_socket, (struct sockaddr *) &sin, sizeof(sin)) < 0) {
        printf("%s exits -> ", argv[0]);
        perror("bind error\n");
        exit(1);
    }
    printf("\tlistening on port %d for incoming stream connections\n",target_port);
    if (listen(server_socket, 5) < 0) {
        printf("%s exits -> ", argv[0]);
        perror("listen failed\n");
        exit(1);
    }
    signal(SIGINT, cc_handler);
    if (zmq_mode) {
        // Initialize zmq
        zsys_init();
        void *context = zmq_ctx_new();
        int maj, min, pat;
        zmq_version(&maj, &min, &pat);
        printf("\t Will publish data using ZMQ version - %d.%d.%d\n", maj, min, pat);
        // Create a ZMQ publish socket
        publish_socket = zmq_socket(context, ZMQ_PUB);
        if (zmq_bind(publish_socket, publisher) == -1) {
            printf("%s exits -> ", argv[0]);
            perror("zmq_bind error :");
            exit(-1);
        }
    }
    out_queue = stream_queue_create(100);
    pthread_t output;
    pthread_create(&output, NULL, output_thread, (void *) NULL);
    signal(SIGINT, cc_handler);
    while (keep_going) {
        struct sockaddr_in from;
        int slen = sizeof(from);
        bzero((char *) &from, slen);
        int connection = accept(server_socket, (struct sockaddr *) &from, (socklen_t *) &slen);
        if (connection > 0) {
            printf("We got a connection from %s\n", inet_ntoa((struct in_addr) from.sin_addr));
            printf("fire up a thread to handle it,\n");
            worker_thread_context_t *thread_context;
            // Create a worker thread structure
            thread_context = (worker_thread_context_t *) malloc(sizeof(worker_thread_context_t));
            assert(thread_context != 0);
            bzero(thread_context, sizeof(worker_thread_context_t));
            pthread_t worker;
            thread_context->socket = connection;
            pthread_create(&worker, NULL, worker_routine,
                           (void *) thread_context);
        }
        else break;
    }
    close(server_socket);
    printf("%s exits\n", argv[0]);
    exit(0);
}
