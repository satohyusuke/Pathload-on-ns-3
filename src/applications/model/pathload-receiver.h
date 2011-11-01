/* -*- Mode:C++; c-file-style:"k&r"; indent-tabs-mode:nil; -*- */

#ifndef PATHLOAD_RECEIVER_H
#define PATHLOAD_RECEIVER_H

#include "ns3/address.h"
#include "ns3/application.h"
#include "ns3/packet.h"
#include "ns3/ptr.h"
#include "ns3/realtime-simulator-impl.h"
#include "ns3/socket.h"
#include "ns3/type-id.h"
#include "ns3/uinteger.h"
#ifndef PATHLOAD_GLOBALS_H
#include "pathload-globals.h"
#endif

namespace ns3 {

const int SCALE_FACTOR = 2;

// Fleet trends.
const int NOTREND = 1;
const int INCREASING = 2;
const int GREY = 3;

const double MEDIUM_LOSS_RATE = 3.0;      // in %
const double HIGH_LOSS_RATE = 15.0;       // in %
const int MIN_PARTITIONED_STREAM_LEN = 5; // number of packets
const int MIN_STREAM_LEN = 36;            // number of packets
const double PCT_THRESHOLD = 0.55;
const double PDT_THRESHOLD = 0.4;
const double AGGREGATE_THRESHOLD = 0.6; // 60%

const int NUM_RETRY_RATE_MISMATCH = 0;
const int NUM_RETRY_CS = 1;
const int MAX_LOSSY_STREAM_FRACTION = 50;
const int MIN_TIME_INTERVAL = 7;      // in usec
const int MAX_TIME_INTERVAL = 200000; // in usec

// PathloadReceiver アプリケーション
class PathloadReceiver : public Application {
public:
     static TypeId GetTypeId();

     PathloadReceiver();          // コンストラクタ
     virtual ~PathloadReceiver(); // デストラクタ

protected:
     virtual void DoDispose();

private:
     virtual void StartApplication();
     virtual void StopApplication();

     void HandleRecv(Ptr<Socket> tcpsock);
     void HandleRecvfrom (Ptr<Socket> udpsock);
     void HandleConnected(Ptr<Socket> tcpsock);

     // PathloadReceiver のメインループ
     inline void MainLoop() { MainLoopPart1(); }
     void MainLoopPart1();
     void MainLoopPart2();
     void MainLoopPart3();
     void MainLoopPart4();

     inline void Dummy() {};
     // inline bool IsValidControlCode()
     //      { return (ctr_code != 0 ? true : false); }

     Ptr<Socket> m_tcpsock;     // TCP connect ソケット
     Ptr<Socket> m_udpsock;     // UDP ソケット

     Address m_snd_tcp_addr;    // 送信ホスト TCP アドレス
     Address m_snd_udp_addr;    // 送信ホスト UDP アドレス
     Address m_rcv_tcp_addr;    // 受信ホスト TCP アドレス
     Address m_rcv_udp_addr;    // 受信ホスト UDP アドレス

     template<typename T> void Order(const T unord[], T ord[], int n);
     template<typename T> void Order(const T unord[], T ord[],
                                     int start, int num_elems);

     l_int32 aggregate_trend_result();
     double time_to_us_delta(struct timeval tv1, struct timeval tv2);
     double get_avg(double data[], l_int32 no_values);
     double PairwiseComparisonTest(double array[], l_int32 start, l_int32 end);
     double PairwiseDifferenceTest(double array[], l_int32 start, l_int32 end);
     l_int32 rate_adjustment(l_int32 flag);
     l_int32 eliminate_sndr_side_CS(double sndr_time_stamp[],
                                    l_int32 split_owd[]);
     l_int32 eliminate_rcvr_side_CS(double rcvr_time_stamp[], double[], double[],
                                    l_int32, l_int32, l_int32 *, l_int32 *);
     l_int32 eliminate_b2b_pkt_ic(double rcvr_time_stamp[], double[], double[],
                                  l_int32, l_int32, l_int32 *, l_int32 *);
     void adjust_offset_to_zero(double owd[], l_int32 last_pkt_id);
     l_int32 recv_fleet();
     void print_contextswitch_info(l_int32 num_sndr_cs[], l_int32 num_rcvr_cs[],
                                   l_int32 discard[], l_int32 stream_cnt);
     void *ctrl_listen(void *);
     void radj_notrend();
     void radj_increasing();
     void radj_greymin();
     void radj_greymax();
     l_int32 calc_param();
     void get_sending_rate();
     double get_adr();
     l_int32 recv_train(l_int32, struct timeval *, l_int32);
     l_int32 recvfrom_latency(struct sockaddr_in rcv_udp_addr);
     double grey_bw_resolution();
     void get_pct_trend(double[], l_int32[], l_int32);
     void get_pdt_trend(double[], l_int32[], l_int32);
     void get_trend(double owdfortd[], l_int32 pkt_cnt);
     l_int32 get_sndr_time_interval(double snd_time[], double *sum);
     void sig_alrm();
     void terminate_gracefully(struct timeval exp_start_time);
     l_int32 check_intr_coalescence(struct timeval time[], l_int32, l_int32 *);
     void order_float(float unord_arr[], float ord_arr[],
                      l_int32 start, l_int32 num_elems);
     void order_dbl(double unord_arr[], double ord_arr[],
                    l_int32 start, l_int32 num_elems);
     void order_int(l_int32 unord_arr[], l_int32 ord_arr[], l_int32 num_elems);
     void send_ctr_mesg(uint8_t *ctr_buff, l_int32 ctr_code);
     l_int32 recv_ctr_mesg(Ptr<Socket> ctr_strm, uint8_t *ctr_buff);
     void recv_ctr_mesg(Ptr<Socket> ctr_strm, l_int32 &ctr_code);
     l_int32 equal(double a, double b);
     l_int32 less_than(double a, double b);
     l_int32 grtr_than(double a, double b);
     void netlogger();
     void print_time(FILE *fp, l_int32 time);
     void sig_sigusr1();

     l_int32 exp_flag;
     l_int32 grey_flag;
     double tr;
     double adr;
     double tr_max;
     double tr_min;
     double grey_max, grey_min;
     // l_int32 sock_tcp, sock_udp; // moved to Ptr<Socket> m_tcpsock, m_udpsock.
     struct timeval first_time, second_time;
     l_int32 exp_fleet_id;
     float bw_resol;
     l_uint32 min_time_interval, snd_latency, rcv_latency;
     l_int32 repeat_fleet;
     l_int32 counter; // # of consecutive times actual rate != request rate
     l_int32 converged_gmx_rmx;
     l_int32 converged_gmn_rmn;
     l_int32 converged_rmn_rmx;
     l_int32 converged_gmx_rmx_tm;
     l_int32 converged_gmn_rmn_tm;
     l_int32 converged_rmn_rmx_tm;
     double cur_actual_rate, cur_req_rate;
     double prev_actual_rate, prev_req_rate;
     double max_rate, min_rate;
     l_int32 max_rate_flag, min_rate_flag;
     l_int32 bad_fleet_cs, bad_fleet_rate_mismatch;
     l_int32 retry_fleet_cnt_cs, retry_fleet_cnt_rate_mismatch;
     FILE *pathload_fp, *netlog_fp;
     double pct_metric[50], pdt_metric[50];
     l_int32 trend_idx;
     double snd_time_interval;
     l_int32 min_rsleep_time;
     l_int32 num;
     l_int32 slow;
     l_int32 interrupt_coalescence;
     l_int32 ic_flag;
     l_int32 netlog;
     char hostname[256];
     l_int32 repeat_1, repeat_2;
     l_int32 lower_bound;
     l_int32 increase_stream_len;
     l_int32 requested_delay;
     struct timeval exp_start_time;
     uint8_t ctr_buff[8];       // moved from main().
     UintegerValue mss;
     l_int32 ctr_code;

     // Characteristics of a packet stream.
     l_int32 m_time_interval;      // in usec
     l_uint32 m_transmission_rate; // in bit/sec
     l_int32 m_cur_pkt_sz;         // in bytes
     l_int32 m_max_pkt_sz;         // in bytes
     l_int32 m_rcv_max_pkt_sz;     // in bytes
     l_int32 m_snd_max_pkt_sz;     // in bytes
     l_int32 m_num_stream;
     l_int32 m_stream_len;      // # of packets

     // テスト用
     void GetMetrics(l_int32 stream_id);
     void SendControlMessage(l_int32 ctr_code);
     double *m_owd;             // 1 ... m_stream_len
     int m_prove_pkt_cnt;
     double *m_pct, *m_pdt;         // 1 ... m_num_stream
     double *m_pct_avg, *m_pdt_avg; // 1 ... m_period
     double *m_pct_med, *m_pdt_med; // 1 ... m_period
     l_int32 m_stream_id;
     l_int32 m_fleet_id;
     l_int32 m_duration;
     l_int32 m_period;


#ifdef THRLIB
typedef struct {
    pthread_t ptid;
    l_int32 finished_stream;
    l_int32 stream_cnt;
} thr_arg;
#endif
};

} // namespace ns3

#endif  // PATHLOAD_RECEIVER_H
