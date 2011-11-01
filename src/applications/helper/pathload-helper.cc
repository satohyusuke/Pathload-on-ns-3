/* -*- Mode:C++; c-file-style:"k&r"; indent-tabs-mode:nil; -*- */

#include "ns3/names.h"
#include "ns3/pathload-sender.h"
#include "ns3/pathload-receiver.h"
#include "pathload-helper.h"

namespace ns3 {

PathloadSenderHelper::PathloadSenderHelper()
{
    m_factory.SetTypeId(PathloadSender::GetTypeId());
}

void PathloadSenderHelper::SetAttribute(std::string name,
                                        const AttributeValue &value)
{
    m_factory.Set(name, value);
}

ApplicationContainer
PathloadSenderHelper::Install(NodeContainer c)
{
    ApplicationContainer apps;
    Ptr<Application> app = m_factory.Create<Application>();

    for (NodeContainer::Iterator i = c.Begin(); i != c.End(); ++i) {
        Ptr<Node> node = *i;
        node->AddApplication(app);
        apps.Add(app);
    }

    return apps;
}

ApplicationContainer
PathloadSenderHelper::Install(Ptr<Node> node)
{
    ApplicationContainer apps;
    Ptr<Application> app = m_factory.Create<Application>();

    node->AddApplication(app);
    apps.Add(app);

    return apps;
}

ApplicationContainer
PathloadSenderHelper::Install(std::string nodename)
{
    ApplicationContainer apps;
    Ptr<Application> app = m_factory.Create<Application>();
    Ptr<Node> node = Names::Find<Node>(nodename);

    node->AddApplication(app);
    apps.Add(app);

    return apps;
}


PathloadReceiverHelper::PathloadReceiverHelper()
{
    m_factory.SetTypeId(PathloadReceiver::GetTypeId());
}

void
PathloadReceiverHelper::SetAttribute(std::string name,
                                     const AttributeValue &value)
{
    m_factory.Set(name, value);
}

ApplicationContainer
PathloadReceiverHelper::Install(NodeContainer c)
{
    ApplicationContainer apps;
    Ptr<Application> app = m_factory.Create<Application>();

    for (NodeContainer::Iterator i = c.Begin(); i != c.End(); ++i) {
        Ptr<Node> node = *i;
        node->AddApplication(app);
        apps.Add(app);
    }

    return apps;
}

ApplicationContainer
PathloadReceiverHelper::Install(Ptr<Node> node)
{
    ApplicationContainer apps;
    Ptr<Application> app = m_factory.Create<Application>();

    node->AddApplication(app);
    apps.Add(app);

    return apps;
}

ApplicationContainer
PathloadReceiverHelper::Install(std::string nodename)
{
    ApplicationContainer apps;
    Ptr<Application> app = m_factory.Create<Application>();
    Ptr<Node> node = Names::Find<Node>(nodename);

    node->AddApplication(app);
    apps.Add(app);

    return apps;
}

} // namespace ns3
