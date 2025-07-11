//
// Copyright (C) 2016 OpenSim Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later
//


#include "inet/linklayer/ieee80211/mac/coordinationfunction/Dcf.h"

#include "inet/common/ModuleAccess.h"
#include "inet/linklayer/ieee80211/mac/Ieee80211Mac.h"
#include "inet/linklayer/ieee80211/mac/framesequence/DcfFs.h"
#include "inet/linklayer/ieee80211/mac/rateselection/RateSelection.h"
#include "inet/linklayer/ieee80211/mac/recipient/RecipientAckProcedure.h"

namespace inet {
namespace ieee80211 {

using namespace inet::physicallayer;

Define_Module(Dcf);

void Dcf::initialize(int stage)
{
    ModeSetListener::initialize(stage);
    if (stage == INITSTAGE_LINK_LAYER) {
        //SWSH ADDED for statistics about collisions and amount of sent ndf packets
        ndfPacketsSent = registerSignal("ndfPacketsSent");
        avgNdfPacketSent = registerSignal("avgNdfPacketSent");
        ndfPacketCounter = 0;
        //SWSH ADDEND

        startRxTimer = new cMessage("startRxTimeout");
        mac = check_and_cast<Ieee80211Mac *>(getContainingNicModule(this)->getSubmodule("mac"));
        dataAndMgmtRateControl = dynamic_cast<IRateControl *>(getSubmodule(("rateControl")));
        tx = check_and_cast<ITx *>(getModuleByPath(par("txModule")));
        rx = check_and_cast<IRx *>(getModuleByPath(par("rxModule")));
        channelAccess = check_and_cast<Dcaf *>(getSubmodule("channelAccess"));
        originatorDataService = check_and_cast<IOriginatorMacDataService *>(getSubmodule(("originatorMacDataService")));
        recipientDataService = check_and_cast<IRecipientMacDataService *>(getSubmodule("recipientMacDataService"));
        recoveryProcedure = check_and_cast<NonQosRecoveryProcedure *>(getSubmodule("recoveryProcedure"));
        rateSelection = check_and_cast<IRateSelection *>(getSubmodule("rateSelection"));
        rtsProcedure = new RtsProcedure();
        rtsPolicy = check_and_cast<IRtsPolicy *>(getSubmodule("rtsPolicy"));
        recipientAckProcedure = new RecipientAckProcedure();
        recipientAckPolicy = check_and_cast<IRecipientAckPolicy *>(getSubmodule("recipientAckPolicy"));
        originatorAckPolicy = check_and_cast<IOriginatorAckPolicy *>(getSubmodule("originatorAckPolicy"));
        frameSequenceHandler = new FrameSequenceHandler();
        ackHandler = check_and_cast<AckHandler *>(getSubmodule("ackHandler"));
        ctsProcedure = new CtsProcedure();
        ctsPolicy = check_and_cast<ICtsPolicy *>(getSubmodule("ctsPolicy"));
        stationRetryCounters = new StationRetryCounters();
        originatorProtectionMechanism = check_and_cast<OriginatorProtectionMechanism *>(getSubmodule("originatorProtectionMechanism"));



        // SWSH ADDED: Necessary to cope with FH-delay
        endTransmissionTimer = new cMessage("endTransmissionTimer");
        // SWSH ADDED: Necessary so that the consecutive data Transmission works properly
        channelGrantedEvent = new cMessage("channelGrantedEvent");
        // SWSH ADDED: To make the first enqueing of data without having transmitted anything before possible - get the stone rolling
        initialFtNdfEnqueuing = new cMessage("initialFtNdfEnqueuing");
        // SWSH ADDED: Timer for calculating the average ndfPacketCount
        saveAvgNdfCounter = new cMessage("saveAvgNdfCounter");

        // SWSH ADDED: Toggle for choosing channelAccess mechanism
        std::string tmpString = par("fhChannelAccessMechanism");
        fhChannelAccessMechanism = tmpString;
        if (fhChannelAccessMechanism == "cefi" || fhChannelAccessMechanism == "okamoto"){ // cefi case
            // SWSH ADDED: User definable parameter to set the NAV Extension
            navExtension = par("navExtension");
            // SWSH ADDED:
            isFronthaulAffectedAp = par("isFronthaulAffectedAp");
            // SWSH ADDED: Parameter to decide, whether the data is sent SIFS after transmitting ACK
            alpha = par("alpha");
            // SWSH ADDED: IP Address for correctly addressing the FT NDF
            std::string tmpString2 = par("apAddress");
            apAddress = tmpString2;
            apMacAddress = inet::MacAddress(apAddress.c_str());
            // initial scheduling for saving the average NDF Counter
            scheduleAfter(1, saveAvgNdfCounter); // initial saving after one second

        }
        else { // Standard Case: Use regular channel Access
            navExtension = 0;
            isFronthaulAffectedAp = false;
            alpha = 1;
        }

        // SWSH ADDED:  Initialize to false
        ndfWasSent = false;

        // SWSH ADDED: Initial NDF Frame queuing
        if ((fhChannelAccessMechanism == "cefi") && !isFronthaulAffectedAp) { // True for STAs associated with FH-affected AP
            scheduleAfter(0, initialFtNdfEnqueuing);
        }
        // addEnd
    }
}



// SWSH ADDED: Is a NDF enqueued?
bool Dcf::isFtNdfFrameEnqueued() {
    auto pendingQueue = channelAccess->getPendingQueue();
    if (!pendingQueue->isEmpty()) {
        Packet *firstPacketInQueue = pendingQueue->getPacket(0);
        EV_DEBUG << "First packet in queue is " << firstPacketInQueue->getName() << endl;
        const auto& firstHeader = firstPacketInQueue->peekAtFront<Ieee80211MacHeader>();
        if (auto nullDataHeader = dynamicPtrCast<const Ieee80211NullDataHeader>(firstHeader)){
            return true;
        }
    }
    return false;
}
// addEnd


// SWSH ADDED: Function to enqueue FtNd
void Dcf::enqueueFtNdfFrame() {
    Enter_Method("enqueueFtNdfFrame");
    auto ftNdfFrame = makeShared<Ieee80211NullDataHeader>();
    inet::MacAddress receiverAddress = inet::MacAddress(apAddress.c_str());
    EV_DEBUG << "Receiver Address when enqueuing the Ft NDF is: " << receiverAddress << endl;
    ftNdfFrame->setReceiverAddress(receiverAddress);
    auto ftNdfPacket = new Packet("FT_NDF", ftNdfFrame);
    ftNdfPacket->insertAtBack(makeShared<Ieee80211MacTrailer>());
    auto pendingQueue = channelAccess->getPendingQueue();
    pendingQueue->enqueuePacket(ftNdfPacket);
    EV_DEBUG << "Enqueued a FT-NDF" << endl;
}
// addEnd




// SWSH ADDED: To solve the problem of the belated configureRadioMode
void Dcf::txTransmitFrame(Packet *packet, const Ptr<const Ieee80211MacHeader>& header, simtime_t ifs, ITx::ICallback *txCallback){
    auto mode = rateSelection->computeMode(packet, header);
    auto transmissionDuration = mode->getDuration(packet->getDataLength());
    if (!endTransmissionTimer->isScheduled()){
        EV_DEBUG << "The data will take " << transmissionDuration << " s to transmit." << endl;
        scheduleAfter(ifs+transmissionDuration, endTransmissionTimer);
        // Add statistic to count the number of FT-NDF Frames send
        if (auto nullDataHeader = dynamicPtrCast<const Ieee80211NullDataHeader>(header)){
            ndfPacketCounter++;
            emit(ndfPacketsSent, 1);
        }
        tx->transmitFrame(packet, header, ifs, txCallback);
    }
    else EV_DEBUG << "The transmission timer is already running!"<< endl;
}
// addEnd

void Dcf::forEachChild(cVisitor *v)
{
    cSimpleModule::forEachChild(v);
    if (frameSequenceHandler != nullptr && frameSequenceHandler->getContext() != nullptr)
        v->visit(const_cast<FrameSequenceContext *>(frameSequenceHandler->getContext()));
}

void Dcf::handleMessage(cMessage *msg)
{
    if (msg == startRxTimer) {
        if (!isReceptionInProgress()) {
            frameSequenceHandler->handleStartRxTimeout();
            updateDisplayString();
        }
    }
    // SWSH ADDED: To fix transmissions with FH-delay (Fix the radio configuration)
    else if (msg == endTransmissionTimer){
        EV_DEBUG << "The transmission should have ended; Send out configureRadioMode Signal." << endl;
        mac->configureRadioMode(IRadio::RADIO_MODE_RECEIVER);
        tx->radioTransmissionFinished();
    }
    // SWSH ADDED: To enable and time the consecutive data mechanism
    else if (msg == channelGrantedEvent){
        EV_DEBUG << "Channel was granted as part of the consecutive data mechanism." << endl;
        channelAccess->channelAccessGranted();
    }
    // SWSH ADDED: Message to enable initial transmission of data
    else if (msg == initialFtNdfEnqueuing){
        EV_DEBUG << "Initial Forced Traffic NDF was scheduled" << endl;
        enqueueFtNdfFrame();
        if (hasFrameToTransmit())
            channelAccess->requestChannel(this);
    }
    // SWSH ADDED: Event triggered to save the average NDF counter statistic every second
    else if (msg == saveAvgNdfCounter){
        emit(avgNdfPacketSent, ndfPacketCounter);
        ndfPacketCounter = 0;
        scheduleAfter(1, saveAvgNdfCounter);
    }
    // addEnd
    else
        throw cRuntimeError("Unknown msg type");
}

void Dcf::updateDisplayString() const
{
    if (frameSequenceHandler->isSequenceRunning()) {
        auto history = frameSequenceHandler->getFrameSequence()->getHistory();
        getDisplayString().setTagArg("t", 0, ("Fs: " + history).c_str());
    }
    else
        getDisplayString().removeTag("t");
}

void Dcf::channelGranted(IChannelAccess *channelAccess)
{
    Enter_Method("channelGranted");
    ASSERT(this->channelAccess == channelAccess);
    if (!frameSequenceHandler->isSequenceRunning()) {
        frameSequenceHandler->startFrameSequence(new DcfFs(), buildContext(), this);
        emit(IFrameSequenceHandler::frameSequenceStartedSignal, frameSequenceHandler->getContext());
        updateDisplayString();
    }

    // SWSH ADDED: Precaution in case a station has requested channel but got no data
    else if (!hasFrameToTransmit()){
        channelAccess->releaseChannel(this);
    }
    // addEnd
}

void Dcf::processUpperFrame(Packet *packet, const Ptr<const Ieee80211DataOrMgmtHeader>& header)
{
    Enter_Method("processUpperFrame(%s)", packet->getName());
    take(packet);
    EV_INFO << "Processing upper frame: " << packet->getName() << endl;
    auto pendingQueue = channelAccess->getPendingQueue();

    // SWSH ADDED: Remove the enqueued NDF if something is coming from above
    if(isFtNdfFrameEnqueued()){
        EV_DEBUG << "Enqueued Frame should be a NullDataFrame and user data comes in - dequeue NDF Frame" << endl;
        auto removedFrame = pendingQueue->dequeuePacket();
        const auto& removedHeader = removedFrame->peekAtFront<Ieee80211MacHeader>();
        EV_DEBUG << "Removed: "<< removedFrame->getName() << endl;
        delete removedFrame;
        channelAccess->cancelContention();
    }
    // endAdd


    pendingQueue->enqueuePacket(packet);
    if (!pendingQueue->isEmpty()) {
        EV_DETAIL << "Requesting channel" << endl;
        channelAccess->requestChannel(this);
    }
}

void Dcf::transmitControlResponseFrame(Packet *responsePacket, const Ptr<const Ieee80211MacHeader>& responseHeader, Packet *receivedPacket, const Ptr<const Ieee80211MacHeader>& receivedHeader)
{
    Enter_Method("transmitControlResponseFrame");
    responsePacket->insertAtBack(makeShared<Ieee80211MacTrailer>());
    const IIeee80211Mode *responseMode = nullptr;
    if (auto rtsFrame = dynamicPtrCast<const Ieee80211RtsFrame>(receivedHeader))
        responseMode = rateSelection->computeResponseCtsFrameMode(receivedPacket, rtsFrame);
    else if (auto dataOrMgmtHeader = dynamicPtrCast<const Ieee80211DataOrMgmtHeader>(receivedHeader))
        responseMode = rateSelection->computeResponseAckFrameMode(receivedPacket, dataOrMgmtHeader);
    else
        throw cRuntimeError("Unknown received frame type");

    RateSelection::setFrameMode(responsePacket, responseHeader, responseMode);
    emit(IRateSelection::datarateSelectedSignal, responseMode->getDataMode()->getNetBitrate().get(), responsePacket);
    EV_DEBUG << "Datarate for " << responsePacket->getName() << " is set to " << responseMode->getDataMode()->getNetBitrate() << ".\n";
    // SWSH CHANGED: To cope with FH-delay
    txTransmitFrame(responsePacket, responseHeader, modeSet->getSifsTime(), this);
    //tx->transmitFrame(responsePacket, responseHeader, modeSet->getSifsTime(), this); // Original -> Changed to be able to allow for the FH-delay to work properly
    // chaEnd
    delete responsePacket;
}

void Dcf::processMgmtFrame(Packet *packet, const Ptr<const Ieee80211MgmtHeader>& mgmtHeader)
{
    throw cRuntimeError("Unknown management frame");
}

void Dcf::recipientProcessTransmittedControlResponseFrame(Packet *packet, const Ptr<const Ieee80211MacHeader>& header)
{
    emit(packetSentToPeerSignal, packet);
    if (auto ctsFrame = dynamicPtrCast<const Ieee80211CtsFrame>(header))
        ctsProcedure->processTransmittedCts(ctsFrame);
    else if (auto ackFrame = dynamicPtrCast<const Ieee80211AckFrame>(header)){
        recipientAckProcedure->processTransmittedAck(ackFrame);
        EV_DEBUG << "The Ack has been processed." << endl;

        // SWSH ADDED: grant channel access after sending ack
        double probabilisticTrigger = uniform(0.0, 1.0);
        bool triggerTransmission = probabilisticTrigger < alpha;
        if (isFronthaulAffectedAp){
            EV_DEBUG << "The probabilistic trigger was set to " << probabilisticTrigger << " and the transmission decision based on that is " << triggerTransmission << endl;
        }
        bool queueIsEmpty = channelAccess->getPendingQueue()->isEmpty(); // is queue filled?
        bool hasInProgressFrames = channelAccess->getInProgressFrames()->hasInProgressFrames();
        if (isFronthaulAffectedAp && (!queueIsEmpty||hasInProgressFrames) && triggerTransmission){
            EV_DEBUG << "A consecutive data transmission has been triggered." << endl;
            scheduleAfter(modeSet->getSifsTime(), channelGrantedEvent);
        }
        // endAdd
    }
    else
        throw cRuntimeError("Unknown control response frame");
}

void Dcf::scheduleStartRxTimer(simtime_t timeout)
{
    Enter_Method("scheduleStartRxTimer");
    EV_DEBUG << "RxTimeout is set to " << timeout << endl;
    scheduleAfter(timeout, startRxTimer);
}

void Dcf::processLowerFrame(Packet *packet, const Ptr<const Ieee80211MacHeader>& header)
{
    Enter_Method("processLowerFrame(%s)", packet->getName());
    take(packet);
    EV_INFO << "Processing lower frame: " << packet->getName() << endl;

    // SWSH TODO: Simplify by offloading into function
    // SWSH ADDED: Check whether User Data has been transmitted and if it has, restart the NDF contention
    auto currentCwOffset = channelAccess->getCwOffset();
    auto frameReceiverAddress = header->getReceiverAddress();
    auto dataOrMgmtHeader = dynamicPtrCast<const Ieee80211DataOrMgmtHeader>(header);
    auto nullHeader = dynamicPtrCast<const Ieee80211NullDataHeader>(header);
    bool apIsReceiver =(frameReceiverAddress == apMacAddress);

    if(isFtNdfFrameEnqueued() && apIsReceiver && !nullHeader && dataOrMgmtHeader){ // Means Data UL to AP
        EV_DEBUG << "Received frame is contains data and is no NDF, the AP is the receiver and a FT NDF is enqueued!" << endl;
        EV_DEBUG << "Cancel the current extended contention and restart the channel access!" << endl;
        channelAccess->cancelContention();
        channelAccess->requestChannel(this);
        if (currentCwOffset == 0){ // indicating low UL traffic, since the NDF frame went out and worked
            EV_DEBUG << "CWOffset is reset because the current CwOffset is zero UL was received (indicating enough piggybacking ops for the AP) " << endl;
            channelAccess->resetCwOffset();
            EV_DEBUG << "CwOffset reset from " << currentCwOffset << " to " << channelAccess->getCwOffset() << endl;
        }
        else {
            EV_DEBUG << "CwOffset remains same at " << currentCwOffset << " since it was not at zero" << endl;
        }
    }
    else if (isFtNdfFrameEnqueued() && dataOrMgmtHeader && dataOrMgmtHeader->getTransmitterAddress()== apMacAddress){ // Means Data DL
        EV_DEBUG << "DL was received with a NDF enqueued!" << endl;
        EV_DEBUG << "Current CW_Offset is "<< currentCwOffset << endl;
        if (ndfWasSent){ // last sent and received frame was a NDF Frame - this will increase number of NDFs being send...
            EV_DEBUG << "CWOffset is nulled because the current CwOffset is larger than CwMin and DL was received (indicating DL traffic) " << endl;
            channelAccess->nullCwOffset();
            EV_DEBUG << "CwOffset nulled from " << currentCwOffset << " to " << channelAccess->getCwOffset() << endl;
            channelAccess->cancelContention();
            channelAccess->requestChannel(this);
        }
        else if (currentCwOffset > modeSet->getCwMin()){ // Larger than cwMin - indicating low traffic from the AP
            EV_DEBUG << "CWOffset is reset because the current CwOffset is larger than CwMin and DL was received (indicating DL traffic) " << endl;
            channelAccess->resetCwOffset();
            EV_DEBUG << "CwOffset reset from " << currentCwOffset << " to " << channelAccess->getCwOffset() << endl;
            EV_DEBUG << "Current backoffSlots: " << channelAccess->getBackoffSlots() << endl;
            if (channelAccess->getBackoffSlots() > (channelAccess->getCwOffset()+channelAccess->getCw())){
                EV_DEBUG << "Restarting contention because current CW offset is smaller than the current backoffSlots plus the current CW" << endl;
                channelAccess->cancelContention();
                channelAccess->requestChannel(this);
            }
        }
        else {
            EV_DEBUG << "The ndfWasSent flag is not set and CwOffset is in an expected range - do nothing" << endl;
        }
    }
    if (dataOrMgmtHeader){
        ndfWasSent = false; // reset after the reception of any frame that comes in
        EV_DEBUG << "ndfWasSent set to " <<  ndfWasSent << endl;
    }
    // addEnd


    if (frameSequenceHandler->isSequenceRunning()) {
        EV_DEBUG << "A frame sequence is running" << endl;
        // TODO always call processResponses
        EV_DEBUG << "isForUs: " << isForUs(header) << "rxTimer is scheduled: " << startRxTimer->isScheduled() << endl;
        if ((!isForUs(header) && !startRxTimer->isScheduled()) || isForUs(header)) {
            frameSequenceHandler->processResponse(packet);
            updateDisplayString();
        }
        else {
            EV_INFO << "This frame is not for us" << std::endl;
            PacketDropDetails details;
            details.setReason(NOT_ADDRESSED_TO_US);
            emit(packetDroppedSignal, packet, &details);
            delete packet;
        }
        cancelEvent(startRxTimer);
    }
    else if (isForUs(header))
        recipientProcessReceivedFrame(packet, header);
    else {
        EV_INFO << "This frame is not for us" << std::endl;
        PacketDropDetails details;
        details.setReason(NOT_ADDRESSED_TO_US);
        emit(packetDroppedSignal, packet, &details);
        delete packet;
    }
}

void Dcf::transmitFrame(Packet *packet, simtime_t ifs)
{
    Enter_Method("transmitFrame");
    const auto& header = packet->peekAtFront<Ieee80211MacHeader>();
    auto mode = rateSelection->computeMode(packet, header);
    RateSelection::setFrameMode(packet, header, mode);
    emit(IRateSelection::datarateSelectedSignal, mode->getDataMode()->getNetBitrate().get(), packet);
    EV_DEBUG << "Datarate for " << packet->getName() << " is set to " << mode->getDataMode()->getNetBitrate() << ".\n";
    auto pendingPacket = channelAccess->getInProgressFrames()->getPendingFrameFor(packet);
    auto duration = originatorProtectionMechanism->computeDurationField(packet, header, pendingPacket, pendingPacket == nullptr ? nullptr : pendingPacket->peekAtFront<Ieee80211DataOrMgmtHeader>());
    const auto& updatedHeader = packet->removeAtFront<Ieee80211MacHeader>();


    // SWSH ADDED: increase cwOffset when transmitting NDF Frame
     if (auto nullDataHeader = dynamicPtrCast<const Ieee80211NullDataHeader>(header)) {
          EV_DEBUG << "Sending NDF frame, increasing cwOffset!" << endl;
          auto oldCwOffset = channelAccess->getCwOffset();
          channelAccess->incrementCwOffset();
          EV_DEBUG << "Setting ndfWasSent from " << ndfWasSent;
          ndfWasSent = true;
          EV_DEBUG << " to " <<  ndfWasSent << endl;
          EV_DEBUG << "Increasing cwOffset from " << oldCwOffset << " to " << channelAccess->getCwOffset() << endl;
     }
     else{
         ndfWasSent = false;
         EV_DEBUG << "ndfWasSent set to " <<  ndfWasSent << endl;
     }

    // SWSH ADDED: NAV Extension
    if (auto rtsFrame = dynamicPtrCast<const Ieee80211RtsFrame>(header) && (duration != 0)){
        auto oldDuration = duration;
        auto extension = 4*navExtension;
        duration = duration + extension;
        EV_DEBUG << "Increased duration field by " << extension << "s from " << oldDuration << "s to " << duration << "s!" << endl;
    }
    else if (duration != 0){
        auto oldDuration = duration;
        auto extension = 2*navExtension;
        duration = duration + extension; // Normal case e.g. data transmission
        EV_DEBUG << "Increased duration field by " << extension << "s from " << oldDuration << "s to " << duration << "s!" << endl;

    }
    // addEnd

    updatedHeader->setDurationField(duration);
    EV_DEBUG << "Duration for " << packet->getName() << " is set to " << duration << " s.\n";
    packet->insertAtFront(updatedHeader);
    
    // SWSH CHANGED: To be able to work with FH-delay
    txTransmitFrame(packet, packet->peekAtFront<Ieee80211MacHeader>(), ifs, this); //tx->transmitFrame(packet, packet->peekAtFront<Ieee80211MacHeader>(), ifs, this);
    // chaEnd
}

/*
 * TODO  If a PHY-RXSTART.indication primitive does not occur during the ACKTimeout interval,
 * the STA concludes that the transmission of the MPDU has failed, and this STA shall invoke its
 * backoff procedure **upon expiration of the ACKTimeout interval**.
 */

void Dcf::frameSequenceFinished()
{
    Enter_Method("frameSequenceFinished");
    emit(IFrameSequenceHandler::frameSequenceFinishedSignal, frameSequenceHandler->getContext());
    channelAccess->releaseChannel(this);
    if (hasFrameToTransmit())
        channelAccess->requestChannel(this);
    mac->sendDownPendingRadioConfigMsg(); // TODO review
}

bool Dcf::isReceptionInProgress()
{
    return rx->isReceptionInProgress();
}

void Dcf::recipientProcessReceivedFrame(Packet *packet, const Ptr<const Ieee80211MacHeader>& header)
{
    EV_INFO << "Processing received frame " << packet->getName() << " as recipient.\n";
    emit(packetReceivedFromPeerSignal, packet);
    if (auto dataOrMgmtHeader = dynamicPtrCast<const Ieee80211DataOrMgmtHeader>(header))
        recipientAckProcedure->processReceivedFrame(packet, dataOrMgmtHeader, recipientAckPolicy, this);
    // SWSH ADDED:
    if (auto nullDataHeader = dynamicPtrCast<const Ieee80211NullDataHeader>(header)){
        EV_DEBUG << "Received a null data frame" << endl;
        delete packet;
    }
    // addEnd
    else if (auto dataHeader = dynamicPtrCast<const Ieee80211DataHeader>(header))
        sendUp(recipientDataService->dataFrameReceived(packet, dataHeader));
    else if (auto mgmtHeader = dynamicPtrCast<const Ieee80211MgmtHeader>(header))
        sendUp(recipientDataService->managementFrameReceived(packet, mgmtHeader));
    else { // TODO else if (auto ctrlFrame = dynamic_cast<Ieee80211ControlFrame*>(frame))
        sendUp(recipientDataService->controlFrameReceived(packet, header));
        recipientProcessReceivedControlFrame(packet, header);
        delete packet;
    }
}

void Dcf::sendUp(const std::vector<Packet *>& completeFrames)
{
    for (auto frame : completeFrames)
        mac->sendUpFrame(frame);
}

void Dcf::recipientProcessReceivedControlFrame(Packet *packet, const Ptr<const Ieee80211MacHeader>& header)
{
    if (auto rtsFrame = dynamicPtrCast<const Ieee80211RtsFrame>(header))
        ctsProcedure->processReceivedRts(packet, rtsFrame, ctsPolicy, this);
    else
        // SWSH CHANGED: Removed throw to be able to handle the reception of control frames even though the frame sequence has been aborted
        EV_DEBUG << "Received control frame addressed to me, however the corresponding frame sequence was aborted" << endl;
        // throw cRuntimeError("Unknown control frame"); // ORIGINAL
        // changeEnd
}

FrameSequenceContext *Dcf::buildContext()
{
    auto nonQoSContext = new NonQoSContext(originatorAckPolicy);
    return new FrameSequenceContext(mac->getAddress(), modeSet, channelAccess->getInProgressFrames(), rtsProcedure, rtsPolicy, nonQoSContext, nullptr);
}

void Dcf::transmissionComplete(Packet *packet, const Ptr<const Ieee80211MacHeader>& header)
{
    Enter_Method("transmissionComplete");
    if (frameSequenceHandler->isSequenceRunning()) {
        frameSequenceHandler->transmissionComplete();
        updateDisplayString();
    }
    else
        recipientProcessTransmittedControlResponseFrame(packet, header);
}

bool Dcf::hasFrameToTransmit()
{
    return !channelAccess->getPendingQueue()->isEmpty() || channelAccess->getInProgressFrames()->hasInProgressFrames();
}

void Dcf::originatorProcessRtsProtectionFailed(Packet *packet)
{
    Enter_Method("originatorProcessRtsProtectionFailed");
    EV_INFO << "RTS frame transmission failed\n";
    auto protectedHeader = packet->peekAtFront<Ieee80211DataOrMgmtHeader>();
    recoveryProcedure->rtsFrameTransmissionFailed(protectedHeader, stationRetryCounters);
    EV_INFO << "For the current frame exchange, we have CW = " << channelAccess->getCw() << " SRC = " << recoveryProcedure->getShortRetryCount(packet, protectedHeader) << " LRC = " << recoveryProcedure->getLongRetryCount(packet, protectedHeader) << " SSRC = " << stationRetryCounters->getStationShortRetryCount() << " and SLRC = " << stationRetryCounters->getStationLongRetryCount() << std::endl;
    if (recoveryProcedure->isRtsFrameRetryLimitReached(packet, protectedHeader)) {
        recoveryProcedure->retryLimitReached(packet, protectedHeader);
        channelAccess->getInProgressFrames()->dropFrame(packet);
        ackHandler->dropFrame(protectedHeader);
        EV_INFO << "Dropping RTS/CTS protected frame " << packet->getName() << ", because retry limit is reached.\n";
        PacketDropDetails details;
        details.setReason(RETRY_LIMIT_REACHED);
        details.setLimit(recoveryProcedure->getShortRetryLimit());
        emit(packetDroppedSignal, packet, &details);
        emit(linkBrokenSignal, packet);
    }
}

void Dcf::originatorProcessTransmittedFrame(Packet *packet)
{
    Enter_Method("originatorProcessTransmittedFrame");
    EV_INFO << "Processing transmitted frame " << packet->getName() << " as originator in frame sequence.\n";
    emit(packetSentToPeerSignal, packet);
    auto transmittedHeader = packet->peekAtFront<Ieee80211MacHeader>();
    if (auto dataOrMgmtHeader = dynamicPtrCast<const Ieee80211DataOrMgmtHeader>(transmittedHeader)) {
        EV_INFO << "For the current frame exchange, we have CW = " << channelAccess->getCw() << " SRC = " << recoveryProcedure->getShortRetryCount(packet, dataOrMgmtHeader) << " LRC = " << recoveryProcedure->getLongRetryCount(packet, dataOrMgmtHeader) << " SSRC = " << stationRetryCounters->getStationShortRetryCount() << " and SLRC = " << stationRetryCounters->getStationLongRetryCount() << std::endl;
        if (originatorAckPolicy->isAckNeeded(dataOrMgmtHeader)) {
            ackHandler->processTransmittedDataOrMgmtFrame(dataOrMgmtHeader);
        }
        else if (dataOrMgmtHeader->getReceiverAddress().isMulticast()) {
            recoveryProcedure->multicastFrameTransmitted(stationRetryCounters);
            channelAccess->getInProgressFrames()->dropFrame(packet);
        }
    }
    else if (auto rtsFrame = dynamicPtrCast<const Ieee80211RtsFrame>(transmittedHeader)) {
        auto protectedFrame = channelAccess->getInProgressFrames()->getFrameToTransmit(); // KLUDGE
        auto protectedHeader = protectedFrame->peekAtFront<Ieee80211DataOrMgmtHeader>();
        EV_INFO << "For the current frame exchange, we have CW = " << channelAccess->getCw() << " SRC = " << recoveryProcedure->getShortRetryCount(protectedFrame, protectedHeader) << " LRC = " << recoveryProcedure->getLongRetryCount(protectedFrame, protectedHeader) << " SSRC = " << stationRetryCounters->getStationShortRetryCount() << " and SLRC = " << stationRetryCounters->getStationLongRetryCount() << std::endl;
        rtsProcedure->processTransmittedRts(rtsFrame);
    }
}

void Dcf::originatorProcessReceivedFrame(Packet *receivedPacket, Packet *lastTransmittedPacket)
{
    Enter_Method("originatorProcessReceivedFrame");
    EV_INFO << "Processing received frame " << receivedPacket->getName() << " as originator in frame sequence.\n";
    emit(packetReceivedFromPeerSignal, receivedPacket);
    auto receivedHeader = receivedPacket->peekAtFront<Ieee80211MacHeader>();
    auto lastTransmittedHeader = lastTransmittedPacket->peekAtFront<Ieee80211MacHeader>();
    if (receivedHeader->getType() == ST_ACK) {
        auto lastTransmittedDataOrMgmtHeader = dynamicPtrCast<const Ieee80211DataOrMgmtHeader>(lastTransmittedHeader);
        if (dataAndMgmtRateControl) {
            int retryCount = lastTransmittedHeader->getRetry() ? recoveryProcedure->getRetryCount(lastTransmittedPacket, lastTransmittedDataOrMgmtHeader) : 0;
            dataAndMgmtRateControl->frameTransmitted(lastTransmittedPacket, retryCount, true, false);
        }
        recoveryProcedure->ackFrameReceived(lastTransmittedPacket, lastTransmittedDataOrMgmtHeader, stationRetryCounters);
        ackHandler->processReceivedAck(dynamicPtrCast<const Ieee80211AckFrame>(receivedHeader), lastTransmittedDataOrMgmtHeader);
        channelAccess->getInProgressFrames()->dropFrame(lastTransmittedPacket);
        ackHandler->dropFrame(lastTransmittedDataOrMgmtHeader);
    }
    else if (receivedHeader->getType() == ST_RTS)
        ; // void
    else if (receivedHeader->getType() == ST_CTS)
        recoveryProcedure->ctsFrameReceived(stationRetryCounters);
    else
        throw cRuntimeError("Unknown frame type");
}

void Dcf::originatorProcessFailedFrame(Packet *failedPacket)
{
    Enter_Method("originatorProcessFailedFrame");
    EV_INFO << "Data/Mgmt frame transmission failed\n";
    const auto& failedHeader = failedPacket->peekAtFront<Ieee80211DataOrMgmtHeader>();
    ASSERT(failedHeader->getType() != ST_DATA_WITH_QOS);
    ASSERT(ackHandler->getAckStatus(failedHeader) == AckHandler::Status::WAITING_FOR_ACK || ackHandler->getAckStatus(failedHeader) == AckHandler::Status::NO_ACK_REQUIRED);
    recoveryProcedure->dataOrMgmtFrameTransmissionFailed(failedPacket, failedHeader, stationRetryCounters);
    bool retryLimitReached = recoveryProcedure->isRetryLimitReached(failedPacket, failedHeader);
    if (dataAndMgmtRateControl) {
        int retryCount = recoveryProcedure->getRetryCount(failedPacket, failedHeader);
        dataAndMgmtRateControl->frameTransmitted(failedPacket, retryCount, false, retryLimitReached);
    }
    ackHandler->processFailedFrame(failedHeader);
    if (retryLimitReached) {
        recoveryProcedure->retryLimitReached(failedPacket, failedHeader);
        channelAccess->getInProgressFrames()->dropFrame(failedPacket);
        ackHandler->dropFrame(failedHeader);
        EV_INFO << "Dropping frame " << failedPacket->getName() << ", because retry limit is reached.\n";
        PacketDropDetails details;
        details.setReason(RETRY_LIMIT_REACHED);
        details.setLimit(-1); // TODO
        emit(packetDroppedSignal, failedPacket, &details);
        emit(linkBrokenSignal, failedPacket);
    }
    else {
        EV_INFO << "Retrying frame " << failedPacket->getName() << ".\n";
        auto h = failedPacket->removeAtFront<Ieee80211DataOrMgmtHeader>();
        h->setRetry(true);
        failedPacket->insertAtFront(h);
    }
}

bool Dcf::isForUs(const Ptr<const Ieee80211MacHeader>& header) const
{
    return header->getReceiverAddress() == mac->getAddress() || (header->getReceiverAddress().isMulticast() && !isSentByUs(header));
}

bool Dcf::isSentByUs(const Ptr<const Ieee80211MacHeader>& header) const
{
    // FIXME
    // Check the roles of the Addr3 field when aggregation is applied
    // Table 8-19—Address field contents
    if (auto dataOrMgmtHeader = dynamicPtrCast<const Ieee80211DataOrMgmtHeader>(header))
        return dataOrMgmtHeader->getAddress3() == mac->getAddress();
    else
        return false;
}

void Dcf::corruptedFrameReceived()
{
    Enter_Method("corruptedFrameReceived");

    // SWSH ADDED: any action but the reception of DL data after sending a NDF should reset this
    ndfWasSent = false;
    EV_DEBUG << "ndfWasSent set to " <<  ndfWasSent << " due to reception of corrupted frame." << endl;
    // addEnd

    if (frameSequenceHandler->isSequenceRunning() && !startRxTimer->isScheduled()) {
        frameSequenceHandler->handleStartRxTimeout();
        updateDisplayString();
    }
    else
        EV_DEBUG << "Ignoring received corrupt frame.\n";
}

Dcf::~Dcf()
{
    cancelAndDelete(startRxTimer);
    cancelAndDelete(endTransmissionTimer);
    cancelAndDelete(channelGrantedEvent);
    cancelAndDelete(initialFtNdfEnqueuing);
    cancelAndDelete(saveAvgNdfCounter);
    delete rtsProcedure;
    delete recipientAckProcedure;
    delete stationRetryCounters;
    delete ctsProcedure;
    delete frameSequenceHandler;
}

} // namespace ieee80211
} // namespace inet

