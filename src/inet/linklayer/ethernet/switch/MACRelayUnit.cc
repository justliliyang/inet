//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/.
//

#include "inet/linklayer/ethernet/switch/MACRelayUnit.h"

#include "inet/linklayer/common/InterfaceTag_m.h"
#include "inet/linklayer/ethernet/EtherFrame_m.h"
#include "inet/linklayer/ethernet/EtherMACBase.h"
#include "inet/linklayer/ethernet/Ethernet.h"
#include "inet/common/IProtocolRegistrationListener.h"
#include "inet/common/LayeredProtocolBase.h"
#include "inet/common/ModuleAccess.h"
#include "inet/common/Protocol.h"
#include "inet/common/ProtocolTag_m.h"
#include "inet/common/lifecycle/NodeOperations.h"

namespace inet {

Define_Module(MACRelayUnit);

void MACRelayUnit::initialize(int stage)
{
    if (stage == INITSTAGE_LOCAL) {
        numProcessedFrames = numDiscardedFrames = 0;

        addressTable = getModuleFromPar<IMACAddressTable>(par("macTableModule"), this);
        ift = getModuleFromPar<IInterfaceTable>(par("interfaceTableModule"), this);

        WATCH(numProcessedFrames);
        WATCH(numDiscardedFrames);
    }
    else if (stage == INITSTAGE_LINK_LAYER) {
        NodeStatus *nodeStatus = dynamic_cast<NodeStatus *>(findContainingNode(this)->getSubmodule("status"));
        isOperational = (!nodeStatus) || nodeStatus->getState() == NodeStatus::UP;
        registerProtocol(Protocol::ethernet, gate("ifOut"));
    }
}

void MACRelayUnit::handleMessage(cMessage *msg)
{
    if (!isOperational) {
        EV << "Message '" << msg << "' arrived when module status is down, dropped it\n";
        delete msg;
        return;
    }
    Packet *packet = check_and_cast<Packet *>(msg);
    const auto& frame = packet->peekHeader<EtherFrame>();
    delete packet->removeTag<DispatchProtocolReq>();
    // Frame received from MAC unit
    emit(LayeredProtocolBase::packetReceivedFromLowerSignal, packet);
    handleAndDispatchFrame(packet, frame);
}

void MACRelayUnit::handleAndDispatchFrame(Packet *packet, const std::shared_ptr<EtherFrame>& frame)
{
    int inputInterfaceId = packet->getMandatoryTag<InterfaceInd>()->getInterfaceId();

    numProcessedFrames++;

    // update address table
    addressTable->updateTableWithAddress(inputInterfaceId, frame->getSrc());

    // handle broadcast frames first
    if (frame->getDest().isBroadcast()) {
        EV << "Broadcasting broadcast frame " << frame << endl;
        broadcastFrame(packet, inputInterfaceId);
        return;
    }

    // Finds output port of destination address and sends to output port
    // if not found then broadcasts to all other ports instead
    int outputInterfaceId = addressTable->getPortForAddress(frame->getDest());
    // should not send out the same frame on the same ethernet port
    // (although wireless ports are ok to receive the same message)
    if (inputInterfaceId == outputInterfaceId) {
        EV << "Output port is same as input port, " << packet->getFullName()
           << " dest " << frame->getDest() << ", discarding frame\n";
        numDiscardedFrames++;
        delete packet;
        return;
    }

    if (outputInterfaceId >= 0) {
        EV << "Sending frame " << frame << " with dest address " << frame->getDest() << " to port " << outputInterfaceId << endl;
        packet->ensureTag<InterfaceReq>()->setInterfaceId(outputInterfaceId);
        emit(LayeredProtocolBase::packetSentToLowerSignal, frame);
        send(packet, "ifOut");
    }
    else {
        EV << "Dest address " << frame->getDest() << " unknown, broadcasting frame " << packet << endl;
        broadcastFrame(packet, inputInterfaceId);
    }
}

void MACRelayUnit::broadcastFrame(Packet *packet, int inputInterfaceId)
{
    int numPorts = ift->getNumInterfaces();
    for (int i = 0; i < numPorts; ++i) {
        InterfaceEntry *ie = ift->getInterface(i);
        if (ie->isLoopback() || !ie->isBroadcast())
            continue;
        int ifId = ie->getInterfaceId();
        if (inputInterfaceId != ifId) {
            Packet *dupFrame = packet->dup();
            dupFrame->ensureTag<InterfaceReq>()->setInterfaceId(ifId);
            emit(LayeredProtocolBase::packetSentToLowerSignal, dupFrame);
            send(dupFrame, "ifOut");
        }
    }
    delete packet;
}

void MACRelayUnit::start()
{
    addressTable->clearTable();
    isOperational = true;
}

void MACRelayUnit::stop()
{
    addressTable->clearTable();
    isOperational = false;
}

bool MACRelayUnit::handleOperationStage(LifecycleOperation *operation, int stage, IDoneCallback *doneCallback)
{
    Enter_Method_Silent();

    if (dynamic_cast<NodeStartOperation *>(operation)) {
        if ((NodeStartOperation::Stage)stage == NodeStartOperation::STAGE_LINK_LAYER) {
            start();
        }
    }
    else if (dynamic_cast<NodeShutdownOperation *>(operation)) {
        if ((NodeShutdownOperation::Stage)stage == NodeShutdownOperation::STAGE_LINK_LAYER) {
            stop();
        }
    }
    else if (dynamic_cast<NodeCrashOperation *>(operation)) {
        if ((NodeCrashOperation::Stage)stage == NodeCrashOperation::STAGE_CRASH) {
            stop();
        }
    }
    else {
        throw cRuntimeError("Unsupported operation '%s'", operation->getClassName());
    }

    return true;
}

void MACRelayUnit::finish()
{
    recordScalar("processed frames", numProcessedFrames);
    recordScalar("discarded frames", numDiscardedFrames);
}

} // namespace inet

