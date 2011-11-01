/* -*- Mode:C++; c-file-style:"k&r"; indent-tabs-mode:nil; -*- */

#include "pathload-original-globals.h"

int send_fleet();
int send_train();
int send_ctr_mesg(char *ctr_buff, l_int32 ctr_code);
l_int32 recv_ctr_mesg( char *ctr_buff);
l_int32 send_latency();
void min_sleeptime();
double time_to_us_delta(struct timeval tv1, struct timeval tv2);
void order_int(int unord_arr[], int ord_arr[], int num_elems);
void order_dbl(double unord_arr[], double ord_arr[],
               int start, int num_elems);
void order_float(float unord_arr[], float ord_arr[],
                 int start, int num_elems);
void help();

l_int32 fleet_id_n;
l_int32 fleet_id;
int sock_udp, sock_tcp, ctr_strm, send_buff_sz, rcv_tcp_adrlen;
struct sockaddr_in snd_udp_addr, snd_tcp_addr, rcv_udp_addr, rcv_tcp_addr;
l_int32 min_sleep_interval; /* in usec */
l_int32 min_timer_intr;     /* in usec */
int gettimeofday_latency;
int quiet;
