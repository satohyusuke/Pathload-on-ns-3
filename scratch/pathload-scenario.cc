/* -*- Mode:C++; c-file-style:"k&r"; indent-tabs-mode:nil; -*- */

// シミュレーショントポロジ
//
// +----+                        +----+
// | s1 |------+          +----- | t1 |
// +----+      |          |      +----+
//             |          |
// +----+    +----+    +----+    +----+
// | s2 |----| r1 |----| r2 |----| t2 |
// +----+    +----+    +----+    +----+
//             |          |
// +-----+     |          |     +-----+
// | ctg |-----+          +-----| cts |
// +-----+                      +-----+
//
// s1 -> t1, s2 -> t2, ctg -> cts

#include "ns3/core-module.h"
#include "ns3/csma-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"

#include "ns3/pathload-globals.h"
#include "ns3/pathload-sender.h"
#include "ns3/pathload-receiver.h"
#include "ns3/pathload-helper.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("PathloadScenario");

const Time MONTH = Seconds(60*60*24*30);

// メイン関数
int main(int argc, char *argv[])
{
     // ログレベルの設定
     // LogComponentEnable("PathloadScenario", LOG_LEVEL_INFO);
     // LogComponentEnable("PathloadSender", LOG_LEVEL_FUNCTION);
     // LogComponentEnable("PathloadSender", LOG_LEVEL_INFO);
     // LogComponentEnable("PathloadReceiver", LOG_LEVEL_FUNCTION);
     // LogComponentEnable("PathloadReceiver", LOG_LEVEL_INFO);

     uint32_t nSender = 1;      // 送信ホストの数
     uint32_t nReceiver = 1;    // 受信ホストの数
     uint32_t nRouter = 2;      // 中間ルータの数
     int32_t packetsize;

     // コマンドラインからパラメータを設定可能にする
     CommandLine cmd;
     cmd.AddValue("nSender",
                  "Number of senders of Pathload.",
                  nSender);
     cmd.AddValue("nReceiver",
                  "Number of receivers of Pathload.",
                  nReceiver);
     cmd.AddValue("nRouter",
                  "Number of routers located between senders and receivers.",
                  nRouter);
     cmd.AddValue("PacketSize",
                  "A packet size of PathloadSender.",
                  packetsize);
     cmd.Parse(argc, argv);

     NS_LOG_INFO("A Pathload Simulator.");

     // ノード r1, s1 を作成
     NS_LOG_INFO("Creating Nodes.");
     NodeContainer r1s1;
     r1s1.Create(2);

     // ノード s2 を作成
     NodeContainer r1s2;
     r1s2.Add(r1s1.Get(0));
     r1s2.Create(1);

     // ノード ctg を作成
     NodeContainer r1ctg;
     r1ctg.Add(r1s1.Get(0));
     r1ctg.Create(1);

     // ノード r2, t1 を作成
     NodeContainer r2t1;
     r2t1.Create(2);

     // ノード t2 を作成
     NodeContainer r2t2;
     r2t2.Add(r2t1.Get(0));
     r2t2.Create(1);

     // ノード cts を作成
     NodeContainer r2cts;
     r2cts.Add(r2t1.Get(0));
     r2cts.Create(1);

     NodeContainer r1r2;
     r1r2.Add(r1s1.Get(0));
     r1r2.Add(r2t1.Get(0));

     // 通信路の設定
     NS_LOG_INFO("Setting up channels.");
     CsmaHelper csma;
     csma.SetChannelAttribute("DataRate", StringValue("100Mbps"));
     csma.SetChannelAttribute("Delay", StringValue("5ms"));
     csma.SetQueue("ns3::DropTailQueue",
                   "Mode", EnumValue(DropTailQueue::PACKETS),
                   "MaxPackets", UintegerValue(10000000));

     // ネットデバイスの作成
     NS_LOG_INFO("Setting up net devices.");
     NetDeviceContainer dev_r1s1 = csma.Install(r1s1);
     NetDeviceContainer dev_r1s2 = csma.Install(r1s2);
     NetDeviceContainer dev_r1ctg = csma.Install(r1ctg);
     NetDeviceContainer dev_r2t1 = csma.Install(r2t1);
     NetDeviceContainer dev_r2t2 = csma.Install(r2t2);
     NetDeviceContainer dev_r2cts = csma.Install(r2cts);
     NetDeviceContainer dev_r1r2 = csma.Install(r1r2);

     // すべてのノードに TCP/IP スタックをインストール
     NS_LOG_INFO("Installing TCP/IP stack to all nodes.");
     InternetStackHelper stack;
     stack.InstallAll();

     // IP アドレスの割り当て
     NS_LOG_INFO("Assigning IP addresses.");
     Ipv4AddressHelper ipv4addr;
     ipv4addr.SetBase("10.1.1.0", "255.255.255.0");
     Ipv4InterfaceContainer ipv4if_r1s1 = ipv4addr.Assign(dev_r1s1);
     ipv4addr.SetBase("10.1.2.0", "255.255.255.0");
     Ipv4InterfaceContainer ipv4if_r1s2 = ipv4addr.Assign(dev_r1s2);
     ipv4addr.SetBase("10.1.3.0", "255.255.255.0");
     Ipv4InterfaceContainer ipv4if_r1ctg = ipv4addr.Assign(dev_r1ctg);
     ipv4addr.SetBase("10.2.1.0", "255.255.255.0");
     Ipv4InterfaceContainer ipv4if_r2t1 = ipv4addr.Assign(dev_r2t1);
     ipv4addr.SetBase("10.2.2.0", "255.255.255.0");
     Ipv4InterfaceContainer ipv4if_r2t2 = ipv4addr.Assign(dev_r2t2);
     ipv4addr.SetBase("10.2.3.0", "255.255.255.0");
     Ipv4InterfaceContainer ipv4if_r2cts = ipv4addr.Assign(dev_r2cts);
     ipv4addr.SetBase("10.3.1.0", "255.255.255.0");
     Ipv4InterfaceContainer ipv4if_r1r2 = ipv4addr.Assign(dev_r1r2);

     // グローバルルーティングテーブルの作成
     NS_LOG_INFO("Creating global routing table.");
     Ipv4GlobalRoutingHelper::PopulateRoutingTables();

     // Pathload 送信ホストのアドレス
     InetSocketAddress snd_tcp_addr
          (ipv4if_r1s1.GetAddress(1), TCP_SND_PORT);
     InetSocketAddress snd_udp_addr
          (ipv4if_r1s1.GetAddress(1), UDP_RCV_PORT);

     // Pathload 受信ホストのアドレス
     InetSocketAddress rcv_tcp_addr
          (ipv4if_r2t1.GetAddress(1), TCP_SND_PORT);
     InetSocketAddress rcv_udp_addr
          (ipv4if_r2t1.GetAddress(1), UDP_RCV_PORT);

     // PathloadSender アプリケーションの設定
     NS_LOG_INFO("Setting up applications.");
     PathloadSenderHelper pathloadsnd;
     pathloadsnd.SetAttribute("Local TCP", AddressValue(snd_tcp_addr));
     pathloadsnd.SetAttribute("Local UDP", AddressValue(snd_udp_addr));
     pathloadsnd.SetAttribute("Remote UDP", AddressValue(rcv_udp_addr));
     pathloadsnd.SetAttribute("Packet Size", IntegerValue(packetsize));

     // PathloadReceiver アプリケーションの設定
     PathloadReceiverHelper pathloadrcv;
     pathloadrcv.SetAttribute("Local UDP", AddressValue(rcv_udp_addr));
     pathloadrcv.SetAttribute("Remote TCP", AddressValue(snd_tcp_addr));

     // OnOffApplication の設定
     uint16_t onoff_port = 20000;
     OnOffHelper onoff = OnOffHelper
          ("ns3::UdpSocketFactory",
           InetSocketAddress(ipv4if_r2cts.GetAddress(1), onoff_port));
     onoff.SetAttribute("DataRate", DataRateValue(10000000)); // in bit/sec

     // PacketSink アプリケーションの設定
     uint16_t ps_port = 20001;
     PacketSinkHelper packetsink = PacketSinkHelper
           ("ns3::UdpSocketFactory",
            InetSocketAddress(ipv4if_r2cts.GetAddress(1), ps_port));

     // アプリケーションのインストール
     NS_LOG_INFO("Installing applications.");
     ApplicationContainer sndapps = pathloadsnd.Install(r1s1.Get(1));
     sndapps.Start(Seconds(0.0));
     sndapps.Stop(MONTH);

     ApplicationContainer rcvapps = pathloadrcv.Install(r2t1.Get(1));
     rcvapps.Start(Seconds(0.1));
     rcvapps.Stop(MONTH);

     ApplicationContainer onoffapps = onoff.Install(r1ctg.Get(1));
     onoffapps.Start(Seconds(0.0));
     onoffapps.Stop(MONTH);

     ApplicationContainer psapps = packetsink.Install(r2cts.Get(1));
     psapps.Start(Seconds(0.0));
     psapps.Stop(MONTH);

     // トレースファイルの設定
     // AsciiTraceHelper ascii;
     // csma.EnableAsciiAll(ascii.CreateFileStream("Pathload.tr"));
     // csma.EnablePcapAll("Pathload");

     NS_LOG_INFO("Starting simulation.");
     Simulator::Run();
     Simulator::Destroy();
     NS_LOG_INFO("Simulation done.");

     return 0;
}
