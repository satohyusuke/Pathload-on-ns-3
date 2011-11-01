/* -*- Mode:C++; c-file-style:"k&r"; indent-tabs-mode:nil; -*- */

#define LOCAL
#include "pathload-original-globals.h"
#include "pathload-original-receive.h"

int main(l_int32 argc, char *argv[])
{
     extern char *optarg;
     struct hostent *host_snd;
     struct sockaddr_in snd_tcp_addr, rcv_udp_addr;
     struct utsname uts;         // uname() で使う
     l_int32 ctr_code;
     l_int32 trend;
     l_int32 prev_trend = 0;
     l_int32 opt_len, rcv_buff_sz, mss;
     l_int32 ret_val;
     l_int32 errflg = 0;
     l_int32 file = 0;
     char netlogfile[50], filename[50];
     char ctr_buff[8], myname[50], buff[26];
     char mode[4];
     l_int32 c;
     struct itimerval expireat;
     struct sigaction sigstruct;

    slow = 0;
    requested_delay = 0;
    interrupt_coalescence = 0;
    bad_fleet_cs = 0;
    num_stream = NUM_STREAM;    // NUM_STREAM = 12
    stream_len = STREAM_LEN;    // STREAM_LEN = 100
    exp_flag = 1;
    num = 0;
    snd_time_interval = 0;

    converged_gmx_rmx = 0;
    converged_gmn_rmn = 0;
    converged_rmn_rmx = 0;
    counter = 0;
    prev_actual_rate = 0;                   // previous actual rate?
    prev_req_rate = 0;                      // previous request rate?
    cur_actual_rate = 0;                    // current actual rate?
    cur_req_rate = 0;                       // current request rate?
    gettimeofday(&exp_start_time, NULL);    // Get the experience start time.
    verbose = 1;                            // Enable verbose mode by defalut.
    bw_resol = 0;                           // bandwidth resolution?
    netlog = 0;
    increase_stream_len = 0;
    lower_bound = 0;

    if (argc == 1) errflg++;
    while ((c = getopt(argc, argv, "t:s:w:qvO:o:HhN:")) != EOF)
        switch (c) {
        case 't':
            requested_delay = atoi(optarg);
            break;
        case 's':
            strcpy(hostname, optarg);
            break;
        case 'w':
            bw_resol = atof(optarg);
            break;
        case 'q':
            Verbose = 0;
            verbose = 0;
            break;
        case 'v':
            Verbose = 1;
            break;
        case 'O':
            file = 1;
            strcpy(filename, optarg);
            strcpy(mode, "a");
            break;
        case 'o':
            file = 1;
            strcpy(filename, optarg);
            strcpy(mode, "w");
            break;
        case 'H':
        case 'h':
            help();
            break;
        case 'N':
            netlog = 1;
            strcpy(netlogfile, optarg);
            break;
        case '?':
            errflg++;
            break;
        }
    if (errflg) {
        fprintf(stderr,
                "usage: pathload_rcv [-q|-v] [-o <filename>] [-N <filename>]\
[-w <bw_resol>] [-h|-H] -s <sender>\n");
        exit(0);
    }

    // For logging.
    if (file) pathload_fp = fopen(filename, mode);
    else pathload_fp = fopen("pathload.log", "a");
    fprintf(pathload_fp, "\n\n");
    if (netlog) netlog_fp = fopen(netlogfile, "a");

    // 送信側ホストの IP アドレスを TCP ソケットのアドレスに設定
    if ((host_snd = gethostbyname(hostname)) == NULL) {
        // Check whether the user gave IP address.
        if ((snd_tcp_addr.sin_addr.s_addr = inet_addr(hostname)) == -1) {
            fprintf(stderr, "%s: unknown host\n", hostname);
            exit(-1);
        }
    }
    strncpy(buff, ctime(&(exp_start_time.tv_sec)), 24);
    buff[24] = '\0';
    bzero(myname, 50);
    if (gethostname(myname, 50) == -1) {
        if (uname(&uts) < 0) strcpy(myname, "UNKNOWN");
        else strcpy(myname, uts.nodename);
    }
    if (verbose || Verbose)
        printf("\n\nReceiver %s starts measurements at sender %s on %s \n",
               myname, hostname, buff);
    fprintf(pathload_fp,
            "\n\nReceiver %s starts measurements at sender %s on %s \n",
            myname, hostname, buff);

    // Set up datagram channel.
    if ((sock_udp = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("ERROR :: failed to open DGRAM socket: ");
        exit(-1);
    }

    bzero((char *) &rcv_udp_addr, sizeof(rcv_udp_addr));
    rcv_udp_addr.sin_family = AF_INET;
    // INADDR_ANY を指定すると, どのアドレスからの接続も受け付ける
    rcv_udp_addr.sin_addr.s_addr = INADDR_ANY;
    rcv_udp_addr.sin_port = htons(UDPRCV_PORT);
    if (bind(sock_udp, (struct sockaddr *) &rcv_udp_addr,
             sizeof(rcv_udp_addr)) < 0) {
        perror("ERROR :: failed to bind DGRAM socket: ");
        exit(-1);
    }

    // バッファサイズの設定？
    rcv_buff_sz = UDP_BUFFER_SZ;
    if (setsockopt(sock_udp, SOL_SOCKET, SO_RCVBUF, &rcv_buff_sz,
                   sizeof(rcv_buff_sz)) < 0) {
        rcv_buff_sz /= 2;
        if (setsockopt(sock_udp, SOL_SOCKET, SO_RCVBUF, &rcv_buff_sz,
                       sizeof(rcv_buff_sz)) < 0) {
            printf("ERROR :: Unable to set socket buffer to %d .\n",
                   rcv_buff_sz);
            exit(-1);
        }
    }

    opt_len = 1;
    if (setsockopt(sock_udp, SOL_SOCKET, SO_REUSEADDR,
                   (const char *) &opt_len, sizeof(opt_len)) < 0) {
        perror("setsockopt(sock_udp, SOL_SOCKET, SO_REUSEADDR): ");
        exit(-1);
    }

    // Set up control channel.
    if ((sock_tcp = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket(AF_INET, SOCK_STREAM): ");
        exit(-1);
    }

    opt_len = 1;
    if (setsockopt(sock_tcp, SOL_SOCKET, SO_REUSEADDR,
                   (const char *) &opt_len, sizeof(opt_len)) < 0) {
        perror("setsockopt(sock_tcp, SOL_SOCKET, SO_REUSEADDR): ");
        exit(-1);
    }

    bzero((char *) &snd_tcp_addr, sizeof(snd_tcp_addr));
    snd_tcp_addr.sin_family = AF_INET;
    memcpy((void *) &(snd_tcp_addr.sin_addr.s_addr), host_snd->h_addr,
           host_snd->h_length);
    snd_tcp_addr.sin_port = htons(TCPSND_PORT);

    if (connect(sock_tcp, (struct sockaddr *) &snd_tcp_addr,
                sizeof(snd_tcp_addr)) < 0) {
        perror("Make sure that pathload_snd runs at sender: ");
        exit(-1);
    }

    // TCP ソケットをノンブロッキングにする
    if (fcntl(sock_tcp, F_SETFL, O_NONBLOCK) < 0) {
        perror("fcntl: ");
        exit(-1);
    }

    // ここまで実装し終わったと思う．後日確認

    // Measure max_pkt_sz (based on TCP MSS).
    // This is not accurate because it does not take into
    // account MTU of intermediate routers.
    // * MSS = Maximum Segment Size, MTU = Maximum Transmission Unit
    opt_len = sizeof(mss);
    if (getsockopt(sock_tcp, IPPROTO_TCP, TCP_MAXSEG, (char *) &mss,
                   (socklen_t *) & opt_len) < 0) {
        perror("getsockopt(sock_tcp, IPPROTO_TCP, TCP_MAXSEG): ");
        exit(-1);
    }
    rcv_max_pkt_sz = mss;
    if (rcv_max_pkt_sz == 0 || rcv_max_pkt_sz == 1448)
        rcv_max_pkt_sz = 1472;  // Make it Ethernet sized MTU.
    else
        rcv_max_pkt_sz = mss + 12;

    // SIGALRM シグナルを受け取ったとき, 関数 sig_alrm() を呼ぶように設定.
    // sig_alrm() は内部で terminate_gracefully() を呼んで exit(0) する.
    sigstruct.sa_handler = sig_alrm;
    sigemptyset(&sigstruct.sa_mask);
    sigstruct.sa_flags = 0;
#ifdef SA_INTERRUPT
    sigstruct.sa_flags |= SA_INTERRUPT;
#endif
    sigaction(SIGALRM, &sigstruct, NULL);

    // 60秒後にプロセスを終了させるタイマーを設定.
    // setitimer() は満了するとプロセスに SIGALRM シグナルを送る.
    expireat.it_value.tv_sec = 60;      // RECEIVER ABORTS TIME
    expireat.it_value.tv_usec = 0;
    expireat.it_interval.tv_sec = 0;
    expireat.it_interval.tv_usec = 0;
    setitimer(ITIMER_REAL, &expireat, NULL);

    // Wait for 60 secs to receive sender's max packet size.
    while ((snd_max_pkt_sz = recv_ctr_mesg(sock_tcp, ctr_buff)) == -1);
    if (snd_max_pkt_sz == -2) {
        printf("pathload_snd did not respond for 60 sec\n");
        close(sock_tcp);
        exit(-1);
    }

    // プロセス終了タイマーを停止.
    expireat.it_value.tv_sec = 0;
    expireat.it_value.tv_usec = 0;
    expireat.it_interval.tv_sec = 0;
    expireat.it_interval.tv_usec = 0;
    setitimer(ITIMER_REAL, &expireat, NULL);

    // SIGALRM シグナルを受け取ったときの動作を, SIG_DFL (デフォルト動作) に設定.
    // 言い換えると, SIGALRM に対する動作をリセットしている.
    sigstruct.sa_handler = SIG_DFL;
    sigemptyset(&sigstruct.sa_mask);
    sigstruct.sa_flags = 0;
    sigaction(SIGALRM, &sigstruct, NULL);

    // Send receiver's max packet size.
    send_ctr_mesg(ctr_buff, rcv_max_pkt_sz);

    // max_pkt_sz is the smaller one among rcv_max_pkt_sz and snd_max_pkt_sz.
    max_pkt_sz =
        rcv_max_pkt_sz < snd_max_pkt_sz ? rcv_max_pkt_sz : snd_max_pkt_sz;
    if (Verbose)
        printf("  Maximum packet size          :: %ld bytes\n", max_pkt_sz);
    fprintf(pathload_fp,
            "  Maximum packet size          :: %ld bytes\n", max_pkt_sz);

    // Measure the latency to receive a packet through recvfrom().
    rcv_latency = (l_int32) recvfrom_latency(rcv_udp_addr);

    // Receive the sender's latency for sending a packet.
    while ((snd_latency = recv_ctr_mesg(sock_tcp, ctr_buff)) == -1);

    if (Verbose) {
        printf("  send latency @sndr           :: %ld usec\n", snd_latency);
        printf("  recv latency @rcvr           :: %ld usec\n", rcv_latency);
    }
    fprintf(pathload_fp, "  send latency @sndr           :: %ld usec\n",
            snd_latency);
    fprintf(pathload_fp, "  recv latency @rcvr           :: %ld usec\n",
            rcv_latency);

    // パケット間隔の最低値を計算. 下限は MIN_TIME_INTERVAL で 7 usec.
    min_time_interval =
        SCALE_FACTOR * ((rcv_latency > snd_latency) ? rcv_latency : snd_latency);
    min_time_interval =
        min_time_interval > MIN_TIME_INTERVAL ? min_time_interval : MIN_TIME_INTERVAL;

    if (Verbose)
        printf("  Minimum packet spacing       :: %ld usec\n",
               min_time_interval);
    fprintf(pathload_fp, "  Minimum packet spacing       :: %ld usec\n",
            min_time_interval);

    // The dimension of max_rate and min_rate is Mbit/sec.
    // min_rate = (200 + 28) * 8.0 / 200000
    //          = 0.00912 [bit/usec] (= 0.00912 [Mbit/sec])
    //          = 9.12 [kbit/sec]
    max_rate = (max_pkt_sz + 28) * 8.0 / min_time_interval;
    min_rate = (MIN_PKT_SZ + 28) * 8.0 / MAX_TIME_INTERVAL;

    if (Verbose)
        printf("  Max rate(max_pktsz/min_time) :: %.2fMbps\n", max_rate);
    fprintf(pathload_fp, "  Max rate(max_pktsz/min_time) :: %.2fMbps\n",
            max_rate);

    // Estimate asymptotic dispersion rate (ADR).
    adr = get_adr();

    // Decide the bandwidth resolution from the estimated ADR.
    if (bw_resol == 0 && adr != 0) bw_resol = 0.02 * adr;    // 2% of ADR.
    else if (bw_resol == 0) bw_resol = 2;                    // 2 Mbit/sec.
    if (Verbose)
        printf("  Grey bandwidth resolution    :: %.2f\n",
               grey_bw_resolution());
    fprintf(pathload_fp, "  Grey bandwidth resolution    :: %.2f\n",
            grey_bw_resolution());

    // If interrupt coalescion was detected in ADR estimation,
    // the estimatable bandwidth resolution becomes slightly bad.
    if (interrupt_coalescence) {
        bw_resol = 0.05 * adr;    // 5% of ADR.
        if (verbose || Verbose) printf("  Interrupt coalescion detected\n");
        fprintf(pathload_fp, "  Interrupt coalescion detected\n");
    }

    // Decide the transmission rate, tr, in bit/usec (= Mbit/sec).
    if (adr == 0 || adr > max_rate || adr < min_rate)
        tr = (max_rate + min_rate) / 2.0;
    else
        tr = adr;

    /* パケットストリームを同期するとしたらこのあたり？ */

    // Estimate the available bandwidth.
    // The dimension of transmission_rate is bit/sec.
    transmission_rate = (l_uint32) rint(1000000 * tr);
    max_rate_flag = 0;
    min_rate_flag = 0;
    fflush(pathload_fp);

    // SIGALRM シグナルを受け取ったら sig_alrm() を呼ぶように設定.
    sigstruct.sa_handler = sig_alrm;
    sigemptyset(&sigstruct.sa_mask);
    sigstruct.sa_flags = 0;
#ifdef SA_INTERRUPT
    sigstruct.sa_flags |= SA_INTERRUPT;
#endif
    sigaction(SIGALRM, &sigstruct, NULL);

    if (requested_delay) {
        expireat.it_value.tv_sec = requested_delay;  // RECEIVER ABORTS TIME.
        expireat.it_value.tv_usec = 0;
        expireat.it_interval.tv_sec = 0;
        expireat.it_interval.tv_usec = 0;
        setitimer(ITIMER_REAL, &expireat, NULL);
    }

    while (1) {
        //　パケットサイズ L [byte] とパケット間隔 T [usec] を計算.
        if (calc_param() == -1) {
            ctr_code = TERMINATE | CTR_CODE;
            send_ctr_mesg(ctr_buff, ctr_code);
            terminate_gracefully(exp_start_time);
        }

        // Send parameters of a packet stream to the sender.
        send_ctr_mesg(ctr_buff, transmission_rate);   // R [bit/sec]
        send_ctr_mesg(ctr_buff, cur_pkt_sz);          // L [byte]
        if (increase_stream_len) stream_len = 3 * STREAM_LEN;
        else stream_len = STREAM_LEN;
        send_ctr_mesg(ctr_buff, stream_len);          // K [# of packets]
        send_ctr_mesg(ctr_buff, time_interval);       // T [usec]

        // Send SEND_FLEET code to have the sender send a packet fleet.
        ctr_code = SEND_FLEET | CTR_CODE;
        send_ctr_mesg(ctr_buff, ctr_code);

        while (1) {
            ret_val = recv_ctr_mesg(sock_tcp, ctr_buff);
            if ((((ret_val & CTR_CODE) >> 31) == 1) &&
                ((ret_val & 0x7fffffff) == RECV_FLEET))
                break;
            else if ((((ret_val & CTR_CODE) >> 31) == 1) &&
                     ((ret_val & 0x7fffffff) == FINISHED_STREAM))
                ret_val = recv_ctr_mesg(sock_tcp, ctr_buff);
        }

        // Receive a packet fleet.
        if (recv_fleet() == -1) {
            if (!increase_stream_len) {
                trend = INCREASING;
                if (exp_flag == 1 && prev_trend != 0 && prev_trend != trend)
                    exp_flag = 0;
                prev_trend = trend;
                if (rate_adjustment(INCREASING) == -1)
                    terminate_gracefully(exp_start_time);
            }
        } else {
            get_sending_rate();
            trend = aggregate_trend_result();
            if (trend == -1 && bad_fleet_cs && retry_fleet_cnt_cs > NUM_RETRY_CS)
                terminate_gracefully(exp_start_time);
            else if ((trend == -1 && bad_fleet_cs
                      && retry_fleet_cnt_cs <= NUM_RETRY_CS))
                /* repeat fleet with current rate. */
                continue;

            if (trend != GREY) {
                if (exp_flag == 1 && prev_trend != 0 && prev_trend != trend)
                    exp_flag = 0;
                prev_trend = trend;
            }

            // Calculate the next transmission rate from the current trend?
            if (rate_adjustment(trend) == -1)
                terminate_gracefully(exp_start_time);
        }
        fflush(pathload_fp);
    }

    return 0;
}
