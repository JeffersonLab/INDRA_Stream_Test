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



