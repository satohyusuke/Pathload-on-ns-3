/* -*- Mode:C++; c-file-style:"k&r"; indent-tabs-mode:nil; -*- */

#ifndef PATHLOAD_SENDER_H
#define PATHLOAD_SENDER_H

#include <cmath>
#include <fstream>
#include "ns3/address.h"
#include "ns3/application.h"
#include "ns3/packet.h"
#include "ns3/ptr.h"
#include "ns3/realtime-simulator-impl.h"
#include "ns3/socket.h"
#include "ns3/simulator.h"
#include "ns3/type-id.h"
#include "ns3/uinteger.h"
#ifndef PATHLOAD_GLOBALS_H
#include "pathload-globals.h"
#endif

namespace ns3 {

const uint64_t US_PER_NS = (uint64_t) 1000;
const uint64_t US_PER_SEC = (uint64_t) 1000000;
const uint64_t NS_PER_SEC = (uint64_t) 1000000000;

// PathloadSender アプリケーション
class PathloadSender : public Application {
public:
     static TypeId GetTypeId();

     PathloadSender();             // コンストラクタ
     virtual ~PathloadSender();    // デストラクタ

protected:
     virtual void DoDispose();

private:
     virtual void StartApplication();
     virtual void StopApplication();

     // ソケットコールバック関数
     void HandleRecv(Ptr<Socket> tcpsock);
     void HandleRecvfrom(Ptr<Socket> udpsock);
     void HandleAccept(Ptr<Socket> tcpsock, const Address &addr);
     void HandlePeerClose(Ptr<Socket> tcpsock);
     void HandlePeerError(Ptr<Socket> tcpsock);

     // PathloadSender のメインループ
     inline void MainLoop() { MainLoopPart1(); }
     void MainLoopPart1();
     void MainLoopPart2();
     void MainLoopPart3();
     void MainLoopPart4();

     inline void SendFleet() { SendFleetPart1(); }
     void SendFleetPart1();
     void SendFleetPart2();
     void SendFleetPart3();

     void SendPacketStream(uint8_t *pkt_buf, l_int32 pkt_id);

     template<typename T> void Order(const T unord[], T ord[], int n);
     template<typename T> void Order(const T unord[], T ord[],
                                     int start, int num_elems);
     inline void Dummy() {};
     void UsToTimeval(int64_t us, struct timeval &tv);
     void NsToTimeval(int64_t ns, struct timeval &tv);
     uint64_t TimevalToNs(struct timeval &tv);

     Ptr<Socket> m_tcpsock;     // TCP listen ソケット
     Ptr<Socket> m_ctr_strm;    // TCP 通信ソケット
     Ptr<Socket> m_udpsock;     // UDP ソケット

     Address m_snd_tcp_addr;    // 送信ホスト TCP アドレス
     Address m_snd_udp_addr;    // 送信ホスト UDP アドレス
     Address m_rcv_tcp_addr;    // 受信ホスト TCP アドレス
     Address m_rcv_udp_addr;    // 受信ホスト UDP アドレス

     int send_train();
     int send_ctr_mesg(char *ctr_buff, l_int32 ctr_code);
     int send_ctr_mesg(uint8_t *ctr_buff, l_int32 ctr_code); // added for ns-3
     void SendControlMessage(l_int32 ctr_code);
     l_int32 recv_ctr_mesg(char *ctr_buff);
     l_int32 recv_ctr_mesg(uint8_t *ctr_buff); // added for ns-3
     l_int32 SendLatency();
     void MinSleepTime();
     void GetTimeOfDayLatency();
     inline double TimeToUsDelta(struct timeval tv1, struct timeval tv2)
          { return static_cast<double> (abs((tv2.tv_sec - tv1.tv_sec)) * 1000000
                                        + abs(tv2.tv_usec - tv1.tv_usec)); }
     inline double TimeToUsDelta(Time tv1, Time tv2)
          { return abs(tv2.ToDouble(Time::US) - tv1.ToDouble(Time::US)); }

     l_int32 fleet_id_n;
     l_int32 fleet_id;
     uint32_t send_buff_sz;     // moved from int send_buff_sz.
     int rcv_tcp_adrlen;
     l_int32 min_sleep_interval; // in usec
     l_int32 min_timer_intr;     // in usec
     int m_gettimeofday_latency;
     bool m_quiet;              // Quiet mode flag
     uint8_t ctr_buff[8];       // moved from main().
     l_int32 ctr_code;
     UintegerValue m_mss;       // Maximum Segment Size
     bool iterate;              // iterated estimation flag
     l_uint32 snd_time;

     // Characteristics of a packet stream.
     l_int32 m_time_interval;      // in usec
     // l_uint32 m_transmission_rate; // in bit/sec
     double m_transmission_rate;
     l_int32 m_cur_pkt_sz;         // in bytes
     l_int32 m_max_pkt_sz;         // in bytes
     l_int32 m_snd_max_pkt_sz;     // in bytes
     l_int32 m_rcv_max_pkt_sz;     // in bytes
     l_int32 m_num_stream;         // # of streams
     l_int32 m_stream_len;         // # of packets

     l_int32 m_stream_period;   // パケットストリーム周期 [usec]
     l_int32 m_stream_duration; // パケットストリーム持続時間 [usec]
     l_int32 m_stream_cnt;      // = { 0 ... m_num_stream - 1 }
     l_int32 m_fleet_cnt;       // fleet count
     Time m_stream_tail_time;   // パケットストリームを送り終えた時刻
     Time m_fleet_tail_time;    // パケットフリートを送り終えた時刻
     std::ofstream m_ofs;


};

} // namespace ns3

#endif  // PATHLOAD_SENDER_H
