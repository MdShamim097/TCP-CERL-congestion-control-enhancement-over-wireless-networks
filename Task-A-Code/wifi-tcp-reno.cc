/*  WiFi Tcp Congestion Control for adhoc network*/

#include <fstream>
#include <iostream>
#include "ns3/command-line.h"
#include "ns3/config.h"
#include "ns3/string.h"
#include "ns3/log.h"
#include "ns3/yans-wifi-helper.h"
#include "ns3/ssid.h"
#include "ns3/mobility-helper.h"
#include "ns3/on-off-helper.h"
#include "ns3/yans-wifi-channel.h"
#include "ns3/mobility-model.h"
#include "ns3/packet-sink.h"
#include "ns3/tcp-westwood.h"
#include "ns3/packet-sink-helper.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/flow-monitor-module.h"  // added
#include "ns3/stats-module.h"
#include "ns3/pointer.h"
#include "ns3/aodv-module.h"
#include "ns3/ipv4-list-routing-helper.h"

NS_LOG_COMPONENT_DEFINE ("wifi-tcp");

using namespace ns3;

Ptr<PacketSink> sink;                         /* Pointer to the packet sink application */

int
main (int argc, char *argv[])
{
  uint32_t payloadSize = 100;                       /* Transport layer payload size in bytes. */
  std::string dataRate = "1Mbps";                  /* Application layer datarate. */
  std::string tcpVariant = "TcpNewReno";             /* TCP variant type. */
  std::string phyRate = "HtMcs7";                    /* Physical layer bitrate. */
  double simulationTime = 0.3;                         /* Simulation time in seconds. */
  bool pcapTracing = false;                          /* PCAP Tracing is enabled or not. */
  uint32_t m_protocol=2;

  int nWifis=30;
  int nodeSpeed = 10; //in m/s
  int nodePause = 0; //in s
  
  
  /* Command line argument parser setup. */
  CommandLine cmd (__FILE__);
  cmd.AddValue ("payloadSize", "Payload size in bytes", payloadSize);
  cmd.AddValue ("dataRate", "Application data ate", dataRate);
  cmd.AddValue ("tcpVariant", "Transport protocol to use: TcpNewReno, "
                "TcpHybla, TcpHighSpeed, TcpHtcp, TcpVegas, TcpScalable, TcpVeno, "
                "TcpBic, TcpYeah, TcpIllinois, TcpWestwood, TcpWestwoodPlus, TcpLedbat ", tcpVariant);
  cmd.AddValue ("phyRate", "Physical layer bitrate", phyRate);
  cmd.AddValue ("simulationTime", "Simulation time in seconds", simulationTime);
  cmd.AddValue ("pcap", "Enable/disable PCAP Tracing", pcapTracing);
  cmd.AddValue ("protocol", "1=OLSR;2=AODV;3=DSDV;4=DSR", m_protocol);
  cmd.Parse (argc, argv);

  // Select TCP variant
  if (tcpVariant.compare ("TcpNewReno") == 0)
    {
      Config::SetDefault ("ns3::TcpL4Protocol::SocketType", TypeIdValue (TcpNewReno::GetTypeId ()));
    }
  else
    {
      TypeId tcpTid;
      NS_ABORT_MSG_UNLESS (TypeId::LookupByNameFailSafe (tcpVariant, &tcpTid), "TypeId " << tcpVariant << " not found");
      Config::SetDefault ("ns3::TcpL4Protocol::SocketType", TypeIdValue (TypeId::LookupByName (tcpVariant)));
    }

  /* Configure TCP Options */
  Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (payloadSize));

  WifiMacHelper wifiMac;
  WifiHelper wifiHelper;
  wifiHelper.SetStandard (WIFI_STANDARD_80211n_5GHZ);

  /* Set up Legacy Channel */
  YansWifiChannelHelper wifiChannel;
  wifiChannel.SetPropagationDelay ("ns3::ConstantSpeedPropagationDelayModel");
  wifiChannel.AddPropagationLoss ("ns3::FriisPropagationLossModel", "Frequency", DoubleValue (5e9));

  /* Setup Physical Layer */
  YansWifiPhyHelper wifiPhy;
  wifiPhy.SetChannel (wifiChannel.Create ());
  wifiPhy.SetErrorRateModel ("ns3::YansErrorRateModel");
  wifiHelper.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
                                      "DataMode", StringValue (phyRate),
                                      "ControlMode", StringValue ("HtMcs0"));

  NodeContainer networkNodes;
  networkNodes.Create (nWifis);
  
  NetDeviceContainer networkDevices;
  networkDevices = wifiHelper.Install (wifiPhy, wifiMac, networkNodes);
  
  MobilityHelper mobilityAdhoc;
  ObjectFactory pos;
  pos.SetTypeId ("ns3::RandomRectanglePositionAllocator");
  pos.Set ("X", StringValue ("ns3::UniformRandomVariable[Min=0.0|Max=300.0]"));
  pos.Set ("Y", StringValue ("ns3::UniformRandomVariable[Min=0.0|Max=1500.0]"));

  Ptr<PositionAllocator> taPositionAlloc = pos.Create ()->GetObject<PositionAllocator> ();

  std::stringstream ssSpeed;
  ssSpeed << "ns3::UniformRandomVariable[Min=0.0|Max=" << nodeSpeed << "]";
  std::stringstream ssPause;
  ssPause << "ns3::ConstantRandomVariable[Constant=" << nodePause << "]";
  mobilityAdhoc.SetMobilityModel ("ns3::RandomWaypointMobilityModel",
                                  "Speed", StringValue (ssSpeed.str ()),
                                  "Pause", StringValue (ssPause.str ()),
                                  "PositionAllocator", PointerValue (taPositionAlloc));
  mobilityAdhoc.SetPositionAllocator (taPositionAlloc);
  mobilityAdhoc.Install (networkNodes);
  
  /* Internet stack */
  InternetStackHelper stack;
  stack.Install (networkNodes);
  
  AodvHelper aodv;
  Ipv4ListRoutingHelper list;
 
  list.Add (aodv,100);  
  stack.SetRoutingHelper (list);

  Ipv4AddressHelper address;
  address.SetBase ("10.0.0.0", "255.255.255.0");
  
  Ipv4InterfaceContainer networkInterfaces;
  networkInterfaces = address.Assign (networkDevices);

  /* Install TCP Receiver on the access point */
  PacketSinkHelper sinkHelper ("ns3::TcpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), 9));
  ApplicationContainer sinkApp = sinkHelper.Install (networkNodes);
  sink = StaticCast<PacketSink> (sinkApp.Get (1));

  /* Install TCP/UDP Transmitter on the station */
  OnOffHelper server ("ns3::TcpSocketFactory", (InetSocketAddress (networkInterfaces.GetAddress (1), 9)));
  server.SetAttribute ("PacketSize", UintegerValue (payloadSize));
  server.SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=1]"));
  server.SetAttribute ("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=0]"));
  server.SetAttribute ("DataRate", DataRateValue (DataRate (dataRate)));
  ApplicationContainer serverApp = server.Install (networkNodes);
  
  //AsciiTraceHelper ascii;
  //wifiPhy.EnableAsciiAll (ascii.CreateFileStream ("reno.tr"));
  
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll();
  
  /* Start Simulation */
  Simulator::Stop (Seconds (simulationTime));
  Simulator::Run ();
  
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats ();

  int count=0;
  /*float thr=0.0,dr=0.0,lr=0.0;
  float dl=0.0;
  std::ofstream myfile1,myfile2,myfile3,myfile4;
  myfile1.open ("throughput_r.txt");
  myfile2.open ("delay_r.txt");
  myfile3.open ("delivery-ratio_r.txt");
  myfile4.open ("loss-ratio_r.txt");
  */
  for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator iter = stats.begin (); iter != stats.end (); ++iter)
  {
	  Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (iter->first);
          
          //if((iter->second.timeLastRxPacket.GetSeconds()-iter->second.timeFirstTxPacket.GetSeconds() <=0) || (t.sourceAddress==t.destinationAddress) || (iter->second.delaySum.GetSeconds()>simulationTime) ) continue;
          if((iter->second.timeLastRxPacket.GetSeconds()-iter->second.timeFirstTxPacket.GetSeconds() <=0) || (t.sourceAddress==t.destinationAddress)) continue;
          
          count++;
          
          NS_LOG_UNCOND("-------------------------------------------------------------");
          NS_LOG_UNCOND("Flow ID:" <<iter->first);
          NS_LOG_UNCOND("Source Address: " <<t.sourceAddress << ",  Destination Address: "<< t.destinationAddress);
          NS_LOG_UNCOND("Source Port: " <<t.sourcePort << ",  Destination Port: "<< t.destinationPort);
          NS_LOG_UNCOND("Packet delivery ratio =" <<((iter->second.rxPackets*1.0)*100/iter->second.txPackets) << "%");
          NS_LOG_UNCOND("Packet loss ratio =" << ((iter->second.txPackets-iter->second.rxPackets)*1.0)*100/iter->second.txPackets << "%");
          NS_LOG_UNCOND("End-to-end delay =" <<iter->second.delaySum.GetSeconds()<<"s");
          NS_LOG_UNCOND("Throughput =" <<iter->second.rxBytes * 8.0/(iter->second.timeLastRxPacket.GetSeconds()-iter->second.timeFirstTxPacket.GetSeconds())/1024/1024<<"Mbps");
          
          /*
          thr=iter->second.rxBytes * 8.0/(iter->second.timeLastRxPacket.GetSeconds()-iter->second.timeFirstTxPacket.GetSeconds())/1024/1024;
          if(thr==-0) thr=0; 
          dl=iter->second.delaySum.GetSeconds();
          dr=iter->second.rxPackets*100/iter->second.txPackets ;
          lr=(iter->second.txPackets-iter->second.rxPackets)*100/iter->second.txPackets;
          
          myfile1<< count<<" "<<thr<<"\n";
          myfile2 << count<<" "<<dl<<"\n";
          myfile3 << count<<" "<<dr<<"\n";
          myfile4<< count<<" "<<lr<<"\n";         
          */
  }
  /*
  myfile1.close();
  myfile2.close();
  myfile3.close();
  myfile4.close();
  */
  Simulator::Destroy ();
  
  NS_LOG_UNCOND("------------------------------------------");
  NS_LOG_UNCOND("Total flows: " <<count);
  
  //monitor->SerializeToXmlFile("tcp-new-reno.xml", true, true);
 
  return 0;
}
