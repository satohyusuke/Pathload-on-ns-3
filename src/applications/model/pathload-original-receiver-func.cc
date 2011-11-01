/* -*- Mode:C++; c-file-style:"k&r"; indent-tabs-mode:nil; -*- */

#include "pathload-original-globals.h"
#include "pathload-original-receiver.h"

// Measure the latency to receive a packet through recvfrom().
l_int32 recvfrom_latency(struct sockaddr_in rcv_udp_addr)
{
    char *random_data;
    struct timeval current_time, first_time;
    float min_OSdelta[50], ord_min_OSdelta[50];
    l_int32 j;

    if ((random_data = malloc(max_pkt_sz * sizeof(char))) == NULL) {
        printf("ERROR : unable to malloc %ld bytes \n", max_pkt_sz);
        exit(-1);
    }

    // Create random payload; does it matter?
    srandom(getpid());
    for (j=0; j<max_pkt_sz-1; j++)
        random_data[j] = (char) (random() & 0x000000ff);

    // Measure the latency to receive a packet through recvfrom().
    for (j=0; j<50; j++) {
        if (sendto(sock_udp, random_data, max_pkt_sz, 0,
                   (struct sockaddr *) &rcv_udp_addr,
                   sizeof(rcv_udp_addr)) == -1)
            perror("recvfrom_latency(): ");
        gettimeofday(&first_time, NULL);
        recvfrom(sock_udp, random_data, max_pkt_sz, 0, NULL, NULL);
        gettimeofday(&current_time, NULL);
        min_OSdelta[j] = time_to_us_delta(first_time, current_time);
    }

    // Use median of the measured latencies to avoid outliers.
    order_float(min_OSdelta, ord_min_OSdelta, 0, 50);

    free(random_data);

    return ((l_int32) ord_min_OSdelta[25]);
}

// ADR means asymptotic dispersion rate.
// It is used as the rate of the first fleet.
double get_adr()
{
    struct timeval select_tv, arrv_tv[MAX_STREAM_LEN];
    double delta;
    double bw_msr = 0;
    double bad_bw_msr[10];
    int num_bad_train = 0;
    int first = 1;
    double sum = 0;
    l_int32 exp_train_id = 0;    // experienced train id?
    l_int32 bad_train = 1;
    l_int32 retry = 0;
    l_int32 ctr_code;
    l_int32 train_len = 0;
    l_int32 last = 0;
    l_int32 i;
    l_int32 spacecnt = 24;
    char ctr_buff[8];
    l_int32 num_burst;           // get_adr() 内では特に意味なし？

    if (Verbose) printf("  ADR [");
    fflush(stdout);
    fprintf(pathload_fp, "  ADR [");
    fflush(pathload_fp);

    for (i=0; i<100; i++) {
        arrv_tv[i].tv_sec = 0;
        arrv_tv[i].tv_usec = 0;
    }

    // Send a control code to have the sender send a packet train.
    ctr_code = SEND_TRAIN | CTR_CODE;
    send_ctr_mesg(ctr_buff, ctr_code);

    while (retry < MAX_TRAIN && bad_train) {
        if (train_len == 5)
            train_len = 3;
        else
            train_len = TRAIN_LEN - exp_train_id * 15;

        if (Verbose) printf(".");
        fflush(stdout);
        fprintf(pathload_fp, ".");
        spacecnt--;

        // Receive a packet train.
        // If the train was a bad train, recv_train() returns 1.
        bad_train = recv_train(exp_train_id, arrv_tv, train_len);

        // Compute dispersion and bandwidth measurement.
        if (!bad_train) {
            num_burst = 0;
            interrupt_coalescence =
                check_intr_coalescence(arrv_tv, train_len, &num_burst);
            last = train_len;
            while (!arrv_tv[last].tv_sec) --last;
            delta = time_to_us_delta(arrv_tv[1], arrv_tv[last]);
            bw_msr = ((max_pkt_sz + 28) << 3) * (last - 1) / delta;

            // Tell sender that received train was a good one.
            ctr_code = GOOD_TRAIN | CTR_CODE;
            send_ctr_mesg(ctr_buff, ctr_code);
        } else {
            retry++;
            // Wait for at least 10 msec before requesting another train.
            last = train_len;
            while (!arrv_tv[last].tv_sec) --last;
            first = 1;
            while (!arrv_tv[first].tv_sec) ++first;
            delta = time_to_us_delta(arrv_tv[first], arrv_tv[last]);
            bad_bw_msr[num_bad_train++] =
                ((28 + max_pkt_sz) << 3) * (last - first - 1) / delta;
            select_tv.tv_sec = 0;
            select_tv.tv_usec = 10000;
            select(0, NULL, NULL, NULL, &select_tv);
            ctr_code = BAD_TRAIN | CTR_CODE;
            send_ctr_mesg(ctr_buff, ctr_code);
            exp_train_id++;
        }
    }

    if (Verbose) {
        i = spacecnt;
        putchar(']');
        while (--i > 0) putchar(' ');
        printf(":: ");
    }
    fputc(']', pathload_fp);
    while (--spacecnt > 0) fputc(' ', pathload_fp);
    fprintf(pathload_fp, ":: ");

    if (!bad_train) {
        if (Verbose) printf("%.2fMbps\n", bw_msr);
        fprintf(pathload_fp, "%.2fMbps\n", bw_msr);
    } else {
        for (i = 0; i < num_bad_train; i++)
            if (finite(bad_bw_msr[i])) sum += bad_bw_msr[i];
        bw_msr = sum / num_bad_train;
        if (Verbose) printf("%.2fMbps (I)\n", bw_msr);
        fprintf(pathload_fp, "%.2fMbps (I)\n", bw_msr);
    }

    return bw_msr;
}

// Receive a complete packet train from the sender
l_int32 recv_train(l_int32 exp_train_id, struct timeval *time,
                   l_int32 train_len)
{
    struct sigaction sigstruct;
    struct timeval current_time;
    struct timeval select_tv;
    fd_set readset;
    l_int32 ret_val;
    l_int32 pack_id;
    l_int32 exp_pack_id = 0;
    l_int32 bad_train = 0;
    l_int32 train_id;
    l_int32 rcvd = 0;
    char *pack_buf;
    char ctr_buff[8];
#ifdef THRLIB
    thr_arg arg;
    pthread_t tid;
    pthread_attr_t attr;
#endif

    if ((pack_buf = malloc(max_pkt_sz * sizeof(char))) == NULL) {
        printf("ERROR : unable to malloc %ld bytes \n", max_pkt_sz);
        exit(-1);
    }

    // SIGUSR1 シグナルを受け取ったとき, sig_sigusr1() を呼ぶように設定.
    sigstruct.sa_handler = sig_sigusr1;
    sigemptyset(&sigstruct.sa_mask);
    sigstruct.sa_flags = 0;
#ifdef SA_INTERRUPT
    sigstruct.sa_flags |= SA_INTERRUPT;
#endif
    sigaction(SIGUSR1, &sigstruct, NULL);

#ifdef THRLIB
    arg.finished_stream = 0;
    arg.ptid = pthread_self();
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&tid, &attr, ctrl_listen, &arg) != 0) {
        perror("recv_train():: pthread_create");
        printf("Failed to create thread. exiting...\n");
        fprintf(pathload_fp, "Failed to create thread. exiting...\n");
        exit(-1);
    }
#endif

    do {
#ifndef THRLIB
        FD_ZERO(&readset);
        FD_SET(sock_tcp, &readset);
        FD_SET(sock_udp, &readset);
        select_tv.tv_sec = 1000;
        select_tv.tv_usec = 0;
        if (select(sock_tcp+1, &readset, NULL, NULL, &select_tv) > 0) {
            if (FD_ISSET(sock_udp, &readset)) {
#endif
                if (recvfrom(sock_udp, pack_buf, max_pkt_sz, 0, NULL, NULL) != -1) {
                    gettimeofday(&current_time, NULL);
                    memcpy(&train_id, pack_buf, sizeof(l_int32));
                    train_id = ntohl(train_id);
                    memcpy(&pack_id, pack_buf + sizeof(l_int32), sizeof(l_int32));
                    pack_id = ntohl(pack_id);
                    if (train_id == exp_train_id && pack_id == exp_pack_id) {
                        rcvd++;
                        time[pack_id] = current_time;
                        exp_pack_id++;
                    } else
                        bad_train = 1;
                }
#ifndef THRLIB
            }                   // end of FD_ISSET

            if (FD_ISSET(sock_tcp, &readset)) {
                /* check the control connection. */
                if ((ret_val = recv_ctr_mesg(sock_tcp, ctr_buff)) != -1) {
                    if ((((ret_val & CTR_CODE) >> 31) == 1) &&
                        ((ret_val & 0x7fffffff) == FINISHED_TRAIN)) {
                        break;
                    }
                }
            }
        }                       // end of select
    } while (1);
#else
    } while (!arg.finished_stream);
#endif

    if (rcvd != train_len+1) bad_train = 1;
    gettimeofday(&time[pack_id+1], NULL);

    sigstruct.sa_handler = SIG_DFL;
    sigemptyset(&sigstruct.sa_mask);
    sigstruct.sa_flags = 0;
    sigaction(SIGUSR1, &sigstruct, NULL);

    free(pack_buf);

    return bad_train;
}

l_int32 check_intr_coalescence(struct timeval time[], l_int32 len,
                               l_int32 *burst)
{
    l_int32 min_gap;
    double delta[MAX_STREAM_LEN];
    l_int32 b2b = 0;    // back to back? = 連続した
    l_int32 tmp = 0;
    l_int32 i;

    min_gap =
        MIN_TIME_INTERVAL > 3*rcv_latency ? MIN_TIME_INTERVAL : 3*rcv_latency;
    for (i=2; i<len; i++) {
        delta[i] = time_to_us_delta(time[i-1], time[i]);
        if (delta[i] <= min_gap) {
            b2b++;
            tmp++;
        } else {
            if (tmp >= 3) {
                (*burst)++;    // バーストトラヒックの burst か？
                tmp = 0;
            }
        }
    }

    // back-to-back なパケットが 60% より多くあるかの判定？
    if (b2b > 0.6 * len)
        return 1;
    else
        return 0;
}

// Receive N streams. After each stream, compute the loss rate.
// Mark a stream "lossy" if loss rate in that stream is more than a threshold.
l_int32 recv_fleet()
{
    struct sigaction sigstruct;
    struct timeval snd_tv[MAX_STREAM_LEN], arrv_tv[MAX_STREAM_LEN];
    struct timeval current_time, first_time;
    double pkt_loss_rate;
    double owd[MAX_STREAM_LEN];
    double snd_tm[MAX_STREAM_LEN];
    double arrv_tm[MAX_STREAM_LEN];
    l_int32 ctr_code;
    l_int32 pkt_lost = 0;
    l_int32 stream_id_n, stream_id = 0;
    l_int32 total_pkt_rcvd = 0, pkt_rcvd = 0;
    l_int32 pkt_id = 0;
    l_int32 pkt_id_n = 0;
    l_int32 exp_pkt_id = 0;
    l_int32 stream_cnt = 0;	/* 0->n */
    l_int32 fleet_id, fleet_id_n = 0;
    l_int32 lossy_stream = 0;
    l_int32 return_val = 0;
    l_int32 finished_stream = 0;
    l_int32 stream_duration;
    l_int32 num_sndr_cs[20], num_rcvr_cs[20];
    char ctr_buff[8];
    char *pkt_buf;
    double owdfortd[MAX_STREAM_LEN];
    l_int32 num_substream, substream[MAX_STREAM_LEN];
    l_int32 low, high, len, j;
    l_int32 b2b_pkt_per_stream[20];
    l_int32 tmp_b2b;
#ifdef THRLIB
    pthread_t tid;
    thr_arg arg;
    pthread_attr_t attr;
#endif
    l_int32 num_bursts;
    l_int32 abort_fleet = 0;
    l_int32 p = 0;
    struct timeval select_tv;
    fd_set readset;
    l_int32 ret_val;

    if ((pkt_buf = malloc(cur_pkt_sz * sizeof(char))) == NULL) {
        printf("ERROR : unable to malloc %ld bytes \n", cur_pkt_sz);
        exit(-1);
    }
    trend_idx = 0;
    ic_flag = 0;
    if (verbose && !Verbose)
        printf("Receiving Fleet %ld, Rate %.2fMbps\n", exp_fleet_id, tr);
    if (Verbose) {
        printf("\nReceiving Fleet %ld\n", exp_fleet_id);
        printf("  Fleet Parameter(req)  :: R=%.2fMbps, L=%ldB, K=%ldpackets, \
T=%ldusec\n", tr, cur_pkt_sz, stream_len,
               time_interval);
    }
    fprintf(pathload_fp, "\nReceiving Fleet %ld\n", exp_fleet_id);
    fprintf(pathload_fp, "  Fleet Parameter(req)  :: R=%.2fMbps, L=%ldB, \
K=%ldpackets, T=%ldusec\n", tr, cur_pkt_sz, stream_len,
            time_interval);

    if (Verbose) printf("  Lossrate per stream   :: ");
    fprintf(pathload_fp, "  Lossrate per stream   :: ");

    sigstruct.sa_handler = sig_sigusr1;
    sigemptyset(&sigstruct.sa_mask);
    sigstruct.sa_flags = 0;
#ifdef SA_INTERRUPT
    sigstruct.sa_flags |= SA_INTERRUPT;
#endif
    sigaction(SIGUSR1, &sigstruct, NULL);

    while (stream_cnt < num_stream) {
#ifdef THRLIB
        arg.finished_stream = 0;
        arg.ptid = pthread_self();
        arg.stream_cnt = stream_cnt;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&tid, &attr, ctrl_listen, &arg) != 0) {
            perror("recv_fleet::pthread_create");
            exit(-1);
        }
#endif
        pkt_lost = 0;
        first_time.tv_sec = 0;
        for (j=0; j<stream_len; j++) {
            snd_tv[j].tv_sec = 0;
            snd_tv[j].tv_usec = 0;
            arrv_tv[j].tv_sec = 0;
            arrv_tv[j].tv_usec = 0;
        }

        /* Receive K packets of ith stream */
#ifdef THRLIB
        while (!arg.finished_stream)
#else
        while (1)
#endif
        {
#ifndef THRLIB
            FD_ZERO(&readset);
            FD_SET(sock_tcp, &readset);
            FD_SET(sock_udp, &readset);
            select_tv.tv_sec = 1000;
            select_tv.tv_usec = 0;
            if (select(sock_tcp + 1, &readset, NULL, NULL, &select_tv) > 0) {
                if (FD_ISSET(sock_udp, &readset)) {
#endif
                    if (recvfrom
                        (sock_udp, pkt_buf, cur_pkt_sz, 0, NULL, NULL) > 0) {
                        gettimeofday(&current_time, NULL);
                        memcpy(&fleet_id_n, pkt_buf, sizeof(l_int32));
                        fleet_id = ntohl(fleet_id_n);
                        memcpy(&stream_id_n, pkt_buf + sizeof(l_int32),
                               sizeof(l_int32));
                        stream_id = ntohl(stream_id_n);
                        memcpy(&pkt_id_n, pkt_buf + 2*sizeof(l_int32),
                               sizeof(l_int32));
                        pkt_id = ntohl(pkt_id_n);
                        if (fleet_id == exp_fleet_id && stream_id == stream_cnt
                            && pkt_id >= exp_pkt_id) {
                            if (first_time.tv_sec == 0)
                                first_time = current_time;
                            arrv_tv[pkt_id] = current_time;
                            memcpy(&(snd_tv[pkt_id].tv_sec),
                                   pkt_buf + 3*sizeof(l_int32), sizeof(l_int32));
                            memcpy(&(snd_tv[pkt_id].tv_usec),
                                   pkt_buf + 4*sizeof(l_int32), sizeof(l_int32));
                            if (pkt_id > exp_pkt_id) {
                            /* reordered are considered as lost. */
                                pkt_lost += (pkt_id - exp_pkt_id);
                                exp_pkt_id = pkt_id;
                            }
                            ++exp_pkt_id;
                            ++pkt_rcvd;
                        }
                    }   // end of recvfrom
#ifndef THRLIB
                }       // end of FD_ISSET

                if (FD_ISSET(sock_tcp, &readset)) {
                    /* check the control connection. */
                    if ((ret_val = recv_ctr_mesg(sock_tcp, ctr_buff)) != -1) {
                        if ((((ret_val & CTR_CODE) >> 31) == 1)
                            && ((ret_val & 0x7fffffff) == FINISHED_STREAM)) {
                            while ((ret_val =
                                    recv_ctr_mesg(sock_tcp, ctr_buff)) == -1);
                            if (ret_val == stream_cnt) {
                                break;
                            }
                        }
                    }
                }
            }           // end of select
            else {
                perror("select");
                exit(0);
            }
#endif
        }

        for (j = 0; j < stream_len; j++) {
            snd_tv[j].tv_sec = ntohl(snd_tv[j].tv_sec);
            snd_tv[j].tv_usec = ntohl(snd_tv[j].tv_usec);
            snd_tm[j] = snd_tv[j].tv_sec * 1000000.0 + snd_tv[j].tv_usec;
            arrv_tm[j] = arrv_tv[j].tv_sec * 1000000.0 + arrv_tv[j].tv_usec;
            owd[j] = arrv_tm[j] - snd_tm[j];
        }

        total_pkt_rcvd += pkt_rcvd;
        finished_stream = 0;
        pkt_lost += stream_len - exp_pkt_id;
        pkt_loss_rate = (double) pkt_lost * 100.0 / stream_len;
        if (Verbose) printf(":%.1f", pkt_loss_rate);
        fprintf(pathload_fp, ":%.1f", pkt_loss_rate);
        exp_pkt_id = 0;
        stream_cnt++;

        num_bursts = 0;
        if (interrupt_coalescence)
            ic_flag = check_intr_coalescence(arrv_tv, pkt_rcvd, &num_bursts);
        if (pkt_loss_rate < HIGH_LOSS_RATE && pkt_loss_rate >= MEDIUM_LOSS_RATE)
            lossy_stream++;
        if (pkt_loss_rate >= HIGH_LOSS_RATE ||
            (stream_cnt >= num_stream &&
             lossy_stream * 100.0 / stream_cnt >= MAX_LOSSY_STREAM_FRACTION)) {
            if (increase_stream_len) {
                increase_stream_len = 0;
                lower_bound = 1;
            }
            if (Verbose) printf("\n  Fleet aborted due to high lossrate");
            fprintf(pathload_fp, "\n  Fleet aborted due to high lossrate");
            abort_fleet = 1;
            break;
        } else {
            /* analyze trend in stream */
            num += get_sndr_time_interval(snd_tm, &snd_time_interval);
            adjust_offset_to_zero(owd, stream_len);
            num_substream = eliminate_sndr_side_CS(snd_tm, substream);
            num_sndr_cs[stream_cnt-1] = num_substream;
            substream[num_substream++] = stream_len-1;
            low = 0;
            num_rcvr_cs[stream_cnt-1] = 0;
            tmp_b2b = 0;

            for (j=0; j<num_substream; j++) {
                high = substream[j];
                if (ic_flag) {
                    if (num_bursts < 2) {
                        if (++repeat_1 == 3) {
                            repeat_1 = 0;
                            /* Abort fleet and try to find lower bound */
                            abort_fleet = 1;
                            lower_bound = 1;
                            increase_stream_len = 0;
                            break;
                        }
                    } else if (num_bursts <= 5) {
                        if (++repeat_2 == 3) {
                            repeat_2 = 0;
                            /* Abort fleet and retry with longer stream length */
                            abort_fleet = 1;
                            increase_stream_len = 1;
                            break;
                        }
                    } else {
                        increase_stream_len = 0;
                        len = eliminate_b2b_pkt_ic(arrv_tm, owd, owdfortd, low,
                                                   high,
                                                   &num_rcvr_cs[stream_cnt-1],
                                                   &tmp_b2b);
                        /*
                          for(p=0; p<len; p++)
                              printf("%d %f\n",p,owdfortd[p]);
                        */
                        pct_metric[trend_idx] =
                            pairwise_comparision_test(owdfortd, 0, len);
                        pdt_metric[trend_idx] =
                            pairwise_diff_test(owdfortd, 0, len);
                        trend_idx += 1;
                    }
                } else {
                    len =
                        eliminate_rcvr_side_CS(arrv_tm, owd, owdfortd, low,
                                               high,
                                               &num_rcvr_cs[stream_cnt-1],
                                               &tmp_b2b);
                    if (len > MIN_STREAM_LEN) get_trend(owdfortd, len);
                }
                low = high + 1;
            }
            if (abort_fleet) break;
            else {
                b2b_pkt_per_stream[stream_cnt - 1] = tmp_b2b;
                ctr_code = CONTINUE_STREAM | CTR_CODE;
                send_ctr_mesg(ctr_buff, ctr_code);
            }
        }
        pkt_rcvd = 0;

        /* A hack for slow links */
        stream_duration = stream_len * time_interval;
        if (stream_duration >= 500000) {
            slow = 1;
            break;
        }
    }    /* end of while (stream_cnt < num_stream ) */

    if (Verbose) printf("\n");
    fprintf(pathload_fp, "\n");

    if (abort_fleet) {
        printf("\tAborting fleet. Stream_cnt %d\n", stream_cnt);
        ctr_code = ABORT_FLEET | CTR_CODE;
        send_ctr_mesg(ctr_buff, ctr_code);
        return_val = -1;
    } else
        print_contextswitch_info(num_sndr_cs, num_rcvr_cs,
                                 b2b_pkt_per_stream, stream_cnt);

    exp_fleet_id++;
    free(pkt_buf);

    return return_val;
}

void print_contextswitch_info(l_int32 num_sndr_cs[], l_int32 num_rcvr_cs[],
                              l_int32 discard[], l_int32 stream_cnt)
{
    l_int32 j;

    if (Verbose) printf("  # of CS @ sndr        :: ");
    fprintf(pathload_fp, "  # of CS @ sndr        :: ");
    for (j=0; j<stream_cnt-1; j++) {
        if (Verbose) printf(":%2d", num_sndr_cs[j]);
        fprintf(pathload_fp, ":%2d", num_sndr_cs[j]);
    }
    if (Verbose) printf("\n");
    fprintf(pathload_fp, "\n");
    if (Verbose) printf("  # of CS @ rcvr        :: ");
    fprintf(pathload_fp, "  # of CS @ rcvr        :: ");
    for (j = 0; j < stream_cnt - 1; j++) {
        if (Verbose) printf(":%2d", num_rcvr_cs[j]);
        fprintf(pathload_fp, ":%2d", num_rcvr_cs[j]);
    }
    if (Verbose) printf("\n");
    fprintf(pathload_fp, "\n");
    if (Verbose) printf("  # of DS @ rcvr        :: ");
    fprintf(pathload_fp, "  # of DS @ rcvr        :: ");
    for (j=0; j<stream_cnt-1; j++) {
        if (Verbose) printf(":%2d", discard[j]);
        fprintf(pathload_fp, ":%2d", discard[j]);
    }
    if (Verbose) printf("\n");
    fprintf(pathload_fp, "\n");
}

void sig_sigusr1()
{
    return;
}

void sig_alrm()
{
    terminate_gracefully(exp_start_time);
    exit(0);
}

// TCP ソケットの I/O を監視し, 受け取った control code に応じて arg を設定する.
// マルチスレッドサポート時のみ呼ばれる関数
void *ctrl_listen(void *arg)
{
    struct timeval select_tv;
    fd_set readset;
    l_int32 ret_val;
    char ctr_buff[8];

#ifdef THRLIB
    FD_ZERO(&readset);
    FD_SET(sock_tcp, &readset);

    // select() のタイムアウトは 100 秒
    select_tv.tv_sec = 100;
    select_tv.tv_usec = 0;

    // sock_tcp の I/O を監視, 読み込み可能か判定
    if (select(sock_tcp+1, &readset, NULL, NULL, &select_tv) > 0) {
        if (FD_ISSET(sock_tcp, &readset)) {
            if ((ret_val = recv_ctr_mesg(sock_tcp, ctr_buff)) != -1) {

                // recv_fleet() から呼ばれたときはおそらくこっち
                if ((((ret_val & CTR_CODE) >> 31) == 1)
                    && ((ret_val & 0x7fffffff) == FINISHED_STREAM)) {
                    while ((ret_val = recv_ctr_mesg(sock_tcp, ctr_buff)) == -1);
                    if (ret_val == ((thr_arg *) arg)->stream_cnt) {
                        ((thr_arg *) arg)->finished_stream = 1;
                        pthread_kill(((thr_arg *) arg)->ptid, SIGUSR1);
                        pthread_exit(NULL);
                    }

                // recv_train() から呼ばれたときはおそらくこっち
                } else if ((((ret_val & CTR_CODE) >> 31) == 1) &&
                           ((ret_val & 0x7fffffff) == FINISHED_TRAIN)) {
                    select_tv.tv_sec = 0;
                    select_tv.tv_usec = 2000;
                    select(1, NULL, NULL, NULL, &select_tv);
                    ((thr_arg *) arg)->finished_stream = 1;
                    pthread_kill(((thr_arg *) arg)->ptid, SIGUSR1);
                    pthread_exit(NULL);
                }
            }
        }
    }
#endif

    return NULL;
}

void get_trend(double owdfortd[], l_int32 pkt_cnt)
{
     double median_owd[MAX_STREAM_LEN];
     l_int32 median_owd_len = 0;
     double ordered[MAX_STREAM_LEN];
     l_int32 j, count, pkt_per_min;

     // pkt_per_min = 5;
     pkt_per_min = (int) floor(sqrt((double) pkt_cnt));
     count = 0;
     for (j = 0; j < pkt_cnt; j = j+pkt_per_min) {
          if (j+pkt_per_min >= pkt_cnt)
               count = pkt_cnt - j;
          else
               count = pkt_per_min;
          order_dbl(owdfortd, ordered, j, count);
          if (count % 2 == 0)
               median_owd[median_owd_len++] =
                    (ordered[(int) (count * .5) - 1] + ordered[(int) (count * 0.5)]) / 2;
          else
               median_owd[median_owd_len++] = ordered[(int) (count * 0.5)];
     }
     pct_metric[trend_idx] =
          pairwise_comparision_test(median_owd, 0, median_owd_len);
     pdt_metric[trend_idx] = pairwise_diff_test(median_owd, 0, median_owd_len);
     trend_idx += 1;
}

/*
  Order an array of doubles using bubblesort
*/
void order_dbl(double unord_arr[], double ord_arr[], l_int32 start, l_int32 num_elems)
{
    l_int32 i, j, k;
    double temp;

    for (i=start, k=0; i<start+num_elems; i++, k++) ord_arr[k] = unord_arr[i];
    for (i=1; i<num_elems; i++) {
        for (j=i-1; j>=0; j--)
            if (ord_arr[j+1] < ord_arr[j]) {
                temp = ord_arr[j];
                ord_arr[j] = ord_arr[j + 1];
                ord_arr[j + 1] = temp;
            } else
                break;
    }
}

// Order an array of float using bubblesort.
void order_float(float unord_arr[], float ord_arr[], l_int32 start, l_int32 num_elems)
{
    l_int32 i, j, k;
    double temp;

    for (i=start, k=0; i<start+num_elems; i++, k++) ord_arr[k] = unord_arr[i];
    for (i=1; i<num_elems; i++) {
        for (j=i-1; j>=0; j--)
            if (ord_arr[j + 1] < ord_arr[j]) {
                temp = ord_arr[j];
                ord_arr[j] = ord_arr[j + 1];
                ord_arr[j + 1] = temp;
            } else
                break;
    }
}

// Order an array of l_int32 using bubblesort.
void order_int(l_int32 unord_arr[], l_int32 ord_arr[], l_int32 num_elems)
{
    l_int32 i, j;
    l_int32 temp;

    for (i = 0; i < num_elems; i++) ord_arr[i] = unord_arr[i];
    for (i = 1; i < num_elems; i++) {
        for (j = i - 1; j >= 0; j--)
            if (ord_arr[j + 1] < ord_arr[j]) {
                temp = ord_arr[j];
                ord_arr[j] = ord_arr[j + 1];
                ord_arr[j + 1] = temp;
            } else
                break;
    }
}

// Send a message through the control stream.
void send_ctr_mesg(char *ctr_buff, l_int32 ctr_code)
{
    l_int32 ctr_code_n = htonl(ctr_code);

    memcpy((void *) ctr_buff, &ctr_code_n, sizeof(l_int32));
    if (write(sock_tcp, ctr_buff, sizeof(l_int32)) != sizeof(l_int32)) {
        fprintf(stderr, "send control message failed:\n");
        exit(-1);
    }
}

// Receive message from the control stream
l_int32 recv_ctr_mesg(l_int32 ctr_strm, char *ctr_buff)
{
    l_int32 ctr_code;

    gettimeofday(&first_time, 0);
    if (read(ctr_strm, ctr_buff, sizeof(l_int32)) != sizeof(l_int32))
        return -1;
    gettimeofday(&second_time, 0);
    memcpy(&ctr_code, ctr_buff, sizeof(l_int32));

    return (ntohl(ctr_code));
}


// Compute the time difference in microseconds
// between two timeval measurements.
double time_to_us_delta(struct timeval tv1, struct timeval tv2)
{
    double time_us;

    time_us = (double) ((tv2.tv_sec - tv1.tv_sec) * 1000000 +
                        (tv2.tv_usec - tv1.tv_usec));

    return time_us;
}

// Compute the average of the set of measurements data[].
double get_avg(double data[], l_int32 num_values)
{
    l_int32 i;
    double sum_;

    sum_ = 0;
    for (i=0; i<num_values; i++)
        sum_ += data[i];

    return (sum_ / (double) num_values);
}

// PCT test to detect increasing trend in a stream.
double pairwise_comparision_test(double array[], l_int32 start, l_int32 end)
{
    l_int32 improvement = 0, i;
    double total;

    if ((end - start) >= MIN_PARTITIONED_STREAM_LEN) {
        for (i = start; i < end - 1; i++)
            if (array[i] < array[i + 1]) improvement += 1;
        total = (end - start);
        return ((double) improvement / total);
    } else
        return -1;
}

// PDT test to detect increasing trend in a stream.
double pairwise_diff_test(double array[], l_int32 start, l_int32 end)
{
    double y = 0, y_abs = 0;
    l_int32 i;

    if ((end - start) >= MIN_PARTITIONED_STREAM_LEN) {
        for (i = start + 1; i < end; i++) {
            y += array[i] - array[i - 1];
            y_abs += fabs(array[i] - array[i - 1]);
        }
        return y / y_abs;
    } else
        return 2.0;
}

// Return the grey bandwidth resolution in Mbit/sec.
double grey_bw_resolution()
{
    if (adr)
        return (0.05 * adr < 12 ? 0.05 * adr : 12);
    else
        return min_rate;
}

// Test whether Rmax and Rmin range is smaller than user specified bw resolution
// or whether Gmin and Gmax range is smaller than grey bw resolution.
l_int32 converged()
{
    if ((converged_gmx_rmx_tm && converged_gmn_rmn_tm) || converged_rmn_rmx_tm)
        return 1;
    else if (tr_max != 0 && tr_max != tr_min) {
        if (tr_max - tr_min <= bw_resol) {
            converged_rmn_rmx = 1;
            return 1;
        } else if (tr_max - grey_max <= grey_bw_resolution() &&
                   grey_min - tr_min <= grey_bw_resolution()) {
            converged_gmn_rmn = 1;
            converged_gmx_rmx = 1;
            return 1;
        }
    }

    return 0;
}

// Calculate next fleet rate when current fleet showed INCREASING trend.
void radj_increasing()
{
    if (grey_max != 0 && grey_max >= tr_min) {
        if (tr_max - grey_max <= grey_bw_resolution()) {
            converged_gmx_rmx = 1;
            exp_flag = 0;
            if (grey_min || tr_min) radj_notrend();
            else {
                if (grey_min < grey_max) tr = grey_min / 2.0;
                else tr = grey_max / 2.0;
            }
        } else tr = (tr_max + grey_max) / 2.0;
    } else
        tr = (tr_max + tr_min) / 2.0 < min_rate ? min_rate : (tr_min + tr_max) / 2.;
}

// Calculate next fleet rate when current fleet showed NOTREND trend.
void radj_notrend()
{
    if (exp_flag) tr = (2 * tr > max_rate ? max_rate : 2 * tr);
    else {
        if (grey_min != 0 && grey_min <= tr_max) {
            if (grey_min - tr_min <= grey_bw_resolution()) {
                converged_gmn_rmn = 1;
                radj_increasing();
            } else
                tr = (tr_min + grey_min) / 2.0 < min_rate
                    ? min_rate : (tr_min + grey_min) / 2.0;
        } else
            tr = (tr_max + tr_min) / 2.0 < min_rate
                ? min_rate : (tr_min + tr_max) / 2.0;
    }
}

// Calculate next fleet rate when current fleet showed GREY trend.
void radj_greymax()
{
    if (tr_max == 0)
        tr = (tr + 0.5 * tr) < max_rate ? (tr + 0.5 * tr) : max_rate;
    else if (tr_max - grey_max <= grey_bw_resolution()) {
        converged_gmx_rmx = 1;
        radj_greymin();
    } else
        tr = (tr_max + grey_max) / 2.0;
}

// Calculate next fleet rate when current fleet showed GREY trend.
void radj_greymin()
{
    if (grey_min - tr_min <= grey_bw_resolution()) {
        converged_gmn_rmn = 1;
        radj_greymax();
    } else
        tr = (tr_min + grey_min) / 2.0 < min_rate
            ? min_rate : (tr_min + grey_min) / 2.0;
}

/*
    depending upon trend in fleet :-
    - update the state variables.
    - decide the next fleet rate.
    return -1 when converged.
*/
l_int32 rate_adjustment(l_int32 flag)
{
    l_int32 ret_val = 0;

    if (flag == INCREASING) {
        if (max_rate_flag) max_rate_flag = 0;
        if (grey_max >= tr) grey_max = grey_min = 0;
        tr_max = tr;
        if (!converged_gmx_rmx_tm) {
            if (!converged()) radj_increasing();
            else ret_val = -1;    //return -1;
        } else {
            exp_flag = 0;
            if (!converged()) radj_notrend();
        }
    } else if (flag == NOTREND) {
        if (grey_min < tr) grey_min = 0;
        if (grey_max < tr) grey_max = grey_min = 0;
        if (tr > tr_min) tr_min = tr;
        if (!converged_gmn_rmn_tm && !converged()) radj_notrend();
        else ret_val = -1;    //return -1;
    } else if (flag == GREY) {
        if (grey_max == 0 && grey_min == 0) grey_max = grey_min = tr;
        if (tr == grey_max || tr > grey_max) {
            grey_max = tr;
            if (!converged_gmx_rmx_tm) {
                if (!converged()) radj_greymax();
                else ret_val = -1;    //return -1;
            } else {
                exp_flag = 0;
                if (!converged()) radj_notrend();
                else ret_val = -1;
            }
        } else if (tr < grey_min || grey_min == 0) {
            grey_min = tr;
            if (!converged()) radj_greymin();
            else ret_val = -1;    //return -1;
        }
    }

    if (Verbose) {
        printf("  Rmin-Rmax             :: %.2f-%.2fMbps\n", tr_min, tr_max);
        printf("  Gmin-Gmax             :: %.2f-%.2fMbps\n", grey_min, grey_max);
    }
    fprintf(pathload_fp, "  Rmin-Rmax             :: %.2f-%.2fMbps\n",
            tr_min, tr_max);
    fprintf(pathload_fp, "  Gmin-Gmax             :: %.2f-%.2fMbps\n",
            grey_min, grey_max);

    if (ret_val == -1) return -1;
    if (tr >= max_rate) max_rate_flag++;
    if (max_rate_flag > 1) return -1;
    if (min_rate_flag > 1) return -1;
    transmission_rate = (l_int32) rint(1000000 * tr);

    return 0;
}

// Recalculate stream parameters L (packet size), T (interval of packets).
// calc_param() returns -1 if we have reached to upper/lower limits of
// the stream parameters like L, T. Otherwise returns 0.
l_int32 calc_param()
{
    double tmp_tr;                // [Mbit/sec]
    l_int32 tmp;                  // [byte]
    l_int32 tmp_time_interval;    // [usec]

    // tr が 150 Mbps 未満のとき
    // Dimension: time_interval [usec], cur_pkt_sz [byte]
    if (tr < 150) {
        time_interval = (80 > min_time_interval ? 80 : min_time_interval);
        cur_pkt_sz = rint(tr * time_interval / 8.0) - 28;
        if (cur_pkt_sz < MIN_PKT_SZ) {
            cur_pkt_sz = MIN_PKT_SZ;
            time_interval = rint((cur_pkt_sz + 28) * 8.0 / tr);
            tr = (cur_pkt_sz + 28) * 8.0 / time_interval;
        } else if (cur_pkt_sz > max_pkt_sz) {
            cur_pkt_sz = max_pkt_sz;
            time_interval = min_time_interval;
            tmp_tr = (cur_pkt_sz + 28) * 8.0 / time_interval;
            if (equal(tr, tmp_tr)) tr = tmp_tr;
            else return -1;
        }

    // tr が 150 Mbps 以上 600 Mbps 未満のとき
    } else if (tr < 600) {
        tmp_tr = tr;
        tmp_time_interval = rint((max_pkt_sz + 28) * 8 / tr);
        if (cur_pkt_sz == max_pkt_sz && tmp_time_interval == time_interval)
            return -1;
        time_interval = tmp_time_interval;
        tmp = rint(tr * time_interval / 8.0) - 28;
        cur_pkt_sz = tmp < max_pkt_sz ? tmp : max_pkt_sz;
        tr = (cur_pkt_sz + 28) * 8.0 / time_interval;
        if ((tr_min && (equal(tr, tr_min) || tr < tr_min))
            || (grey_max && tmp_tr > grey_max
                && (equal(tr, grey_max) || tr < grey_max))) {
            do {
                --time_interval;
                cur_pkt_sz = rint(tr * time_interval / 8.0) - 28;
            } while (cur_pkt_sz > max_pkt_sz);
            tr = (cur_pkt_sz + 28) * 8.0 / time_interval;
        }

    // tr が 600 Mbps 以上のとき？
    } else {
        cur_pkt_sz = max_pkt_sz;
        time_interval = rint((cur_pkt_sz + 28) * 8.0 / tr);
        tr = (cur_pkt_sz + 28) * 8.0 / time_interval;
        if ((tr_min && (equal(tr, tr_min) || tr < tr_min))) return -1;
        if (equal(tr, tr_max)) {
            tr_max = tr;
            if (grey_max) {
                converged_gmx_rmx_tm = 1;
                if (!converged_gmn_rmn && !converged_gmn_rmn_tm)
                    radj_notrend();
                else
                    return -1;
            } else {
                converged_rmn_rmx = 1;
                return -1;
            }
        }
    }

    return 0;
}

/*
  splits stream if sender sent packets more than
  time_interval+1000 usec apart.
*/
l_int32 eliminate_sndr_side_CS(double sndr_time_stamp[], l_int32 split_owd[])
{
    l_int32 j = 0, k;
    l_int32 cs_threshold;

    cs_threshold =
        2 * time_interval > time_interval + 1000
        ? 2 * time_interval : time_interval + 1000;
    for (k = 0; k < stream_len - 1; k++) {
        if (sndr_time_stamp[k] == 0 || sndr_time_stamp[k+1] == 0)
            continue;
        else if ((sndr_time_stamp[k+1] - sndr_time_stamp[k]) > cs_threshold)
            split_owd[j++] = k;
    }

    return j;
}

/*
  discards owd of packets received when receiver was NOT running.
*/
l_int32 eliminate_rcvr_side_CS(double rcvr_time_stamp[], double owd[],
                               double owdfortd[], l_int32 low, l_int32 high,
                               l_int32 *num_rcvr_cs, l_int32 *tmp_b2b)
{
    l_int32 b2b_pkt[MAX_STREAM_LEN];
    l_int32 i, k = 0;
    l_int32 len = 0;
    l_int32 min_gap;

    min_gap =
        MIN_TIME_INTERVAL > 1.5 * rcv_latency
        ? MIN_TIME_INTERVAL : 2.5 * rcv_latency;
    for (i = low; i <= high; i++) {
        if (rcvr_time_stamp[i] == 0 || rcvr_time_stamp[i+1] == 0)
            continue;
        else if ((rcvr_time_stamp[i+1] - rcvr_time_stamp[i]) > min_gap)
            owdfortd[len++] = owd[i];
        else
            b2b_pkt[k++] = i;
    }

    /* go through discarded list and count b2b discards as 1 CS instance */
    for (i = 1; i < k; i++)
        if (b2b_pkt[i] - b2b_pkt[i-1] != 1) (*num_rcvr_cs)++;
    *tmp_b2b += k;

    return len;
}

/* eliminates packets received b2b due to IC */
l_int32 eliminate_b2b_pkt_ic(double rcvr_time_stamp[], double owd[],
                             double owdfortd[], l_int32 low, l_int32 high,
                             l_int32 *num_rcvr_cs, l_int32 *tmp_b2b)
{
    l_int32 b2b_pkt[MAX_STREAM_LEN];
    l_int32 i, k = 0;
    l_int32 len = 0;
    l_int32 min_gap;
    l_int32 tmp = 0;

    min_gap =
        MIN_TIME_INTERVAL > 3 * rcv_latency
        ? MIN_TIME_INTERVAL : 3 * rcv_latency;
    for (i = low; i <= high; i++) {
        if (rcvr_time_stamp[i] == 0 || rcvr_time_stamp[i + 1] == 0)
            continue;
        if ((rcvr_time_stamp[i + 1] - rcvr_time_stamp[i]) < min_gap) {
            b2b_pkt[k++] = i;
            tmp++;
        } else {
            if (tmp >= 3) {
                tmp = 0;
                owdfortd[len++] = owd[i];
            }
        }
    }

    return len;
}

/* Adjust offset to zero again  */
void adjust_offset_to_zero(double owd[], l_int32 len)
{
    l_int32 owd_min = 0;
    l_int32 i;

    for (i = 0; i < len; i++) {
        if (owd_min == 0 && owd[i] != 0) owd_min = owd[i];
        else if (owd_min != 0 && owd[i] != 0 && owd[i] < owd_min)
            owd_min = owd[i];
    }

    for (i = 0; i < len; i++) {
        if (owd[i] != 0) owd[i] -= owd_min;
    }
}

#define INCR    1
#define NOTR    2
#define DISCARD 3
#define UNCL    4    // unclassifiable? 分類できない

// Obtain PCT trends of streams from these PCT metrics.
void get_pct_trend(double pct_metric[], l_int32 pct_trend[],
                   l_int32 pct_result_cnt)
{
    l_int32 i;

    for (i = 0; i < pct_result_cnt; i++) {
        pct_trend[i] = UNCL;
        if (pct_metric[i] == -1) {
            if (Verbose) printf("d");
            fprintf(pathload_fp, "d");
            pct_trend[i] = DISCARD;
        } else if (pct_metric[i] > 1.1 * PCT_THRESHOLD) {
            if (Verbose) printf("I");
            fprintf(pathload_fp, "I");
            pct_trend[i] = INCR;
        } else if (pct_metric[i] < 0.9 * PCT_THRESHOLD) {
            if (Verbose) printf("N");
            fprintf(pathload_fp, "N");
            pct_trend[i] = NOTR;
        } else if (pct_metric[i] <= PCT_THRESHOLD * 1.1
                   && pct_metric[i] >= PCT_THRESHOLD * 0.9) {
            if (Verbose) printf("U");
            fprintf(pathload_fp, "U");
            pct_trend[i] = UNCL;
        }
    }

    if (Verbose) printf("\n");
    fprintf(pathload_fp, "\n");
}

// Obtain PDT trends of streams from these PDT metrics.
void get_pdt_trend(double pdt_metric[], l_int32 pdt_trend[],
                   l_int32 pdt_result_cnt)
{
    l_int32 i;

    for (i = 0; i < pdt_result_cnt; i++) {
        if (pdt_metric[i] == 2) {
            if (Verbose) printf("d");
            fprintf(pathload_fp, "d");
            pdt_trend[i] = DISCARD;
        } else if (pdt_metric[i] > 1.1 * PDT_THRESHOLD) {
            if (Verbose) printf("I");
            fprintf(pathload_fp, "I");
            pdt_trend[i] = INCR;
        } else if (pdt_metric[i] < 0.9 * PDT_THRESHOLD) {
            if (Verbose) printf("N");
            fprintf(pathload_fp, "N");
            pdt_trend[i] = NOTR;
        } else if (pdt_metric[i] <= PDT_THRESHOLD * 1.1
                   && pdt_metric[i] >= PDT_THRESHOLD * 0.9) {
            if (Verbose) printf("U");
            fprintf(pathload_fp, "U");
            pdt_trend[i] = UNCL;
        }
    }

    if (Verbose) printf("\n");
    fprintf(pathload_fp, "\n");
}


// Return trend of fleet or -1 if more than 50% of streams were discarded.
l_int32 aggregate_trend_result()
{
    l_int32 pct_trend[TREND_ARRAY_LEN], pdt_trend[TREND_ARRAY_LEN];
    l_int32 i_cnt = 0;             // trend が INCREASING な stream の数
    l_int32 n_cnt = 0;             // trend が NOTREND な stream の数
    l_int32 num_dscrd_strm = 0;    // trend が DISCARD な stream の数
    l_int32 total = 0;             // stream の合計
    l_int32 i = 0;

    // PCT メトリックの表示
    if (Verbose) printf("  PCT metric/stream[%2d] :: ", trend_idx);
    fprintf(pathload_fp, "  PCT metric/stream[%2d] :: ", trend_idx);
    for (i=0; i<trend_idx; i++) {
        if (Verbose) printf("%3.2f:", pct_metric[i]);
        fprintf(pathload_fp, "%3.2f:", pct_metric[i]);
    }
    if (Verbose) printf("\n");
    fprintf(pathload_fp, "\n");

    // PDT メトリックの表示
    if (Verbose) printf("  PDT metric/stream[%2d] :: ", trend_idx);
    fprintf(pathload_fp, "  PDT metric/stream[%2d] :: ", trend_idx);
    for (i = 0; i < trend_idx; i++) {
        if (Verbose) printf("%3.2f:", pdt_metric[i]);
        fprintf(pathload_fp, "%3.2f:", pdt_metric[i]);
    }
    if (Verbose) printf("\n");
    fprintf(pathload_fp, "\n");

    // PCT trend を計算
    if (Verbose) printf("  PCT Trend/stream [%2d] :: ", trend_idx);
    fprintf(pathload_fp, "  PCT Trend/stream [%2d] :: ", trend_idx);
    get_pct_trend(pct_metric, pct_trend, trend_idx);

    // PDT trend を計算
    if (Verbose) printf("  PDT Trend/stream [%2d] :: ", trend_idx);
    fprintf(pathload_fp, "  PDT Trend/stream [%2d] :: ", trend_idx);
    get_pdt_trend(pdt_metric, pdt_trend, trend_idx);

    // PCT/PDT trend から, stream の trend を判断
    if (Verbose) printf("  Trend per stream [%2d] :: ", trend_idx);
    fprintf(pathload_fp, "  Trend per stream [%2d] :: ", trend_idx);
    for (i = 0; i < trend_idx; i++) {
        if (pct_trend[i] == DISCARD || pdt_trend[i] == DISCARD) {
            if (Verbose) printf("d");
            fprintf(pathload_fp, "d");
            num_dscrd_strm++;
        } else if (pct_trend[i] == INCR && pdt_trend[i] == INCR) {
            if (Verbose) printf("I");
            fprintf(pathload_fp, "I");
            i_cnt++;
        } else if (pct_trend[i] == NOTR && pdt_trend[i] == NOTR) {
            if (Verbose) printf("N");
            fprintf(pathload_fp, "N");
            n_cnt++;
        } else if (pct_trend[i] == INCR && pdt_trend[i] == UNCL) {
            if (Verbose) printf("I");
            fprintf(pathload_fp, "I");
            i_cnt++;
        } else if (pct_trend[i] == NOTR && pdt_trend[i] == UNCL) {
            if (Verbose) printf("N");
            fprintf(pathload_fp, "N");
            n_cnt++;
        } else if (pdt_trend[i] == INCR && pct_trend[i] == UNCL) {
            if (Verbose) printf("I");
            fprintf(pathload_fp, "I");
            i_cnt++;
        } else if (pdt_trend[i] == NOTR && pct_trend[i] == UNCL) {
            if (Verbose) printf("N");
            fprintf(pathload_fp, "N");
            n_cnt++;
        } else {
            if (Verbose) printf("U");
            fprintf(pathload_fp, "U");
        }
        total++;
    }
    if (Verbose) printf("\n");
    fprintf(pathload_fp, "\n");

    // check whether number of usable streams is
    // at least 50% of requested number of streams.
    total -= num_dscrd_strm;
    if (total < num_stream / 2 && !slow && !interrupt_coalescence) {
        bad_fleet_cs = 1;
        retry_fleet_cnt_cs++;
        return -1;
    } else {
        bad_fleet_cs = 0;
        retry_fleet_cnt_cs = 0;
    }

    // fleet 全体としての trend を返す
    // ちなみに, AGGREGATE_THRESHOLD = 0.6
    if ((double) i_cnt / (total) >= AGGREGATE_THRESHOLD) {
        if (Verbose) printf("  Aggregate trend       :: INCREASING\n");
        fprintf(pathload_fp, "  Aggregate trend       :: INCREASING\n");
        return INCREASING;
    } else if ((double) n_cnt / (total) >= AGGREGATE_THRESHOLD) {
        if (Verbose) printf("  Aggregate trend       :: NO TREND\n");
        fprintf(pathload_fp, "  Aggregate trend       :: NO TREND\n");
        return NOTREND;
    } else {
        if (Verbose) printf("  Aggregate trend       :: GREY\n");
        fprintf(pathload_fp, "  Aggregate trend       :: GREY\n");
        return GREY;
    }
}

l_int32 get_sndr_time_interval(double snd_time[], double *sum)
{
    l_int32 k, j = 0, new_j = 0;
    double ordered[MAX_STREAM_LEN];
    double ltime_interval[MAX_STREAM_LEN];

    for (k = 0; k < stream_len - 1; k++) {
        if (snd_time[k] == 0 || snd_time[k + 1] == 0) continue;
        else ltime_interval[j++] = snd_time[k + 1] - snd_time[k];
    }
    order_dbl(ltime_interval, ordered, 0, j);
    /* discard the top 15% as outliers  */
    new_j = j - rint(j * 0.15);
    for (k = 0; k < new_j; k++) *sum += ordered[k];

    return new_j;
}

void get_sending_rate()
{
    time_interval = snd_time_interval / num;
    cur_req_rate = tr;
    cur_actual_rate = (28 + cur_pkt_sz) * 8.0 / time_interval;

    if (!equal(cur_req_rate, cur_actual_rate)) {
        if (!grey_max && !grey_min) {
            if (tr_min && tr_max && (less_than(cur_actual_rate, tr_min)
                                     || equal(cur_actual_rate, tr_min)))
                converged_rmn_rmx_tm = 1;
            if (tr_min && tr_max && (less_than(tr_max, cur_actual_rate)
                                     || equal(tr_max, cur_actual_rate)))
                converged_rmn_rmx_tm = 1;
        } else if (cur_req_rate < tr_max && cur_req_rate > grey_max) {
            if (!(less_than(cur_actual_rate, tr_max)
                  && grtr_than(cur_actual_rate, grey_max)))
                converged_gmx_rmx_tm = 1;
        } else if (cur_req_rate < grey_min && cur_req_rate > tr_min) {
            if (!(less_than(cur_actual_rate, grey_min)
                  && grtr_than(cur_actual_rate, tr_min)))
                converged_gmn_rmn_tm = 1;
        }
    }

    tr = cur_actual_rate;
    transmission_rate = (l_int32) rint(1000000 * tr);

    if (Verbose)
        printf("  Fleet Parameter(act)  :: R=%.2fMbps, L=%ldB, K=%ldpackets, T=%ldusec\n",
               cur_actual_rate, cur_pkt_sz, stream_len, time_interval);
    fprintf(pathload_fp,
            "  Fleet Parameter(act)  :: R=%.2fMbps, L=%ldB, K=%ldpackets, T=%ldusec\n",
            cur_actual_rate, cur_pkt_sz, stream_len, time_interval);
    snd_time_interval = 0;
    num = 0;
}

void terminate_gracefully(struct timeval exp_start_time)
{
    l_int32 ctr_code;
    char ctr_buff[8], buff[26];
    struct timeval exp_end_time;
    double min = 0, max = 0;

    ctr_code = TERMINATE | CTR_CODE;
    send_ctr_mesg(ctr_buff, ctr_code);

    gettimeofday(&exp_end_time, NULL);
    strncpy(buff, ctime(&(exp_end_time.tv_sec)), 24);
    buff[24] = '\0';
    if (verbose || Verbose) printf("\n\t*****  RESULT *****\n");
    fprintf(pathload_fp, "\n\t*****  RESULT *****\n");

    if (netlog) netlogger();

    if (min_rate_flag) {
        if (verbose || Verbose) {
            printf("Avail-bw < minimum sending rate.\n");
            printf("Increase MAX_TIME_INTERVAL in pathload_rcv.h from 200000 usec to a higher value.\n");
        }
        fprintf(pathload_fp, "Avail-bw < minimum sending rate.\n");
        fprintf(pathload_fp,
                "Increase MAX_TIME_INTERVAL in pathload_rcv.h from 200000 usec to a higher value.\n");
    } else if (max_rate_flag && !interrupt_coalescence) {
        if (verbose || Verbose) {
            printf("Avail-bw > maximum sending rate.\n");
            if (tr_min) printf("Avail-bw > %.2f (Mbps)\n", tr_min);
        }
        fprintf(pathload_fp, "Avail-bw > maximum sending rate.\n");
        if (tr_min) fprintf(pathload_fp, "Avail-bw > %.2f (Mbps)\n", tr_min);
    } else if (bad_fleet_cs && !interrupt_coalescence) {
        if (verbose || Verbose)
            printf("Measurement terminated due to frequent CS @ sender/receiver.\n");
        fprintf(pathload_fp,
                "Measurement terminated due to frequent CS @ sender/receiver.\n");
        if ((tr_min && tr_max) || (grey_min && grey_max)) {
            if (grey_min && grey_max) {
                min = grey_min;
                max = grey_max;
            } else {
                min = tr_min;
                max = tr_max;
            }

            if (verbose || Verbose)
                printf("Available bandwidth range : %.2f - %.2f (Mbps)\n",
                       min, max);
            fprintf(pathload_fp,
                    "Available bandwidth range : %.2f - %.2f (Mbps)\n",
                    min, max);

            if (verbose || Verbose)
                printf("Measurements finished at %s\n", buff);
            fprintf(pathload_fp, "Measurements finished at %s\n", buff);

            if (verbose || Verbose)
                printf("Measurement latency is %.2f sec\n",
                       time_to_us_delta(exp_start_time, exp_end_time) / 1000000);
            fprintf(pathload_fp, "Measurement latency is %.2f sec\n",
                    time_to_us_delta(exp_start_time, exp_end_time) / 1000000);
        }
    } else {
        if (!interrupt_coalescence
            && ((converged_gmx_rmx_tm && converged_gmn_rmn_tm)
                || converged_rmn_rmx_tm)) {
            if (Verbose)
                printf ("Actual probing rate != desired probing rate.\n");
            fprintf(pathload_fp,
                    "Actual probing rate != desired probing rate.\n");
            if (converged_rmn_rmx_tm) {
                min = tr_min;
                max = tr_max;
            } else {
                min = grey_min;
                max = grey_max;
            }
        } else if (!interrupt_coalescence && converged_rmn_rmx) {
            if (Verbose)
                printf ("User specified bandwidth resolution achieved\n");
            fprintf(pathload_fp,
                    "User specified bandwidth resolution achieved\n");
            min = tr_min;
            max = tr_max;
        } else if (!interrupt_coalescence && converged_gmn_rmn
                   && converged_gmx_rmx) {
            if (Verbose) printf("Exiting due to grey bw resolution\n");
            fprintf(pathload_fp, "Exiting due to grey bw resolution\n");
            min = grey_min;
            max = grey_max;
        } else {
            min = tr_min;
            max = tr_max;
        }

        if (verbose || Verbose) {
            if (lower_bound) {
                printf("Receiver NIC has interrupt coalescence enabled\n");
                printf("Available bandwidth is greater than %.2f (Mbps)\n",
                       min);
            } else
                printf ("Available bandwidth range : %.2f - %.2f (Mbps)\n",
                        min, max);
            printf("Measurements finished at %s \n", buff);
            printf("Measurement latency is %.2f sec \n",
                   time_to_us_delta(exp_start_time, exp_end_time) / 1000000);
        }
        if (lower_bound) {
            fprintf(pathload_fp,
                    "Receiver NIC has interrupt coalescence enabled\n");
            fprintf(pathload_fp,
                    "Available bandwidth is greater than %.2f (Mbps)\n", min);
        } else
            fprintf(pathload_fp,
                    "Available bandwidth range : %.2f - %.2f (Mbps)\n",
                    min, max);
        fprintf(pathload_fp, "Measurements finished at %s \n", buff);
        fprintf(pathload_fp, "Measurement latency is %.2f sec \n",
                time_to_us_delta(exp_start_time, exp_end_time) / 1000000);
    }

    if (netlog) fclose(netlog_fp);
    fclose(pathload_fp);
    close(sock_tcp);

    exit(0);
}

void netlogger()
{
    struct tm *tm;
    struct hostent *rcv_host, *snd_host;
    char rcv_name[256];
    struct timeval curr_time;

    gettimeofday(&curr_time, NULL);
    tm = gmtime(&curr_time.tv_sec);
    fprintf(netlog_fp, "DATE=%4d", tm->tm_year + 1900);
    print_time(netlog_fp, tm->tm_mon + 1);
    print_time(netlog_fp, tm->tm_mday);
    print_time(netlog_fp, tm->tm_hour);
    print_time(netlog_fp, tm->tm_min);
    if (tm->tm_sec < 10) {
        fprintf(netlog_fp, "0");
        fprintf(netlog_fp, "%1.6f", tm->tm_sec + curr_time.tv_usec / 1000000.0);
    } else {
        fprintf(netlog_fp, "%1.6f", tm->tm_sec + curr_time.tv_usec / 1000000.0);
    }
    gethostname(rcv_name, 255);
    rcv_host = gethostbyname(rcv_name);
    if (strcmp(rcv_name, "\0") != 0)
        fprintf(netlog_fp, " HOST=%s", rcv_host->h_name);
    else
        fprintf(netlog_fp, " HOST=NO_NAME");
    fprintf(netlog_fp, " PROG=pathload");
    fprintf(netlog_fp, " LVL=Usage");
    if ((snd_host = gethostbyname(hostname)) == 0) {
        snd_host = gethostbyaddr(hostname, 256, AF_INET);
    }
    fprintf(netlog_fp, " PATHLOAD.SNDR=%s", snd_host->h_name);
    fprintf(netlog_fp, " PATHLOAD.ABWL=%.1fMbps", tr_min);
    fprintf(netlog_fp, " PATHLOAD.ABWH=%.1fMbps\n", tr_max);
    fclose(netlog_fp);
}

// Print l_int32 time.
void print_time(FILE *fp, l_int32 time)
{
    if (time < 10) {
        fprintf(fp, "0");
        fprintf(fp, "%1d", time);
    } else
        fprintf(fp, "%2d", time);
}

l_int32 less_than(double a, double b)
{
    if (!equal(a, b) && a < b) return 1;
    else return 0;
}

l_int32 grtr_than(double a, double b)
{
    if (!equal(a, b) && a > b) return 1;
    else return 0;
}

// If a approx-equals b, returns 1. Otherwise returns 0.
l_int32 equal(double a, double b)
{
    l_int32 maxdiff;

    if ((a < b ? a : b) < 500) maxdiff = 2.5;
    else maxdiff = 5;
    if (abs(a - b) / b <= 0.02 && abs(a - b) < maxdiff) return 1;
    else return 0;
}

// Help
void help()
{
    fprintf(stderr,
            "usage: pathload_rcv [-q|-v] [-o|-O <filename>] [-N <filename>]\
[-w <bw_resol>] [-h|-H] -s <sender>\n");
    fprintf(stderr, "-s        : hostname/ipaddress of sender\n");
    fprintf(stderr, "-q        : quite mode\n");
    fprintf(stderr, "-v        : verbose mode\n");
    fprintf(stderr, "-w        : user specified bw resolution\n");
    fprintf(stderr,
            "-o <file> : write log in user specified file [default is pathload.log]\n");
    fprintf(stderr,
            "-O <file> : append log in user specified file [default is pathload.log]\n");
    fprintf(stderr,
            "-N <file> : print output in netlogger format to <file>\n");
    fprintf(stderr, "-h|H      : print this help and exit\n");
    exit(0);
}
