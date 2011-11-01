/* -*- Mode:C++; c-file-style:"k&r"; indent-tabs-mode:nil; -*- */

#ifdef LOCAL
#define EXTERN
#else
#define EXTERN extern
#endif

#define SCALE_FACTOR                2

// For a fleet trend.
#define NOTREND                     1
#define INCREASING                  2
#define GREY                        3

#define MEDIUM_LOSS_RATE            3        // in %
#define HIGH_LOSS_RATE              15       // in %
#define MIN_PARTITIONED_STREAM_LEN  5        // number of packets
#define MIN_STREAM_LEN              36       // number of packets
#define PCT_THRESHOLD               0.55
#define PDT_THRESHOLD               0.4
#define AGGREGATE_THRESHOLD         0.6      // 60 %

#define NUM_RETRY_RATE_MISMATCH     0
#define NUM_RETRY_CS                1
#define MAX_LOSSY_STREAM_FRACTION   50
#define MIN_TIME_INTERVAL           7        // in usec
#define MAX_TIME_INTERVAL           200000   // in usec

EXTERN l_int32 aggregate_trend_result();
EXTERN double time_to_us_delta(struct timeval tv1, struct timeval tv2);
EXTERN double get_avg(double data[], l_int32 no_values);
EXTERN double pairwise_comparision_test(double array[], l_int32 start,
                                        l_int32 end);
EXTERN double pairwise_diff_test(double array[], l_int32 start, l_int32 end);
EXTERN l_int32 rate_adjustment(l_int32 flag);
EXTERN l_int32 eliminate_sndr_side_CS(double sndr_time_stamp[],
                                      l_int32 split_owd[]);
EXTERN l_int32 eliminate_rcvr_side_CS(double rcvr_time_stamp[], double[],
                                      double[], l_int32, l_int32, l_int32 *,
                                      l_int32 *);
EXTERN l_int32 eliminate_b2b_pkt_ic(double rcvr_time_stamp[], double[], double[],
                                    l_int32, l_int32, l_int32 *, l_int32 *);
EXTERN void adjust_offset_to_zero(double owd[], l_int32 last_pkt_id);
EXTERN l_int32 recv_fleet();
EXTERN void print_contextswitch_info(l_int32 num_sndr_cs[],
                                     l_int32 num_rcvr_cs[],
                                     l_int32 discard[], l_int32 stream_cnt);
EXTERN void *ctrl_listen(void *);
EXTERN void radj_notrend();
EXTERN void radj_increasing();
EXTERN void radj_greymin();
EXTERN void radj_greymax();
EXTERN l_int32 calc_param();
EXTERN void get_sending_rate();
EXTERN double get_adr();
EXTERN l_int32 recv_train(l_int32, struct timeval *, l_int32);
EXTERN l_int32 recvfrom_latency(struct sockaddr_in rcv_udp_addr);
EXTERN double grey_bw_resolution();
EXTERN void get_pct_trend(double[], l_int32[], l_int32);
EXTERN void get_pdt_trend(double[], l_int32[], l_int32);
EXTERN void get_trend(double owdfortd[], l_int32 pkt_cnt);
EXTERN l_int32 get_sndr_time_interval(double snd_time[], double *sum);
EXTERN void sig_alrm();
EXTERN void terminate_gracefully(struct timeval exp_start_time);
EXTERN l_int32 check_intr_coalescence(struct timeval time[], l_int32, l_int32 *);
EXTERN void order_float(float unord_arr[], float ord_arr[],
                        l_int32 start, l_int32 num_elems);
EXTERN void order_dbl(double unord_arr[], double ord_arr[],
                      l_int32 start, l_int32 no_elems);
EXTERN void order_int(l_int32 unord_arr[], l_int32 ord_arr[], l_int32 no_elems);
EXTERN void send_ctr_mesg(char *ctr_buff, l_int32 ctr_code);
EXTERN l_int32 recv_ctr_mesg(l_int32 ctr_strm, char *ctr_buff);
EXTERN l_int32 equal(double a, double b);
EXTERN l_int32 less_than(double a, double b);
EXTERN l_int32 grtr_than(double a, double b);
EXTERN void netlogger();
EXTERN void print_time(FILE *fp, l_int32 time);
EXTERN void sig_sigusr1();
EXTERN void help();

EXTERN l_int32 exp_flag;
EXTERN l_int32 grey_flag;
EXTERN double tr;
EXTERN double adr;
EXTERN double tr_max;
EXTERN double tr_min;
EXTERN double grey_max, grey_min;
EXTERN l_int32 sock_tcp, sock_udp;
EXTERN struct timeval first_time, second_time;
EXTERN l_int32 exp_fleet_id;
EXTERN float bw_resol;
EXTERN l_uint32 min_time_interval, snd_latency, rcv_latency;
EXTERN l_int32 repeat_fleet;
EXTERN l_int32 counter; // # of consecutive times actual rate != request rate
EXTERN l_int32 converged_gmx_rmx;
EXTERN l_int32 converged_gmn_rmn;
EXTERN l_int32 converged_rmn_rmx;
EXTERN l_int32 converged_gmx_rmx_tm;
EXTERN l_int32 converged_gmn_rmn_tm;
EXTERN l_int32 converged_rmn_rmx_tm;
EXTERN double cur_actual_rate, cur_req_rate;
EXTERN double prev_actual_rate, prev_req_rate;
EXTERN double max_rate, min_rate;
EXTERN l_int32 max_rate_flag, min_rate_flag;
EXTERN l_int32 bad_fleet_cs, bad_fleet_rate_mismatch;
EXTERN l_int32 retry_fleet_cnt_cs, retry_fleet_cnt_rate_mismatch;
EXTERN FILE *pathload_fp, *netlog_fp;
EXTERN double pct_metric[50], pdt_metric[50];
EXTERN l_int32 trend_idx;
EXTERN double snd_time_interval;
EXTERN l_int32 min_rsleep_time;
EXTERN l_int32 num;
EXTERN l_int32 slow;
EXTERN l_int32 interrupt_coalescence;
EXTERN l_int32 ic_flag;
EXTERN l_int32 netlog;
EXTERN char hostname[256];
EXTERN l_int32 repeat_1, repeat_2;
EXTERN l_int32 lower_bound;
EXTERN l_int32 increase_stream_len;
EXTERN l_int32 requested_delay;
EXTERN struct timeval exp_start_time;

#ifdef THRLIB
typedef struct {
    pthread_t ptid;
    l_int32 finished_stream;
    l_int32 stream_cnt;
} thr_arg;
#endif
