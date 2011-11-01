/* -*- Mode:C++; c-file-style:"k&r"; indent-tabs-mode:nil; -*- */

// #define LOCAL

#include <algorithm>
#include <fstream>
#include "ns3/inet-socket-address.h"
#include "ns3/log.h"
#include "ns3/tcp-socket-factory.h"
#include "ns3/timer.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/uinteger.h"
#include "pathload-globals.h"
#include "pathload-receiver.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("PathloadReceiver");
NS_OBJECT_ENSURE_REGISTERED(PathloadReceiver);

TypeId PathloadReceiver::GetTypeId()
{
     static TypeId tid = TypeId("ns3::PathloadReceiver")
          .SetParent<Application>()
          .AddConstructor<PathloadReceiver>()
          .AddAttribute("Remote TCP",
                        "An remote IPv4 address and a port number for TCP.",
                        AddressValue(),
                        MakeAddressAccessor(&PathloadReceiver::m_snd_tcp_addr),
                        MakeAddressChecker())
          .AddAttribute("Local UDP",
                        "An local IPv4 address and a port number for UDP.",
                        AddressValue(),
                        MakeAddressAccessor(&PathloadReceiver::m_rcv_udp_addr),
                        MakeAddressChecker())
          ;

     return tid;
}

PathloadReceiver::PathloadReceiver() : mss(0)
{
     NS_LOG_FUNCTION_NOARGS();

     m_tcpsock = 0;
     m_snd_max_pkt_sz = 0;
}

PathloadReceiver::~PathloadReceiver()
{
     NS_LOG_FUNCTION_NOARGS();
}

void PathloadReceiver::DoDispose()
{
     NS_LOG_FUNCTION_NOARGS();

     delete [] m_owd;
     delete [] m_pct;
     delete [] m_pdt;
     delete [] m_pct_avg;
     delete [] m_pdt_avg;
     delete [] m_pct_med;
     delete [] m_pdt_med;

     Application::DoDispose();  // chain up to parent class
}

void PathloadReceiver::StartApplication()
{
     NS_LOG_FUNCTION_NOARGS();

     // メンバ初期化
     m_time_interval = 80;
     m_snd_max_pkt_sz = 0;
     m_stream_len = STREAM_LEN;
     m_num_stream = NUM_STREAM;
     m_duration = m_time_interval * m_stream_len;
     m_period = 9 * m_duration;
     m_prove_pkt_cnt = 0;
     m_stream_id = 0;
     m_fleet_id = 0;

     m_owd = new double[m_stream_len];
     std::fill(m_owd, m_owd+m_stream_len, 0.0);

     m_pct = new double[m_num_stream];
     m_pdt = new double[m_num_stream];
     std::fill(m_pct, m_pct+m_num_stream, 0.0);
     std::fill(m_pdt, m_pdt+m_num_stream, 0.0);

     m_pct_avg = new double[m_period];
     m_pdt_avg = new double[m_period];
     m_pct_med = new double[m_period];
     m_pdt_med = new double[m_period];
     std::fill(m_pct_avg, m_pct_avg+m_period, 0.0);
     std::fill(m_pdt_avg, m_pdt_avg+m_period, 0.0);
     std::fill(m_pct_med, m_pct_med+m_period, 0.0);
     std::fill(m_pdt_med, m_pdt_med+m_period, 0.0);

     // TCP ソケットを作成
     if (!m_tcpsock) {
          m_tcpsock = Socket::CreateSocket
               (GetNode(), TcpSocketFactory::GetTypeId());
          m_tcpsock->SetAttribute("SegmentSize", UintegerValue(1448));
     }

     // コールバック関数の設定
     m_tcpsock->SetRecvCallback
          (MakeCallback(&PathloadReceiver::HandleRecv, this));
     m_tcpsock->SetConnectCallback
          (MakeCallback(&PathloadReceiver::HandleConnected, this),
           MakeNullCallback<void, Ptr<Socket> >());

     // UDP ソケットの設定
     if (!m_udpsock) {
          m_udpsock = Socket::CreateSocket
               (GetNode(), UdpSocketFactory::GetTypeId());
          m_udpsock->SetAttribute
               ("RcvBufSize", UintegerValue(UDP_BUFFER_SZ));
          m_udpsock->Bind(m_rcv_udp_addr);  // bind()
     }

     // コールバック関数の設定
     m_udpsock->SetRecvCallback
          (MakeCallback(&PathloadReceiver::HandleRecvfrom, this));

     m_tcpsock->Connect(m_snd_tcp_addr); // connect()

     // 続く処理は HandleConnect()
}

void PathloadReceiver::StopApplication()
{
     NS_LOG_FUNCTION_NOARGS();

     // TCP ソケットを close
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

void PathloadReceiver::HandleRecv(Ptr<Socket> tcpsock)
{
     NS_LOG_FUNCTION_NOARGS();

     l_int32 ctr_code_n;
     uint8_t *ctr_buff = new uint8_t[sizeof(ctr_code_n)];

     uint32_t toRead = std::min((uint32_t) mss.Get(),
                                tcpsock->GetRxAvailable());
     Ptr<Packet> p = tcpsock->Recv(toRead, 0);
     p->CopyData(ctr_buff, sizeof(ctr_buff));
     memcpy(&ctr_code_n, ctr_buff, sizeof(l_int32));
     l_int32 ctr_code = ntohl(ctr_code_n);

     if (!m_snd_max_pkt_sz) {
          m_snd_max_pkt_sz = ctr_code;
          MainLoopPart2();
     } else if (ctr_code == (CTR_CODE | FINISHED_STREAM)) {
          std::cout << "PathloadReceiver: received FINISHED_STREAM." << std::endl;
          GetMetrics(m_stream_id);
          SendControlMessage(CTR_CODE | CONTINUE_STREAM);
          std::cout << "PathloadReceiver: sent CONTINUE_STREAM." << std::endl;
     }

     delete [] ctr_buff;
}

void PathloadReceiver::HandleRecvfrom(Ptr<Socket> udpsock)
{
     NS_LOG_FUNCTION_NOARGS();

     // Expected structure of a packet:
     //   +----------+-----------+-----------+----------+-----------+
     //   | fleet id | stream id | packet id | ts (sec) | ts (usec) |
     //   +----------+-----------+-----------+----------+-----------+
     //   * ts = time stamp

     NS_ASSERT(m_max_pkt_sz);

     uint8_t *pkt_buff = new uint8_t[m_max_pkt_sz];
     l_int32 fleet_id_n, stream_id_n, pkt_id_n, sec_n, usec_n;

     Ptr<Packet> p = udpsock->Recv(m_max_pkt_sz, 0);
     NS_ASSERT(p);
     p->CopyData(pkt_buff, p->GetSize());
     m_prove_pkt_cnt++;

     memcpy(&fleet_id_n, pkt_buff, sizeof(l_int32));
     m_fleet_id = ntohl(fleet_id_n);
     memcpy(&stream_id_n, pkt_buff + sizeof(l_int32), sizeof(l_int32));
     m_stream_id = ntohl(stream_id_n);
     memcpy(&pkt_id_n, pkt_buff + 2 * sizeof(l_int32), sizeof(l_int32));
     l_int32 pkt_id = ntohl(pkt_id_n);
     memcpy(&sec_n, pkt_buff + 3 * sizeof(l_int32), sizeof(l_int32));
     l_int32 sec = ntohl(sec_n);
     memcpy(&usec_n, pkt_buff + 4 * sizeof(l_int32), sizeof(l_int32));
     l_int32 usec = ntohl(usec_n);

     // 片道遅延の計測
     Time current = Simulator::Now();
     m_owd[pkt_id] =
          current.ToDouble(Time::US) - static_cast<double>(sec * 1000000 + usec);

     delete [] pkt_buff;
}

void PathloadReceiver::HandleConnected(Ptr<Socket> tcpsock)
{
     NS_LOG_FUNCTION_NOARGS();

     // MSS の取得
     m_tcpsock->GetAttribute(std::string("SegmentSize"), mss);

     MainLoop();                // メインループへ
}

void PathloadReceiver::MainLoopPart1()
{
     NS_LOG_FUNCTION_NOARGS();

     // extern char *optarg;
     // struct hostent *host_snd;
     // struct sockaddr_in snd_tcp_addr, rcv_udp_addr;
     // struct utsname uts;         // uname() で使う
     // l_int32 ctr_code = 0;
     // l_int32 trend;
     // l_int32 prev_trend = 0;
     // l_int32 opt_len, rcv_buff_sz, mss;
     // l_int32 ret_val;
     // l_int32 errflg = 0;
     // l_int32 file = 0;
     // char netlogfile[50], filename[50];
     // char myname[50], buff[26];
     // char mode[4];
     // l_int32 c;
     // struct itimerval expireat;
     // struct sigaction sigstruct;

     // 続く処理は HandleRecv()
}

void PathloadReceiver::MainLoopPart2()
{
     NS_LOG_FUNCTION_NOARGS();

     // Decide Receiver's max packet size.
     m_rcv_max_pkt_sz = static_cast<int>(mss.Get());
     if (m_rcv_max_pkt_sz == 0 || m_rcv_max_pkt_sz == 1448)
          m_rcv_max_pkt_sz = 1472; // Make it Ethernet sized MTU.
     else
          m_rcv_max_pkt_sz = static_cast<int>(mss.Get()) + 12;

     SendControlMessage(m_rcv_max_pkt_sz);

     m_max_pkt_sz = m_snd_max_pkt_sz < m_rcv_max_pkt_sz ?
          m_snd_max_pkt_sz : m_rcv_max_pkt_sz;

     SendControlMessage(CTR_CODE | SEND_FLEET);
}

void PathloadReceiver::GetMetrics(l_int32 stream_id)
{
     NS_LOG_FUNCTION_NOARGS();

     if (stream_id == 0) {
          std::ofstream ofs("pathload_receiver_owd.txt", std::ios::out|std::ios::trunc);
          for (int i = 1; i < m_stream_len; i++) {
               ofs << i << ' ' << m_owd[i] - m_owd[i-1] << std::endl;
          }
          ofs.close();
     }

     // パケットロスをカウント
     int lost_packets = 0;
     for (int i = 0; i < m_stream_len; i++) {
          if (m_owd[i] == 0.0) lost_packets++;
     }

     // メトリックを計算
     if (lost_packets >= (m_stream_len * HIGH_LOSS_RATE * 0.01)) {
          m_pct[stream_id] = 1.0;
          m_pdt[stream_id] = 1.0;
     } else {
          l_int32 owd_med_len =
               static_cast<l_int32>(sqrt(m_stream_len));
          double *owd_med = new double[owd_med_len];

          // OWD の中央値を抽出
          for (int i = 0; i < owd_med_len; i++) {
               double unord[owd_med_len], ord[owd_med_len];
               for (int j = 0; j < owd_med_len; j++) {
                    unord[j] = m_owd[i*owd_med_len + j];
               }
               Order(unord, ord, owd_med_len);
               owd_med[i] = ord[owd_med_len / 2];
          }

          m_pct[stream_id] =
               PairwiseComparisonTest(owd_med, 0, owd_med_len);
          m_pdt[stream_id] =
               PairwiseDifferenceTest(owd_med, 0, owd_med_len);

          delete [] owd_med;
     }

     if ((stream_id == m_num_stream - 1) && (m_fleet_id < m_period)) {
          std::cout << Simulator::Now().GetSeconds() << " ";

          double sum = 0.0;
          for (int i = 0; i < m_num_stream; i++) sum += m_pct[i];
          std::cout << "pct_avg[" << m_fleet_id << "] = ";
          std::cout << sum / (double) m_num_stream << ", ";

          double *pct_ord = new double[m_num_stream];
          Order(m_pct, pct_ord, m_num_stream);
          m_pct_med[m_fleet_id] = pct_ord[m_num_stream / 2];
          std::cout << "pct_med[" << m_fleet_id << "] = ";
          std::cout << m_pct_med[m_fleet_id] << ", ";

          sum = 0.0;
          for (int i = 0; i < m_num_stream; i++) sum += m_pdt[i];
          std::cout << "pdt_avg[" << m_fleet_id << "] = ";
          std::cout << sum / (double) m_num_stream << ", ";

          double *pdt_ord = new double[m_num_stream];
          Order(m_pdt, pdt_ord, m_num_stream);
          m_pdt_med[m_fleet_id] = pdt_ord[m_num_stream / 2];
          std::cout << "pdt_med[" << m_fleet_id << "] = ";
          std::cout << m_pdt_med[m_fleet_id] << std::endl;

          m_stream_id = 0;

          delete [] pct_ord;
          delete [] pdt_ord;
     }

     //
     std::fill(m_owd, m_owd+m_stream_len, 0.0);
     m_prove_pkt_cnt = 0;
     m_stream_id++;
}

l_int32 PathloadReceiver::aggregate_trend_result()
{
     return 0;
}

double PathloadReceiver::time_to_us_delta(struct timeval tv1,
                                          struct timeval tv2)
{
     return 0;
}

double PathloadReceiver::get_avg(double data[], l_int32 no_values)
{
     return 0;
}

// PCT to detect increasing trend in a stream.
double
PathloadReceiver::PairwiseComparisonTest(double array[], l_int32 start, l_int32 end)
{
     NS_LOG_FUNCTION_NOARGS();

     l_int32 improvement = 0;

     if ((end - start) >= MIN_PARTITIONED_STREAM_LEN) {
          for (int i = start; i < end-1; i++)
               if (array[i] < array[i+1]) improvement++;
          double total = static_cast<double> (end - start);
          return (static_cast<double>(improvement) / total);
     } else
          return -1;
}

// PDT to detect increasing trend in a stream.
double
PathloadReceiver::PairwiseDifferenceTest(double array[], l_int32 start, l_int32 end)
{
     NS_LOG_FUNCTION_NOARGS();

     double y = 0.0;
     double y_abs = 0.0;

     if ((end - start) >= MIN_PARTITIONED_STREAM_LEN) {
          for (int i = start+1; i < end; i++) {
               y += array[i] - array[i-1];
               y_abs += fabs(array[i] - array[i-1]);
          }
          return (y_abs == 0 ? 0 : y/y_abs);
     } else
          return 2.0;
}

l_int32
PathloadReceiver::rate_adjustment(l_int32 flag)
{
     return 0;
}

l_int32
PathloadReceiver::eliminate_sndr_side_CS(double sndr_time_stamp[],
                                         l_int32 split_owd[])
{
     return 0;
}

l_int32
PathloadReceiver::eliminate_rcvr_side_CS(double rcvr_time_stamp[], double owd[],
                                         double owdfortd[], l_int32 low, l_int32 high,
                                         l_int32 *num_rcvr_cs, l_int32 *tmp_b2b)
{
     return 0;
}

l_int32
PathloadReceiver::eliminate_b2b_pkt_ic(double rcvr_time_stamp[], double owd[],
                                       double owdfortd[], l_int32 low, l_int32 high,
                                       l_int32 *num_rcvr_cs, l_int32 *tmp_b2b)
{
     return 0;
}

void
PathloadReceiver::adjust_offset_to_zero(double owd[], l_int32 last_pkt_id)
{
}

l_int32 PathloadReceiver::recv_fleet()
{
     return 0;
}

void
PathloadReceiver::print_contextswitch_info(l_int32 num_sndr_cs[],
                                           l_int32 num_rcvr_cs[],
                                           l_int32 discard[],
                                           l_int32 stream_cnt)
{
}

void *
PathloadReceiver::ctrl_listen(void *arg)
{
     return NULL;
}

void PathloadReceiver::radj_notrend()
{
}

void PathloadReceiver::radj_increasing()
{
}

void PathloadReceiver::radj_greymin()
{
}

void PathloadReceiver::radj_greymax()
{
}

l_int32 PathloadReceiver::calc_param()
{
     return 0;
}

void PathloadReceiver::get_sending_rate()
{
}

double PathloadReceiver::get_adr()
{
     return 0.0;
}

l_int32
PathloadReceiver::recv_train(l_int32 exp_train_id, struct timeval *time,
                             l_int32 train_len)
{
     return 0;
}

l_int32 PathloadReceiver::recvfrom_latency(struct sockaddr_in rcv_udp_addr)
{
     return 0;
}

double PathloadReceiver::grey_bw_resolution()
{
     return 0.0;
}

void
PathloadReceiver::get_pct_trend(double pct_metric[], l_int32 pct_trend[],
                                l_int32 pct_result_cnt)
{
}

void
PathloadReceiver::get_pdt_trend(double pdt_metric[], l_int32 pdf_trend[],
                                l_int32 pdf_result_cnt)
{
}

void PathloadReceiver::get_trend(double owdfortd[], l_int32 pkt_cnt)
{
     NS_LOG_FUNCTION_NOARGS();

     double median_owd[MAX_STREAM_LEN];
     l_int32 median_owd_len = 0;
     double ordered[MAX_STREAM_LEN];
     l_int32 count, pkt_per_min;

     // pkt_per_min = 5;
     pkt_per_min = (int) floor(sqrt((double) pkt_cnt));
     count = 0;
     for (int i = 0; i < pkt_cnt; i = i+pkt_per_min) {
          if (i+pkt_per_min >= pkt_cnt)
               count = pkt_cnt - i;
          else
               count = pkt_per_min;
          Order(owdfortd, ordered, i, count);
          if (count % 2 == 0)
               median_owd[median_owd_len++] =
                    (ordered[(int) (count * .5) - 1] + ordered[(int) (count * 0.5)]) / 2;
          else
               median_owd[median_owd_len++] = ordered[(int) (count * 0.5)];
     }
     pct_metric[trend_idx] =
          PairwiseComparisonTest(median_owd, 0, median_owd_len);
     pdt_metric[trend_idx] =
          PairwiseDifferenceTest(median_owd, 0, median_owd_len);

     trend_idx += 1;
}

l_int32
PathloadReceiver::get_sndr_time_interval(double snd_time[], double *sum)
{
     return 0;
}

void PathloadReceiver::sig_alrm()
{
}

void PathloadReceiver::terminate_gracefully(struct timeval exp_start_time)
{
}

l_int32
PathloadReceiver::check_intr_coalescence(struct timeval time[], l_int32 len,
                                         l_int32 *burst)
{
     return 0;
}

// Order an array of float using bubblesort.
void PathloadReceiver::order_float(float unord_arr[], float ord_arr[],
                                   l_int32 start, l_int32 num_elems)
{
     l_int32 i, j, k;
     double temp;

     for (i = start, k = 0; i < start+num_elems; i++, k++)
          ord_arr[k] = unord_arr[i];
     for (i = 1; i < num_elems; i++) {
          for (j = i-1; j >= 0; j--) {
               if (ord_arr[j+1] < ord_arr[j]) {
                    temp = ord_arr[j];
                    ord_arr[j] = ord_arr[j+1];
                    ord_arr[j+1] = temp;
               } else
                    break;
          }
     }
}


// Order an array of doubles using bubblesort.
void PathloadReceiver::order_dbl(double unord_arr[], double ord_arr[],
                                 l_int32 start, l_int32 num_elems)
{
     l_int32 i, j, k;
     double temp;

     for (i = start, k = 0; i < start+num_elems; i++, k++)
          ord_arr[k] = unord_arr[i];
     for (i = 1; i < num_elems; i++) {
          for (j = i-1; j >= 0; j--) {
               if (ord_arr[j+1] < ord_arr[j]) {
                    temp = ord_arr[j];
                    ord_arr[j] = ord_arr[j+1];
                    ord_arr[j+1] = temp;
               } else
                    break;
          }
     }
}

void PathloadReceiver::order_int(l_int32 unord_arr[], l_int32 ord_arr[],
                                 l_int32 num_elems)
{
     l_int32 i, j;
     l_int32 temp;

     for (i = 0; i < num_elems; i++) ord_arr[i] = unord_arr[i];
     for (i = 1; i < num_elems; i++) {
          for (j = i - 1; j >= 0; j--) {
               if (ord_arr[j + 1] < ord_arr[j]) {
                    temp = ord_arr[j];
                    ord_arr[j] = ord_arr[j + 1];
                    ord_arr[j + 1] = temp;
               } else
                    break;
          }
     }
}

template<typename T>
void PathloadReceiver::Order(const T unord[], T ord[], int n)
{
     NS_LOG_FUNCTION_NOARGS();

     Order(unord, ord, 0, n);

     // for (int i = 0; i < n; i++) ord[i] = unord[i];
     // for (int i = 1; i < n; i++) {
     //      for (int j = i-1; j >= 0; j--)
     //           if (ord[j+1] < ord[j]) std::swap(ord[j], ord[j+1]);
     //           else break;
     // }
}

template<typename T>
void PathloadReceiver::Order(const T unord[], T ord[], int start, int num_elems)
{
     NS_LOG_FUNCTION_NOARGS();

     int i, j;

     for (i = start, j = 0; i < start+num_elems; i++, j++)
          ord[j] = unord[i];
     for (i = 1; i < num_elems; i++) {
          for (j = i-1; j >= 0; j--)
               if (ord[j+1] < ord[j]) std::swap(ord[j], ord[j+1]);
               else break;
     }
}

// Send a message through the control stream.
void PathloadReceiver::send_ctr_mesg(uint8_t *ctr_buff, l_int32 ctr_code)
{
     NS_LOG_FUNCTION_NOARGS();

     l_int32 ctr_code_n = htonl(ctr_code);
     memcpy(ctr_buff, &ctr_code_n, sizeof(l_int32));
     Ptr<Packet> p = Create<Packet>(ctr_buff, sizeof(ctr_buff));

     if (m_tcpsock->Send(p, 0) == -1) {
          std::cerr << "send control message failed:" << std::endl;
          NS_ASSERT(false);
     }

     // l_int32 ctr_code_n = htonl(ctr_code);

     // memcpy((void *) ctr_buff, &ctr_code_n, sizeof(l_int32));
     // if (write(sock_tcp, ctr_buff, sizeof(l_int32)) != sizeof(l_int32)) {
     //      fprintf(stderr, "send control message failed:\n");
     //      exit(-1);
     // }
}

void PathloadReceiver::SendControlMessage(l_int32 ctr_code)
{
     NS_LOG_FUNCTION_NOARGS();

     l_int32 ctr_code_n = htonl(ctr_code);
     uint8_t *ctr_buff = new uint8_t[sizeof(ctr_code_n)];
     memcpy(ctr_buff, &ctr_code_n, sizeof(ctr_code_n));
     Ptr<Packet> p = Create<Packet>(ctr_buff, sizeof(ctr_buff));
     NS_ASSERT(m_tcpsock->Send(p, 0) != 0);
}

// Receive message from the control stream.
l_int32
PathloadReceiver::recv_ctr_mesg(Ptr<Socket> ctr_strm, uint8_t *ctr_buff)
{
     NS_LOG_FUNCTION_NOARGS();

     return 0;

     // l_int32 ctr_code;

     // gettimeofday(&first_time, 0);
     // if (read(ctr_strm, ctr_buff, sizeof(l_int32)) != sizeof(l_int32))
     //     return -1;
     // gettimeofday(&second_time, 0);
     // memcpy(&ctr_code, ctr_buff, sizeof(l_int32));

     // return (ntohl(ctr_code));
}

// Receive message from the control stream.
void
PathloadReceiver::recv_ctr_mesg(Ptr<Socket> ctr_strm, l_int32 &ctr_code)
{
     NS_LOG_FUNCTION_NOARGS();

     // l_int32 ctr_code;

     // gettimeofday(&first_time, 0);
     // if (read(ctr_strm, ctr_buff, sizeof(l_int32)) != sizeof(l_int32))
     //     return -1;
     // gettimeofday(&second_time, 0);
     // memcpy(&ctr_code, ctr_buff, sizeof(l_int32));

     // return (ntohl(ctr_code));
}

l_int32 PathloadReceiver::equal(double a, double b)
{
     return 0;
}

l_int32 PathloadReceiver::less_than(double a, double b)
{
     return 0;
}

l_int32 PathloadReceiver::grtr_than(double a, double b)
{
     return 0;
}

void PathloadReceiver::netlogger()
{
}

void PathloadReceiver::print_time(FILE *fp, l_int32 time)
{
}

void PathloadReceiver::sig_sigusr1()
{
}

} // namespace ns3
