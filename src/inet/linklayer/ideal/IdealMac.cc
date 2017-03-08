//
// Copyright (C) 2013 OpenSim Ltd
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program; if not, see <http://www.gnu.org/licenses/>.
//
// author: Zoltan Bojthe
//

#include <stdio.h>
#include <string.h>
#include "inet/common/INETUtils.h"
#include "inet/common/packet/Packet.h"
#include "inet/common/ProtocolTag_m.h"
#include "inet/common/queue/IPassiveQueue.h"
#include "inet/linklayer/common/EtherTypeTag_m.h"
#include "inet/linklayer/common/Ieee802Ctrl.h"
#include "inet/linklayer/common/InterfaceTag_m.h"
#include "inet/linklayer/common/MACAddressTag_m.h"
#include "inet/linklayer/ideal/IdealMac.h"
#include "inet/linklayer/ideal/IdealMacHeader_m.h"
#include "inet/networklayer/contract/IInterfaceTable.h"

namespace inet {

Define_Module(IdealMac);

simsignal_t IdealMac::dropPkNotForUsSignal = registerSignal("dropPkNotForUs");

IdealMac::IdealMac()
{
}

IdealMac::~IdealMac()
{
    delete lastSentPk;
    cancelAndDelete(ackTimeoutMsg);
}

void IdealMac::flushQueue()
{
    ASSERT(queueModule);
    while (!queueModule->isEmpty()) {
        cMessage *msg = queueModule->pop();
        //TODO emit(dropPkIfaceDownSignal, msg); -- 'pkDropped' signals are missing in this module!
        delete msg;
    }
    queueModule->clear();    // clear request count
}

void IdealMac::clearQueue()
{
    ASSERT(queueModule);
    queueModule->clear();
}

void IdealMac::initialize(int stage)
{
    MACProtocolBase::initialize(stage);
    if (stage == INITSTAGE_LOCAL) {
        outStandingRequests = 0;

        bitrate = par("bitrate").doubleValue();
        headerLength = par("headerLength").longValue();
        promiscuous = par("promiscuous");
        fullDuplex = par("fullDuplex");
        useAck = par("useAck");
        ackTimeout = par("ackTimeout");

        cModule *radioModule = gate("lowerLayerOut")->getPathEndGate()->getOwnerModule();
        radioModule->subscribe(IRadio::transmissionStateChangedSignal, this);
        radio = check_and_cast<IRadio *>(radioModule);
        transmissionState = IRadio::TRANSMISSION_STATE_UNDEFINED;

        // find queueModule
        cGate *queueOut = gate("upperLayerIn")->getPathStartGate();
        queueModule = dynamic_cast<IPassiveQueue *>(queueOut->getOwnerModule());
        if (!queueModule)
            throw cRuntimeError("Missing queueModule");

        initializeMACAddress();
    }
    else if (stage == INITSTAGE_LINK_LAYER) {
        radio->setRadioMode(fullDuplex ? IRadio::RADIO_MODE_TRANSCEIVER : IRadio::RADIO_MODE_RECEIVER);
        if (useAck)
            ackTimeoutMsg = new cMessage("link-break");
        getNextMsgFromHL();
        registerInterface();
    }
}

void IdealMac::initializeMACAddress()
{
    const char *addrstr = par("address");

    if (!strcmp(addrstr, "auto")) {
        // assign automatic address
        address = MACAddress::generateAutoAddress();

        // change module parameter from "auto" to concrete address
        par("address").setStringValue(address.str().c_str());
    }
    else {
        address.setAddress(addrstr);
    }
}

InterfaceEntry *IdealMac::createInterfaceEntry()
{
    InterfaceEntry *e = new InterfaceEntry(this);

    // data rate
    e->setDatarate(bitrate);

    // generate a link-layer address to be used as interface token for IPv6
    e->setMACAddress(address);
    e->setInterfaceToken(address.formInterfaceIdentifier());

    // MTU: typical values are 576 (Internet de facto), 1500 (Ethernet-friendly),
    // 4000 (on some point-to-point links), 4470 (Cisco routers default, FDDI compatible)
    e->setMtu(par("mtu").longValue());

    // capabilities
    e->setMulticast(true);
    e->setBroadcast(true);

    return e;
}

void IdealMac::receiveSignal(cComponent *source, simsignal_t signalID, long value, cObject *details)
{
    Enter_Method_Silent();
    if (signalID == IRadio::transmissionStateChangedSignal) {
        IRadio::TransmissionState newRadioTransmissionState = (IRadio::TransmissionState)value;
        if (transmissionState == IRadio::TRANSMISSION_STATE_TRANSMITTING && newRadioTransmissionState == IRadio::TRANSMISSION_STATE_IDLE) {
            radio->setRadioMode(fullDuplex ? IRadio::RADIO_MODE_TRANSCEIVER : IRadio::RADIO_MODE_RECEIVER);
            if (!lastSentPk)
                getNextMsgFromHL();
        }
        transmissionState = newRadioTransmissionState;
    }
}

void IdealMac::startTransmitting(Packet *msg)
{
    // if there's any control info, remove it; then encapsulate the packet
    if (lastSentPk)
        throw cRuntimeError("Model error: unacked send");
    MACAddress dest = msg->getMandatoryTag<MacAddressReq>()->getDestAddress();
    encapsulate(check_and_cast<Packet *>(msg));

    if (!dest.isBroadcast() && !dest.isMulticast() && !dest.isUnspecified()) {    // unicast
        if (useAck) {
            lastSentPk = msg->dup();
            scheduleAt(simTime() + ackTimeout, ackTimeoutMsg);
        }
    }
    else
        frame->setSrcModuleId(-1);

    // send
    EV << "Starting transmission of " << msg << endl;
    radio->setRadioMode(fullDuplex ? IRadio::RADIO_MODE_TRANSCEIVER : IRadio::RADIO_MODE_TRANSMITTER);
    sendDown(msg);
}

void IdealMac::getNextMsgFromHL()
{
    ASSERT(outStandingRequests >= queueModule->getNumPendingRequests());
    if (outStandingRequests == 0) {
        queueModule->requestPacket();
        outStandingRequests++;
    }
    ASSERT(outStandingRequests <= 1);
}

void IdealMac::handleUpperPacket(cPacket *msg)
{
    outStandingRequests--;
    if (radio->getTransmissionState() == IRadio::TRANSMISSION_STATE_TRANSMITTING) {
        // Logic error: we do not request packet from the external queue when radio is transmitting
        throw cRuntimeError("Received msg for transmission but transmitter is busy");
    }
    else {
        // We are idle, so we can start transmitting right away.
        EV << "Received " << msg << " for transmission\n";
        startTransmitting(check_and_cast<Packet *>(msg));
    }
}

void IdealMac::handleLowerPacket(cPacket *msg)
{
    auto packet = check_and_cast<Packet *>(msg);
    auto frame = packet->peekHeader<IdealMacHeader>();
    if (msg->hasBitError()) {
        EV << "Received " << frame << " contains bit errors or collision, dropping it\n";
        // TODO: add reason? emit(LayeredProtocolBase::packetFromLowerDroppedSignal, frame);
        return;
    }

    if (!dropFrameNotForUs(frame.get())) {
        int senderModuleId = frame->getSrcModuleId();
        IdealMac *senderMac = dynamic_cast<IdealMac *>(getSimulation()->getModule(senderModuleId));
        // TODO: this whole out of bounds ack mechanism is fishy
        if (senderMac && senderMac->useAck)
            senderMac->acked(packet);
        // decapsulate and attach control info
        decapsulate(packet);
        EV << "Passing up contained packet `" << packet->getName() << "' to higher layer\n";
        sendUp(packet);
    }
}

void IdealMac::handleSelfMessage(cMessage *message)
{
    if (message == ackTimeoutMsg) {
        EV_DETAIL << "IdealMac: timeout: " << lastSentPk->getFullName() << " is lost\n";
        // packet lost
        emit(NF_LINK_BREAK, lastSentPk);
        delete lastSentPk;
        lastSentPk = nullptr;
        getNextMsgFromHL();
    }
    else {
        MACProtocolBase::handleSelfMessage(message);
    }
}

void IdealMac::acked(Packet *frame)
{
    Enter_Method_Silent();
    ASSERT(useAck);

    EV_DEBUG << "IdealMac::acked(" << frame->getFullName() << ") is ";

    if (lastSentPk && lastSentPk->getTreeId() == frame->getTreeId()) {
        EV_DEBUG << "accepted\n";
        cancelEvent(ackTimeoutMsg);
        delete lastSentPk;
        lastSentPk = nullptr;
        getNextMsgFromHL();
    }
    else
        EV_DEBUG << "unaccepted\n";
}

void IdealMac::encapsulate(Packet *packet)
{
    auto idealMacHeader = std::make_shared<IdealMacHeader>();
    idealMacHeader->setChunkLength(headerLength);
    auto macAddressReq = packet->getMandatoryTag<MacAddressReq>();
    idealMacHeader->setSrc(macAddressReq->getSrcAddress());
    idealMacHeader->setDest(macAddressReq->getDestAddress());
    MACAddress dest = macAddressReq->getDestAddress();
    if (dest.isBroadcast() || dest.isMulticast() || dest.isUnspecified())
        idealMacHeader->setSrcModuleId(-1);
    else
        idealMacHeader->setSrcModuleId(getId());
    auto ethTypeTag = packet->getTag<EtherTypeReq>();
    idealMacHeader->setNetworkProtocol(ethTypeTag ? ethTypeTag->getEtherType() : -1);
    idealMacHeader->markImmutable();
    packet->pushHeader(idealMacHeader);
}

bool IdealMac::dropFrameNotForUs(IdealMacHeader *frame)
{
    // Current implementation does not support the configuration of multicast
    // MAC address groups. We rather accept all multicast frames (just like they were
    // broadcasts) and pass it up to the higher layer where they will be dropped
    // if not needed.
    // All frames must be passed to the upper layer if the interface is
    // in promiscuous mode.

    if (frame->getDest().equals(address))
        return false;

    if (frame->getDest().isBroadcast())
        return false;

    if (promiscuous || frame->getDest().isMulticast())
        return false;

    EV << "Frame `" << frame->getName() << "' not destined to us, discarding\n";
    emit(dropPkNotForUsSignal, frame);
    delete frame;
    return true;
}

void IdealMac::decapsulate(Packet *packet)
{
    const auto& idealMacHeader = packet->popHeader<IdealMacHeader>();
    auto macAddressInd = packet->ensureTag<MacAddressInd>();
    macAddressInd->setSrcAddress(idealMacHeader->getSrc());
    macAddressInd->setDestAddress(idealMacHeader->getDest());
    packet->ensureTag<InterfaceInd>()->setInterfaceId(interfaceEntry->getInterfaceId());
    packet->ensureTag<EtherTypeInd>()->setEtherType(idealMacHeader->getNetworkProtocol());
    packet->ensureTag<DispatchProtocolReq>()->setProtocol(ProtocolGroup::ethertype.getProtocol(idealMacHeader->getNetworkProtocol()));
}

} // namespace inet

