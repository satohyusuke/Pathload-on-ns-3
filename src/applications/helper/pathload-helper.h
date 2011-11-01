/* -*- Mode:C++; c-file-style:"k&r"; indent-tabs-mode:nil; -*- */

#ifndef PATHLOAD_HELPER_H
#define PATHLOAD_HELPER_H

#include "ns3/application-container.h"
#include "ns3/node-container.h"
#include "ns3/object-factory.h"

namespace ns3 {

class PathloadSenderHelper {
public:
     PathloadSenderHelper(); // コンストラクタ

     void SetAttribute(std::string name, const AttributeValue &value);

     // インストールメンバ関数
     ApplicationContainer Install(NodeContainer c);
     ApplicationContainer Install(Ptr<Node> node);
     ApplicationContainer Install(std::string nodename);

private:
     ObjectFactory m_factory;
};

class PathloadReceiverHelper {
public:
     PathloadReceiverHelper(); // コンストラクタ

     void SetAttribute(std::string name, const AttributeValue &value);

     // インストールメンバ関数
     ApplicationContainer Install(NodeContainer c);
     ApplicationContainer Install(Ptr<Node> node);
     ApplicationContainer Install(std::string nodename);

private:
     ObjectFactory m_factory;
};

} // namespace ns3

#endif  // PATHLOAD_HELPER_H
