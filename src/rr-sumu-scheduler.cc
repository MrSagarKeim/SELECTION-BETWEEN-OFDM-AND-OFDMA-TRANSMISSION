/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2020 Universita' degli Studi di Napoli Federico II
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Stefano Avallone <stavallo@unina.it>
 */

#include "ns3/log.h"
#include "rr-sumu-scheduler.h"
#include "ns3/wifi-protection.h"
#include "ns3/wifi-acknowledgment.h"
#include "ns3/wifi-psdu.h"
#include "he-frame-exchange-manager.h"
#include "he-configuration.h"
#include "he-phy.h"
#include <algorithm>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <ns3/wifi-net-device.h> 
#include <numeric>




namespace ns3 {

// extern NetDeviceContainer m_apDevices;  

NS_LOG_COMPONENT_DEFINE ("RrsumuScheduler");

NS_OBJECT_ENSURE_REGISTERED (RrsumuScheduler);

 //std::vector<Mac48Address> macaddresses;
 //Ptr<WifiMacQueue> que;

TypeId
RrsumuScheduler::GetTypeId (void)
{

 

  static TypeId tid = TypeId ("ns3::RrsumuScheduler")
    .SetParent<MultiUserScheduler> ()
    .SetGroupName ("Wifi")
    .AddConstructor<RrsumuScheduler> ()
    .AddAttribute ("NStations",
                   "The maximum number of stations that can be granted an RU in a DL MU OFDMA transmission",
                   UintegerValue (6),
                   MakeUintegerAccessor (&RrsumuScheduler::m_nStations),
                   MakeUintegerChecker<uint8_t> (1, 74))
    .AddAttribute ("EnableTxopSharing",
                   "If enabled, allow A-MPDUs of different TIDs in a DL MU PPDU.",
                   BooleanValue (true),
                   MakeBooleanAccessor (&RrsumuScheduler::m_enableTxopSharing),
                   MakeBooleanChecker ())
    .AddAttribute ("ForceDlOfdma",
                   "If enabled, return DL_MU_TX even if no DL MU PPDU could be built.",
                   BooleanValue (false),
                   MakeBooleanAccessor (&RrsumuScheduler::m_forceDlOfdma),
                   MakeBooleanChecker ())
    .AddAttribute ("EnableUlOfdma",
                   "If enabled, return UL_MU_TX if DL_MU_TX was returned the previous time.",
                   BooleanValue (true),
                   MakeBooleanAccessor (&RrsumuScheduler::m_enableUlOfdma),
                   MakeBooleanChecker ())
    .AddAttribute ("EnableBsrp",
                   "If enabled, send a BSRP Trigger Frame before an UL MU transmission.",
                   BooleanValue (true),
                   MakeBooleanAccessor (&RrsumuScheduler::m_enableBsrp),
                   MakeBooleanChecker ())
    .AddAttribute ("UlPsduSize", 
                   "The default size in bytes of the solicited PSDU (to be sent in a TB PPDU)",
                   UintegerValue (500),
                   MakeUintegerAccessor (&RrsumuScheduler::m_ulPsduSize),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("UseCentral26TonesRus",
                   "If enabled, central 26-tone RUs are allocated, too, when the "
                   "selected RU type is at least 52 tones.",
                   BooleanValue (false),
                   MakeBooleanAccessor (&RrsumuScheduler::m_useCentral26TonesRus),
                   MakeBooleanChecker ())
    .AddAttribute ("MaxCredits",
                   "Maximum amount of credits a station can have. When transmitting a DL MU PPDU, "
                   "the amount of credits received by each station equals the TX duration (in "
                   "microseconds) divided by the total number of stations. Stations that are the "
                   "recipient of the DL MU PPDU have to pay a number of credits equal to the TX "
                   "duration (in microseconds) times the allocated bandwidth share",
                   TimeValue (Seconds (1)),
                   MakeTimeAccessor (&RrsumuScheduler::m_maxCredits),
                   MakeTimeChecker ())
    .AddAttribute ("Numstations",
                   "stations in the environment",
                   UintegerValue (4),
                   MakeUintegerAccessor (&RrsumuScheduler::num_stations),
                   MakeUintegerChecker<uint8_t> (1, 74))
    .AddAttribute ("Threshold",
                    "threshold to decide SU tx or MU tx",
                    UintegerValue (4),
                      MakeDoubleAccessor (&RrsumuScheduler::threshold),
                      MakeDoubleChecker<double> (0, 10000))
   .AddAttribute ("APqueue",
                   "Pointer to AP queue",
                    PointerValue(nullptr),
                      MakePointerAccessor (&RrsumuScheduler::que),
                     MakePointerChecker<UniformRandomVariable> ())
                     ;
  return tid;
}


RrsumuScheduler::RrsumuScheduler ()
  : m_ulTriggerType (TriggerFrameType::BASIC_TRIGGER)
{
  NS_LOG_FUNCTION (this);
}

RrsumuScheduler::~RrsumuScheduler ()
{
  NS_LOG_FUNCTION_NOARGS ();
}


 void RrsumuScheduler::setMacaddresses(std::vector<Mac48Address> macaddresses ){
   // this->macaddresses = macaddresses ; 
 }

 void RrsumuScheduler::setAPqueue(Ptr<WifiMacQueue> que){
     this->que = que;
 }
//std::vector<Mac48Address> RrsumuScheduler:: getMacaddresses(){
   //  return macaddresses; 
 //}

Ptr<WifiMacQueue> RrsumuScheduler:: getAPqueue(){
    return que;
}

void
RrsumuScheduler::DoInitialize (void)
{ 
   
  /*Initialize the MU and SU AMPDU variables*/
  
  mu_ampdu = std::vector<int>(18, 7);
  su_ampdu = 0; 

  NS_LOG_FUNCTION (this);
  NS_ASSERT (m_apMac != nullptr);
  m_apMac->TraceConnectWithoutContext ("AssociatedSta",
                                       MakeCallback (&RrsumuScheduler::NotifyStationAssociated, this));
  m_apMac->TraceConnectWithoutContext ("DeAssociatedSta",
                                       MakeCallback (&RrsumuScheduler::NotifyStationDeassociated, this));
  for (const auto& ac : wifiAcList)
    {
      m_staList.insert ({ac.first, {}});
    }
  MultiUserScheduler::DoInitialize ();
}

void
RrsumuScheduler::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  m_staList.clear ();
  m_candidates.clear ();
  m_candidates2.clear (); 

  m_trigger = nullptr;
  m_txParams.Clear ();
  m_txParams2.Clear ();
  m_apMac->TraceDisconnectWithoutContext ("AssociatedSta",
                                          MakeCallback (&RrsumuScheduler::NotifyStationAssociated, this));
  m_apMac->TraceDisconnectWithoutContext ("DeAssociatedSta",
                                          MakeCallback (&RrsumuScheduler::NotifyStationDeassociated, this));
  MultiUserScheduler::DoDispose ();
}

MultiUserScheduler::TxFormat
RrsumuScheduler::SelectTxFormat (void)
{
  NS_LOG_FUNCTION (this); 
  if (m_enableUlOfdma && m_enableBsrp && GetLastTxFormat () == DL_MU_TX)
    {
        
      return TrySendingBsrpTf ();
    }

  if (m_enableUlOfdma && (GetLastTxFormat () == DL_MU_TX
                          || m_ulTriggerType == TriggerFrameType::BSRP_TRIGGER))
    {
      TxFormat txFormat = TrySendingBasicTf ();

      if (txFormat != DL_MU_TX)
        {
          return txFormat;
        }
    }

  return TrySendingDlMuPpdu ();
}

//Not invoked*****
MultiUserScheduler::TxFormat
RrsumuScheduler::TrySendingBsrpTf (void)
{
  NS_LOG_FUNCTION (this);

  CtrlTriggerHeader trigger (TriggerFrameType::BSRP_TRIGGER, GetDlMuInfo ().txParams.m_txVector);

  WifiTxVector txVector = GetDlMuInfo ().txParams.m_txVector;
  txVector.SetGuardInterval (trigger.GetGuardInterval ());

  Ptr<Packet> packet = Create<Packet> ();
  packet->AddHeader (trigger);

  Mac48Address receiver = Mac48Address::GetBroadcast ();
  if (trigger.GetNUserInfoFields () == 1)
    {
      NS_ASSERT (m_apMac->GetStaList ().find (trigger.begin ()->GetAid12 ()) != m_apMac->GetStaList ().end ());
      receiver = m_apMac->GetStaList ().at (trigger.begin ()->GetAid12 ());
    }

  WifiMacHeader hdr (WIFI_MAC_CTL_TRIGGER);
  hdr.SetAddr1 (receiver);
  hdr.SetAddr2 (m_apMac->GetAddress ());
  hdr.SetDsNotTo ();
  hdr.SetDsNotFrom ();

  Ptr<WifiMacQueueItem> item = Create<WifiMacQueueItem> (packet, hdr);

  m_txParams.Clear ();
  // set the TXVECTOR used to send the Trigger Frame
  m_txParams.m_txVector = m_apMac->GetWifiRemoteStationManager ()->GetRtsTxVector (receiver);

  if (!m_heFem->TryAddMpdu (item, m_txParams, m_availableTime))
    {
      // sending the BSRP Trigger Frame is not possible, hence return NO_TX. In
      // this way, no transmission will occur now and the next time we will
      // try again sending a BSRP Trigger Frame.
      NS_LOG_DEBUG ("Remaining TXOP duration is not enough for BSRP TF exchange");
      return NO_TX;
    }

  // Compute the time taken by each station to transmit 8 QoS Null frames
  Time qosNullTxDuration = Seconds (0);
  for (const auto& userInfo : trigger)
    {
      Time duration = WifiPhy::CalculateTxDuration (m_sizeOf8QosNull, txVector,
                                                    m_apMac->GetWifiPhy ()->GetPhyBand (),
                                                    userInfo.GetAid12 ());
      qosNullTxDuration = Max (qosNullTxDuration, duration);
    }

  if (m_availableTime != Time::Min ())
    {
      // TryAddMpdu only considers the time to transmit the Trigger Frame
      NS_ASSERT (m_txParams.m_protection && m_txParams.m_protection->protectionTime != Time::Min ());
      NS_ASSERT (m_txParams.m_acknowledgment && m_txParams.m_acknowledgment->acknowledgmentTime.IsZero ());
      NS_ASSERT (m_txParams.m_txDuration != Time::Min ());

      if (m_txParams.m_protection->protectionTime
          + m_txParams.m_txDuration     // BSRP TF tx time
          + m_apMac->GetWifiPhy ()->GetSifs ()
          + qosNullTxDuration
          > m_availableTime)
        {
          NS_LOG_DEBUG ("Remaining TXOP duration is not enough for BSRP TF exchange");
          return NO_TX;
        }
    }

  NS_LOG_DEBUG ("Duration of QoS Null frames: " << qosNullTxDuration.As (Time::MS));
  trigger.SetUlLength (HePhy::ConvertHeTbPpduDurationToLSigLength (qosNullTxDuration,
                                                                    m_apMac->GetWifiPhy ()->GetPhyBand ()));
  trigger.SetCsRequired (true);
  m_heFem->SetTargetRssi (trigger);

  packet = Create<Packet> ();
  packet->AddHeader (trigger);
  m_trigger = Create<WifiMacQueueItem> (packet, hdr);

  m_ulTriggerType = TriggerFrameType::BSRP_TRIGGER;
  m_tbPpduDuration = qosNullTxDuration;

  return UL_MU_TX;
}


//Not invoked*****
MultiUserScheduler::TxFormat
RrsumuScheduler::TrySendingBasicTf (void)
{
  NS_LOG_FUNCTION (this);

  // check if an UL OFDMA transmission is possible after a DL OFDMA transmission
  NS_ABORT_MSG_IF (m_ulPsduSize == 0, "The UlPsduSize attribute must be set to a non-null value");
  
  //std::cout << "Preparing an UL_MU_OFDMA Tx" << std::endl;

  // determine which of the stations served in DL have UL traffic
  uint32_t maxBufferSize = 0;
  // candidates sorted in decreasing order of queue size
  std::multimap<uint8_t, CandidateInfo, std::greater<uint8_t>> ulCandidates;

  for (const auto& candidate : m_candidates)
    {
      uint8_t queueSize = m_apMac->GetMaxBufferStatus (candidate.first->address);
      if (queueSize == 255)
        {
          NS_LOG_DEBUG ("Buffer status of station " << candidate.first->address << " is unknown");
          //std::cout << "Buffer status of station " << candidate.first->address << " is unknown" << std::endl;

          maxBufferSize = std::max (maxBufferSize, m_ulPsduSize);
        }
      else if (queueSize == 254)
        {
          NS_LOG_DEBUG ("Buffer status of station " << candidate.first->address << " is not limited");
          //std::cout << "Buffer status of station " << candidate.first->address << " is not limited" << std::endl;

          maxBufferSize = 0xffffffff;
        }
      else
        {
          NS_LOG_DEBUG ("Buffer status of station " << candidate.first->address << " is " << +queueSize);
          //std::cout << "Buffer status of station " << candidate.first->address << " is " << +queueSize << std::endl;

          maxBufferSize = std::max (maxBufferSize, static_cast<uint32_t> (queueSize * 256));
        }
      // serve the station if its queue size is not null
      if (queueSize > 0)
        {
          //std::cout << "Adding station " << candidate.first->aid << " to UL OFDMA candidate" << std::endl;
          ulCandidates.emplace (queueSize, candidate);
        }
      // else if ( queueSize == 0 ) { // Send Trigger regardless, during solicitation no data frame would be sent

      //     std::cout << "Adding station " << candidate.first->aid << " to UL OFDMA candidate" << std::endl;

      //     maxBufferSize = m_ulPsduSize;
      //     ulCandidates.emplace (queueSize, candidate);
      // }  
    }

  // if the maximum buffer size is 0, skip UL OFDMA and proceed with trying DL OFDMA
  if (maxBufferSize > 0)
    {
      NS_ASSERT (!ulCandidates.empty ());
      std::size_t count = ulCandidates.size ();
      std::size_t nCentral26TonesRus;
      HeRu::RuType ruType = HeRu::GetEqualSizedRusForStations (m_apMac->GetWifiPhy ()->GetChannelWidth (),
                                                               count, nCentral26TonesRus);
      if (!m_useCentral26TonesRus || ulCandidates.size () == count)
        {
          nCentral26TonesRus = 0;
        }
      else
        {
          nCentral26TonesRus = std::min (ulCandidates.size () - count, nCentral26TonesRus);
        }

      WifiTxVector txVector;
      txVector.SetPreambleType (WIFI_PREAMBLE_HE_TB);
      auto candidateIt = ulCandidates.begin ();

      if (GetLastTxFormat () == DL_MU_TX)
        {
          txVector.SetChannelWidth (GetDlMuInfo ().txParams.m_txVector.GetChannelWidth ());
          txVector.SetGuardInterval (CtrlTriggerHeader ().GetGuardInterval ());

          for (std::size_t i = 0; i < count + nCentral26TonesRus; i++)
            {
              NS_ASSERT (candidateIt != ulCandidates.end ());
              uint16_t staId = candidateIt->second.first->aid;
              // AssignRuIndices will be called below to set RuSpec
              txVector.SetHeMuUserInfo (staId,
                                        {{(i < count ? ruType : HeRu::RU_26_TONE), 1, false},
                                        GetDlMuInfo ().txParams.m_txVector.GetMode (staId),
                                        GetDlMuInfo ().txParams.m_txVector.GetNss (staId)});

              candidateIt++;
            }
        }
      else
        {
          CtrlTriggerHeader trigger;
          GetUlMuInfo ().trigger->GetPacket ()->PeekHeader (trigger);

          txVector.SetChannelWidth (trigger.GetUlBandwidth ());
          txVector.SetGuardInterval (trigger.GetGuardInterval ());

          for (std::size_t i = 0; i < count + nCentral26TonesRus; i++)
            {
              NS_ASSERT (candidateIt != ulCandidates.end ());
              uint16_t staId = candidateIt->second.first->aid;
              auto userInfoIt = trigger.FindUserInfoWithAid (staId);
              NS_ASSERT (userInfoIt != trigger.end ());
              // AssignRuIndices will be called below to set RuSpec
              txVector.SetHeMuUserInfo (staId,
                                        {{(i < count ? ruType : HeRu::RU_26_TONE), 1, false},
                                        HePhy::GetHeMcs (userInfoIt->GetUlMcs ()),
                                        userInfoIt->GetNss ()});

              candidateIt++;
            }
        }

      // remove candidates that will not be served
      ulCandidates.erase (candidateIt, ulCandidates.end ());
      AssignRuIndices (txVector);

      CtrlTriggerHeader trigger (TriggerFrameType::BASIC_TRIGGER, txVector);
      Ptr<Packet> packet = Create<Packet> ();
      packet->AddHeader (trigger);

      Mac48Address receiver = Mac48Address::GetBroadcast ();
      if (ulCandidates.size () == 1)
        {
          receiver = ulCandidates.begin ()->second.first->address;
        }

      WifiMacHeader hdr (WIFI_MAC_CTL_TRIGGER);
      hdr.SetAddr1 (receiver);
      hdr.SetAddr2 (m_apMac->GetAddress ());
      hdr.SetDsNotTo ();
      hdr.SetDsNotFrom ();

      Ptr<WifiMacQueueItem> item = Create<WifiMacQueueItem> (packet, hdr);

      // compute the maximum amount of time that can be granted to stations.
      // This value is limited by the max PPDU duration
      Time maxDuration = GetPpduMaxTime (txVector.GetPreambleType ());

      m_txParams.Clear ();
      // set the TXVECTOR used to send the Trigger Frame
      m_txParams.m_txVector = m_apMac->GetWifiRemoteStationManager ()->GetRtsTxVector (receiver);

      if (!m_heFem->TryAddMpdu (item, m_txParams, m_availableTime))
        {
          // an UL OFDMA transmission is not possible, hence return NO_TX. In
          // this way, no transmission will occur now and the next time we will
          // try again performing an UL OFDMA transmission.
          //std::cout << "Remaining TXOP duration is not enough for UL MU exchange" << std::endl;
          NS_LOG_DEBUG ("Remaining TXOP duration is not enough for UL MU exchange");
          return NO_TX;
        }

      if (m_availableTime != Time::Min ())
        {
          // TryAddMpdu only considers the time to transmit the Trigger Frame
          NS_ASSERT (m_txParams.m_protection && m_txParams.m_protection->protectionTime != Time::Min ());
          NS_ASSERT (m_txParams.m_acknowledgment && m_txParams.m_acknowledgment->acknowledgmentTime != Time::Min ());
          NS_ASSERT (m_txParams.m_txDuration != Time::Min ());

          maxDuration = Min (maxDuration, m_availableTime
                                          - m_txParams.m_protection->protectionTime
                                          - m_txParams.m_txDuration
                                          - m_apMac->GetWifiPhy ()->GetSifs ()
                                          - m_txParams.m_acknowledgment->acknowledgmentTime);
          if (maxDuration.IsNegative ())
            {
              //std::cout << "Remaining TXOP duration is not enough for UL MU exchange" << std::endl;
              NS_LOG_DEBUG ("Remaining TXOP duration is not enough for UL MU exchange");
              return NO_TX;
            }
        }

      // Compute the time taken by each station to transmit a frame of maxBufferSize size
      Time bufferTxTime = Seconds (0);
      for (const auto& userInfo : trigger)
        {
          Time duration = WifiPhy::CalculateTxDuration (maxBufferSize, txVector,
                                                        m_apMac->GetWifiPhy ()->GetPhyBand (),
                                                        userInfo.GetAid12 ());
          bufferTxTime = Max (bufferTxTime, duration);
        }

      if (bufferTxTime < maxDuration)
        {
          // the maximum buffer size can be transmitted within the allowed time
          maxDuration = bufferTxTime;
        }
      else
        {
          // maxDuration may be a too short time. If it does not allow any station to
          // transmit at least m_ulPsduSize bytes, give up the UL MU transmission for now
          Time minDuration = Seconds (0);
          for (const auto& userInfo : trigger)
            {
              Time duration = WifiPhy::CalculateTxDuration (m_ulPsduSize, txVector,
                                                            m_apMac->GetWifiPhy ()->GetPhyBand (),
                                                            userInfo.GetAid12 ());
              minDuration = (minDuration.IsZero () ? duration : Min (minDuration, duration));
            }

          if (maxDuration < minDuration)
            {
              // maxDuration is a too short time, hence return NO_TX. In this way,
              // no transmission will occur now and the next time we will try again
              // performing an UL OFDMA transmission.
              //std::cout << "Available time " << maxDuration.As (Time::MS) << " is too short" << std::endl;

              NS_LOG_DEBUG ("Available time " << maxDuration.As (Time::MS) << " is too short");
              return NO_TX;
            }
        }

      // maxDuration is the time to grant to the stations. Finalize the Trigger Frame
      NS_LOG_DEBUG ("TB PPDU duration: " << maxDuration.As (Time::MS));
      trigger.SetUlLength (HePhy::ConvertHeTbPpduDurationToLSigLength (maxDuration,
                                                                       m_apMac->GetWifiPhy ()->GetPhyBand ()));
      trigger.SetCsRequired (true);
      m_heFem->SetTargetRssi (trigger);
      // set Preferred AC to the AC that gained channel access
      for (auto& userInfo : trigger)
        {
          userInfo.SetBasicTriggerDepUserInfo (0, 0, m_edca->GetAccessCategory ());
        }

      packet = Create<Packet> ();
      packet->AddHeader (trigger);
      m_trigger = Create<WifiMacQueueItem> (packet, hdr);

      m_ulTriggerType = TriggerFrameType::BASIC_TRIGGER;
      m_tbPpduDuration = maxDuration;

      return UL_MU_TX;
    }
  return DL_MU_TX;
}

void
RrsumuScheduler::NotifyStationAssociated (uint16_t aid, Mac48Address address)
{
  NS_LOG_FUNCTION (this << aid << address);

  if (GetWifiRemoteStationManager ()->GetHeSupported (address))
    {
      for (auto& staList : m_staList)
        {
          staList.second.push_back (MasterInfo {aid, address, 0.0});
        }
    }
}

void
RrsumuScheduler::NotifyStationDeassociated (uint16_t aid, Mac48Address address)
{
  NS_LOG_FUNCTION (this << aid << address);

  if (GetWifiRemoteStationManager ()->GetHeSupported (address))
    {
      for (auto& staList : m_staList)
        {
          staList.second.remove_if ([&aid, &address] (const MasterInfo& info)
                                    { return info.aid == aid && info.address == address; });
        }
    }
}

int 
RrsumuScheduler::pickSuAmpduByProbability(const std::map<int, int>& valueCounts) {
    // Extract keys and weights
    std::vector<int> keys;
    std::vector<int> weights;

    for (const auto& pair : valueCounts) {
        keys.push_back(pair.first);
        weights.push_back(pair.second);
    }

    // Create a random number generator
    std::random_device rd;
    std::mt19937 gen(rd());
    std::discrete_distribution<> dist(weights.begin(), weights.end());

    // Select an index based on the weights
    int selectedIndex = dist(gen);
    return keys[selectedIndex];
}


MultiUserScheduler::TxFormat
RrsumuScheduler::TrySendingDlMuPpdu (void)
{
  NS_LOG_FUNCTION (this);

  AcIndex primaryAc = m_edca->GetAccessCategory ();

  if (m_staList[primaryAc].empty ())
    {
      NS_LOG_DEBUG ("No HE stations associated: return SU_TX");
      return TxFormat::SU_TX;
    }

  std::size_t count = std::min (static_cast<std::size_t> (m_nStations), m_staList[primaryAc].size ());
  std::size_t nCentral26TonesRus;
  HeRu::RuType ruType = HeRu::GetEqualSizedRusForStations (m_apMac->GetWifiPhy ()->GetChannelWidth (), count,
                                                           nCentral26TonesRus);

  // std::size_t count2 = 1
  // std::size_t nCentral26TonesRus_su;
  // HeRu::RuType ruType2 = HeRu::GetEqualSizedRusForStations (m_apMac->GetWifiPhy ()->GetChannelWidth (), count2,
  //                                                          nCentral26TonesRus_su);                                                         
  NS_ASSERT (count >= 1);
 

  if (!m_useCentral26TonesRus)
    {
      nCentral26TonesRus = 0;
    }

  uint8_t currTid = wifiAcList.at (primaryAc).GetHighTid ();

  Ptr<const WifiMacQueueItem> mpdu = m_edca->PeekNextMpdu ();

  if (mpdu != nullptr && mpdu->GetHeader ().IsQosData ())
    {
      currTid = mpdu->GetHeader ().GetQosTid ();
    }

  // determine the list of TIDs to check
  std::vector<uint8_t> tids;

  if (m_enableTxopSharing)
    {
      for (auto acIt = wifiAcList.find (primaryAc); acIt != wifiAcList.end (); acIt++)
        {
          uint8_t firstTid = (acIt->first == primaryAc ? currTid : acIt->second.GetHighTid ());
          tids.push_back (firstTid);
          tids.push_back (acIt->second.GetOtherTid (firstTid));
        }
    }
  else
    {
      tids.push_back (currTid);
    }

  Ptr<HeConfiguration> heConfiguration = m_apMac->GetHeConfiguration ();
  NS_ASSERT (heConfiguration != 0);

  m_txParams.Clear ();
  m_txParams.m_txVector.SetPreambleType (WIFI_PREAMBLE_HE_MU);
  m_txParams.m_txVector.SetChannelWidth (m_apMac->GetWifiPhy ()->GetChannelWidth ());
  m_txParams.m_txVector.SetGuardInterval (heConfiguration->GetGuardInterval ().GetNanoSeconds ());
  m_txParams.m_txVector.SetBssColor (heConfiguration->GetBssColor ());

 // '''
 // SU Configuration 
 // ''' 
  m_txParams2.Clear ();
  m_txParams2.m_txVector.SetPreambleType (WIFI_PREAMBLE_HE_MU);
  m_txParams2.m_txVector.SetChannelWidth (m_apMac->GetWifiPhy ()->GetChannelWidth ());
  m_txParams2.m_txVector.SetGuardInterval (heConfiguration->GetGuardInterval ().GetNanoSeconds ());
  m_txParams2.m_txVector.SetBssColor (heConfiguration->GetBssColor ());

 
 
  // The TXOP limit can be exceeded by the TXOP holder if it does not transmit more
  // than one Data or Management frame in the TXOP and the frame is not in an A-MPDU
  // consisting of more than one MPDU (Sec. 10.22.2.8 of 802.11-2016).
  // For the moment, we are considering just one MPDU per receiver.
  Time actualAvailableTime = (m_initialFrame ? Time::Min () : m_availableTime);
  
  // Time actualAvailableTime2 = (m_initialFrame2 ? Time::Min () : m_availableTime2);

  // iterate over the associated stations until an enough number of stations is identified
  auto staIt = m_staList[primaryAc].begin ();
  m_candidates.clear ();

 // '''
 // SU Configuration 
 // ''' 
  m_candidates2.clear(); 
  
  // std::cout<<"Hello1"<<std::endl;   

  while (staIt != m_staList[primaryAc].end ()
         && m_candidates.size () < std::max (static_cast<std::size_t> (m_nStations), count + nCentral26TonesRus))
        //&& m_candidates.size () < std::min (static_cast<std::size_t> (m_nStations), count + nCentral26TonesRus))
    {
      NS_LOG_DEBUG ("Next candidate STA (MAC=" << staIt->address << ", AID=" << staIt->aid << ")");
      //std::cout << "Next candidate STA (MAC=" << staIt->address << ", AID=" << staIt->aid << ")" << std::endl;

      HeRu::RuType currRuType = (m_candidates.size () < count ? ruType : HeRu::RU_26_TONE);
      
      // check if the AP has at least one frame to be sent to the current station
      for (uint8_t tid : tids)
        {
          AcIndex ac = QosUtilsMapTidToAc (tid);
          NS_ASSERT (ac >= primaryAc);
          // check that a BA agreement is established with the receiver for the
          // considered TID, since ack sequences for DL MU PPDUs require block ack
          if (m_apMac->GetQosTxop (ac)->GetBaAgreementEstablished (staIt->address, tid))
            {
              mpdu = m_apMac->GetQosTxop (ac)->PeekNextMpdu (tid, staIt->address);

              // we only check if the first frame of the current TID meets the size
              // and duration constraints. We do not explore the queues further.
              
              if (mpdu != 0)
                {
                 
                  // std::cout<<"Cannot create mpdu" <<std::endl; 
                   // Use a temporary TX vector including only the STA-ID of the
                  // candidate station to check if the MPDU meets the size and time limits.
                  // An RU of the computed size is tentatively assigned to the candidate
                  // station, so that the TX duration can be correctly computed.
                  WifiTxVector suTxVector = GetWifiRemoteStationManager ()->GetDataTxVector (mpdu->GetHeader ()),
                               txVectorCopy = m_txParams.m_txVector;

                  m_txParams.m_txVector.SetHeMuUserInfo (staIt->aid,
                                                         {{currRuType, 1, false},
                                                          suTxVector.GetMode (),
                                                          suTxVector.GetNss ()});
                  
                  WifiTxVector suTxVector2 = GetWifiRemoteStationManager ()->GetDataTxVector (mpdu->GetHeader ()),
                               txVectorCopy2 = m_txParams2.m_txVector;
                  
                  m_txParams2.m_txVector.SetHeMuUserInfo (staIt->aid,
                                                         {{currRuType, 1, false},
                                                          suTxVector2.GetMode (),
                                                          suTxVector2.GetNss ()});

                                                        
             
                  if (!m_heFem->TryAddMpdu (mpdu, m_txParams, actualAvailableTime))
                    {
                      NS_LOG_DEBUG ("Adding the peeked frame violates the time constraints");
                      // std::cout << "Adding the peeked frame violates the time constraints" << std::endl;
                      m_txParams.m_txVector = txVectorCopy;
                      m_txParams2.m_txVector = txVectorCopy2;
                      
                    }
                  else
                    {
                      // the frame meets the constraints
                      NS_LOG_DEBUG ("Adding candidate STA (MAC=" << staIt->address << ", AID="
                                    << staIt->aid << ") TID=" << +tid);
                      m_candidates.push_back ({staIt, mpdu});
                      m_candidates2.push_back ({staIt, mpdu});
                      //std::cout << "Adding station " << staIt->aid << " to DL OFDMA candidates" << std::endl;
                      break;    // terminate the for loop
                    }
                }
              else
                {
                  std::cout << "No frames to send to " << staIt->address << " with TID=" << +tid << std::endl;
                  NS_LOG_DEBUG ("No frames to send to " << staIt->address << " with TID=" << +tid);
                }
            }
            else {
              //std::cout << "Block Agreement not established with station. for TID = " << +tid << std::endl;   
            }
        }

      // move to the next station in the list
      staIt++;
    }

  if (m_candidates.empty ())
    {
      if (m_forceDlOfdma)
        {
          std::cout << "No candidates left -- forced DL OFDMA" << std::endl;    
 
          NS_LOG_DEBUG ("The AP does not have suitable frames to transmit: return NO_TX");
          return NO_TX;
        }

      // std::cout << "No candidates left" << m_candidates.size()<< std::endl;   
      NS_LOG_DEBUG ("The AP does not have suitable frames to transmit: return SU_TX");
      return SU_TX;
    }

 
  // std::cout<<"Hello"<<std::endl;   



  
  // for (const auto& value : mu_ampdu) {
  //   std::cout << value << std::endl;
  // }
     
  std::cout << "MU Data Transmission Duration MU" << m_txParams.m_txDuration << std::endl;
  std::cout << "MU Ack Transmission Duration MU" << m_txParams.m_acknowledgment->acknowledgmentTime << std::endl;  
  
  //Initialise MU TX data and Ack time values 
  mu_txdata= m_txParams.m_txDuration; 
  mu_Back= m_txParams.m_acknowledgment->acknowledgmentTime; 
  
  // MU Preamble duration 
  WifiDlMuAggregateTf* dlMuAggrTfAcknowledgment = static_cast<WifiDlMuAggregateTf*> (m_txParams.m_acknowledgment.get ());
  WifiTxVector* responseTxVector = nullptr;
  responseTxVector = &dlMuAggrTfAcknowledgment->stationsReplyingWithBlockAck.begin ()->second.blockAckTxVector;
  //std::cout << "Preamble  Duration ACKKK----------- MU" << m_apMac->GetWifiPhy()->CalculatePhyPreambleAndHeaderDuration (*responseTxVector)<<std::endl;

  mu_Pul= m_apMac->GetWifiPhy()->CalculatePhyPreambleAndHeaderDuration (*responseTxVector); 
  
  
  su_ampdu = calculate_su_mpdu(); //calculate the SU AMPDU size 
  // calculate_mu_mpdu(); //calculate the MU AMPDU size 
  // ComputeDlMuInfo(); 
  
  std::cout<<"SU: AMPDU size: "<<su_ampdu<<std::endl;   
  std::cout<<"Length of m_candidates "<<m_candidates.size()<<" Length of m_candidates2 "<<m_candidates2.size(); 
  
  std::cout<<"Total Packets"<<que->GetNPackets()<<std::endl;
  std::cout<<"Maximum Size"<<que->GetMaxSize().GetValue()<<std::endl;
  
  //Retrieve single MPDU Size 
if (mpdu != nullptr) {
    try {
        mpdu_size = mpdu->GetSize();
    } 
    catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    } 
    catch (...) {
        std::cerr << "Unknown exception occurred while getting MPDU size." << std::endl;
    }
} else {
    // Pointer is null; no action needed
}

  //MU Tx and Ack values 
  double mu_Pdl_val =   mu_Pdl.GetMicroSeconds() ; 
  double mu_txdata_val = mu_txdata.GetMicroSeconds() ; 
  double mu_Pul_val = mu_Pul.GetMicroSeconds(); 
  double mu_Back_val = mu_Back.GetMicroSeconds(); 


  //SU Tx and Ack values 
  double su_Pdl_val =   su_Pdl.GetMicroSeconds() ; 
  double su_txdata_val = su_txdata.GetMicroSeconds() ; 
  double su_Pul_val = su_Pul.GetMicroSeconds(); 
  double su_Back_val = su_Back.GetMicroSeconds(); 

  

  su_tpt = 8*mpdu_size*su_ampdu / (aifs + bo + su_Pdl_val + su_txdata_val +sifs+su_Pul_val +su_Back_val); 
  
  mu_tpt = 8*mpdu_size*std::accumulate(mu_ampdu.begin(), mu_ampdu.end(), 0) / (aifs + bo + mu_Pdl_val + mu_txdata_val + pe + sifs + mu_Pul_val + mu_Back_val + pe); 

  // double percentfilled = (1.0*que->GetNPackets())/que->GetMaxSize().GetValue();
  // std::cout<<"Percent Filled"<<percentfilled<<std::endl;
 
  std::cout<< "Estimated    MU Tpt:   "<<mu_tpt<<"Mbps  SU Tpt:   "<<su_tpt<<"Mbps"<<std::endl; 
  
  if(su_tpt > mu_tpt){
    std::cout<<"Single User Transmission"<<std::endl;
    return TxFormat::SU_TX;
  }
  std::cout<<"Multi User Transmission"<<std::endl;
  return TxFormat::DL_MU_TX;


  // if(percentfilled > 0.6){
  //   std::cout<<"Single User Transmission"<<std::endl;
  //   return TxFormat::SU_TX;
  // }
  // std::cout<<"Multi User Transmission"<<std::endl;
  // return TxFormat::DL_MU_TX;
}



int 
RrsumuScheduler::calculate_su_mpdu(void){


  if (m_candidates2.empty ())
    {
      return 0; 
    }

  uint16_t bw = m_apMac->GetWifiPhy ()->GetChannelWidth ();

  // compute how many stations can be granted an RU and the RU size
  std::size_t nRusAssigned = 1;
  
  std::size_t nCentral26TonesRus;
 

  HeRu::RuType ruType2 = HeRu::GetEqualSizedRusForStations (bw, nRusAssigned, nCentral26TonesRus);

  NS_LOG_DEBUG (nRusAssigned << " stations are being assigned a " << ruType2 << " RU");
  std::cout << nRusAssigned << " stations are being assigned a " << ruType2 << " RU" << std::endl;
 
  if (!m_useCentral26TonesRus || m_candidates2.size () == nRusAssigned)
    {
      nCentral26TonesRus = 0;
    }
  else
    {
      nCentral26TonesRus = std::min (m_candidates2.size () - nRusAssigned, nCentral26TonesRus);
      NS_LOG_DEBUG (nCentral26TonesRus << " stations are being assigned a 26-tones RU");
      //std::cout << nCentral26TonesRus << " stations are being assigned a 26-tones RU" << std::endl;
    }
  // std::cout<<"before dlMuInfo2 initialization" <<std::endl; 
  
  nCentral26TonesRus = 0;

  DlMuInfo dlMuInfo2;

  // We have to update the TXVECTOR
  dlMuInfo2.txParams.m_txVector.SetPreambleType (m_txParams2.m_txVector.GetPreambleType ());
  dlMuInfo2.txParams.m_txVector.SetChannelWidth (m_txParams2.m_txVector.GetChannelWidth ());
  dlMuInfo2.txParams.m_txVector.SetGuardInterval (m_txParams2.m_txVector.GetGuardInterval ());
  dlMuInfo2.txParams.m_txVector.SetBssColor (m_txParams2.m_txVector.GetBssColor ());

  CandidateInfo candidate_su = m_candidates2.front();  // iterator over the list of candidate receivers
  // std::cout<<"after dlMuInfo2 initialization" <<std::endl; 

  uint16_t staId = candidate_su.first->aid;
    // AssignRuIndices will be called below to set RuSpec
    dlMuInfo2.txParams.m_txVector.SetHeMuUserInfo (staId,
                                                  {{(HeRu::RU_484_TONE), 1, false},
                                                    m_txParams2.m_txVector.GetMode (staId),
                                                    m_txParams2.m_txVector.GetNss (staId)});
    

  // remove candidates that will not be served
  // m_candidates2.erase (candidateIt, m_candidates2.end ());

  AssignRuIndices (dlMuInfo2.txParams.m_txVector);
  m_txParams2.Clear ();

  Ptr<const WifiMacQueueItem> mpdu2;

  // Compute the TX params (again) by using the stored MPDUs and the final TXVECTOR
  Time actualAvailableTime2 = (m_initialFrame ? Time::Min () : m_availableTime2);

  
  
  mpdu2 = candidate_su.second;
  NS_ASSERT (mpdu2 != nullptr);

  // std::cout<<"The available time for SU Transmission is "<<actualAvailableTime2<<std::endl; 
  bool ret = m_heFem->TryAddMpdu (mpdu2, dlMuInfo2.txParams, actualAvailableTime2);
  NS_UNUSED (ret);
  NS_ASSERT_MSG (ret, "Weird that an MPDU does not meet constraints when "
                          "transmitted over a larger RU");
    

  // We have to complete the PSDUs to send
  Ptr<WifiMacQueue> queue;
  Mac48Address receiver2;

  // std::ostringstream oss;
  // oss << "rr_aggregation_log_same1000.txt";
  
  // std::ofstream file;
  // file.open(oss.str(), std::ios_base::app | std::ios_base::out);

  // oss.str("");
  // oss.clear();

  // file << "======= SCHEDULER BEGINS AGGREGATION ========\n";

     

        // Let us try first A-MSDU aggregation if possible
      // mpdu2 = candidate_su.second;
      // std::cout<<"SU : This is the MPDU: " << mpdu2 <<std::endl; 

      // NS_ASSERT (mpdu2 != nullptr);
      // uint8_t tid = mpdu2->GetHeader ().GetQosTid ();
      // receiver2 = mpdu2->GetHeader ().GetAddr1 ();
      // NS_ASSERT (receiver2 == candidate_su.first->address);

      // NS_ASSERT (mpdu2->IsQueued ());
      // WifiMacQueueItem::QueueIteratorPair queueIt2 = mpdu2->GetQueueIteratorPairs ().front ();
      // NS_ASSERT (queueIt2.queue != nullptr);
      // Ptr<WifiMacQueueItem> item2 = *queueIt2.it;
      // queueIt2.it++;

      // if (!mpdu2->GetHeader ().IsRetry ())
      //   {
      //     // this MPDU must have been dequeued from the AC queue and we can try
      //     // A-MSDU aggregation
      //     item2 = m_heFem->GetMsduAggregator ()->GetNextAmsdu (mpdu2, dlMuInfo2.txParams, actualAvailableTime2, queueIt2);

      //     if (item2 == nullptr)
      //       {
      //         // A-MSDU aggregation failed or disabled
      //         item2 = *mpdu2->GetQueueIteratorPairs ().front ().it;
      //       }
      //     m_apMac->GetQosTxop (QosUtilsMapTidToAc (tid))->AssignSequenceNumber (item2);
      //   }

      // // Now, let's try A-MPDU aggregation if possible
      // std::vector<Ptr<WifiMacQueueItem>> mpduList2 = m_heFem->GetMpduAggregator ()->GetNextAmpdu (item2, dlMuInfo2.txParams, actualAvailableTime2, queueIt2);

      // // if ( mpduList2.size() > 0 )
      // //   std::cout << "STA_" << candidate.first->aid << " is being sent an A-MPDU of size " << mpduList2.size() << " after aggregation" << std::endl;
      // if (mpduList2.size () > 1)
      //   {
      //     //file << "STA_" << candidate.first->aid << " assigned a PSDU of size " << mpduList.size() << " after aggregation\n";
      //     std::cout<<"SU Aggregation successful"<<std::endl; 
      //     // A-MPDU aggregation succeeded, update psduMap
      //     dlMuInfo2.psduMap[candidate_su.first->aid] = Create<WifiPsdu> (std::move (mpduList2));
      //   }
      // else
      //   {

      //     //file << "STA_" << candidate.first->aid << " assigned a PSDU of size " << mpduList.size() << " without aggregation\n";
      //     std::cout<<"SU Aggregration failed"<<std::endl; 
      //     dlMuInfo2.psduMap[candidate_su.first->aid] = Create<WifiPsdu> (item2, true); 
      //   }
        
      //   size_t queueSize = queueIt2.queue->GetNPackets();
      //   std::cout<<"SU Queue Size is: " << queueSize <<std::endl; 

      //   size_t ampduSize = dlMuInfo2.psduMap[candidate_su.first->aid]->GetNMpdus();
      //   std::cout << "For SU -- STA_" << candidate_su.first->aid << " is being sent an A-MPDU of size " << ampduSize << " after aggregation" << std::endl;
     

    std::cout << "SU Data Transmission Duration SU" << dlMuInfo2.txParams.m_txDuration << std::endl;
    std::cout << "SU Ack Transmission Duration SU" << dlMuInfo2.txParams.m_acknowledgment->acknowledgmentTime << std::endl;
    
    //Initialize SU Tx data duration and Ack duration 
    su_txdata = dlMuInfo2.txParams.m_txDuration; 
    su_Back = dlMuInfo2.txParams.m_acknowledgment->acknowledgmentTime; 

    // std::cout << "Preamble  Duration -----------" << m_apMac->GetWifiPhy()->CalculatePhyPreambleAndHeaderDuration (m_txParams.m_txVector)<<std::endl; 
    
    //Initialize MU and SU Downlink Preamble Duration 
    // mu_Pdl = m_apMac->GetWifiPhy()->CalculatePhyPreambleAndHeaderDuration (m_txParams.m_txVector); 
    su_Pdl = m_apMac->GetWifiPhy()->CalculatePhyPreambleAndHeaderDuration (dlMuInfo2.txParams.m_txVector); 


    WifiDlMuAggregateTf* dlMuAggrTfAcknowledgment2 = static_cast<WifiDlMuAggregateTf*> (dlMuInfo2.txParams.m_acknowledgment.get ());
    WifiTxVector* responseTxVector2 = nullptr;
    responseTxVector2 = &dlMuAggrTfAcknowledgment2->stationsReplyingWithBlockAck.begin ()->second.blockAckTxVector;
    //std::cout << "Preamble  Duration ACKKK -----------SU" << m_apMac->GetWifiPhy()->CalculatePhyPreambleAndHeaderDuration (*responseTxVector2)<<std::endl;
      
    su_Pul = m_apMac->GetWifiPhy()->CalculatePhyPreambleAndHeaderDuration (*responseTxVector2); 

    std::map<int, int> valueCounts ={{64, 2513}, {14, 1256}}; //statistics from single user run 
    size_t ampduSize = pickSuAmpduByProbability(valueCounts);   
    
    return ampduSize;

 
     

}

MultiUserScheduler::DlMuInfo
RrsumuScheduler::ComputeDlMuInfo (void)
{
  NS_LOG_FUNCTION (this);

  if (m_candidates.empty ())
    {
      return DlMuInfo ();
    }

  uint16_t bw = m_apMac->GetWifiPhy ()->GetChannelWidth ();

  // compute how many stations can be granted an RU and the RU size
  std::size_t nRusAssigned = m_txParams.GetPsduInfoMap ().size ();
  // std::size_t nRusAssigned = 1; 
  
  std::cout<<"No. of RUs assigned for MU: "<< nRusAssigned <<std::endl; 
  
  std::size_t nCentral26TonesRus1;
  // std::size_t nCentral26TonesRus2;

  HeRu::RuType ruType = HeRu::GetEqualSizedRusForStations (bw, nRusAssigned, nCentral26TonesRus1);
  // HeRu::RuType ruType2 = HeRu::GetEqualSizedRusForStations (bw, nRusAssigned2, nCentral26TonesRus2);
  NS_LOG_DEBUG (nRusAssigned << " stations are being assigned a " << ruType << " RU");
  std::cout << nRusAssigned << " stations are being assigned a " << ruType << " RU" << std::endl;
  // std::cout << nCentral26TonesRus1 << " stations are being assigned a " << "26-tones" << " RU" << std::endl;

  // std::cout << 1 << " station is being assigned a " << ruType2 << " RU" << std::endl;

  if (!m_useCentral26TonesRus || m_candidates.size () == nRusAssigned)
    {
      // std::cout<<"Inside if condition";
      nCentral26TonesRus1 = 0;
    }
  else
    {
      nCentral26TonesRus1 = std::min (m_candidates.size () - nRusAssigned, nCentral26TonesRus1);
      NS_LOG_DEBUG (nCentral26TonesRus1 << " stations are being assigned a 26-tones RU");
      //std::cout << nCentral26TonesRus << " stations are being assigned a 26-tones RU" << std::endl;
    }

  // nCentral26TonesRus2 = 0; 
  
  // std::cout<<"Before DlMUInfo";
  DlMuInfo dlMuInfo;

  // We have to update the TXVECTOR
  dlMuInfo.txParams.m_txVector.SetPreambleType (m_txParams.m_txVector.GetPreambleType ());
  dlMuInfo.txParams.m_txVector.SetChannelWidth (m_txParams.m_txVector.GetChannelWidth ());
  dlMuInfo.txParams.m_txVector.SetGuardInterval (m_txParams.m_txVector.GetGuardInterval ());
  dlMuInfo.txParams.m_txVector.SetBssColor (m_txParams.m_txVector.GetBssColor ());

  
  auto candidateIt = m_candidates.begin (); // iterator over the list of candidate receivers
  // auto candidateIt2 = m_candidates2.begin (); // iterator over the list of candidate receivers for single user transmission
  

  
  for (std::size_t i = 0; i < nRusAssigned + nCentral26TonesRus1; i++)
    {
      NS_ASSERT (candidateIt != m_candidates.end ());

      uint16_t staId = candidateIt->first->aid;
      // AssignRuIndices will be called below to set RuSpec
      dlMuInfo.txParams.m_txVector.SetHeMuUserInfo (staId,
                                                    {{(i < nRusAssigned ? ruType : HeRu::RU_26_TONE), 1, false},
                                                      m_txParams.m_txVector.GetMode (staId),
                                                      m_txParams.m_txVector.GetNss (staId)});
      candidateIt++;
    }

    

    

  // for (std::size_t i = 0; i < 1; i++)
  //   {
  //     NS_ASSERT (candidateIt2 != m_candidates2.end ());

  //     uint16_t staId = candidateIt2->first->aid;
  //     // AssignRuIndices will be called below to set RuSpec
  //     dlMuInfo2.txParams.m_txVector.SetHeMuUserInfo (staId,
  //                                                   {{(HeRu::RU_484_TONE), 1, false},
  //                                                     m_txParams2.m_txVector.GetMode (staId),
  //                                                     m_txParams2.m_txVector.GetNss (staId)});
  //     candidateIt2++;
  //   }
  // remove candidates that will not be served
  m_candidates.erase (candidateIt, m_candidates.end ());

  
  // m_candidates2.erase (candidateIt2, m_candidates2.end ());
  
  std::cout<<"In ComputeDlMuInfo \n Length of m_candidates: "<<m_candidates.size()<<std::endl; 
  // std::cout<<"In ComputeDlMuInfo \n Length of m_candidates: "<<m_candidates.size()<<"Length of m_candidates2: "<<m_candidates2.size()<<std::endl; 

  AssignRuIndices (dlMuInfo.txParams.m_txVector);

  m_txParams.Clear();
  
  Ptr<const WifiMacQueueItem> mpdu;

  // Compute the TX params (again) by using the stored MPDUs and the final TXVECTOR
  Time actualAvailableTime = (m_initialFrame ? Time::Min () : m_availableTime);
  

  // std::cout<<"MU TXOP started? : "<< m_initialFrame<<std::endl;  
  for (const auto& candidate : m_candidates)
    {
      mpdu = candidate.second;
      NS_ASSERT (mpdu != nullptr);
      
      // std::cout<<"The available time for MU Transmission is "<<actualAvailableTime<<std::endl; 
      bool ret = m_heFem->TryAddMpdu (mpdu, dlMuInfo.txParams, actualAvailableTime);
      NS_UNUSED (ret);
      NS_ASSERT_MSG (ret, "Weird that an MPDU does not meet constraints when "
                          "transmitted over a larger RU");
    }

    
  // We have to complete the PSDUs to send
  // Ptr<WifiMacQueue> queue;
  Mac48Address receiver;

  // We have to complete the PSDUs to send
 
  // std::ostringstream oss;
  // oss << "rr_aggregation_log_same1000.txt";
  
  // std::ofstream file;
  // file.open(oss.str(), std::ios_base::app | std::ios_base::out);

  // oss.str("");
  // oss.clear();

  // file << "======= SCHEDULER BEGINS AGGREGATION ========\n";
  // uint8_t candidateIterator=0; 
  // std::cout<<"Total Packets before MU aggregation "<<que->GetNPackets()<<std::endl;
  int iterator=0; 

  for (const auto& candidate : m_candidates)
    {
      // Let us try first A-MSDU aggregation if possible
      mpdu = candidate.second;
      // std::cout<<"MU: This is the MPDU: " << mpdu <<std::endl; 
      NS_ASSERT (mpdu != nullptr);
      uint8_t tid = mpdu->GetHeader ().GetQosTid ();
      receiver = mpdu->GetHeader ().GetAddr1 ();
      NS_ASSERT (receiver == candidate.first->address);
      
      NS_ASSERT (mpdu->IsQueued ());
      WifiMacQueueItem::QueueIteratorPair queueIt = mpdu->GetQueueIteratorPairs ().front ();
      NS_ASSERT (queueIt.queue != nullptr);
      // size_t initialQueueSize = queueIt.queue->GetNPackets(); 
      
      Ptr<WifiMacQueueItem> item = *queueIt.it;
     
      // Ptr<WifiMacQueueItem> item2 = *queueIt.it;
      queueIt.it++;

      if (!mpdu->GetHeader ().IsRetry ())
        {
          // this MPDU must have been dequeued from the AC queue and we can try
          // A-MSDU aggregation
          item = m_heFem->GetMsduAggregator ()->GetNextAmsdu (mpdu, dlMuInfo.txParams, m_availableTime, queueIt);
          
          // item2 = m_heFem->GetMsduAggregator ()->GetNextAmsdu (mpdu, dlMuInfo2.txParams, m_availableTime, queueIt);
          if (item == nullptr)
            {
              // A-MSDU aggregation failed or disabled
              
              item = *mpdu->GetQueueIteratorPairs ().front ().it;
              
            }
          // if (item2 ==nullptr){
            //  item2 = *mpdu->GetQueueIteratorPairs ().front ().it;
          // }  
          m_apMac->GetQosTxop (QosUtilsMapTidToAc (tid))->AssignSequenceNumber (item);
          
          // m_apMac->GetQosTxop (QosUtilsMapTidToAc (tid))->AssignSequenceNumber (item2);
        }

      // std::cout<<"MU Item value: "<< item<<std::endl;  
      // Now, let's try A-MPDU aggregation if possible
      std::vector<Ptr<WifiMacQueueItem>> mpduList = m_heFem->GetMpduAggregator ()->GetNextAmpdu (item, dlMuInfo.txParams, m_availableTime, queueIt);
      
      
      // if ( mpduList.size() > 0 )
      //   std::cout << "STA_" << candidate.first->aid << " is being sent an A-MPDU of size " << mpduList.size() << " after aggregation" << std::endl;
      if (mpduList.size () > 1)
        {
          //file << "STA_" << candidate.first->aid << " assigned a PSDU of size " << mpduList.size() << " after aggregation\n";
           
          // A-MPDU aggregation succeeded, update psduMap
          dlMuInfo.psduMap[candidate.first->aid] = Create<WifiPsdu> (std::move (mpduList));
          
        }

        else
        {

          //file << "STA_" << candidate.first->aid << " assigned a PSDU of size " << mpduList.size() << " without aggregation\n";

          dlMuInfo.psduMap[candidate.first->aid] = Create<WifiPsdu> (item, true);
        
        }
       
       
       size_t ampduSize = dlMuInfo.psduMap[candidate.first->aid]->GetNMpdus();
       
       std::cout << "For MU -- STA_" << candidate.first->aid << " is being sent an A-MPDU of size " << ampduSize << " after aggregation" << std::endl;
       
       mu_ampdu[iterator++]=ampduSize; 
      
    }

      std::cout<<"Total Packets after MU aggregation "<<que->GetNPackets()<<std::endl;

//Inference: Queue size remains same before and after aggregation 
 
  // file << "======= SCHEDULER ENDS AGGREGATION ========\n";
  // file.close();

  AcIndex primaryAc = m_edca->GetAccessCategory ();

  // The amount of credits received by each station equals the TX duration (in
  // microseconds) divided by the number of stations.
  double creditsPerSta = dlMuInfo.txParams.m_txDuration.ToDouble (Time::US)
                        / m_staList[primaryAc].size ();
  // Transmitting stations have to pay a number of credits equal to the TX duration
  // (in microseconds) times the allocated bandwidth share.
  double debitsPerMhz = dlMuInfo.txParams.m_txDuration.ToDouble (Time::US)
                        / (nRusAssigned * HeRu::GetBandwidth (ruType)
                          + nCentral26TonesRus1 * HeRu::GetBandwidth (HeRu::RU_26_TONE));

  
  // assign credits to all stations
  for (auto& sta : m_staList[primaryAc])
    {
      sta.credits += creditsPerSta;
      sta.credits = std::min (sta.credits, m_maxCredits.ToDouble (Time::US));
    }

  // subtract debits to the selected stations
  candidateIt = m_candidates.begin ();

  for (std::size_t i = 0; i < nRusAssigned + nCentral26TonesRus1; i++)
    {
      NS_ASSERT (candidateIt != m_candidates.end ());

      candidateIt->first->credits -= debitsPerMhz * HeRu::GetBandwidth (i < nRusAssigned ? ruType : HeRu::RU_26_TONE);

      candidateIt++;
    }

  

  // sort the list in decreasing order of credits
  m_staList[primaryAc].sort ([] (const MasterInfo& a, const MasterInfo& b)
                              { return a.credits > b.credits; });

  NS_LOG_DEBUG ("Next station to serve has AID=" << m_staList[primaryAc].front ().aid);
  
  // int result = calculate_su_mpdu();
  // std::cout<<"Result here: "<<result; 

  return dlMuInfo;
}

void
RrsumuScheduler::AssignRuIndices (WifiTxVector& txVector)
{
  NS_LOG_FUNCTION (this << txVector);

  uint8_t bw = txVector.GetChannelWidth ();

  // find the RU types allocated in the TXVECTOR
  std::set<HeRu::RuType> ruTypeSet;
  for (const auto& userInfo : txVector.GetHeMuUserInfoMap ())
    {
      ruTypeSet.insert (userInfo.second.ru.GetRuType ());
    }

  std::vector<HeRu::RuSpec> ruSet, central26TonesRus;

  // This scheduler allocates equal sized RUs and optionally the remaining 26-tone RUs
  if (ruTypeSet.size () == 2)
    {
      // central 26-tone RUs have been allocated
      NS_ASSERT (ruTypeSet.find (HeRu::RU_26_TONE) != ruTypeSet.end ());
      ruTypeSet.erase (HeRu::RU_26_TONE);
      NS_ASSERT (ruTypeSet.size () == 1);
      central26TonesRus = HeRu::GetCentral26TonesRus (bw, *ruTypeSet.begin ());
    }

  NS_ASSERT (ruTypeSet.size () == 1);
  ruSet = HeRu::GetRusOfType (bw, *ruTypeSet.begin ());

  auto ruSetIt = ruSet.begin ();
  auto central26TonesRusIt = central26TonesRus.begin ();

  for (const auto& userInfo : txVector.GetHeMuUserInfoMap ())
    {
      if (userInfo.second.ru.GetRuType () == *ruTypeSet.begin ())
        {
          NS_ASSERT (ruSetIt != ruSet.end ());
          txVector.SetRu (*ruSetIt, userInfo.first);
          ruSetIt++;
        }
      else
        {
          NS_ASSERT (central26TonesRusIt != central26TonesRus.end ());
          txVector.SetRu (*central26TonesRusIt, userInfo.first);
          central26TonesRusIt++;
        }
    }
}

MultiUserScheduler::UlMuInfo
RrsumuScheduler::ComputeUlMuInfo (void)
{
  return UlMuInfo {m_trigger, m_tbPpduDuration, std::move (m_txParams)};
}

} //namespace ns3
