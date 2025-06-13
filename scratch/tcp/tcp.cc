#include "../../src/internet/helper/ipv4-interface-container.h"
#include "../../src/internet/model/ipv4.h"
#include "../../src/traffic-control/model/tbf-queue-disc.h"
#include "tutorial-app.h"

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-flow-classifier.h"
#include "ns3/log.h"
#include "ns3/mobility-helper.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-dumbbell.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ssid.h"
#include "ns3/traffic-control-module.h"
#include "ns3/wifi-helper.h"
#include "ns3/wifi-module.h"
#include "ns3/wifi-phy.h"
#include "ns3/wifi-standards.h"

#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <tuple>
#include <vector>

PointToPointDumbbellHelper* dumbbell;

/**
 * Congestion window change callback
 *
 * @param oldCwnd Old congestion window.
 * @param newCwnd New congestion window.
 */
static void
CwndChange(Ptr<OutputStreamWrapper> stream, uint32_t oldCwnd, uint32_t newCwnd)
{
    NS_LOG_UNCOND(Simulator::Now().GetSeconds() << "\t" << newCwnd);
    *stream->GetStream() << Simulator::Now().GetSeconds() << "\t" << oldCwnd << "\t" << newCwnd
                         << std::endl;
}

/**
 * Logs a metric to a specified directory and file.
 *
 * @param directory Directory to write to (e.g., "metrics")
 * @param filename  Name of the file (e.g., "jfi.txt", "fct.txt")
 * @param message   The string message or data to log
 */
void LogMetric(const std::string& directory, const std::string& filename, const std::string& message)
{
    std::filesystem::create_directories(directory);
    std::string fullPath = directory + "/" + filename;
    std::ofstream outFile(fullPath, std::ios_base::app);

    if (!outFile.is_open())
    {
        std::cerr << "Failed to open " + fullPath +" for appending.\n";
        return;
    }

    outFile << message << std::endl;
    outFile.close();
}

// method used to measure average throughput at 0.1 second intervals
// utilized for graphing the results of simulations
void
CheckThroughput(Ptr<FlowMonitor> monitor, FlowMonitorHelper* flowHelper, std::vector<Ipv4Address> senderAddresses, std::vector<Ipv4Address> receiverAddresses)
{
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowHelper->GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();
    for (auto& flow : stats) {
        auto t = classifier->FindFlow(flow.first);
        if (std::find(senderAddresses.begin(), senderAddresses.end(), t.sourceAddress) != senderAddresses.end())
        {
            FlowId id = flow.first;
            double throughput = (flow.second.rxBytes * 8.0) / (flow.second.timeLastRxPacket.GetSeconds() - flow.second.timeFirstTxPacket.GetSeconds()) / 1e6; // Mbps

            LogMetric("metrics", "throughputs.txt", "Throughput for Flow " + std::to_string(id) + " at " + std::to_string(Simulator::Now().GetSeconds()) + " = " +  std::to_string(throughput));
        }
    }

    Simulator::Schedule(Seconds(0.1), &CheckThroughput, monitor, flowHelper, senderAddresses,receiverAddresses);
}

// sets up a wireless dumbbell topology
// this is the method where we vary the bandwith and delay in-transmission
std::tuple<std::tuple<std::vector<Ipv4Address>,std::vector<Ipv4Address>>,NodeContainer> SetupWirelessDumbbellTopology()
{
    uint32_t nLeaf = 3;
    NodeContainer leftNodes, rightNodes, apNodes;
    leftNodes.Create(nLeaf);
    rightNodes.Create(nLeaf);
    apNodes.Create(2);

    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(leftNodes);
    mobility.Install(rightNodes);
    mobility.Install(apNodes);

    YansWifiChannelHelper channelLeft  = YansWifiChannelHelper::Default();
    YansWifiPhyHelper     phyLeft      = YansWifiPhyHelper();
    phyLeft.SetChannel(channelLeft.Create());

    YansWifiChannelHelper channelRight = YansWifiChannelHelper::Default();
    YansWifiPhyHelper     phyRight     = YansWifiPhyHelper();
    phyRight.SetChannel(channelRight.Create());

    WifiHelper wifi;
    WifiMacHelper mac;
    Ssid ssidLeft  = Ssid("left-network");
    Ssid ssidRight = Ssid("right-network");

    mac.SetType("ns3::StaWifiMac",
            "Ssid",           SsidValue(ssidLeft),
            "ActiveProbing",  BooleanValue(false));
    NetDeviceContainer leftDevices  = wifi.Install(phyLeft, mac, leftNodes);

    mac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssidLeft));
    NetDeviceContainer apLeftDevice = wifi.Install(phyLeft, mac, apNodes.Get(0));

    mac.SetType("ns3::StaWifiMac",
            "Ssid",           SsidValue(ssidRight),
            "ActiveProbing",  BooleanValue(false));
    NetDeviceContainer rightDevices = wifi.Install(phyRight, mac, rightNodes);

    mac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssidRight));
    NetDeviceContainer apRightDevice = wifi.Install(phyRight, mac, apNodes.Get(1));

    // THIS IS WHERE WE ALTER CERTAIN VALUES IN ORDER TO RUN THE EXPERIMENTS

    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("10Mbps")); // starting bandwidth
    p2p.SetChannelAttribute("Delay",    StringValue("50ms")); // starting delay
    NetDeviceContainer routerLink = p2p.Install(apNodes.Get(0), apNodes.Get(1));
    Ptr<NetDevice> device = routerLink.Get(0);
    Ptr<PointToPointChannel> channel = DynamicCast<PointToPointChannel>(device->GetChannel());
    TrafficControlHelper tch;
    tch.SetRootQueueDisc("ns3::TbfQueueDisc",
                     "Rate", StringValue("10Mbps")); // also starting bandwidth - supposed to be the same as the one above
    apNodes.Get(0)->AggregateObject(CreateObject<TrafficControlLayer>());
    apNodes.Get(1)->AggregateObject(CreateObject<TrafficControlLayer>());
    QueueDiscContainer qdiscs = tch.Install(routerLink);

    Ptr<QueueDisc> tbf = qdiscs.Get(0);
    tbf->SetAttribute("MaxSize",StringValue("12p")); // manually set queue size here - to 2xBDP



    // EXAMPLE OF BANDWIDTH CHANGE MID-TRANSMISSION
    Simulator::Schedule(Seconds(10), &QueueDisc::SetAttribute,
             tbf,
             "Rate",
            StringValue("50Mbps")); // fill in the second of simulation at which you want to vary the delay and the new bandwidth value
    // EXAMPLE OF DELAY CHANGE MID-TRANSMISSION
    Simulator::Schedule(Seconds(10), &PointToPointChannel::SetAttribute,
           channel, "Delay", TimeValue(Seconds(0.3))); // fill in the second of simulation at which you want to vary the delay and the new delay value




    InternetStackHelper stack;
    stack.Install(leftNodes);
    stack.Install(rightNodes);
    stack.Install(apNodes);

    Ipv4AddressHelper leftAddress;
    leftAddress.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer leftInterfaces = leftAddress.Assign(leftDevices);
    leftAddress.Assign(apLeftDevice);

    Ipv4AddressHelper rightAddress;
    rightAddress.SetBase("10.1.2.0", "255.255.255.0");
    Ipv4InterfaceContainer rightInterfaces = rightAddress.Assign(rightDevices);
    rightAddress.Assign(apRightDevice);

    Ipv4AddressHelper routerAddress;
    routerAddress.SetBase("10.1.3.0", "255.255.255.0");
    routerAddress.Assign(routerLink);

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    NodeContainer allNodes;
    allNodes.Add(leftNodes);
    allNodes.Add(rightNodes);
    allNodes.Add(apNodes);

    std::vector<Ipv4Address> senderAddresses;
    std::vector<Ipv4Address> receiverAddresses;

    Ipv4InterfaceContainer::Iterator i;
    for (i = leftInterfaces.Begin (); i != leftInterfaces.End (); i++)
    {
        std::pair<Ptr<Ipv4>, uint32_t> pair = *i;
        Ptr<Ipv4> ipv4 = pair.first;
        senderAddresses.push_back(ipv4->GetAddress(1,0).GetLocal());
    }
    for (i = rightInterfaces.Begin (); i != rightInterfaces.End (); i++)
    {
        std::pair<Ptr<Ipv4>, uint32_t> pair = *i;
        Ptr<Ipv4> ipv4 = pair.first;
        receiverAddresses.push_back(ipv4->GetAddress(1,0).GetLocal());
    }
    return std::make_tuple(std::make_tuple(senderAddresses, receiverAddresses),allNodes);

}


// adds a flow to the connection
// currently there are 3 nodes on each side of the topology, so srcIndex should be 0,1 or 2 and dstIndex should be 3, 4 or 5
// rest of the variables should be fairly self-explanatory
void AddWirelessTcpFlow(uint32_t flowIndex, uint32_t srcIndex, uint32_t dstIndex, std::string tcpType, uint32_t packetSize, uint32_t nPackets, double startTime, double stopTime, NodeContainer nodes) {
    // set the TCP variant
    if (tcpType == "cubic") {
        Config::Set("/NodeList/*/$ns3::TcpL4Protocol/SocketType", TypeIdValue(TcpCubic::GetTypeId()));
    } else if (tcpType == "bbr") {
        // BBRv1
        Config::Set("/NodeList/*/$ns3::TcpL4Protocol/SocketType", TypeIdValue(TcpBbr::GetTypeId()));
    } else if (tcpType == "bbrv3")
    {
        // BBRv3
        Config::Set("/NodeList/*/$ns3::TcpL4Protocol/SocketType", TypeIdValue(TcpBbr3::GetTypeId()));
    }
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(1 << 20));
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(1 << 20));
    // get the destination node's address
    Ptr<Node> dstNode = nodes.Get(dstIndex);
    Ipv4Address dstAddr = dstNode->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal();

    // set up port and sink application
    uint16_t port = 50000 + srcIndex;
    Address sinkAddr(InetSocketAddress(dstAddr, port));

    PacketSinkHelper sink("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer sinkApp = sink.Install(dstNode);
    sinkApp.Start(Seconds(0.0));
    sinkApp.Stop(Seconds(stopTime + 1));

    // set up Application
    Ptr<Node> srcNode = nodes.Get(srcIndex);

    Ptr<Socket> ns3TcpSocket = Socket::CreateSocket(srcNode, TcpSocketFactory::GetTypeId());

    Ptr<TutorialApp> app = CreateObject<TutorialApp>();
    app->Setup(ns3TcpSocket, sinkAddr, packetSize, nPackets, DataRate("11Mbps")); // CHANGE THIS TO 110-120% OF MAXIMUM BOTTLENECK BANDWIDTH DURING THE CONNECTION TO PREVENT INTERFERENCE
    srcNode->AddApplication(app);
    app->SetStartTime(Seconds(startTime));
    app->SetStopTime(Seconds(stopTime));

    // set up congestion window tracing
    AsciiTraceHelper asciiTraceHelper;
    Ptr<OutputStreamWrapper> stream = asciiTraceHelper.CreateFileStream("metrics/flow-" + std::to_string(flowIndex) + ".txt");
    ns3TcpSocket->TraceConnectWithoutContext("CongestionWindow",
                                             MakeBoundCallback(&CwndChange, stream));


    // debugging purposes
    std::cout << "Destination IP: " << dstAddr << std::endl;

    Ptr<PacketSink> sink1 = DynamicCast<PacketSink>(sinkApp.Get(0));
    Simulator::Schedule(Seconds(stopTime + 0.5), [=]() {
        std::cout << "Sink received: " << sink1->GetTotalRx() << " bytes\n";
    });
}
// helper function to set up any amount of wireless flows
// here is where you add flows with all the parameters you wish it to have
std::tuple<std::vector<Ipv4Address>,std::vector<Ipv4Address>> SetupMultipleWirelessFlows()
{
    // set up topology and simulate flows
    std::tuple<std::tuple<std::vector<Ipv4Address>,std::vector<Ipv4Address>>,NodeContainer> result = SetupWirelessDumbbellTopology();

    uint32_t flowCnt = 0;
    AddWirelessTcpFlow(flowCnt++, 0, 3, "bbr", 1024, 10000000, 1.0, 21.0,std::get<1>(result));
  //  AddWirelessTcpFlow(flowCnt++, 1, 4, "bbr", 1024, 10000000, 1.0, 10.0,std::get<1>(result));
    return std::get<0>(result);
}

// this is where the simulations are ran and the metrics logged
int main(int argc, char* argv[]) {
    CommandLine cmd(__FILE__);
    cmd.Parse(argc, argv);

    Time::SetResolution(Time::NS);

    //std::tuple<std::vector<Ipv4Address>,std::vector<Ipv4Address>> addresses = SetupMultipleFlows();
    std::tuple<std::vector<Ipv4Address>,std::vector<Ipv4Address>> addresses = SetupMultipleWirelessFlows();
   // std::tuple<std::vector<Ipv4Address>,std::vector<Ipv4Address>> addresses = SetupSingleFlow();
    std::vector<Ipv4Address> senderAddresses = std::get<0>(addresses);
    std::vector<Ipv4Address> receiverAddresses =  std::get<1>(addresses);
    // set up flow monitor
    Ptr<FlowMonitor> flowMonitor;
    FlowMonitorHelper flowHelper;
    flowMonitor = flowHelper.InstallAll();
    Simulator::Schedule(Seconds(1.1), &CheckThroughput, flowMonitor, &flowHelper, senderAddresses,receiverAddresses); // check every 0.5s
    Simulator::Stop(Seconds(21.0));
    Simulator::Run();
    double sumThroughput = 0.0;
    double sumSquaredThroughput = 0.0;
    uint32_t flowCount = 0;
    flowMonitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());
    auto stats = flowMonitor->GetFlowStats();
    // print for debugging
    for (auto& flow : stats) {
        auto t = classifier->FindFlow(flow.first);
        if (std::find(senderAddresses.begin(), senderAddresses.end(), t.sourceAddress) != senderAddresses.end())
        {
            double throughput = (flow.second.rxBytes * 8.0 /
                (flow.second.timeLastRxPacket.GetSeconds() -
                 flow.second.timeFirstTxPacket.GetSeconds()) / 1e6);
            double fct = (flow.second.timeLastRxPacket - flow.second.timeFirstTxPacket).GetSeconds();
            std::cout << "Flow " << std::to_string(flowCount + 1)<< " (" << t.sourceAddress << " -> " << t.destinationAddress << ")\n";
            std::cout << "  Tx Bytes:   " << flow.second.txBytes << "\n";
            std::cout << "  Rx Bytes:   " << flow.second.rxBytes << "\n";
            std::cout << "  Lost Packets: " << flow.second.lostPackets << "\n";
            std::cout << "  Throughput: " << throughput << " Mbps\n";
            std::cout << "Flow Completion Time: " << fct << "\n\n";
            LogMetric("metrics", "fct.txt", "FCT for Flow " + std::to_string(flowCount + 1) + " = " + std::to_string(fct));
            LogMetric("metrics", "packets.txt", "Packet loss for Flow " + std::to_string(flowCount + 1) + " = " + std::to_string(flow.second.lostPackets));
            sumThroughput += throughput;
            sumSquaredThroughput += throughput * throughput;
            flowCount++;
        }
    }
    double jfi = (sumThroughput * sumThroughput) / (flowCount * sumSquaredThroughput);
    std::cout << "Jain's Fairness Index: " << jfi << std::endl;
    LogMetric("metrics", "fct.txt", "\n");
    LogMetric("metrics", "packets.txt", "\n");
    LogMetric("metrics", "jfi.txt", "JFI = " + std::to_string(jfi) + "\n");
    Simulator::Destroy();
    delete dumbbell;  // cleanup
    return 0;
}


// METHODS THAT ARE NOT UTILISED FOR RUNNING EXPERIMENTS FROM THE PAPER

// sets up a wired dumbbell topology
std::tuple<std::vector<Ipv4Address>,std::vector<Ipv4Address>> SetupDumbbellTopology() {
    // number of nodes on the left and right
    uint32_t nLeaf = 2;

    PointToPointHelper accessLink;
    accessLink.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
    accessLink.SetChannelAttribute("Delay", StringValue("2ms"));

    PointToPointHelper bottleneckLink;
    bottleneckLink.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
    bottleneckLink.SetChannelAttribute("Delay", StringValue("20ms"));

    dumbbell = new PointToPointDumbbellHelper(
        nLeaf, accessLink,
        nLeaf, accessLink,
        bottleneckLink
    );

    // install stack on all nodes
    InternetStackHelper stack;
    dumbbell->InstallStack(stack);

    Ipv4AddressHelper leftIp, rightIp, routerIp;
    leftIp.SetBase("10.1.1.0", "255.255.255.0");
    rightIp.SetBase("10.2.1.0", "255.255.255.0");
    routerIp.SetBase("10.3.1.0", "255.255.255.0");

    dumbbell->AssignIpv4Addresses (leftIp, rightIp, routerIp);

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // print routing tables to a file
    AsciiTraceHelper ascii;
    Ptr<OutputStreamWrapper> stream = ascii.CreateFileStream("routing-table.txt");
    Ipv4GlobalRoutingHelper::PrintRoutingTableAllAt(Seconds(0.5), stream);
    std::vector<Ipv4Address> senderAddresses;
    std::vector<Ipv4Address> receiverAddresses;Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    for (uint32_t i = 0; i<dumbbell->LeftCount();i++)
    {
        Ptr<Node> node = dumbbell->GetLeft(i);
        Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
        senderAddresses.push_back(ipv4->GetAddress(1,0).GetLocal());
    }
    for (uint32_t i = 0; i<dumbbell->RightCount();i++)
    {
        Ptr<Node> node = dumbbell->GetRight(i);
        Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
        receiverAddresses.push_back(ipv4->GetAddress(1,0).GetLocal());
    }
    return std::make_tuple(senderAddresses, receiverAddresses);
}

// adds a flow to the wired connection
void AddTcpFlow(uint32_t flowIndex, uint32_t srcIndex, uint32_t dstIndex, std::string tcpType, uint32_t packetSize, uint32_t nPackets, double startTime, double stopTime) {
    // set the TCP variant
    if (tcpType == "cubic") {
        Config::Set("/NodeList/*/$ns3::TcpL4Protocol/SocketType", TypeIdValue(TcpCubic::GetTypeId()));
    } else if (tcpType == "bbr") {
        // BBRv1
        Config::Set("/NodeList/*/$ns3::TcpL4Protocol/SocketType", TypeIdValue(TcpBbr::GetTypeId()));
    }

    // get the destination node's address
    Ptr<Node> dstNode = dumbbell->GetRight(dstIndex);
    Ptr<Ipv4> ipv4 = dstNode->GetObject<Ipv4>();
    Ipv4Address dstAddr = ipv4->GetAddress(1, 0).GetLocal();

    // set up port and sink application
    uint16_t port = 50000 + srcIndex;
    Address sinkAddr(InetSocketAddress(dstAddr, port));

    PacketSinkHelper sink("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer sinkApp = sink.Install(dstNode);
    sinkApp.Start(Seconds(0.0));
    sinkApp.Stop(Seconds(stopTime + 1));

    // set up Application
    Ptr<Node> srcNode = dumbbell->GetLeft(srcIndex);

    Ptr<Socket> ns3TcpSocket = Socket::CreateSocket(srcNode, TcpSocketFactory::GetTypeId());

    Ptr<TutorialApp> app = CreateObject<TutorialApp>();
    app->Setup(ns3TcpSocket, sinkAddr, packetSize, nPackets, DataRate("11Mbps"));
    srcNode->AddApplication(app);
    app->SetStartTime(Seconds(startTime));
    app->SetStopTime(Seconds(stopTime));

    // set up congestion window tracing
    AsciiTraceHelper asciiTraceHelper;
    Ptr<OutputStreamWrapper> stream = asciiTraceHelper.CreateFileStream("flow-" + std::to_string(flowIndex) + ".cwnd");
    ns3TcpSocket->TraceConnectWithoutContext("CongestionWindow",
                                             MakeBoundCallback(&CwndChange, stream));


    // debugging purposes
    std::cout << "Destination IP: " << dstAddr << std::endl;

    Ptr<PacketSink> sink1 = DynamicCast<PacketSink>(sinkApp.Get(0));
    Simulator::Schedule(Seconds(stopTime + 0.5), [=]() {
        std::cout << "Sink received: " << sink1->GetTotalRx() << " bytes\n";
    });
}
// setup method for a single wired flow
std::tuple<std::vector<Ipv4Address>,std::vector<Ipv4Address>> SetupSingleFlow()
{
    // set up topology and simulate flows
    std::tuple<std::vector<Ipv4Address>,std::vector<Ipv4Address>> result = SetupDumbbellTopology();

    uint32_t flowCnt = 0;
    AddTcpFlow(flowCnt++, 0, 1, "cubic", 1024, 1000, 2.0, 10.0);
    return result;
}
// setup method for multiple wired flows
std::tuple<std::vector<Ipv4Address>,std::vector<Ipv4Address>> SetupMultipleFlows()
{
    // set up topology and simulate flows
    std::tuple<std::vector<Ipv4Address>,std::vector<Ipv4Address>> result = SetupDumbbellTopology();

    uint32_t flowCnt = 0;
    AddTcpFlow(flowCnt++, 0, 1, "cubic", 1024, 1000, 2.0, 10.0);
    AddTcpFlow(flowCnt++, 1, 0, "cubic", 1024, 1000, 3.0, 10.0);
    return result;
}