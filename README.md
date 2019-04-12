# INDRA_Stream_Test
Streaming capable hardware will generate a stream of data records from a data source. One transport mechanism is via a TCP/IP network over Ethernet. To test this capability, we require software that will receive the data on a server and route it to a client application for analysis. The test software suite consists of three parts:
•	A test sender that can send blocks in the correct format at a high enough rate to emulate the hardware.
•	A receiver router that can receive data from the emulator or from hardware data sources and publish the data so that subscribing analyzers have access.
•	An example subscriber.
## Implementation
The open source ZeroMQ (0MQ) library was chosen as the publish-subscribe data transport between the router and the subscriber. This choice was driven by the ease of use and availability of a mature library that is currently well maintained and should continue to be maintained and evolve in the future. The choice of pub-sub software was made first since it was felt that it could be a driving factor in the software architecture of the router. It is impractical at this time to implement in firmware an implementation of the higher-level connection types available in 0MQ. Instead we assume that the sender will send using lower level protocols. TCP was chosen over UDP since the software is simplified by relying on TCP to provide reliable delivery and flow control.



![image-20190412132316689](./readme_images/image-20190412132316689.png)



The following sections describe the three parts of the test suite in more detail.

## Test Client

### Operation

Many years ago, the DAQ group wrote a pair of C programs, *blaster.c* and *blastee.c*, to stress test data transport over TCP for various types of network link. The *blaster.c* code was used as the basis for the test client. 

The goal of the test client is to provide a mechanism for sending data on a TCP socket at a rate that emulates a streaming data source. It provides the ability to test the link under various conditions.  

By default, the client attempts to connect to a server listening on TCP port 5555 running on the same host. Command line options to change these values will be described later. If the server accepts connection, then the client first sends an eight byte data preamble on the newly connected socked. These bytes encode two uint32_t values:

| Type         | Name         | Value                               |
| ------------ | ------------ | ----------------------------------- |
| **uint32_t** | Magic Number | 0xC0DA2019                          |
| **uint32_t** | Source ID    | Numberic value (default 0xC0DA0001) |

The value named "Magic Number" is a unique code that is unlikely to be the first four bytes sent over a connection if the sender is not this software. The receiver checks this code and immediately closes the connection if the test fails. The value names "source ID" is a 32-bit number that uniquely identifies the source of the data. 

> Note: In a large system with many data source the sourece ID need not be unique system wide. It is only required to be unique for all data sources sending to the same TCP port.

If the preamble is accepted by the server the client can then send one or more data records to the server. Each data record begins with a header followed by the data payload. The header format is as follows :

| **Type**     | **Name**          | **Comment**                                                  |
| ------------ | ----------------- | ------------------------------------------------------------ |
| **uint32_t** | source_id         | 32-bit identifier for this data source. The default   value is 0xC0DA0001 but can   be overridden from the command line. It   appears first in the record since this simplifies code in the router (see   later section). |
| **uint32_t** | magic             | 32-bit marker with the hexadecimal value 0xC0DA2019. The use of a   marker word protects against the case where there happens to be some random   software already listening on the chosen TCP port. It also protects the   server since it unlikely that some random software accidentally connecting   would send that particular byte sequence. |
| **uint32_t** | total_length      | The length of the entire record, including the   header, in units of bytes. This limits the maximum record length to the   maximum value of uint32_t. *total_length*   is always divisible by 4 and must be rounded up if the sum of data and header   lengths is aligned. |
| **uint32_t** | payload_length    | The length of the data that follows the header if the payload is   uncompressed. In this case the total_length = header length + payload_length. |
| **uint32_t** | compressed_length | The length of the data that follows the header if   the payload is compressed. In this case total_length = header_length +   compressed_length. If *compressed_length*   is zero the payload is assumed to be uncompressed. *payload_length* must still be set so that the receiver can   allocate space for the payload after uncompression. |
| **uint32_t** | format_version    | An integer value that   identifies the header format.        |
| **uint64_t** | record_counter    | A count of the number of records sent since the   connection opened. It must increment by 1 for each record received and   protects against unintended retransmission or dropping of a record. |
| **uint64_t** | timestamp_sec     | 64-bit number of seconds in the 128-bit timestamp.           |
| **uint64_t** | timestamp_nsec    | 64-bit number of nanoseconds in the 128-bit   timestamp.     |

The following diagram shows the relationship between the three length fields. 

The Total Length is the length in bytes of the entire record, including header at the start and any padding at the end. The ability to pad allows the  possibility that the next record can begin aligned to a word boundary to aid mapping of the header onto a C/C++ structure. It also allows for implementations where the record length is fixed irrespective of how much of the space is used to store data.

The pair of length, Payload Length and Compressed Length, allow for future implementations that compress the large data payload before it is sent over the network.

Independent of whether compression is used or not the Payload Length always represents the length of the data payload in uncompressed form. If the data is not compressed then this is identical to the Compressed Length field. If the payload has been compressed the Compressed Length field holds the length of the space in the record occupied by the compressed payload. In that case the Payload Length is has the same definition as earlier, the space required to hold the payload after decompression.



![image-20190412145122093](/Users/heyes/develop/INDRA-SGC/INDRA_Stream_Test/readme_images/image-20190412145122093.png)



When run with no command line parameters the default is to send a single record of length equal to the header length plus 40 bytes. In order to test the performance of the network link the test client has a command line option to loop and send a number of buffers after which the average buffer rate and data rate are displayed. To allow for comprehensive testing the number of buffers and buffer size can be set. It is possible that there may be data dependent artifacts that would skew test results (for example hardware or drivers giving special treatment long sequences of the same value). To mitigate this the test client defaults to sending a buffer filled with random numbers or the user can provide a data file from which the data is read. To improve and display test accuracy the client can automatically repeat the test a number of times and displays a mean and standard deviation at the end.

 

The test client can also be used to emulate part of a larger system with multiple data sources. In this mode the user can provide a suggested data rate, in kilobytes per second, and the test client attempts to generate data at this rate.

 

For low level testing the client has a "verbose" option which turns on debug prints. This is particularly useful when sending a small number of relatively small buffers since the buffer contents are printed in hexadecimal format. With the same option used at the receiving end manual data quality checks can be made.

 

The command line options are summarized here:

​            

| -v          | verbose (optional)                                           |
| ----------- | ------------------------------------------------------------ |
| -t <target> | specify a host (optional, default   \"localhost\")           |
| -f <file>   | read source data from a file (optional,   default random data) |
| -p <n>      | specify a port (default 5555).                               |
| -n <n>      | number of buffers per loop (default 1).                      |
| -l <n>      | total number of cycles (default 1).                          |
| -b <n>      | n bytes per data packet (default 40).                        |
| -r <n>      | rate in kbyte/s (default, fast as possible).                 |
| -c          | compress data before send (optional, not   implemented)      |

 

# Implementation details

The code is written in C for portability and speed using standard Posix and Unix system calls. The command line arguments are decoded by the main routine which creates the socket over which the data will be sent. For maximum flexibility the target host can be specified as an IP address in dot notation or a hostname. A use of the dot notation is to force routing of the data through a specific interface. Both filling the data buffer with random numbers and reading the content from a file are time consuming tasks so a master buffer is created and filled once. The contents of the master are then copied to four buffers that are then queued in a thread safe FIFO, the "free fifo". An empty FIFO, the "output fifo", is created along with a thread to handle writing on the socket. The main thread then enters a loop. Each time around it takes a buffer off the "free fifo", updates the record_counter field in the header, and puts the buffer on the "output fifo". The number of loops is either 1 or the product of the -n and -l option values. Meanwhile, the write thread waits on the "output fifo", dequeues buffers which are written on the socket and returned the "free fifo". 

 ![image-20190412151514212](/Users/heyes/develop/INDRA-SGC/INDRA_Stream_Test/readme_images/image-20190412151514212.png)

This sounds overly complicated but is done this way to provide "hooks" for future development. For example, a future implementation could use several write threads in parallel to improve throughput. Another partially implemented option is to add a compression stage into the pipeline.  A buffer from the "free fifo" would be passed via fifos to one of several compression threads which would compress the data part of buffers before they are put into the "output fifo". Once the compressed data has gone over the network the data in the buffer can be restored via a quick copy from the master copy and put back in the "free fifo".

 

The main thread loop exits when the requested number of records have been queued. Since the writing is done in a separate thread the main routine must wait for all of the writing thread to finish before it can exit.

 