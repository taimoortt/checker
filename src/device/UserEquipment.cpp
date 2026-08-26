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


#include "UserEquipment.h"
#include "NetworkNode.h"
#include "ENodeB.h"
#include "HeNodeB.h"
#include "Gateway.h"
#include "../phy/ue-lte-phy.h"
#include "../phy/interference.h"
#include "CqiManager/cqi-manager.h"
#include "../core/eventScheduler/simulator.h"
#include "../componentManagers/NetworkManager.h"
#include "../protocolStack/rrc/ho/handover-entity.h"
#include "../protocolStack/rrc/ho/ho-manager.h"
#include "../utility/eesm-effective-sinr.h"
#include "../core/spectrum/bandwidth-manager.h"
#include "stdio.h"
#include "fstream"
#include <cassert>
#include <cstdlib>
#include <sstream>

#define TRACE_LENGTH 1000

UserEquipment::UserEquipment ()
{}

UserEquipment::UserEquipment (int idElement,
							  double posx,
							  double posy,
							  Cell *cell,
							  NetworkNode* target,
							  bool handover,
							  Mobility::MobilityModel model)
{
  SetIDNetworkNode (idElement);
  SetNodeType(NetworkNode::TYPE_UE);
  SetCell(cell);

  m_targetNode = target;

  ProtocolStack *stack = new ProtocolStack (this);
  SetProtocolStack (stack);

  Classifier *classifier = new Classifier ();
  classifier->SetDevice (this);
  SetClassifier (classifier);
  SetNodeState(STATE_IDLE);

  //Setup Mobility Model
  Mobility *m;
  if (model == Mobility::RANDOM_DIRECTION)
    {
	  m = new RandomDirection ();
    }
  else if (model == Mobility::RANDOM_WALK)
    {
	  m = new RandomWalk ();
    }
  else if (model == Mobility::RANDOM_WAYPOINT)
    {
	  m = new RandomWaypoint ();
    }
  else if (model == Mobility::CONSTANT_POSITION)
    {
	  m = new ConstantPosition ();
    }
  else if (model == Mobility::MANHATTAN)
    {
	  m = new Manhattan ();
    }
  else
    {
	  std::cout << "ERROR: incorrect Mobility Model"<< std::endl;
	  m = new RandomDirection ();
    }
  CartesianCoordinates *position = new CartesianCoordinates (posx, posy);
  m->SetHandover (handover);
  m->SetAbsolutePosition (position);
  m->SetNodeID (idElement);
  SetMobilityModel (m);

  m_timePositionUpdate = 0.001;
  Simulator::Init()->Schedule(m_timePositionUpdate,
		  &UserEquipment::UpdateUserPosition,
		  this,
		  Simulator::Init ()->Now());

  delete position;

  UeLtePhy *phy = new UeLtePhy ();
  phy->SetDevice(this);
  phy->SetBandwidthManager (target->GetPhy ()->GetBandwidthManager ());
  SetPhy(phy);

  m_cqiManager = NULL;
  m_isIndoor = false;
}

UserEquipment::UserEquipment (int idElement,
							  double posx,
							  double posy,
							  int speed,
							  double speedDirection,
							  Cell *cell,
							  NetworkNode* target,
							  bool handover,
							  Mobility::MobilityModel model)
{
  SetIDNetworkNode (idElement);
  SetNodeType(NetworkNode::TYPE_UE);
  SetCell(cell);

  m_targetNode = target;

  ProtocolStack *stack = new ProtocolStack (this);
  SetProtocolStack (stack);

  Classifier *classifier = new Classifier ();
  classifier->SetDevice (this);
  SetClassifier (classifier);
  SetNodeState (STATE_IDLE);
  //Setup Mobility Model
  Mobility *m;
  if (model == Mobility::RANDOM_DIRECTION)
    {
	  m = new RandomDirection ();
    }
  else if (model == Mobility::RANDOM_WALK)
    {
	  m = new RandomWalk ();
    }
  else if (model == Mobility::RANDOM_WAYPOINT)
    {
	  m = new RandomWaypoint ();
    }
  else if (model == Mobility::CONSTANT_POSITION)
    {
	  m = new ConstantPosition ();
    }
  else if (model == Mobility::MANHATTAN)
    {
	  m = new Manhattan ();
    }
  else
    {
	  std::cout << "ERROR: incorrect Mobility Model"<< std::endl;
	  m = new RandomDirection ();
    }
  CartesianCoordinates *position = new CartesianCoordinates(posx, posy);
  m->SetHandover(handover);
  m->SetAbsolutePosition(position);
  m->SetNodeID(idElement);
  m->SetSpeed(speed);
  m->SetSpeedDirection(speedDirection);
  SetMobilityModel (m);

  m_timePositionUpdate = 0.001;
  Simulator::Init()->Schedule(m_timePositionUpdate,
		  &UserEquipment::UpdateUserPosition,
		  this,
		  Simulator::Init ()->Now());

  delete position;


  UeLtePhy *phy = new UeLtePhy ();
  phy->SetDevice(this);
  phy->SetBandwidthManager (target->GetPhy ()->GetBandwidthManager ());
  SetPhy (phy);

  m_cqiManager = NULL;
  m_isIndoor = false;

}

UserEquipment::~UserEquipment()
{
  m_targetNode = NULL;
  delete m_cqiManager;
  Destroy ();
}

void
UserEquipment::SetTargetNode (NetworkNode* n)
{
  m_targetNode = n;
  SetCell (n->GetCell ());
}

NetworkNode*
UserEquipment::GetTargetNode (void)
{
  return m_targetNode;
}


void
UserEquipment::UpdateUserPosition (double time)
{
  GetMobilityModel ()->UpdatePosition (time);

    SetIndoorFlag(NetworkManager::Init()->CheckIndoorUsers(this));

    if (GetMobilityModel ()->GetHandover () == true)
      {
           NetworkNode* targetNode = GetTargetNode ();

        if (targetNode->GetProtocolStack ()->GetRrcEntity ()->
                   GetHandoverEntity ()->CheckHandoverNeed (this))
          {
           NetworkNode* newTagertNode = targetNode->GetProtocolStack ()
                           ->GetRrcEntity ()->GetHandoverEntity ()->GetHoManager ()->m_target;

           NetworkManager::Init()->HandoverProcedure(time, this, targetNode, newTagertNode);
          }
      }


    //schedule the new update after m_timePositionUpdate
    Simulator::Init()->Schedule(m_timePositionUpdate,
                                                           &UserEquipment::UpdateUserPosition,
                                                           this,
                                                           Simulator::Init ()->Now());
}


void
UserEquipment::SetCqiManager (CqiManager *cm)
{
  m_cqiManager = cm;
}

CqiManager*
UserEquipment::GetCqiManager (void)
{
  return m_cqiManager;
}

void
UserEquipment::SetIndoorFlag ( bool flag)
{
  m_isIndoor = flag;
}

bool
UserEquipment::IsIndoor (void)
{
  return m_isIndoor;
}

void UserEquipment::CalculateUpdatedRSRP(){
  // cout << "Updating RSRP for UE: " << this->GetIDNetworkNode() << endl;
  double powerTx = pow (10., (this->GetTargetNode()->GetPhy()->GetTxPower() - 30) / 10); // in natural unit
  int channels = this->GetTargetNode()->GetPhy()->GetBandwidthManager()->GetDlSubChannels().size();
  primary_rsrp = 10 * log10 (powerTx / channels); //in dB
  double distance = this->GetMobilityModel()->GetAbsolutePosition()->GetDistance(this->GetTargetNode()->GetMobilityModel ()->GetAbsolutePosition ());
  double m_pathLoss = 128.1 + (37.6 * log10 (distance * 0.001)); // URBAN AREA PATH LOSS
  double m_penetrationLoss = 10;
  double m_shadowing = 0;
  double loss = - m_pathLoss - m_penetrationLoss - m_shadowing;
  primary_rsrp += loss;

  // Calculate Interference RSRP
  interference_rsrp = this->GetPhy()->GetInterference()->ComputeInterference(this);
  for (auto it = interference_rsrp.begin(); it != interference_rsrp.end(); ++it) {
    interference_rsrp[it->first] = 10. * log10(it->second);
  }

  // cout << "Primary RSRP: " << primary_rsrp << endl;
  // for (auto const& pair: interference_rsrp){
  //   cout << "Interfering ID: " << pair.first << "\tPower: " << pair.second << endl;
  // }

  ReadAllTraces();
}

double UserEquipment::GetPrimaryRSRP(){
  return primary_rsrp;
}
std::map<int, double> UserEquipment::GetInterferenceRSRP(){
  return interference_rsrp;
}

std::vector<std::vector<float>> UserEquipment::GenOneTrace(int rsrp) {
  const char* configured_path = std::getenv("RADIONINJA_TRACE_DIR");
  std::string base_path = configured_path == NULL ? "data/csl_2120" : configured_path;
  if (!base_path.empty() && base_path.back() != '/') {
      base_path += '/';
  }
  const std::string fname = base_path + std::to_string(rsrp) + "db.log";
  std::ifstream file(fname);
  if (!file) {
      std::cerr << "Unable to open file: " << fname << std::endl;
      exit(1);
  }

  const int num_rbg = 65;
  std::vector<std::vector<float>> rsrps;
  rsrps.reserve(1000);  // Assuming a typical file contains 1000 TTI entries
  std::string line;
  std::istringstream iss;

  while (rsrps.size() < TRACE_LENGTH && getline(file, line)) {
      iss.clear();
      iss.str(line);
      float num;
      int count = 0;
      std::vector<float> current_rsrps;
      current_rsrps.reserve(num_rbg);

      while (iss >> num && count < num_rbg) {
          if (num != -1) {
              current_rsrps.push_back(num);
              count++;
          }
      }
      if (!current_rsrps.empty()) {
          rsrps.push_back(std::move(current_rsrps));  // Use move semantics to avoid copying
      }
  }
  if (rsrps.size() != TRACE_LENGTH) {
      std::cerr << "Trace file has fewer than " << TRACE_LENGTH
                << " usable rows: " << fname << std::endl;
      exit(1);
  }
  return rsrps;
}

void
UserEquipment::ReadAllTraces()
{
    int user_id = GetIDNetworkNode();
    int rsrp = static_cast<int>(std::round(primary_rsrp));
    if (rsrp < -170){
      rsrp = -170;
    } else if (primary_rsrp > -89){
      rsrp = -89;
    }
    m_trace = GenOneTrace(rsrp);
    // READ INTERFERNCE TRACES
    std::map<int, double> interference_levels = GetInterferenceRSRP();
    for (auto const & pair: interference_levels){
      int interference_level = static_cast<int>(std::round(pair.second));
      if (interference_level < -171){
        interference_level = -171;
      } else if (interference_level > -89){
        interference_level = -89;
      }
      m_interferenceTrace[pair.first] = GenOneTrace(interference_level);
    }
}

vector<double> UserEquipment::GetTrace(int timestamp){
  const auto& rsrp_tti = m_trace.at(timestamp % TRACE_LENGTH);
  vector<double> trace;
  for (size_t rbg_id = 0; rbg_id != rsrp_tti.size(); rbg_id++) {
    for (int i = 0; i < RBG_SIZE; i++) {
      trace.push_back((double)rsrp_tti[rbg_id]);
    }
  }
  return trace; 
}

map<int, vector<double>> UserEquipment::GetInterferenceTrace(int timestamp){
  map<int, vector<double>> interference_trace;
  for (const auto& pair : m_interferenceTrace){
    const auto& rsrp_tti = m_interferenceTrace[pair.first].at(timestamp % TRACE_LENGTH);
    vector<double> trace;
    for (size_t rbg_id = 0; rbg_id != rsrp_tti.size(); rbg_id++) {
      for (int i = 0; i < RBG_SIZE; i++) {
        trace.push_back((double)rsrp_tti[rbg_id]);
      }
    }
    interference_trace[pair.first] = trace;
  }
  return interference_trace;
}

void UserEquipment::SetNbCells(int cells){
  nbCells = cells;
}

int UserEquipment::GetNbCells(void){
  return nbCells;
}


//Debug
void
UserEquipment::Print (void)
{
  std::cout <<" m_idNetworkNode = " << GetIDNetworkNode () <<
	  " idCell = " << GetCell ()->GetIdCell () <<
	  " idtargetNode = " << GetTargetNode ()->GetIDNetworkNode () <<
	  " m_AbsolutePosition_X = " <<  GetMobilityModel ()->GetAbsolutePosition()->GetCoordinateX()<<
	  " m_AbsolutePosition_Y = " << GetMobilityModel ()->GetAbsolutePosition()->GetCoordinateY()<<
      "\n\t m_speed = " << GetMobilityModel ()->GetSpeed () <<
      "\n\t m_speedDirection = " << GetMobilityModel ()->GetSpeedDirection () <<
      std::endl;
}
