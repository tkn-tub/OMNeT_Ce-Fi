//
// Copyright (C) 2016 OpenSim Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later
//


#ifndef __INET_DCF_H
#define __INET_DCF_H

#include "inet/linklayer/ieee80211/mac/channelaccess/Dcaf.h"
#include "inet/linklayer/ieee80211/mac/common/ModeSetListener.h"
#include "inet/linklayer/ieee80211/mac/contract/ICoordinationFunction.h"
#include "inet/linklayer/ieee80211/mac/contract/ICtsPolicy.h"
#include "inet/linklayer/ieee80211/mac/contract/ICtsProcedure.h"
#include "inet/linklayer/ieee80211/mac/contract/IFrameSequenceHandler.h"
#include "inet/linklayer/ieee80211/mac/contract/IOriginatorMacDataService.h"
#include "inet/linklayer/ieee80211/mac/contract/IRateControl.h"
#include "inet/linklayer/ieee80211/mac/contract/IRecipientAckPolicy.h"
#include "inet/linklayer/ieee80211/mac/contract/IRecipientAckProcedure.h"
#include "inet/linklayer/ieee80211/mac/contract/IRecipientMacDataService.h"
#include "inet/linklayer/ieee80211/mac/contract/IRtsProcedure.h"
#include "inet/linklayer/ieee80211/mac/contract/IRx.h"
#include "inet/linklayer/ieee80211/mac/contract/ITx.h"
#include "inet/linklayer/ieee80211/mac/framesequence/FrameSequenceContext.h"
#include "inet/linklayer/ieee80211/mac/lifetime/DcfReceiveLifetimeHandler.h"
#include "inet/linklayer/ieee80211/mac/lifetime/DcfTransmitLifetimeHandler.h"
#include "inet/linklayer/ieee80211/mac/originator/AckHandler.h"
#include "inet/linklayer/ieee80211/mac/originator/NonQosRecoveryProcedure.h"
#include "inet/linklayer/ieee80211/mac/protectionmechanism/OriginatorProtectionMechanism.h"

namespace inet {
namespace ieee80211 {

class Ieee80211Mac;

/**
 * Implements IEEE 802.11 Distributed Coordination Function.
 */
class INET_API Dcf : public ICoordinationFunction, public IFrameSequenceHandler::ICallback, public IChannelAccess::ICallback, public ITx::ICallback, public IProcedureCallback, public ModeSetListener
{
  private:
    // SWSH ADDED: Signal to count the number of send ndfPackets
    simsignal_t ndfPacketsSent;
    simsignal_t avgNdfPacketSent;
    // addEnd
  protected:
    // SWSH ADDED: Timer to determine, when the transmission is done
    cMessage *endTransmissionTimer = nullptr;
    // SWSH ADDED: Timer for channel access, so that it does not crash
    cMessage *channelGrantedEvent = nullptr;
    // SWSH ADDED: Timer for the initial NtNdf Transmission:
    cMessage *initialFtNdfEnqueuing = nullptr;
    // SWSH ADDED: Timer for calculating the average ndfPacketCount
    cMessage *saveAvgNdfCounter = nullptr;
    // addEnd

    Ieee80211Mac *mac = nullptr;
    IRateControl *dataAndMgmtRateControl = nullptr;

    cMessage *startRxTimer = nullptr;

    // Transmission and reception
    IRx *rx = nullptr;
    ITx *tx = nullptr;

    IRateSelection *rateSelection = nullptr;

    // Channel access method
    Dcaf *channelAccess = nullptr;

    // MAC Data Service
    IOriginatorMacDataService *originatorDataService = nullptr;
    IRecipientMacDataService *recipientDataService = nullptr;

    // MAC Procedures
    AckHandler *ackHandler = nullptr;
    IOriginatorAckPolicy *originatorAckPolicy = nullptr;
    IRecipientAckProcedure *recipientAckProcedure = nullptr;
    IRecipientAckPolicy *recipientAckPolicy = nullptr;
    IRtsProcedure *rtsProcedure = nullptr;
    IRtsPolicy *rtsPolicy = nullptr;
    ICtsProcedure *ctsProcedure = nullptr;
    ICtsPolicy *ctsPolicy = nullptr;
    NonQosRecoveryProcedure *recoveryProcedure = nullptr;

    // TODO Unimplemented
    ITransmitLifetimeHandler *transmitLifetimeHandler = nullptr;
    DcfReceiveLifetimeHandler *receiveLifetimeHandler = nullptr;

    // Protection mechanism
    OriginatorProtectionMechanism *originatorProtectionMechanism = nullptr;

    // Frame sequence handler
    IFrameSequenceHandler *frameSequenceHandler = nullptr;

    // Station counters
    StationRetryCounters *stationRetryCounters = nullptr;

  protected:
    virtual int numInitStages() const override { return NUM_INIT_STAGES; }
    virtual void initialize(int stage) override;
    virtual void forEachChild(cVisitor *v) override;
    virtual void handleMessage(cMessage *msg) override;
    virtual void updateDisplayString() const;

    virtual void sendUp(const std::vector<Packet *>& completeFrames);
    virtual bool hasFrameToTransmit();
    virtual bool isReceptionInProgress();
    virtual FrameSequenceContext *buildContext();

    virtual void recipientProcessReceivedFrame(Packet *packet, const Ptr<const Ieee80211MacHeader>& header);
    virtual void recipientProcessReceivedControlFrame(Packet *packet, const Ptr<const Ieee80211MacHeader>& header);
    virtual void recipientProcessTransmittedControlResponseFrame(Packet *packet, const Ptr<const Ieee80211MacHeader>& header);

    // SWSH ADDED: Necessary to change the transmit function to work properly with FH delay since the changeRadioMode was sent as a signal
    void txTransmitFrame(Packet *packet, const Ptr<const Ieee80211MacHeader>& header, simtime_t ifs, ITx::ICallback *txCallback);
    // SWSH ADDED: Function to see, whether a Forced Transmission Null-Data-Frame is enqueued
    bool isFtNdfFrameEnqueued();
    // addEnd


    // SWSH ADDED: Toggle to choose channel access mechanism
    std::string fhChannelAccessMechanism;
    // SWSH ADDED: Variable to indicate to the class how much the duration field has to be extended
    simtime_t navExtension;
    // SWSH ADDED: Variable to see, whether the consecutiveData mechanism (Okamoto) is being used
    bool isFronthaulAffectedAp;
    // SWSH ADDED: flag whether a Ndf was sent
    bool ndfWasSent;
    // SWSH ADDED: For statistic purposes
    int ndfPacketCounter;
    // SWSH ADDED: probabilistic variable to decide, whether consecutiveData transmission is triggered or not
    double alpha;
    // SWSH ADDED: IP Address for correctly addressing the FT NDF
    std::string apAddress;
    // SWSH ADDED: Actual address:
    inet::MacAddress apMacAddress;
    // addEnd

  protected:
    // IChannelAccess::ICallback
    virtual void channelGranted(IChannelAccess *channelAccess) override;

    // IFrameSequenceHandler::ICallback
    virtual void transmitFrame(Packet *packet, simtime_t ifs) override;
    virtual void originatorProcessRtsProtectionFailed(Packet *packet) override;
    virtual void originatorProcessTransmittedFrame(Packet *packet) override;
    virtual void originatorProcessReceivedFrame(Packet *packet, Packet *lastTransmittedPacket) override;
    virtual void originatorProcessFailedFrame(Packet *packet) override;
    virtual void frameSequenceFinished() override;
    virtual void scheduleStartRxTimer(simtime_t timeout) override;

    // ITx::ICallback
    virtual void transmissionComplete(Packet *packet, const Ptr<const Ieee80211MacHeader>& header) override;



    // IProcedureCallback
    virtual void transmitControlResponseFrame(Packet *responsePacket, const Ptr<const Ieee80211MacHeader>& responseHeader, Packet *receivedPacket, const Ptr<const Ieee80211MacHeader>& receivedHeader) override;
    virtual void processMgmtFrame(Packet *mgmtPacket, const Ptr<const Ieee80211MgmtHeader>& mgmtHeader) override;

    virtual bool isSentByUs(const Ptr<const Ieee80211MacHeader>& header) const;
    virtual bool isForUs(const Ptr<const Ieee80211MacHeader>& header) const;

  public:
    virtual ~Dcf();

    // SWSH ADDED: Function required to enqueue FT Null Data Frames
    virtual void enqueueFtNdfFrame() override;
    // addEnd

    // ICoordinationFunction
    virtual void processUpperFrame(Packet *packet, const Ptr<const Ieee80211DataOrMgmtHeader>& header) override;
    virtual void processLowerFrame(Packet *packet, const Ptr<const Ieee80211MacHeader>& header) override;
    virtual void corruptedFrameReceived() override;
};

} /* namespace ieee80211 */
} /* namespace inet */

#endif

