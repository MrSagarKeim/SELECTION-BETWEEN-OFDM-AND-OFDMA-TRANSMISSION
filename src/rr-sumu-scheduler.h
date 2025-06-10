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

#ifndef RR_MULTI_USER_SCHEDULER_H
#define RR_MULTI_USER_SCHEDULER_H

#include "multi-user-scheduler.h"
#include <list>
#include <map>
#include <vector>
#include <random>

namespace ns3 {

/**
 * \ingroup wifi
 *
 * RrsumuScheduler is a simple OFDMA scheduler that indicates to perform a DL OFDMA
 * transmission if the AP has frames to transmit to at least one station.
 * RrsumuScheduler assigns RUs of equal size (in terms of tones) to stations to
 * which the AP has frames to transmit belonging to the AC who gained access to the
 * channel or higher. The maximum number of stations that can be granted an RU
 * is configurable. Associated stations are served in a round robin fashion.
 *
 * \todo Take the supported channel width of the stations into account while selecting
 * stations and assigning RUs to them.
 */
class RrsumuScheduler : public MultiUserScheduler
{
public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);
  RrsumuScheduler ();
  virtual ~RrsumuScheduler ();
   void setMacaddresses(std::vector<Mac48Address> macaddresses );
   
   void setAPqueue(Ptr<WifiMacQueue> que);
   std::vector<Mac48Address> getMacaddresses( );
   
   Ptr<WifiMacQueue> getAPqueue( );

protected:
  void DoDispose (void) override;
  void DoInitialize (void) override;

private:
  TxFormat SelectTxFormat (void) override;
  DlMuInfo ComputeDlMuInfo (void) override;
  UlMuInfo ComputeUlMuInfo (void) override;

  virtual int calculate_su_mpdu (void); //Checks if there are candidates available for SU Transmission
  // virtual int calculate_mu_mpdu (void); 
  
  virtual int pickSuAmpduByProbability(const std::map<int, int>& valueCounts);
  /**
   * Check if it is possible to send a BSRP Trigger Frame given the current
   * time limits.
   *
   * \return UL_MU_TX if it is possible to send a BSRP TF, NO_TX otherwise
   */
  virtual TxFormat TrySendingBsrpTf (void);

  /**
   * Check if it is possible to send a Basic Trigger Frame given the current
   * time limits.
   *
   * \return UL_MU_TX if it is possible to send a Basic TF, DL_MU_TX if we can try
   *         to send a DL MU PPDU and NO_TX if the remaining time is too short
   */
  virtual TxFormat TrySendingBasicTf (void);

  

  /**
   * Check if it is possible to send a DL MU PPDU given the current
   * time limits.
   *
   * \return DL_MU_TX if it is possible to send a DL MU PPDU, SU_TX if a SU PPDU
   *         can be transmitted (e.g., there are no HE stations associated or sending
   *         a DL MU PPDU is not possible and m_forceDlOfdma is false) or NO_TX otherwise
   */
  virtual TxFormat TrySendingDlMuPpdu (void);



  /**
   * Assign an RU index to all the RUs allocated by the given TXVECTOR. Allocated
   * RUs must all have the same size, except for allocated central 26-tone RUs.
   *
   * \param txVector the given TXVECTOR
   */
  void AssignRuIndices (WifiTxVector& txVector);

  /**
   * Notify the scheduler that a station associated with the AP
   *
   * \param aid the AID of the station
   * \param address the MAC address of the station
   */
  void NotifyStationAssociated (uint16_t aid, Mac48Address address);
  /**
   * Notify the scheduler that a station deassociated with the AP
   *
   * \param aid the AID of the station
   * \param address the MAC address of the station
   */
  void NotifyStationDeassociated (uint16_t aid, Mac48Address address);

  /**
   * Information used to sort stations
   */
  struct MasterInfo
  {
    uint16_t aid;                 //!< station's AID
    Mac48Address address;         //!< station's MAC Address
    double credits;               //!< credits accumulated by the station
  };

  /**
   * Information stored for candidate stations
   */
  typedef std::pair<std::list<MasterInfo>::iterator, Ptr<const WifiMacQueueItem>> CandidateInfo;

  uint8_t m_nStations;                                  //!< Number of stations/slots to fill
  bool m_enableTxopSharing;                             //!< allow A-MPDUs of different TIDs in a DL MU PPDU
  bool m_forceDlOfdma;                                  //!< return DL_OFDMA even if no DL MU PPDU was built
  bool m_enableUlOfdma;                                 //!< enable the scheduler to also return UL_OFDMA
  bool m_enableBsrp;                                    //!< send a BSRP before an UL MU transmission
  bool m_useCentral26TonesRus;                          //!< whether to allocate central 26-tone RUs
  uint32_t m_ulPsduSize;                                //!< the size in byte of the solicited PSDU
  std::map<AcIndex, std::list<MasterInfo>> m_staList;   //!< Per-AC list of stations (next to serve first)
  std::list<CandidateInfo> m_candidates;                //!< Candidate stations for MU TX
  
  Time m_maxCredits;                                    //!< Max amount of credits a station can have
  Ptr<WifiMacQueueItem> m_trigger;                      //!< Trigger Frame to send
  Time m_tbPpduDuration;                                //!< Duration of the solicited TB PPDUs
  WifiTxParameters m_txParams;                          //!< TX parameters for MU 
  TriggerFrameType m_ulTriggerType;                     //!< Trigger Frame type for UL MU
  uint32_t num_stations;                                //!< stations in the environment
  // std::vector<Mac48Address> macaddresses;               //!< MAC addresses of stations in the environment
  Ptr<WifiMacQueue> que ;                               //!< pointer to AP queue
  double threshold;                                     //!< threshold to decide SU tx or MU tx
  
  uint32_t slot_time = 9; 
  uint32_t sifs = 16; 
  uint32_t aifs = sifs+3*slot_time;                      //best effort time 
  uint32_t mpdu_size ; 
  double bo = (15/2)*slot_time;
   
  //**MU Parameters */
  double mu_tpt; 
  std::vector<int> mu_ampdu;                            // List of MU AMPDUs for different stations 
  Time mu_txdata, mu_Back;                               // MU Tx data and Ack 
  Time mu_Pdl, mu_Pul;   

  //**SU Parameters */
  double su_tpt; 
  std::list<CandidateInfo> m_candidates2;               //!< Candidate stations for SU TX
  WifiTxParameters m_txParams2;                         //!< TX parameters for SU    
  uint32_t su_ampdu;                                    // SU AMPDU 
  Time su_txdata, su_Back;                               // SU Tx data and Ack
  uint32_t pe = 16;  
  Time su_Pdl, su_Pul;   

};

} //namespace ns3

#endif /* RR_MULTI_USER_SCHEDULER_H */
