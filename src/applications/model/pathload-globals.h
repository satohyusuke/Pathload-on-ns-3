/* -*- Mode:C++; c-file-style:"k&r"; indent-tabs-mode:nil; -*- */

#ifndef PATHLOAD_GLOBALS_H
#define PATHLOAD_GLOBALS_H

// #if SIZEOF_LONG == 4
// typedef long l_int32;
// typedef unsigned long l_uint32;
// #elif SIZEOF_INT == 4
typedef int l_int32;
typedef unsigned int l_uint32;
// #endif

#ifdef LOCAL
#define EXTERN
#else
#define EXTERN extern
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>
#include <ctype.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <strings.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>
#include <float.h>
#include <fcntl.h>
#include <sys/utsname.h>
#ifdef THRLIB
#include <pthread.h>
#endif

namespace ns3 {

// Code numbers sent from receiver to sender through the TCP control stream.
const l_int32 CTR_CODE = 0x80000000;
const l_int32 SEND_FLEET = 0x00000001;
const l_int32 RECV_FLEET = 0x00000002;
const l_int32 CONTINUE_STREAM = 0x00000003;
const l_int32 FINISHED_STREAM = 0x00000007;
const l_int32 TERMINATE = 0x00000005;
const l_int32 ABORT_FLEET = 0x00000006;
const l_int32 SEND_TRAIN = 0x00000008;
const l_int32 FINISHED_TRAIN = 0x00000009;
const l_int32 BAD_TRAIN = 0x0000000a;
const l_int32 GOOD_TRAIN = 0x0000000b;

// Port numbers (UDP for receiver, TCP for sender)
const uint16_t UDP_RCV_PORT = 55001;
const uint16_t TCP_SND_PORT = 55002;
const uint32_t UDP_BUFFER_SZ = 400000; // bytes
const int TREND_ARRAY_LEN = 50;

// Packet stream parameters.
const l_int32 NUM_STREAM = 12;  // # of packet streams
const l_int32 STREAM_LEN = 100; // # of packets
const l_int32 MAX_STREAM_LEN = 400;
const l_int32 MIN_PKT_SZ = 200; // in bytes
const int MAX_TRAIN = 5;
const int TRAIN_LEN = 50;       // # of packets

EXTERN l_int32 verbose;
EXTERN l_int32 Verbose;

} // namespace ns3

#endif  // PATHLOAD_GLOBALS_H
