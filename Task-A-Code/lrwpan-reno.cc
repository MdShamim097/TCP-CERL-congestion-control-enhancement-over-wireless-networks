#include <fstream>
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/internet-apps-module.h"
#include "ns3/mobility-module.h"
#include "ns3/spectrum-module.h"
#include "ns3/propagation-module.h"
#include "ns3/sixlowpan-module.h"
#include "ns3/lr-wpan-module.h"
#include "ns3/csma-module.h"
#include "ns3/applications-module.h"
#include "ns3/ipv6-flow-classifier.h"
#include "ns3/flow-monitor-helper.h"
#include <ns3/lr-wpan-error-model.h>
#include "ns3/flow-monitor-module.h"
#include "ns3/stats-module.h"
#include "ns3/aodv-module.h"
#include "ns3/ipv6-list-routing-helper.h"
#include <ns3/propagation-loss-model.h>
#include <ns3/propagation-delay-model.h>
#include <ns3/single-model-spectrum-channel.h>

using namespace ns3;

int main (int argc, char** argv) {
  uint16_t nNodes=30;
  uint32_t nWsnNodes; // Wireless Sensor Network
  uint16_t sinkPort=9;
  double simulationTime = 10.0;
  std::string tcpVariant = "TcpNewReno";
  std::string dataRate = "1Mbps";                  /* Application layer datarate. */
  uint32_t payloadSize = 100;                       /* Transport layer payload size in bytes. */
  uint32_t m_protocol=2;
  int nodeSpeed = 10; //in m/s
  int nodePause = 0; //in s

  CommandLine cmd (__FILE__);
  cmd.AddValue ("dataRate", "Application data ate", dataRate);
  cmd.AddValue ("protocol", "1=OLSR;2=AODV;3=DSDV;4=DSR", m_protocol);
  cmd.AddValue ("tcpVariant", "Transport protocol to use: TcpNewReno, "
                "TcpHybla, TcpHighSpeed, TcpHtcp, TcpVegas, TcpScalable, TcpVeno, "
                "TcpBic, TcpYeah, TcpIllinois, TcpWestwood,TcpCerl, TcpWestwoodPlus, TcpLedbat ", tcpVariant);
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
                    
  //Create nodes
  nWsnNodes = nNodes + 1;
  NodeContainer wsnNodes;
  wsnNodes.Create (nWsnNodes);

  NodeContainer wiredNode;
  wiredNode.Create (1);
  wiredNode.Add (wsnNodes.Get (0));

  // Set mobility
  MobilityHelper mobility;
  ObjectFactory pos;
  pos.SetTypeId ("ns3::RandomRectanglePositionAllocator");
  pos.Set ("X", StringValue ("ns3::UniformRandomVariable[Min=0.0|Max=300.0]"));
  pos.Set ("Y", StringValue ("ns3::UniformRandomVariable[Min=0.0|Max=1500.0]"));

  Ptr<PositionAllocator> taPositionAlloc = pos.Create ()->GetObject<PositionAllocator> ();

  std::stringstream ssSpeed;
  ssSpeed << "ns3::UniformRandomVariable[Min=0.0|Max=" << nodeSpeed << "]";
  std::stringstream ssPause;
  ssPause << "ns3::ConstantRandomVariable[Constant=" << nodePause << "]";
  mobility.SetMobilityModel ("ns3::RandomWaypointMobilityModel",
                                  "Speed", StringValue (ssSpeed.str ()),
                                  "Pause", StringValue (ssPause.str ()),
                                  "PositionAllocator", PointerValue (taPositionAlloc));
  mobility.SetPositionAllocator (taPositionAlloc);
  mobility.Install (wsnNodes);

  // Add and install the LrWpanNetDevice for each node
  LrWpanHelper lrWpanHelper;
  NetDeviceContainer lrwpanDevices = lrWpanHelper.Install(wsnNodes);

  // Fake PAN association and short address assignment.
  // This is needed because the lr-wpan module does not provide (yet)
  // a full PAN association procedure.
  lrWpanHelper.AssociateToPan (lrwpanDevices, 0);
  
  // Each device must be attached to the same channel
  Ptr<SingleModelSpectrumChannel> channel = CreateObject<SingleModelSpectrumChannel> ();
  Ptr<LogDistancePropagationLossModel> propModel = CreateObject<LogDistancePropagationLossModel> ();
  Ptr<ConstantSpeedPropagationDelayModel> delayModel = CreateObject<ConstantSpeedPropagationDelayModel> ();
  channel->AddPropagationLossModel (propModel);
  channel->SetPropagationDelayModel (delayModel);
  
  lrWpanHelper.SetChannel(channel);
  
  //Install internet
  InternetStackHelper internetv6;
  internetv6.Install (wsnNodes);
  internetv6.Install (wiredNode.Get(0));
  
  AodvHelper aodv;
  Ipv4ListRoutingHelper list;
 
  list.Add (aodv,100);  
  internetv6.SetRoutingHelper (list);

  //Setup a sixlowpan stack to be used as a shim between IPv6 and a generic NetDevice
  SixLowPanHelper sixLowPanHelper;
  NetDeviceContainer sixLowPanDevices = sixLowPanHelper.Install (lrwpanDevices);

  CsmaHelper csmaHelper;
  NetDeviceContainer csmaDevices = csmaHelper.Install (wiredNode);

  //Assign IP address
  Ipv6AddressHelper ipv6;
  ipv6.SetBase (Ipv6Address ("2001:cafe::"), Ipv6Prefix (64));
  Ipv6InterfaceContainer wiredDeviceInterfaces;
  wiredDeviceInterfaces = ipv6.Assign (csmaDevices);
  wiredDeviceInterfaces.SetForwarding (1, true);
  wiredDeviceInterfaces.SetDefaultRouteInAllNodes (1);

  ipv6.SetBase (Ipv6Address ("2001:f00d::"), Ipv6Prefix (64));
  Ipv6InterfaceContainer wsnDeviceInterfaces;
  wsnDeviceInterfaces = ipv6.Assign (sixLowPanDevices);
  wsnDeviceInterfaces.SetForwarding (0, true);
  wsnDeviceInterfaces.SetDefaultRouteInAllNodes (0);

  for (uint32_t i = 0; i < sixLowPanDevices.GetN (); i++) {
    Ptr<NetDevice> dev = sixLowPanDevices.Get (i);
    dev->SetAttribute ("UseMeshUnder", BooleanValue (true));
    dev->SetAttribute ("MeshUnderRadius", UintegerValue (10));
  }

  for( uint32_t i=1; i<=nNodes; i++ ) {
    OnOffHelper server("ns3::TcpSocketFactory", (Inet6SocketAddress (wiredDeviceInterfaces.GetAddress (0,1), sinkPort)));
    server.SetAttribute ("PacketSize", UintegerValue (payloadSize));
    server.SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=1]"));
    server.SetAttribute ("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=0]"));
    server.SetAttribute ("DataRate", DataRateValue (DataRate (dataRate)));
    ApplicationContainer sourceApps = server.Install(wsnNodes.Get (i));

    PacketSinkHelper sinkApp ("ns3::TcpSocketFactory",
    Inet6SocketAddress (Ipv6Address::GetAny (), sinkPort));
    sinkApp.SetAttribute ("Protocol", TypeIdValue (TcpSocketFactory::GetTypeId ()));
    ApplicationContainer sinkApps = sinkApp.Install (wiredNode.Get(0));

    sinkPort++;
  }

  //Flow Statistics
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll ();

  Simulator::Stop (Seconds (simulationTime));
  Simulator::Run ();

  Ptr<Ipv6FlowClassifier> classifier = DynamicCast<Ipv6FlowClassifier> (flowmon.GetClassifier6 ());
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
	  Ipv6FlowClassifier::FiveTuple t = classifier->FindFlow (iter->first);
          
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
  NS_LOG_UNCOND("------------------------------------------");
  NS_LOG_UNCOND("Total flows: " <<count);

  Simulator::Destroy ();

  return 0;
}

