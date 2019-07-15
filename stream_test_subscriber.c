/*
 * test_subscriber.c
 *
 *  Created on: Mar 8, 2019
 *      Author: heyes
 */

#include <getopt.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zmq.h>
#include <fcntl.h>
#include <unistd.h>

#include "stream_tools.h"

int do_debug = 0;

char *data_file;

int of, wf, cf;

char *date(void) {
  time_t now = time(&now);
  struct tm *info = localtime(&now);
  char *text = asctime(info);
  text[strlen(text) - 1] = '\0'; // remove '\n'
  return (text);
}

void print_usage(char *pname) {
  printf("usage: %s [-v] [-u url] <key>\n\n", pname);
  printf("\t<key>: four byte hex source ID to match\n");
  printf("\t-v: increment debug level\n");
  printf("\t-f: name of file to write buffers too\n");
}

int main(int argc, char **argv) {
  // Handle command line arguments

  char opt;
  char *url = "tcp://127.0.0.1:5556";

  while ((opt = getopt(argc, argv, "vuf:")) != -1) {
    switch (opt) {
    case 'v':
      do_debug++;
      printf("Debug level %d\n", do_debug);
      break;
    case 'u':
      url = strdup(optarg);
      break;
    case 'f':
      printf("Writing file %s to current working directory\n", optarg);
      data_file = strdup(optarg);
      break;
    default:
      print_usage(argv[0]);
      return (0);
    } // opt switch
  } // while condition
  if (argc - optind < 1) {
    print_usage(argv[0]);
    exit(0);
  }

  uint32_t source_id = strtol(argv[optind], NULL, 0);

  void *context = zmq_init(1);
  void *socket = zmq_socket(context, ZMQ_SUB);
  
  // configure zmq socket
  printf("Subscribe to URL: %s\n", url);
  // create outgoing connection from socket
  int ret = zmq_connect(socket, url);
  if (ret < 0) {
    perror("zmq_connect error :");
    exit(0);
  }

  // set zmq socket options
  printf("Filter = %08x\n", source_id);
  ret = zmq_setsockopt(socket, ZMQ_SUBSCRIBE, &source_id, 4);
  if (ret < 0) {
    perror("zmq_setsockopt error :");
    exit(0);
  }

  // create the data file to write too
  if (data_file != NULL) {
    of = open(data_file, O_RDWR | O_CREAT);
    if (!of) {printf("Error opening file %s\n", data_file); exit(-1);}
  }
  
  // subscribe to zmq socket
  printf("Subscribing to data source %08X\n", source_id);
  for (;;) {

    // recieve a message part from a socket
    zmq_msg_t msg;
    zmq_msg_init(&msg);
    ret = zmq_recvmsg(socket, &msg, 0);
    if (ret < 0) {
      perror("zmq_recvmsg error :");
      exit(0);
    }

    // handle the buffer
    stream_buffer_t *buf = (stream_buffer_t *) zmq_msg_data(&msg);
    // print debug output
    if ((buf->record_counter < 10) || (buf->record_counter % 1000 == 0)) {
      // recieve message content size in bytes
      int size = zmq_msg_size(&msg);
      // print debug messages
      printf("id %08X == %08X length %d, counter %" PRIu64 "\n",
	     source_id, buf->source_id, size, buf->record_counter);
      if (do_debug > 0)
	print_data_hex((uint8_t *) buf, buf->total_length);
    } // buffer print condition

    // hand the data file
    if (data_file != NULL) {
      // write buffer payload to the open file
      wf = write(of, buf->payload, buf->payload_length);
      if (!wf) {printf ("Error while writing file %s\n", data_file); exit(-1);}
    } // data file condition

    // release the message
    zmq_msg_close(&msg);
  } // infinite for loop

  // close the open file
  cf = close(of);
  if(cf == -1) {printf("Error while closing file %s\n", data_file); exit(-1);}

} // main

