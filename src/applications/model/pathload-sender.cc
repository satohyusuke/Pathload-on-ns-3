/* -*- Mode:C++; c-file-style:"k&r"; indent-tabs-mode:nil; -*- */

#define LOCAL

#include <algorithm>
#include <cstring>
#include <iostream>
#include <fstream>
#include "ns3/boolean.h"
#include "ns3/inet-socket-address.h"
#include "ns3/integer.h"
#include "ns3/log.h"
#include "ns3/nstime.h"
#include "ns3/tcp-socket-factory.h"
#include "ns3/timer.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/uinteger.h"
#include "pathload-globals.h"
#include "pathload-sender.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("PathloadSender");
NS_OBJECT_ENSURE_REGISTERED(PathloadSender);

TypeId PathloadSender::GetTypeId()
{
     static TypeId tid = TypeId("ns3::PathloadSender")
          .SetParent<Application>()
          .AddConstructor<PathloadSender>()
          .AddAttribute("Local TCP",
                        "An local IPv4 address and a port number for TCP.",
                        AddressValue(),
                        MakeAddressAccessor(&PathloadSender::m_snd_tcp_addr),
                        MakeAddressChecker())
          .AddAttribute("Local UDP",
                        "An local IPv4 address and a port number for UDP",
                        AddressValue(),
                        MakeAddressAccessor(&PathloadSender::m_snd_udp_addr),
                        MakeAddressChecker())
          .AddAttribute("Remote UDP",
                        "An remote IPv4 address and a port number for UDP",
                        AddressValue(),
                        MakeAddressAccessor(&PathloadSender::m_rcv_udp_addr),
                        MakeAddressChecker())
          .AddAttribute("Packet Size",
                        "Size of a packet that constituent a packet stream",
                        IntegerValue(500),
                        MakeIntegerAccessor(&PathloadSender::m_cur_pkt_sz),
                        MakeIntegerChecker<l_int32> ())
          .AddAttribute("Iterate",
                        "Enable iterated estimation.",
                        BooleanValue(false),
                        MakeBooleanAccessor(&PathloadSender::iterate),
                        MakeBooleanChecker())
          ;
     return tid;
}

PathloadSender::PathloadSender() : m_mss(0)
{
     NS_LOG_FUNCTION_NOARGS();

     m_tcpsock = 0;
     m_udpsock = 0;
}

PathloadSender::~PathloadSender()
{
     NS_LOG_FUNCTION_NOARGS();
}

void PathloadSender::DoDispose()
{
     NS_LOG_FUNCTION_NOARGS();

     Application::DoDispose();  // chain up to parent class
}

void PathloadSender::StartApplication()
{
     NS_LOG_FUNCTION_NOARGS();

     // メンバ初期化
     m_quiet = true;
     m_num_stream = NUM_STREAM;
     m_fleet_cnt = 0;
     m_stream_cnt = 0;
     m_snd_max_pkt_sz = 0;
     m_rcv_max_pkt_sz = 0;
     m_stream_len = STREAM_LEN; // # of packets
     m_time_interval = 80;      // in usec
     m_stream_duration = m_stream_len * m_time_interval; // in usec
     m_stream_period = 9 * m_stream_duration;            // in usec

     // パラメータ出力
     m_transmission_rate =
          (static_cast<double>(8*m_cur_pkt_sz) / static_cast<double>(m_time_interval));
     std::cout << "Packet size = " << m_cur_pkt_sz << " byte" << std::endl;
     std::cout << "Packet interval = " << m_time_interval << " usec" << std::endl;
     std::cout << "Transmission rate = " << m_transmission_rate << " Mbit/sec" << std::endl;

     m_ofs.open("pathload_sender_packet_interval.txt", std::ios::out|std::ios::trunc);

     MinSleepTime();
     GetTimeOfDayLatency();

     // TCP listen ソケットを作成
     if (!m_tcpsock) {
          m_tcpsock = Socket::CreateSocket
               (GetNode(), TcpSocketFactory::GetTypeId());
          m_tcpsock->SetAttribute("SegmentSize", UintegerValue(1448));
          m_tcpsock->Bind(m_snd_tcp_addr); // bind()
          m_tcpsock->Listen();             // listen()
     }

     // TCP ソケットにコールバック関数を設定
     m_tcpsock->SetRecvCallback
          (MakeCallback(&PathloadSender::HandleRecv, this));
     m_tcpsock->SetAcceptCallback
          (MakeNullCallback<bool, Ptr<Socket>, const Address &>(),
           MakeCallback(&PathloadSender::HandleAccept, this));
     m_tcpsock->SetCloseCallbacks
          (MakeCallback(&PathloadSender::HandlePeerClose, this),
           MakeCallback(&PathloadSender::HandlePeerError, this));

     // UDP ソケットを作成
     if (!m_udpsock) {
          m_udpsock = Socket::CreateSocket
               (GetNode(), UdpSocketFactory::GetTypeId());
          m_udpsock->SetAttribute
               ("RcvBufSize", UintegerValue(UDP_BUFFER_SZ));
          m_udpsock->Bind(m_snd_udp_addr); // bind()
     }

     // UDP ソケットにコールバック関数を設定
     m_udpsock->SetRecvCallback
          (MakeCallback(&PathloadSender::HandleRecvfrom, this));

     if (!m_quiet) std::cout << "Waiting for the receiver to establish control stream ... ";

     // PathloadReceiver からの connect 待ち
     // 続く処理は HandleAccept()
}

void PathloadSender::StopApplication()
{
     NS_LOG_FUNCTION_NOARGS();

     if (m_ctr_strm) {
          m_ctr_strm->Close();
          m_ctr_strm->SetRecvCallback
               (MakeNullCallback<void, Ptr<Socket> >());
     }

     // listen ソケットを close
     if (m_tcpsock) {
          m_tcpsock->Close();
          m_tcpsock->SetRecvCallback
               (MakeNullCallback<void, Ptr<Socket> >());
     }

     // UDP ソケットを close
     if (m_udpsock) {
          m_udpsock->Close();
          m_udpsock->SetRecvCallback
               (MakeNullCallback<void, Ptr<Socket> >());
     }
}

void PathloadSender::HandleRecv(Ptr<Socket> tcpsock)
{
     NS_LOG_FUNCTION_NOARGS();

     l_int32 ctr_code_n;
     uint8_t *ctr_buff = new uint8_t[sizeof(ctr_code_n)];

     uint32_t toRead = std::min((uint32_t) m_mss.Get(),
                                tcpsock->GetRxAvailable());
     Ptr<Packet> p = tcpsock->Recv(toRead, 0);
     p->CopyData(ctr_buff, sizeof(ctr_buff));
     memcpy(&ctr_code_n, ctr_buff, sizeof(l_int32));
     l_int32 ctr_code = ntohl(ctr_code_n);

     if (!m_rcv_max_pkt_sz) {
          std::cout << "PathloadSender: received reveiver's max packet size." << std::endl;
          m_rcv_max_pkt_sz = ctr_code;
          MainLoopPart2();      // メインループへ
     } else if (ctr_code == (CTR_CODE | SEND_FLEET)) {
          std::cout << "PathloadSender: sending a packet fleet." << std::endl;
          SendFleet();
     } else if (ctr_code == (CTR_CODE | CONTINUE_STREAM)) {
          std::cout << "PathloadSender: received CONTINUE_STREAM." << std::endl;
          if (m_stream_cnt < m_num_stream) {
               std::cout << "PathloadSender: continuing packet stream." << std::endl;
               Time elapsed = Simulator::Now() - m_stream_tail_time;
               if (elapsed.GetMicroSeconds() > m_stream_period) {
                    Simulator::ScheduleNow(&PathloadSender::SendFleet, this);
               } else {
                    l_int32 waittime =
                         m_stream_period - elapsed.GetMicroSeconds();
                    Simulator::Schedule(MicroSeconds(waittime),
                                        &PathloadSender::SendFleet, this);
               }
          } else if (m_fleet_cnt < m_stream_period) {
               m_stream_cnt = 0;
               m_fleet_cnt++;

               std::cout << "PathloadSender: sending next fleet." << std::endl;
               Time elapsed = Simulator::Now() - m_fleet_tail_time;
               if (elapsed.GetMicroSeconds() > m_stream_period) {
                    Simulator::ScheduleNow(&PathloadSender::SendFleet, this);
               } else {
                    l_int32 waittime =
                         m_stream_period - elapsed.GetMicroSeconds();
                    Simulator::Schedule(MicroSeconds(waittime),
                                        &PathloadSender::SendFleet, this);
               }
          }
     } else {
          std::cout << "PathloadSender: received unknown control message." << std::endl;
     }

     delete [] ctr_buff;
}

void PathloadSender::HandleRecvfrom(Ptr<Socket> udpsock)
{
     NS_LOG_FUNCTION_NOARGS();
}

void PathloadSender::HandleAccept(Ptr<Socket> tcpsock, const Address &addr)
{
     NS_LOG_FUNCTION_NOARGS();
     NS_LOG_INFO("PathloadSender: Connection accepted");

     if (!m_quiet) std::cout << "OK." << std::endl;
     m_ctr_strm = tcpsock;
     NS_ASSERT(m_ctr_strm);
     m_ctr_strm->SetRecvCallback
          (MakeCallback(&PathloadSender::HandleRecv, this));

     MainLoop();                // メインループへ
}

void PathloadSender::HandlePeerClose(Ptr<Socket> tcpsock)
{
     NS_LOG_FUNCTION_NOARGS();
     NS_LOG_INFO("PathloadSender: Peer Closed");
}

void PathloadSender::HandlePeerError(Ptr<Socket> tcpsock)
{
     NS_LOG_FUNCTION_NOARGS();
     NS_LOG_INFO("PathloadSender: Peer Error");
}

// PathloadSender のメインループ
void PathloadSender::MainLoopPart1()
{
     NS_LOG_FUNCTION_NOARGS();

     time_t localtm = time(NULL);
     if (!m_quiet) {
          std::cout << "Receiver " << m_snd_tcp_addr;
          std::cout << " starts measurements at ";
          std::cout << ctime(&localtm) << std::endl;
     }

     // Connect UDP socket.
     m_udpsock->Connect(m_rcv_udp_addr);

     // Make TCP socket non-blocking.
     // ns-3 はノンブロッキングモデルのソケットのみなので不要？

     // MSS の取得
     m_tcpsock->GetAttribute(std::string("SegmentSize"), m_mss);

     // Decide sender's max packet size.
     m_snd_max_pkt_sz = static_cast<l_int32>(m_mss.Get());
     if (m_snd_max_pkt_sz == 0 || m_snd_max_pkt_sz == 1448)
          m_snd_max_pkt_sz = 1472; // Make it ethernet sized MTU.
     else
          m_snd_max_pkt_sz = static_cast<l_int32>(m_mss.Get()) + 12;

     // Tell receiver our max packet size.
     SendControlMessage(m_snd_max_pkt_sz);

     // HandleRecv() へ
}

void PathloadSender::MainLoopPart2()
{
     NS_LOG_FUNCTION_NOARGS();

     // Decide the max packet size used for this estimation.
     m_max_pkt_sz = (m_rcv_max_pkt_sz < m_snd_max_pkt_sz) ?
          m_rcv_max_pkt_sz : m_snd_max_pkt_sz;
     if (!m_quiet) {
          std::cout << "Maximum packet size          :: ";
          std::cout << m_max_pkt_sz << " bytes" << std::endl;
     }

     // Tell receiver our send latency.
     // snd_time = SendLatency();
     // send_ctr_mesg(ctr_buff, snd_time);

     // ここまで同じ

     // Send a packet stream.
}

void PathloadSender::SendFleetPart1()
{
     NS_LOG_FUNCTION_NOARGS();

     l_int32 pkt_cnt = 0;       // = { 0 ... m_stream_len - 1 }

     uint8_t *pkt_buf = new uint8_t[m_cur_pkt_sz];
     if (!pkt_buf) {
          std::cout << "ERROR: SendFleet(): unable to malloc ";
          std::cout << m_cur_pkt_sz << " bytes" << std::endl;
          NS_ASSERT(false);
     }

     // Create random payload; does it matter?
     srandom(getpid());
     for (int i = 0; i < m_cur_pkt_sz-1; i++)
          pkt_buf[i] = static_cast<uint8_t> (random() & 0x000000ff);

     if (!m_quiet) std::cout << "Sending fleet " << m_fleet_cnt << " ";

     // Send a packet fleet; send packet streams.
     // if (m_stream_cnt < m_num_stream) {
          if (!m_quiet) std::cout << "#";
          std::cout << std::flush;

          // Time stamp for the first packet of the stream.
          // Time tmp1 = Simulator::Now();
          // Time tmp2;
          // l_int32 tmp;
          // double t1 = tmp1.ToDouble(Time::US);
          // double t2;

          // Send a packet stream.
          std::cout << "PathloadSender: sending packet stream " << m_stream_cnt << std::endl;
          SendPacketStream(pkt_buf, pkt_cnt);

          // // Wait for 2000 usec after sending a packet stream.
          // gettimeofday(&tmp2, NULL);
          // t1 = (double) tmp2.tv_sec * 1000000.0 + (double) tmp2.tv_usec;
          // do {
          //      gettimeofday(&tmp2, NULL);
          //      t2 = (double) tmp2.tv_sec * 1000000.0 + (double) tmp2.tv_usec;
          // } while ((t2 - t1) < 2000);

          // // Send end of stream message with the stream id.
          // ctr_code = FINISHED_STREAM | CTR_CODE;
          // if (send_ctr_mesg(ctr_buff, ctr_code) == -1) {
          //      delete [] pkt_buf;
          //      perror("send_ctr_mesg(): FINISHED_STREAM");
          //      return -1;
          // }

          // // Send current stream count.
          // if (send_ctr_mesg(ctr_buff, stream_cnt) == -1) {
          //      delete [] pkt_buf;
          //      return -1;
          // }

          // // Wait for continue/cancel message from receiver.
          // if ((ret_val = recv_ctr_mesg(ctr_buff)) == -1) {
          //      delete [] pkt_buf;
          //      return -1;
          // }
          // if ((((ret_val & CTR_CODE) >> 31) == 1) &&
          //     ((ret_val & 0x7fffffff) == CONTINUE_STREAM))
          //      stream_cnt++;
          // else if ((((ret_val & CTR_CODE) >> 31) == 1) &&
          //          ((ret_val & 0x7fffffff) == ABORT_FLEET)) {
          //      delete [] pkt_buf;
          //      return 0;
          // }

          // // Inter-stream latency is max{ RTT, 9*stream_duration }.
          // stream_duration = stream_len * time_interval;
          // if ((t2-t1) < (9*stream_duration)) {
          //      // Release CPU if inter-stream gap is longer than min_sleep_time.
          //      if (t2 - t1 - 9*stream_duration > min_sleep_interval) {
          //           sleep_tm_usec =
          //                time_interval - tmp
          //                - ((time_interval - tmp) % min_sleep_interval)
          //                - min_sleep_interval;
          //           sleep_time.tv_sec = (int) (sleep_tm_usec / 1000000);
          //           sleep_time.tv_usec =
          //                sleep_tm_usec - sleep_time.tv_sec * 1000000;
          //           select(1, NULL, NULL, NULL, &sleep_time);
          //           gettimeofday(&tmp2, NULL);
          //           t2 = (double) tmp2.tv_sec * 1000000.0 + (double) tmp2.tv_usec;
          //      }

          //      // Busy wait for sending next packet stream.
          //      do {
          //           gettimeofday(&tmp2, NULL);
          //           t2 = (double) tmp2.tv_sec * 1000000.0 + (double) tmp2.tv_usec;
          //      } while ((t2-t1) < 9*stream_duration);
          // }

          // // A hack for slow links.
          // if (stream_duration >= 500000) break;
     // }
     // delete [] pkt_buf;
}

void PathloadSender::SendPacketStream(uint8_t *pkt_buf, l_int32 pkt_id)
{
     // NS_LOG_FUNCTION_NOARGS();

     // Expected structure of a packet:
     //   +----------+-----------+-----------+----------+-----------+
     //   | fleet id | stream id | packet id | ts (sec) | ts (usec) |
     //   +----------+-----------+-----------+----------+-----------+
     // * ts = time stamp

     if (pkt_id < m_stream_len) {
          Time ts = Simulator::Now();

          if (m_stream_cnt == 0 && m_fleet_cnt == 0) {
               m_ofs << pkt_id << ' ' << ts.GetMicroSeconds() << std::endl;
          }

          l_int32 fleet_id_n = htonl(m_fleet_cnt);
          memcpy(pkt_buf, &fleet_id_n, sizeof(l_int32));
          l_int32 stream_id_n = htonl(m_stream_cnt);
          memcpy(pkt_buf + sizeof(l_int32), &stream_id_n, sizeof(l_int32));
          l_int32 pkt_id_n = htonl(pkt_id);
          memcpy(pkt_buf + 2 * sizeof(l_int32), &pkt_id_n, sizeof(l_int32));
          l_int32 sec_n =
               htonl(static_cast<l_int32>(ts.ToInteger(Time::US) / US_PER_SEC));
          memcpy(pkt_buf + 3 * sizeof(l_int32), &sec_n, sizeof(l_int32));
          l_int32 usec_n =
               htonl(static_cast<l_int32>(ts.ToInteger(Time::US) % US_PER_SEC));
          memcpy(pkt_buf + 4 * sizeof(l_int32), &usec_n, sizeof(l_int32));

          Ptr<Packet> p = Create<Packet>(pkt_buf, m_cur_pkt_sz);
          m_udpsock->Send(p, 0);

          // m_time_interval [usec] 後に次のパケットを送るようにスケジューリング
          Simulator::Schedule
               (MicroSeconds(m_time_interval),
                &PathloadSender::SendPacketStream,
                this, pkt_buf, ++pkt_id);
     } else {
          m_stream_tail_time = Simulator::Now();
          m_stream_cnt++;
          if (m_stream_cnt == (m_num_stream - 1))
               m_fleet_tail_time = Simulator::Now();

          // 2000 [usec] 後にパケットストリーム終端を示す制御コードを送信
          Simulator::Schedule
               (MicroSeconds(2000),
                &PathloadSender::SendControlMessage,
                this, CTR_CODE|FINISHED_STREAM);

          delete [] pkt_buf;
     }
}

// int PathloadSender::send_train()
// {
//      struct timeval select_tv;
//      uint8_t *pack_buf;
//      int train_id = 0, train_id_n;
//      int pack_id, pack_id_n;
//      l_int32 ctr_code;
//      int ret_val;
//      int train_len = 0;
//      uint8_t ctr_buff[8];

//      pack_buf = new uint8_t[max_pkt_sz];
//      if (!pack_buf) {
//           std::cout << "ERROR: send_train(): unable to malloc ";
//           std::cout << max_pkt_sz << " bytes" << std::endl;
//           NS_ASSERT(false);
//      }

//      // Create random payload; does it matter?
//      srandom(getpid());
//      for (int i = 0; i < max_pkt_sz-1; i++)
//           pack_buf[i] = (uint8_t) (random() & 0x000000ff);

//      // Send a packet train.
//      // Expected structure of a packet in the packet train:
//      //   +----------+------------+
//      //   | train id |  packet id |
//      //   +----------+------------+
//      while (train_id < MAX_TRAIN-1) {
//           if (train_len == 5)
//                train_len = 3;
//           else
//                train_len = TRAIN_LEN - (train_id * 15);
//           train_id_n = htonl(train_id);
//           memcpy(pack_buf, &train_id_n, sizeof(l_int32));

//           // Send packets with the packet id.
//           for (pack_id = 0; pack_id <= train_len; pack_id++) {
//                pack_id_n = htonl(pack_id);
//                memcpy(pack_buf + sizeof(l_int32), &pack_id_n, sizeof(l_int32));
//                Ptr<Packet> p = Create<Packet>(pack_buf, max_pkt_sz);
//                m_udpsock->Send(p, 0);
//           }

//           // ここまでやった

//           // Wait for 1000 usec after the train for sending control code.
//           select_tv.tv_sec = 0;
//           select_tv.tv_usec = 1000;
//           select(0, NULL, NULL, NULL, &select_tv);

//           // Send FINISHED_TRAIN code to receiver.
//           ctr_code = FINISHED_TRAIN | CTR_CODE;
//           send_ctr_mesg(ctr_buff, ctr_code);

//           // If received BAD_TRAIN code from receiver,
//           // continue to send a packet train that has less packets.
//           if ((ret_val = recv_ctr_mesg(ctr_buff)) == -1) return -1;
//           if ((((ret_val & CTR_CODE) >> 31) == 1) &&
//               ((ret_val & 0x7fffffff) == BAD_TRAIN)) {
//                train_id++;
//                continue;
//           } else {
//                delete [] pack_buf;
//                return 0;
//           }
//      }
//      delete [] pack_buf;

//      return 0;
// }

// Send a message through the control stream.
// Return -1 if failed to send.
int PathloadSender::send_ctr_mesg(uint8_t *ctr_buff, l_int32 ctr_code)
{
     NS_LOG_FUNCTION_NOARGS();

     l_int32 ctr_code_n = htonl(ctr_code);
     memcpy(ctr_buff, &ctr_code_n, sizeof(ctr_code_n));
     Ptr<Packet> p = Create<Packet>(ctr_buff, sizeof(ctr_buff));

     if (m_ctr_strm->Send(p, 0) == -1)
          return -1;
     else
          return 0;
}

void PathloadSender::SendControlMessage(l_int32 ctr_code)
{
     NS_LOG_FUNCTION_NOARGS();

     l_int32 ctr_code_n = htonl(ctr_code);
     uint8_t *ctr_buff = new uint8_t[sizeof(ctr_code_n)];
     memcpy(ctr_buff, &ctr_code_n, sizeof(ctr_code_n));
     Ptr<Packet> p = Create<Packet>(ctr_buff, sizeof(ctr_buff));
     NS_ASSERT(m_ctr_strm->Send(p, 0) != 0);
     delete [] ctr_buff;
}

// Measure how long it takes to send a packet in usec.
l_int32 PathloadSender::SendLatency()
{
     uint8_t *pack_buf;
     float min_OSdelta[50], ord_min_OSdelta[50];
     Ptr<Socket> sock_udp;
     Time first_time, current_time;
     Address snd_udp_addr, rcv_udp_addr;

     NS_ASSERT(m_max_pkt_sz);
     pack_buf = new uint8_t[m_max_pkt_sz];
     if (!pack_buf) {
          std::cout << "ERROR: SendLatency(): unable to malloc ";
          std::cout << m_max_pkt_sz << " bytes." << std::endl;
          NS_ASSERT(false);
     }

     sock_udp = Socket::CreateSocket
          (GetNode(), UdpSocketFactory::GetTypeId());
     NS_ASSERT(sock_udp);
     sock_udp->SetRecvCallback
          (MakeNullCallback<void, Ptr<Socket> >());
     snd_udp_addr = InetSocketAddress(Ipv4Address::GetLoopback(), 0);
     NS_ASSERT(!sock_udp->Bind(snd_udp_addr)); // bind()
     NS_ASSERT(!sock_udp->GetSockName(rcv_udp_addr));
     NS_ASSERT(!sock_udp->Connect(rcv_udp_addr)); // connect()

     // Create random payload; does it matter?
     srandom(getpid());
     for (int i = 0; i < m_max_pkt_sz-18; i++)
          pack_buf[i] = (uint8_t) (random() & 0x000000ff);

     Ptr<Packet> p = Create<Packet>
          (pack_buf, m_max_pkt_sz * sizeof(uint8_t));

     // Measure how long it takes to send a packet in usec.
     for (int i = 0; i < 50; i++) {
          first_time = Simulator::Now();
          if (sock_udp->Send(p, 0) == -1)
               perror("SendLatency(): sock_udp->Send()");
          current_time = Simulator::Now();
          min_OSdelta[i] = TimeToUsDelta(first_time, current_time);
     }

     // Use median of the measured latencies to avoid outliers.
     Order(min_OSdelta, ord_min_OSdelta, 0, 50);

     if (pack_buf) delete [] pack_buf;

     return (ord_min_OSdelta[25]);
}

const int NUM_SELECT_CALL = 31;
void PathloadSender::MinSleepTime()
{
     struct timeval sleep_time;
     Time time[NUM_SELECT_CALL];
     int64_t res[NUM_SELECT_CALL], ord_res[NUM_SELECT_CALL];
     l_int32 tm;

     time[0] = Simulator::Now();
     for (int i = 1; i < NUM_SELECT_CALL; i++) {
          sleep_time.tv_sec = 0;
          sleep_time.tv_usec = 1;
          time[i] = Simulator::Now();
          select(0, NULL, NULL, NULL, &sleep_time);
     }

     for (int i = 1; i < NUM_SELECT_CALL; i++) {
          res[i-1] = time[i].GetMicroSeconds() - time[i-1].GetMicroSeconds();
     }

     Order(res, ord_res, NUM_SELECT_CALL - 1);
     min_sleep_interval = static_cast<l_int32>
          ((ord_res[NUM_SELECT_CALL/2] + ord_res[NUM_SELECT_CALL/2 + 1]) / 2);
     time[0] = Simulator::Now();
     tm = min_sleep_interval + min_sleep_interval / 4;

     for (int i = 1; i < NUM_SELECT_CALL; i++) {
          sleep_time.tv_sec = 0;
          sleep_time.tv_usec = tm;
          time[i] = Simulator::Now();
          select(0, NULL, NULL, NULL, &sleep_time);
     }

     for (int i = 1; i < NUM_SELECT_CALL; i++) {
          res[i-1] = time[i].GetMicroSeconds() - time[i-1].GetMicroSeconds();
     }

     Order(res, ord_res, NUM_SELECT_CALL-1);
     min_timer_intr = static_cast<l_int32>
          ((ord_res[NUM_SELECT_CALL/2] + ord_res[NUM_SELECT_CALL/2 + 1]) / 2 - min_sleep_interval);

#ifdef DEBUG
     std::cout << "DEBUG: min_sleep_interval " << min_sleep_interval << std::endl;
     std::cout << "DEBUG: min_timer_intr " << min_timer_intr << std::endl;
#endif
}

// Measure our gettimeofday() latency.
void PathloadSender::GetTimeOfDayLatency()
{
     Time tv1, tv2;
     int64_t latency[30], ord_latency[30];

     for (int i = 0; i < 30; i++) {
          tv1 = Simulator::Now();
          tv2 = Simulator::Now();
          latency[i] = tv2.GetMicroSeconds() - tv1.GetMicroSeconds();
     }
     Order(latency, ord_latency, 30);
     m_gettimeofday_latency = static_cast<int>(ord_latency[15]);

#ifdef DEBUG
     std::cout << "DEBUG :: gettimeofday_latency = ";
     std::cout << m_gettimeofday_latency << std::endl;
#endif

}

template<typename T>
void PathloadSender::Order(const T unord[], T ord[], int n)
{
     NS_LOG_FUNCTION_NOARGS();

     Order(unord, ord, 0, n);

     // int i, j;

     // for (i = 0; i < n; i++) ord[i] = unord[i];
     // for (i = 1; i < n; i++) {
     //      for (j = i-1; j >= 0; j--)
     //           if (ord[j+1] < ord[j]) {
     //                std::swap(ord[j], ord[j+1]);
     //           } else
     //                break;
     // }
}

template<typename T>
void PathloadSender::Order(const T unord[], T ord[], int start, int num_elems)
{
     NS_LOG_FUNCTION_NOARGS();

     int i, j;

     for (i = start, j = 0; i < start+num_elems; i++, j++)
          ord[j] = unord[i];
     for (i = 1; i < num_elems; i++) {
          for (j = i-1; j >= 0; j--)
               if (ord[j+1] < ord[j]) {
                    std::swap(ord[j], ord[j+1]);
               } else
                    break;
     }
}

void PathloadSender::UsToTimeval(int64_t us, struct timeval &tv)
{
     tv.tv_sec = us / US_PER_SEC;
     tv.tv_usec = us % US_PER_SEC;
}
void PathloadSender::NsToTimeval(int64_t ns, struct timeval &tv)
{
     NS_ASSERT((ns % US_PER_NS) == 0);
     tv.tv_sec = ns / NS_PER_SEC;
     tv.tv_usec = (ns % NS_PER_SEC) / US_PER_NS;
}

uint64_t PathloadSender::TimevalToNs(struct timeval &tv)
{
     uint64_t nsResult =
          tv.tv_sec * NS_PER_SEC + tv.tv_usec * US_PER_NS;
     NS_ASSERT ((nsResult % US_PER_NS) == 0);

     return nsResult;
}

// // Receive a message through the control stream.
// // Return -1 if time out occured.
// l_int32 PathloadSender::recv_ctr_mesg(uint8_t *ctr_buff)
// {
//      struct timeval select_tv;   // for select()
//      fd_set readset;
//      l_int32 ctr_code;

//      // If no control message for 50 sec, terminate.
//      select_tv.tv_sec = 50;
//      select_tv.tv_usec = 0;

//      FD_ZERO(&readset);
//      FD_SET(m_ctr_strm, &readset);
//      bzero(ctr_buff, sizeof(l_int32));

//      if (select(m_ctr_strm+1, &readset, NULL, NULL, &select_tv) > 0) {
//           if (read(m_ctr_strm, ctr_buff, sizeof(l_int32)) < 0) return -1;
//           memcpy(&ctr_code, ctr_buff, sizeof(l_int32));
//           return (ntohl(ctr_code));
//      } else {
//           printf("Receiver hasn't responded.\n");
//           return -1;
//      }
// }

// // Order an array of int using bubblesort.
// void PathloadSender::order_int(int unord_arr[], int ord_arr[], int num_elems)
// {
//      int i, j;
//      int temp;

//      for (i = 0; i < num_elems; i++) ord_arr[i] = unord_arr[i];
//      for (i = 1; i < num_elems; i++) {
//           for (j = i-1; j >= 0; j--)
//                if (ord_arr[j+1] < ord_arr[j]) {
//                     temp = ord_arr[j];
//                     ord_arr[j] = ord_arr[j+1];
//                     ord_arr[j+1] = temp;
//                } else
//                     break;
//      }
// }

// // Order an array of double using bubblesort.
// void PathloadSender::order_dbl(double unord_arr[], double ord_arr[],
//                                int start, int num_elems)
// {
//      int i, j, k;
//      double temp;

//      for (i = start, k = 0; i < start+num_elems; i++, k++)
//           ord_arr[k] = unord_arr[i];
//      for (i = 1; i < num_elems; i++) {
//           for (j = i-1; j >= 0; j--)
//                if (ord_arr[j + 1] < ord_arr[j]) {
//                     temp = ord_arr[j];
//                     ord_arr[j] = ord_arr[j+1];
//                     ord_arr[j+1] = temp;
//                } else
//                     break;
//      }
// }

// Order an array of float using bubblesort.
// void PathloadSender::order_float(float unord_arr[], float ord_arr[],
//                                  int start, int num_elems)
// {
//      int i, j, k;
//      double temp;

//      for (i = start, k = 0; i < start+num_elems; i++, k++)
//           ord_arr[k] = unord_arr[i];
//      for (i = 1; i < num_elems; i++) {
//           for (j = i-1; j >= 0; j--)
//                if (ord_arr[j+1] < ord_arr[j]) {
//                     temp = ord_arr[j];
//                     ord_arr[j] = ord_arr[j+1];
//                     ord_arr[j+1] = temp;
//                } else
//                     break;
//      }
// }

} // namespace ns3
