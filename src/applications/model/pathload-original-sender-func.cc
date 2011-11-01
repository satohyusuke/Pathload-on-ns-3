/* -*- Mode:C++; c-file-style:"k&r"; indent-tabs-mode:nil; -*- */

#include "pathload_gbls.h"
#include "pathload_snd.h"

// Send fleet for available bandwidth estimation.
// pkt_id and fleet_id start with 0.
int send_fleet()
{
     struct timeval tmp1, tmp2;
     struct timeval sleep_time;
     double t1 = 0, t2 = 0;
     l_int32 ctr_code;
     l_int32 pkt_id;
     l_int32 pkt_cnt = 0;        // packet count = {0, ...}
     l_int32 stream_cnt = 0;     // stream count = {0, NUM_STREAM}
     l_int32 stream_id_n = 0;
     l_int32 usec_n, sec_n, pkt_id_n;
     l_int32 sleep_tm_usec;
     int ret_val;
     int stream_duration;
     int diff;
     int i;
     l_int32 tmp = 0;
     char *pkt_buf;
     char ctr_buff[8];

     if ((pkt_buf = malloc(cur_pkt_sz * sizeof(char))) == NULL) {
          printf("ERROR: send_fleet(): unable to malloc %ld bytes\n",
                 cur_pkt_sz);
          exit(-1);
     }

     // Create random payload; does it matter?
     srandom(getpid());
     for (i = 0; i < cur_pkt_sz-1; i++)
          pkt_buf[i] = (char) (random() & 0x000000ff);

     pkt_id = 0;
     if (!quiet) printf("Sending fleet %ld ", fleet_id);

     // Send packet streams.
     while (stream_cnt < num_stream) {
          if (!quiet) printf("#");
          fflush(stdout);
          fleet_id_n = htonl(fleet_id);
          memcpy(pkt_buf, &fleet_id_n, sizeof(l_int32));
          stream_id_n = htonl(stream_cnt);
          memcpy(pkt_buf + sizeof(l_int32), &stream_id_n, sizeof(l_int32));

          // Time stamp for the first packet of the stream.
          gettimeofday(&tmp1, NULL);
          t1 = (double) tmp1.tv_sec * 1000000.0 + (double) tmp1.tv_usec;

          // Send packets with fleet id, stream id, packet id and time stamp?
          // Expected structure of a packet:
          //   +----------+-----------+-----------+----------+-----------+
          //   | fleet id | stream id | packet id | ts (sec) | ts (usec) |
          //   +----------+-----------+-----------+----------+-----------+
          //   * ts = time stamp
          for (pkt_cnt = 0; pkt_cnt < stream_len; pkt_cnt++) {
               pkt_id_n = htonl(pkt_cnt);
               memcpy(pkt_buf + 2*sizeof(l_int32), &pkt_id_n, sizeof(l_int32));
               sec_n = htonl(tmp1.tv_sec);
               memcpy(pkt_buf + 3*sizeof(l_int32), &sec_n, sizeof(l_int32));
               usec_n = htonl(tmp1.tv_usec);
               memcpy(pkt_buf + 4*sizeof(l_int32), &usec_n, sizeof(l_int32));
               if (send(sock_udp, pkt_buf, cur_pkt_sz, 0) == -1) {
                    perror("send(): ");
                    return -1;
               }
               gettimeofday(&tmp2, NULL);
               t2 = (double) tmp2.tv_sec * 1000000.0 + (double) tmp2.tv_usec;
               tmp = (l_int32) (t2 - t1);
               if (pkt_cnt < (stream_len-1)) {
                    l_int32 tm_remaining = time_interval - tmp;
                    if (tm_remaining > min_sleep_interval) {
                         sleep_tm_usec =
                              tm_remaining - (tm_remaining % min_timer_intr)
                              - (min_timer_intr < 200 ? 2*min_timer_intr : min_timer_intr);
                         sleep_time.tv_sec = (int) (sleep_tm_usec / 1000000);
                         sleep_time.tv_usec =
                              sleep_tm_usec - sleep_time.tv_sec * 1000000;
                         select(1, NULL, NULL, NULL, &sleep_time);
                    }
                    gettimeofday(&tmp2, NULL);
                    t2 = (double) tmp2.tv_sec * 1000000.0 + (double) tmp2.tv_usec;
                    diff = gettimeofday_latency > 0 ? gettimeofday_latency - 1 : 0;
                    while ((t2 - t1) < (time_interval - diff)) {
                         gettimeofday(&tmp2, NULL);
                         t2 = (double) tmp2.tv_sec * 1000000.0 + (double) tmp2.tv_usec;
                    }
                    tmp1 = tmp2;
                    t1 = t2;
               }
          }

          // Wait for 2000 usec after sending a packet stream.
          gettimeofday(&tmp2, NULL);
          t1 = (double) tmp2.tv_sec * 1000000.0 + (double) tmp2.tv_usec;
          do {
               gettimeofday(&tmp2, NULL);
               t2 = (double) tmp2.tv_sec * 1000000.0 + (double) tmp2.tv_usec;
          } while ((t2 - t1) < 2000);

          // Send end of stream message with the stream id.
          ctr_code = FINISHED_STREAM | CTR_CODE;
          if (send_ctr_mesg(ctr_buff, ctr_code) == -1) {
               free(pkt_buf);
               perror("send_ctr_mesg(): FINISHED_STREAM");
               return -1;
          }

          // Send current stream count.
          if (send_ctr_mesg(ctr_buff, stream_cnt) == -1) {
               free(pkt_buf);
               return -1;
          }

          // Wait for continue/cancel message from receiver.
          if ((ret_val = recv_ctr_mesg(ctr_buff)) == -1) {
               free(pkt_buf);
               return -1;
          }
          if ((((ret_val & CTR_CODE) >> 31) == 1) &&
              ((ret_val & 0x7fffffff) == CONTINUE_STREAM))
               stream_cnt++;
          else if ((((ret_val & CTR_CODE) >> 31) == 1) &&
                   ((ret_val & 0x7fffffff) == ABORT_FLEET)) {
               free(pkt_buf);
               return 0;
          }

          // Inter-stream latency is max{RTT, 9*stream_duration}.
          stream_duration = stream_len * time_interval;
          if ((t2-t1) < (9*stream_duration)) {
               // Release CPU if inter-stream gap is longer than min_sleep_time.
               if (t2 - t1 - 9*stream_duration > min_sleep_interval) {
                    sleep_tm_usec =
                         time_interval - tmp
                         - ((time_interval - tmp) % min_sleep_interval)
                         - min_sleep_interval;
                    sleep_time.tv_sec = (int) (sleep_tm_usec / 1000000);
                    sleep_time.tv_usec =
                         sleep_tm_usec - sleep_time.tv_sec * 1000000;
                    select(1, NULL, NULL, NULL, &sleep_time);
                    gettimeofday(&tmp2, NULL);
                    t2 = (double) tmp2.tv_sec * 1000000.0 + (double) tmp2.tv_usec;
               }

               // Busy wait for sending next packet stream.
               do {
                    gettimeofday(&tmp2, NULL);
                    t2 = (double) tmp2.tv_sec * 1000000.0 + (double) tmp2.tv_usec;
               } while ((t2-t1) < 9*stream_duration);
          }

          // A hack for slow links.
          if (stream_duration >= 500000) break;
     }
     free(pkt_buf);

     return 0;
}

// Send a message through the control stream.
// Return -1 if failed to send.
int send_ctr_mesg(char *ctr_buff, l_int32 ctr_code)
{
     l_int32 ctr_code_n = htonl(ctr_code);

     memcpy((void *) ctr_buff, &ctr_code_n, sizeof(l_int32));
     if (write(ctr_strm, ctr_buff, sizeof(l_int32)) != sizeof(l_int32))
          return -1;
     else
          return 0;
}

// Receive a message through the control stream.
// Return -1 if time out occured.
l_int32 recv_ctr_mesg(char *ctr_buff)
{
     struct timeval select_tv;   // for select()
     fd_set readset;
     l_int32 ctr_code;

     select_tv.tv_sec = 50;      // If no control message for 50 sec, terminate.
     select_tv.tv_usec = 0;
     FD_ZERO(&readset);
     FD_SET(ctr_strm, &readset);
     // bzero(ctr_buff, 4);
     bzero(ctr_buff, sizeof(l_int32));

     if (select(ctr_strm+1, &readset, NULL, NULL, &select_tv) > 0) {
          if (read(ctr_strm, ctr_buff, sizeof(l_int32)) < 0) return -1;
          memcpy(&ctr_code, ctr_buff, sizeof(l_int32));
          return (ntohl(ctr_code));
     } else {
          printf("Receiver hasn't responded.\n");
          return -1;
     }
}

// Compute the time difference between two timevals (tv1, tv2) in usec.
// Assumes tv2 > tv1.
double time_to_us_delta(struct timeval tv1, struct timeval tv2)
{
     double time_us;

     time_us = (double) ((tv2.tv_sec - tv1.tv_sec) * 1000000
                         + (tv2.tv_usec - tv1.tv_usec));

     return time_us;
}

// Measure how long does it take to send a packet in usec.
l_int32 send_latency()
{
     char *pack_buf;
     float min_OSdelta[50], ord_min_OSdelta[50];
     int i, len;
     int sock_udp;
     struct timeval first_time, current_time;
     struct sockaddr_in snd_udp_addr, rcv_udp_addr;

     if (max_pkt_sz == 0
         || (pack_buf = malloc(max_pkt_sz * sizeof(char))) == NULL) {
          printf("ERROR: send_latency(): unable to malloc %ld bytes\n",
                 max_pkt_sz);
          exit(-1);
     }

     if ((sock_udp = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
          perror("socket(AF_INET,SOCK_DGRAM,0): ");
          exit(-1);
     }
     bzero((char *) &snd_udp_addr, sizeof(snd_udp_addr));
     snd_udp_addr.sin_family = AF_INET;
     snd_udp_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // ループバック
     snd_udp_addr.sin_port = 0;

     if (bind(sock_udp, (struct sockaddr *) &snd_udp_addr,
              sizeof(snd_udp_addr)) < 0) {
          perror("bind(sock_udp): ");
          exit(-1);
     }

     len = sizeof(rcv_udp_addr);
     if (getsockname(sock_udp, (struct sockaddr *) &rcv_udp_addr, &len) < 0) {
          perror("getsockname(): ");
          exit(-1);
     }

     if (connect(sock_udp, (struct sockaddr *) &rcv_udp_addr,
                 sizeof(rcv_udp_addr)) < 0) {
          perror("connect(sock_udp)");
          exit(-1);
     }

     // Create random payload; does it matter?
     srandom(getpid());
     for (i = 0; i < max_pkt_sz-18; i++)
          pack_buf[i] = (char) (random() & 0x000000ff);

     // Measure how long does it take to send a packet in usec.
     for (i = 0; i < 50; i++) {
          gettimeofday(&first_time, NULL);
          if (send(sock_udp, pack_buf, max_pkt_sz, 0) == -1)
               perror("sendto(): ");
          gettimeofday(&current_time, NULL);
          recv(sock_udp, pack_buf, max_pkt_sz, 0);
          min_OSdelta[i] = time_to_us_delta(first_time, current_time);
     }

     // Use median of the measured latencies to avoid outliers.
     order_float(min_OSdelta, ord_min_OSdelta, 0, 50);
     if (pack_buf != NULL) free(pack_buf);

     return (ord_min_OSdelta[25]);
}

// Send packet trains.
int send_train()
{
     struct timeval select_tv;
     char *pack_buf;
     int train_id = 0, train_id_n;
     int pack_id, pack_id_n;
     l_int32 ctr_code;
     int ret_val;
     int i;
     int train_len = 0;
     char ctr_buff[8];

     if ((pack_buf = malloc(max_pkt_sz * sizeof(char))) == NULL) {
          printf("ERROR : send_train : unable to malloc %ld bytes \n",
                 max_pkt_sz);
          exit(-1);
     }

     // Create random payload; does it matter?
     srandom(getpid());
     for (i = 0; i < max_pkt_sz-1; i++)
          pack_buf[i] = (char) (random() & 0x000000ff);

     // Send a packet train.
     // Expected structure of a packet in the packet train:
     //   +----------+------------+
     //   | train id |  packet id |
     //   +----------+------------+
     while (train_id < MAX_TRAIN-1) {
          if (train_len == 5)
               train_len = 3;
          else
               train_len = TRAIN_LEN - train_id * 15;
          train_id_n = htonl(train_id);
          memcpy(pack_buf, &train_id_n, sizeof(l_int32));

          // Send packets with the packet id.
          for (pack_id = 0; pack_id <= train_len; pack_id++) {
               pack_id_n = htonl(pack_id);
               memcpy(pack_buf + sizeof(l_int32), &pack_id_n, sizeof(l_int32));
               send(sock_udp, pack_buf, max_pkt_sz, 0);
          }

          // Wait for 1000 usec after the train for sending control code.
          select_tv.tv_sec = 0;
          select_tv.tv_usec = 1000;
          select(0, NULL, NULL, NULL, &select_tv);

          // Send FINISHED_TRAIN code to receiver.
          ctr_code = FINISHED_TRAIN | CTR_CODE;
          send_ctr_mesg(ctr_buff, ctr_code);

          // If received BAD_TRAIN code from receiver,
          // continue to send a train having shoter packets.
          if ((ret_val = recv_ctr_mesg(ctr_buff)) == -1) return -1;
          if ((((ret_val & CTR_CODE) >> 31) == 1) &&
              ((ret_val & 0x7fffffff) == BAD_TRAIN)) {
               train_id++;
               continue;
          } else {
               free(pack_buf);
               return 0;
          }
     }
     free(pack_buf);

     return 0;
}

const int NUM_SELECT_CALL = 31;
void min_sleeptime()
{
     struct timeval sleep_time, time[NUM_SELECT_CALL];
     int res[NUM_SELECT_CALL], ord_res[NUM_SELECT_CALL];
     int i;
     l_int32 tm;

     gettimeofday(&time[0], NULL);
     for (i = 1; i < NUM_SELECT_CALL; i++) {
          sleep_time.tv_sec = 0;
          sleep_time.tv_usec = 1;
          gettimeofday(&time[i], NULL);
          select(0, NULL, NULL, NULL, &sleep_time);
     }

     for (i = 1; i < NUM_SELECT_CALL; i++) {
          res[i-1] = (time[i].tv_sec - time[i-1].tv_sec) * 1000000 +
               time[i].tv_usec - time[i-1].tv_usec;
#ifdef DEBUG
          printf("DEBUG: %.2f ",
                 (time[i].tv_sec - time[i-1].tv_sec) * 1000000. +
                 time[i].tv_usec - time[i-1].tv_usec);
          printf("DEBUG: %d \n", res[i-1]);
#endif
     }

     order_int(res, ord_res, NUM_SELECT_CALL - 1);
     min_sleep_interval =
          (ord_res[NUM_SELECT_CALL/2] + ord_res[NUM_SELECT_CALL/2 + 1]) / 2;
     gettimeofday(&time[0], NULL);
     tm = min_sleep_interval + min_sleep_interval / 4;
     for (i = 1; i < NUM_SELECT_CALL; i++) {
          sleep_time.tv_sec = 0;
          sleep_time.tv_usec = tm;
          gettimeofday(&time[i], NULL);
          select(0, NULL, NULL, NULL, &sleep_time);
     }
     for (i = 1; i < NUM_SELECT_CALL; i++) {
          res[i-1] = (time[i].tv_sec - time[i-1].tv_sec) * 1000000
               + time[i].tv_usec - time[i-1].tv_usec;
#ifdef DEBUG
          printf("DEBUG: %.2f ",
                 (time[i].tv_sec - time[i-1].tv_sec) * 1000000. +
                 time[i].tv_usec - time[i-1].tv_usec);
          printf("DEBUG: %d \n", res[i-1]);
#endif
     }
     order_int(res, ord_res, NUM_SELECT_CALL-1);
     min_timer_intr =
          (ord_res[NUM_SELECT_CALL/2] + ord_res[NUM_SELECT_CALL/2 + 1]) / 2
          - min_sleep_interval;
#ifdef DEBUG
     printf("DEBUG: min_sleep_interval %ld\n", min_sleep_interval);
     printf("DEBUG: min_timer_intr %ld\n", min_timer_intr);
#endif
}

//  Order an array of int using bubblesort.
void order_int(int unord_arr[], int ord_arr[], int num_elems)
{
     int i, j;
     int temp;

     for (i = 0; i < num_elems; i++) ord_arr[i] = unord_arr[i];
     for (i = 1; i < num_elems; i++) {
          for (j = i-1; j >= 0; j--)
               if (ord_arr[j+1] < ord_arr[j]) {
                    temp = ord_arr[j];
                    ord_arr[j] = ord_arr[j+1];
                    ord_arr[j+1] = temp;
               } else
                    break;
     }
}

// Order an array of float using bubblesort.
void order_float(float unord_arr[], float ord_arr[],
                 int start, int num_elems)
{
     int i, j, k;
     double temp;

     for (i = start, k = 0; i < start+num_elems; i++, k++)
          ord_arr[k] = unord_arr[i];
     for (i = 1; i < num_elems; i++) {
          for (j = i-1; j >= 0; j--)
               if (ord_arr[j+1] < ord_arr[j]) {
                    temp = ord_arr[j];
                    ord_arr[j] = ord_arr[j+1];
                    ord_arr[j+1] = temp;
               } else
                    break;
     }
}

// Order an array of double using bubblesort.
void order_dbl(double unord_arr[], double ord_arr[],
               int start, int num_elems)
{
     int i, j, k;
     double temp;

     for (i = start, k = 0; i < start+num_elems; i++, k++)
          ord_arr[k] = unord_arr[i];
     for (i = 1; i < num_elems; i++) {
          for (j = i-1; j >= 0; j--)
               if (ord_arr[j + 1] < ord_arr[j]) {
                    temp = ord_arr[j];
                    ord_arr[j] = ord_arr[j+1];
                    ord_arr[j+1] = temp;
               } else
                    break;
     }
}

void help()
{
     fprintf(stderr, "usage: pathload_snd [-q] [-h|-H] [-i]\n");
     fprintf(stderr, "-q        : quite mode\n");
     fprintf(stderr, "-h|-H     : print this help and exit\n");
     fprintf(stderr,
             "-i        : run sender in iterative \"persistent\" mode\n");
     exit(0);
}
