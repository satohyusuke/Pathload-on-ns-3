/* -*- Mode:C++; c-file-style:"k&r"; indent-tabs-mode:nil; -*- */

#include "pathload-original-globals.h"
#include "pathload-original-sender.h"

int main(int argc, char *argv[])
{
    struct hostent *host_rcv;
    struct timeval tv1, tv2;
    l_uint32 snd_time;
    l_int32 ctr_code;
    time_t localtm;
    int opt_len, mss;
    int ret_val;
    int iterate = 0;
    int done = 0;
    int latency[30], ord_latency[30];
    int i;
    int c;
    int errflg = 0;
    char pkt_buf[256];
    char ctr_buff[8];

    quiet = 0;
    while ((c = getopt(argc, argv, "ihHq")) != EOF)
        switch (c) {
        case 'H':
        case 'h':
            help();
            break;
        case 'i':
            iterate = 1;
            break;
        case 'q':
            quiet = 1;
            break;
        case '?':
            errflg++;
        }
    if (errflg) {
        fprintf(stderr, "usage: pathload_snd [-q] [-H|-h]\n");
        exit(-1);
    }

    num_stream = NUM_STREAM;
    min_sleeptime();

    // Measure our gettimeofday() latency.
    for (i=0; i<30; i++) {
        gettimeofday(&tv1, NULL);
        gettimeofday(&tv2, NULL);
        latency[i] = (tv2.tv_sec*1000000 + tv2.tv_usec)
            - (tv1.tv_sec*1000000 + tv1.tv_usec);
    }
    order_int(latency, ord_latency, 30);
    gettimeofday_latency = ord_latency[15];

#ifdef DEBUG
    printf("DEBUG :: gettimeofday_latency = %d\n", gettimeofday_latency);
#endif

    // Set up control stream: TCP connection.
    if ((sock_tcp = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket(AF_INET, SOCK_STREAM, 0): ");
        exit(-1);
    }

    opt_len = 1;
    if (setsockopt(sock_tcp, SOL_SOCKET, SO_REUSEADDR,
                   (char *) &opt_len, sizeof(opt_len)) < 0) {
        perror("setsockopt(SOL_SOCKET, SO_REUSEADDR): ");
        exit(-1);
    }

    bzero((char *) &snd_tcp_addr, sizeof(snd_tcp_addr));
    snd_tcp_addr.sin_family = AF_INET;
    snd_tcp_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    snd_tcp_addr.sin_port = htons(TCPSND_PORT);

    if (bind(sock_tcp, (struct sockaddr *) &snd_tcp_addr,
             sizeof(snd_tcp_addr)) < 0) {
        perror("bind(sock_tcp): ");
        exit(-1);
    }

    if (listen(sock_tcp, 1) < 0) {
        perror("listen(sock_tcp, 1): ");
        exit(-1);
    }

    // Set up data stream: UDP socket.
    if ((sock_udp = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket(AF_INET, SOCK_DGRAM, 0): ");
        exit(-1);
    }

    bzero((char *) &snd_udp_addr, sizeof(snd_udp_addr));
    snd_udp_addr.sin_family = AF_INET;
    snd_udp_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    snd_udp_addr.sin_port = htons(0);

    if (bind(sock_udp, (struct sockaddr *) &snd_udp_addr,
             sizeof(snd_udp_addr)) < 0) {
        perror("bind(sock_udp): ");
        exit(-1);
    }

    send_buff_sz = UDP_BUFFER_SZ;
    if (setsockopt(sock_udp, SOL_SOCKET, SO_SNDBUF, (char *) &send_buff_sz,
                   sizeof(send_buff_sz)) < 0) {
        send_buff_sz /= 2;
        if (setsockopt(sock_udp, SOL_SOCKET, SO_SNDBUF, (char *) &send_buff_sz,
                       sizeof(send_buff_sz)) < 0) {
            perror("setsockopt(SOL_SOCKET, SO_SNDBUF): ");
            exit(-1);
        }
    }

    do {
        if (!quiet) {
            printf("\n\n");
            printf("Waiting for the receiver to establish control stream ... ");
        }
        fflush(stdout);

        // Wait until receiver attempts to connect
        // for starting new measurement cycle.
        rcv_tcp_adrlen = sizeof(rcv_tcp_addr);
        ctr_strm = accept(sock_tcp, (struct sockaddr *) &rcv_tcp_addr,
                          &rcv_tcp_adrlen);
        if (ctr_strm < 0) {
            perror("accept(sock_tcp): ");
            exit(-1);
        }
        if (!quiet) printf("OK.\n");
        localtm = time(NULL);
        gethostname(pkt_buf, 256);
        host_rcv = gethostbyaddr((char *) &(rcv_tcp_addr.sin_addr),
                                 sizeof(rcv_tcp_addr.sin_addr), AF_INET);

        if (host_rcv != NULL) {
            if (!quiet)
                printf("Receiver %s starts measurements on %s",
                       host_rcv->h_name, ctime(&localtm));
        } else if (!quiet)
            printf("Unknown receiver starts measurements at %s",
                   ctime(&localtm));

        // Form receiving UDP address.
        bzero((char *) &rcv_udp_addr, sizeof(rcv_udp_addr));
        rcv_udp_addr.sin_family = AF_INET;
        rcv_udp_addr.sin_addr.s_addr = rcv_tcp_addr.sin_addr.s_addr;
        rcv_udp_addr.sin_port = htons(UDPRCV_PORT);

        // Connect UDP socket.
        connect(sock_udp, (struct sockaddr *) &rcv_udp_addr,
                sizeof(rcv_udp_addr));

        // Make TCP socket non-blocking.
        if (fcntl(ctr_strm, F_SETFL, O_NONBLOCK) < 0) {
            perror("fcntl(ctr_strm, F_SETFL, O_NONBLOCK): ");
            exit(-1);
        }

        // MSS の取得
        opt_len = sizeof(mss);
        if (getsockopt(ctr_strm, IPPROTO_TCP, TCP_MAXSEG, (char *) &mss,
                       &opt_len) < 0) {
            perror("getsockopt(sock_tcp, IPPROTO_TCP, TCP_MAXSEG): ");
            exit(-1);
        }

        // Decide sender's max packet size.
        snd_max_pkt_sz = mss;
        if (snd_max_pkt_sz == 0 || snd_max_pkt_sz == 1448)
            snd_max_pkt_sz = 1472; // Make it ethernet sized MTU.
        else
            snd_max_pkt_sz = mss + 12;

        // Tell receiver our max packet size.
        send_ctr_mesg(ctr_buff, snd_max_pkt_sz);

        // Receive receiver's max packet size.
        while ((rcv_max_pkt_sz = recv_ctr_mesg(ctr_buff)) == -1);

        // Decide the max packet size used for this estimation.
        max_pkt_sz =
            (rcv_max_pkt_sz < snd_max_pkt_sz) ? rcv_max_pkt_sz : snd_max_pkt_sz;
        if (!quiet)
            printf("Maximum packet size          :: %ld bytes\n", max_pkt_sz);

        // Tell receiver our send latency.
        snd_time = (l_int32) send_latency();
        send_ctr_mesg(ctr_buff, snd_time);

        // Wait for receiver to start ADR measurement.
        if ((ret_val = recv_ctr_mesg(ctr_buff)) == -1) break;
        if ((((ret_val & CTR_CODE) >> 31) == 1) &&
            ((ret_val & 0x7fffffff) == SEND_TRAIN)) {
            if (!quiet)
                printf("Estimating ADR to initialize rate adjustment algorithm => ");
            fflush(stdout);
            if (send_train() == -1) continue;
            if (!quiet) printf("Done\n");
        }

        // Start available bandwidth measurement.
        fleet_id = 0;
        while (!done) {
            if ((ret_val = recv_ctr_mesg(ctr_buff)) == -1) break;
            if ((((ret_val & CTR_CODE) >> 31) == 1)
                && ((ret_val & 0x7fffffff) == TERMINATE)) {
                if (!quiet) printf("Terminating current run.\n");
                done = 1;
            } else {
                // Receive parameters of packet stream for the measurement.
                transmission_rate = ret_val;
                if ((cur_pkt_sz = recv_ctr_mesg(ctr_buff)) <= 0) break;
                if ((stream_len = recv_ctr_mesg(ctr_buff)) <= 0) break;
                if ((time_interval = recv_ctr_mesg(ctr_buff)) <= 0) break;
                if ((ret_val = recv_ctr_mesg(ctr_buff)) == -1) break;

                // Control code received just above should be SEND_FLEET.
                // Tell the receiver that we are going to send packet fleets.
                ctr_code = RECV_FLEET | CTR_CODE;
                if (send_ctr_mesg(ctr_buff, ctr_code) == -1) break;

                // Send packet fleets.
                if (send_fleet() == -1) break;

                if (!quiet) printf("\n");
                fleet_id++;
            }
        }
        close(ctr_strm);    // End of one available bandwidth measurement.
    } while (iterate);

    return 0;
}
