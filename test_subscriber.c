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

#include "stream_tools.h"

int do_debug = 0;

char *
date(void) {
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

}

int main(int argc, char **argv) {
	// Handle command line arguments

	char opt;
	char *url = "tcp://127.0.0.1:5556";

	while ((opt = getopt(argc, argv, "vu:")) != -1) {
		switch (opt) {
		case 'v':
			do_debug++;
			printf("Debug level %d\n", do_debug);
			break;
		case 'u':
			url = strdup(optarg);
			break;
		default:
			print_usage(argv[0]);

			return (0);
		}

	}

	if (argc - optind < 1) {

		print_usage(argv[0]);
		exit(0);
	}

	uint32_t source_id = strtol(argv[optind], NULL, 0);

	void *context = zmq_init(1);
	void *socket = zmq_socket(context, ZMQ_SUB);

	printf("Subscribe to URL: %s\n", url);
	int ret = zmq_connect(socket, url);
	if (ret < 0) {
		perror("zmq_connect error :");
		exit(0);
	}
	printf("Filter = %08x\n", source_id);
	ret = zmq_setsockopt(socket, ZMQ_SUBSCRIBE, &source_id, 4);
	if (ret < 0) {
		perror("zmq_setsockopt error :");
		exit(0);
	}

	printf("Subscribing to data source %08X\n", source_id);

	for (;;) {
		zmq_msg_t msg;

		zmq_msg_init(&msg);

		ret = zmq_recvmsg(socket, &msg, 0);
		if (ret < 0) {
			perror("zmq_recvmsg error :");
			exit(0);
		}

		stream_buffer_t *buf = (stream_buffer_t *) zmq_msg_data(&msg);

		if ((buf->record_counter < 10) || (buf->record_counter % 1000 == 0)) {
			int size = zmq_msg_size(&msg);
			printf("id %08X == %08X length %d, counter %" PRIu64 "\n",
					source_id, buf->source_id, size, buf->record_counter);
			if (do_debug > 1)
				print_data_hex((uint8_t *) buf, buf->total_length);
		}
		zmq_msg_close(&msg);
	}
}

