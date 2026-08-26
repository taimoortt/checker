/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2010,2011,2012,2013 TELEMATICS LAB, Politecnico di Bari
 *
 * This file is part of LTE-Sim
 *
 * LTE-Sim is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation;
 *
 * LTE-Sim is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with LTE-Sim; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Giuseppe Piro <g.piro@poliba.it>
 */


#ifndef ENB_MAC_ENTITY_H
#define ENB_MAC_ENTITY_H

#include <list>
#include "mac-entity.h"
#include <map>
#include <vector>
using std::vector;


/*
 * This class implements the MAC layer of the eNodeB device
 */

class PacketScheduler;
class CqiIdealControlMessage;
class CqiWithMuteIdealControlMessage;
class RSRPIdealControlMessage;
class PdcchMapIdealControlMessage;
class SchedulingRequestIdealControlMessage;

struct CqiReport {
  int cqi;              // the "dirty" cqi without muting any cell
  int cqi_mute_one;    // the cqi with just the neighbor cell muted
  int cqi_mute_two;
  int cell_one;
  int cell_two;

  CqiReport(int _cqi, int _cqi_mute_one, int _cqi_mute_two,
    int _cell_one, int _cell_two)
  : cqi(_cqi), cqi_mute_one(_cqi_mute_one),
    cqi_mute_two(_cqi_mute_two), cell_one(_cell_one),
    cell_two(_cell_two) {}
};

class EnbMacEntity : public MacEntity
{
public:

  EnbMacEntity (void);
  virtual ~EnbMacEntity (void);


  void SetUplinkPacketScheduler (PacketScheduler* s);
  void SetDownlinkPacketScheduler (PacketScheduler* s);
  PacketScheduler* GetUplinkPacketScheduler (void);
  PacketScheduler* GetDownlinkPacketScheduler (void);

  void ReceiveCqiIdealControlMessage  (CqiIdealControlMessage* msg);
  void ReceiveCqiWithMuteIdealControlMessage (CqiWithMuteIdealControlMessage* msg);
  void ReceiveRSRPIdealControlMessage (RSRPIdealControlMessage* msg);
  void SendPdcchMapIdealControlMessage  (PdcchMapIdealControlMessage* msg);
  void ReceiveSchedulingRequestIdealControlMessage (SchedulingRequestIdealControlMessage *msg);
private:
  std::map<int, vector<vector<int>>> m_userCQITrace;
  std::vector<int> m_userMapping;

  PacketScheduler* m_uplinkScheduler;
  PacketScheduler* m_downlinkScheduler;
};


#endif /* ENB_MAC_ENTITY_H */
