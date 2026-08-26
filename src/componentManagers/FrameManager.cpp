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

#include "FrameManager.h"
#include "../core/spectrum/bandwidth-manager.h"
#include "../device/ENodeB.h"
#include "../device/HeNodeB.h"
#include "../device/UserEquipment.h"
#include "../flows/application/Application.h"
#include "../flows/radio-bearer.h"
#include "../load-parameters.h"
#include "../phy/enb-lte-phy.h"
#include "../protocolStack/mac/AMCModule.h"
#include "../protocolStack/mac/packet-scheduler/downlink-packet-scheduler.h"
#include "../protocolStack/mac/packet-scheduler/radiosaber-downlink-scheduler.h"
#include "../utility/eesm-effective-sinr.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <random>
#include <stdexcept>
#include <unordered_set>
using SchedulerAlgo = SliceContext::SchedulerAlgo;
bool weighted_pf = false;

int min_number_users_per_cell = 5;

FrameManager *FrameManager::ptr = NULL;

FrameManager::FrameManager() {
  m_nbFrames = 0;
  m_nbSubframes = 0;
  m_TTICounter = 0;
  m_frameStructure = FrameManager::FRAME_STRUCTURE_FDD; // Default Value
  m_TDDFrameConfiguration = 1;                          // Default Value
  Simulator::Init()->Schedule(0.0, &FrameManager::Start, this);
}

FrameManager::~FrameManager() {}

void FrameManager::SetFrameStructure(
    FrameManager::FrameStructure frameStructure) {
  m_frameStructure = frameStructure;
}

FrameManager::FrameStructure FrameManager::GetFrameStructure(void) const {
  return m_frameStructure;
}

void FrameManager::SetTDDFrameConfiguration(int configuration) {
  if (configuration < 0 && configuration > 6) {
    m_TDDFrameConfiguration = 0; // Default Value
  } else {
    m_TDDFrameConfiguration = configuration;
  }
}

int FrameManager::GetTDDFrameConfiguration(void) const {
  return m_TDDFrameConfiguration;
}

int FrameManager::GetSubFrameType(int nbSubFrame) {
  return TDDConfiguration[GetTDDFrameConfiguration()][nbSubFrame - 1];
}

void FrameManager::UpdateNbFrames(void) { m_nbFrames++; }

int FrameManager::GetNbFrames(void) const { return m_nbFrames; }

void FrameManager::UpdateNbSubframes(void) { m_nbSubframes++; }

void FrameManager::ResetNbSubframes(void) { m_nbSubframes = 0; }

int FrameManager::GetNbSubframes(void) const { return m_nbSubframes; }

void FrameManager::UpdateTTIcounter(void) { m_TTICounter++; }

unsigned long FrameManager::GetTTICounter() const { return m_TTICounter; }

void FrameManager::Start(void) {
#ifdef FRAME_MANAGER_DEBUG
  std::cout << " LTE Simulation starts now! " << std::endl;
#endif

  Simulator::Init()->Schedule(0.0, &FrameManager::StartFrame, this);
}

void FrameManager::StartFrame(void) {
  UpdateNbFrames();

#ifdef FRAME_MANAGER_DEBUG
  std::cout << " +++++++++ Start Frame, time =  " << Simulator::Init()->Now()
            << " +++++++++" << std::endl;
#endif

  Simulator::Init()->Schedule(0.0, &FrameManager::StartSubframe, this);
}

void FrameManager::StopFrame(void) {
  Simulator::Init()->Schedule(0.0, &FrameManager::StartFrame, this);
}

void FrameManager::StartSubframe(void) {
#ifdef FRAME_MANAGER_DEBUG
  // std::cout << " --------- Start SubFrame, time =  " <<
  // Simulator::Init()->Now()
  //           << " --------- "
  //           << "TTI: " << GetTTICounter() << std::endl;
#endif

  // Prints UE positioning logs after the handovers have occured.
  if (GetTTICounter() == 120) {
    std::vector<ENodeB *> *enodebs = GetNetworkManager()->GetENodeBContainer();
    cout << "ENB Size: " << enodebs->size() << endl;
    for (auto iter = enodebs->begin(); iter != enodebs->end(); iter++) {
      ENodeB *enb = *iter;
      ENodeB::UserEquipmentRecords *ue_records = enb->GetUserEquipmentRecords();
      cout << "UE Records Size: " << ue_records->size() << endl;
      for (size_t i = 0; i < ue_records->size(); i++) {
        UserEquipment *x = (*ue_records)[i]->GetUE();
        cout << GetTTICounter();
        x->Print();
      }
    }
  }

  if (GetTTICounter() == 990) {
    std::vector<ENodeB *> *enodebs = GetNetworkManager()->GetENodeBContainer();
    cout << "ENB Size: " << enodebs->size() << endl;
    for (auto iter = enodebs->begin(); iter != enodebs->end(); iter++) {
      ENodeB *enb = *iter;
      ENodeB::UserEquipmentRecords *ue_records = enb->GetUserEquipmentRecords();
      cout << "UE Records Size: " << ue_records->size() << endl;
      for (size_t i = 0; i < ue_records->size(); i++) {
        UserEquipment *x = (*ue_records)[i]->GetUE();
        cout << GetTTICounter();
        x->Print();
      }
    }
  }

  UpdateTTIcounter();
  UpdateNbSubframes();

  /*
   * At the beginning of each sub-frame the simulation
   * update user position. This function could be
   * implemented also every TTI.
   */
#ifdef PLOT_USER_POSITION
  NetworkManager::Init()->PrintUserPosition();
#endif

  /*
   * According to the Frame Structure, the DW/UL link scheduler
   * will be called for each sub-frame.
   * (RBs allocation)
   */
  Simulator::Init()->Schedule(0, &FrameManager::CentralResourceAllocation,
                              this);
  Simulator::Init()->Schedule(0.001, &FrameManager::StopSubframe, this);
}

void FrameManager::StopSubframe(void) {
  if (GetNbSubframes() == 10) {
    ResetNbSubframes();
    Simulator::Init()->Schedule(0.0, &FrameManager::StopFrame, this);
  } else {
    Simulator::Init()->Schedule(0.0, &FrameManager::StartSubframe, this);
  }
}

NetworkManager *FrameManager::GetNetworkManager(void) {
  return NetworkManager::Init();
}

void FrameManager::UpdateUserPosition(void) {
  GetNetworkManager()->UpdateUserPosition(GetTTICounter() / 1000.0);
}

void FrameManager::ResourceAllocation(void) {
  std::vector<ENodeB *> *records = GetNetworkManager()->GetENodeBContainer();
  std::vector<ENodeB *>::iterator iter;
  ENodeB *record;
  for (iter = records->begin(); iter != records->end(); iter++) {
    record = *iter;

#ifdef FRAME_MANAGER_DEBUG
    std::cout << " FRAME_MANAGER_DEBUG: RBs allocation for eNB "
              << record->GetIDNetworkNode() << std::endl;
#endif

    if (GetFrameStructure() == FrameManager::FRAME_STRUCTURE_FDD) {
      // record->ResourceBlocksAllocation ();
      Simulator::Init()->Schedule(0.0, &ENodeB::ResourceBlocksAllocation,
                                  record);
    } else {
      // see frame configuration in TDDConfiguration
      if (GetSubFrameType(GetNbSubframes()) == 0) {
#ifdef FRAME_MANAGER_DEBUG
        std::cout << " FRAME_MANAGER_DEBUG: SubFrameType = "
                     "	SUBFRAME_FOR_DOWNLINK "
                  << std::endl;
#endif
        // record->DownlinkResourceBlockAllocation();
        Simulator::Init()->Schedule(
            0.0, &ENodeB::DownlinkResourceBlockAllocation, record);
      } else if (GetSubFrameType(GetNbSubframes()) == 1) {
#ifdef FRAME_MANAGER_DEBUG
        std::cout << " FRAME_MANAGER_DEBUG: SubFrameType = "
                     "	SUBFRAME_FOR_UPLINK "
                  << std::endl;
#endif
        // record->UplinkResourceBlockAllocation();
        Simulator::Init()->Schedule(0.0, &ENodeB::UplinkResourceBlockAllocation,
                                    record);
      } else {
#ifdef FRAME_MANAGER_DEBUG
        std::cout << " FRAME_MANAGER_DEBUG: SubFrameType = "
                     "	SPECIAL_SUBFRAME"
                  << std::endl;
#endif
      }
    }
  }

  std::vector<HeNodeB *> *records_2 =
      GetNetworkManager()->GetHomeENodeBContainer();
  std::vector<HeNodeB *>::iterator iter_2;
  HeNodeB *record_2;
  for (iter_2 = records_2->begin(); iter_2 != records_2->end(); iter_2++) {
    record_2 = *iter_2;

#ifdef FRAME_MANAGER_DEBUG
    std::cout << " FRAME_MANAGER_DEBUG: RBs allocation for eNB "
              << record_2->GetIDNetworkNode() << std::endl;
#endif

    if (GetFrameStructure() == FrameManager::FRAME_STRUCTURE_FDD) {
      // record_2->ResourceBlocksAllocation ();
      Simulator::Init()->Schedule(0.0, &ENodeB::ResourceBlocksAllocation,
                                  record_2);
    } else {
      // see frame configuration in TDDConfiguration
      if (GetSubFrameType(GetNbSubframes()) == 0) {
#ifdef FRAME_MANAGER_DEBUG
        std::cout << " FRAME_MANAGER_DEBUG: SubFrameType = "
                     "	SUBFRAME_FOR_DOWNLINK "
                  << std::endl;
#endif
        // record_2->DownlinkResourceBlockAllocation();
        Simulator::Init()->Schedule(
            0.0, &ENodeB::DownlinkResourceBlockAllocation, record_2);
      } else if (GetSubFrameType(GetNbSubframes()) == 1) {
#ifdef FRAME_MANAGER_DEBUG
        std::cout << " FRAME_MANAGER_DEBUG: SubFrameType = "
                     "	SUBFRAME_FOR_UPLINK "
                  << std::endl;
#endif
        // record_2->UplinkResourceBlockAllocation();
        Simulator::Init()->Schedule(0.0, &ENodeB::UplinkResourceBlockAllocation,
                                    record_2);
      } else {
#ifdef FRAME_MANAGER_DEBUG
        std::cout << " FRAME_MANAGER_DEBUG: SubFrameType = "
                     "	SPECIAL_SUBFRAME"
                  << std::endl;
#endif
      }
    }
  }
}

void FrameManager::UpdateUserPriorityPerApp(
    std::vector<DownlinkPacketScheduler *> &schedulers, int priority) {
  DownlinkPacketScheduler *scheduler = schedulers[0];
  weighted_pf = scheduler->user_weights;
  if (GetTTICounter() < 102) {
    cout << "Setting Weighted PF for Global Algo to: " << weighted_pf << endl;
  }
  for (int i = 0; i < schedulers.size(); i++) {
    DownlinkPacketScheduler *scheduler = schedulers[i];
    auto flows = scheduler->GetFlowsToSchedule();
    for (auto it = flows->begin(); it != flows->end(); it++) {
      FlowToSchedule *flow = *it;
      int app_type = flow->GetBearer()->GetApplication()->GetApplicationType();
      if (app_type != 2) {
        flow->GetBearer()->SetPriority(priority);
        if (GetTTICounter() < 102) {
          cout << "Updating Priority - Flow: "
               << flow->GetBearer()->GetDestination()->GetIDNetworkNode()
               << " Slice: " << flow->GetSliceID() << " Cell: "
               << flow->GetBearer()->GetSource()->GetIDNetworkNode()
               << " Priority: " << flow->GetBearer()->GetPriority() << endl;
        }
      }
    }
  }
}

void FrameManager::UpdateUserPriorityPerUser(
    std::vector<DownlinkPacketScheduler *> &schedulers, int cell_id,
    int slice_id, int priority) {
  DownlinkPacketScheduler *scheduler = schedulers[cell_id];
  weighted_pf = scheduler->user_weights;
  if (GetTTICounter() < 102) {
    cout << "Setting Weighted PF for Global Algo to: " << weighted_pf << endl;
  }
  auto flows = scheduler->GetFlowsToSchedule();
  for (auto it = flows->begin(); it != flows->end(); it++) {
    FlowToSchedule *flow = *it;
    if (flow->GetSliceID() == slice_id) {
      flow->GetBearer()->SetPriority(priority);
      if (GetTTICounter() < 102) {
        cout << "Updating Priority - Flow: "
             << flow->GetBearer()->GetDestination()->GetIDNetworkNode()
             << " Slice: " << flow->GetSliceID()
             << " Cell: " << flow->GetBearer()->GetSource()->GetIDNetworkNode()
             << " Priority: " << flow->GetBearer()->GetPriority() << endl;
      }
    }
  }
}

void FrameManager::CentralResourceAllocation(void) {
  std::vector<ENodeB *> *enodebs = GetNetworkManager()->GetENodeBContainer();
  assert(GetFrameStructure() == FrameManager::FRAME_STRUCTURE_FDD);
#ifdef FRAME_MANAGER_DEBUG
  // std::cout << "Resource Allocation for eNB:";
  // for (auto iter = enodebs->begin (); iter != enodebs->end (); iter++) {
  // ENodeB* enb = *iter;
  // std::cout << " " << enb->GetIDNetworkNode();
  // }
  // std::cout << std::endl;
#endif

#ifdef SET_CENTRAL_SCHEDULER
  // std::cout << "Central Downlink RBs Allocation!" << std::endl;
  CentralDownlinkRBsAllocation();
#else
  std::cout << "Original Per-Cell Downlink RBs Allocation!" << std::endl;
  for (auto iter = enodebs->begin(); iter != enodebs->end(); iter++) {
    ENodeB *enb = *iter;
    enb->DownlinkResourceBlockAllocation();
  }
#endif
  for (auto iter = enodebs->begin(); iter != enodebs->end(); iter++) {
    ENodeB *enb = *iter;
    enb->UplinkResourceBlockAllocation();
  }
}

void FrameManager::CentralDownlinkRBsAllocation(void) {
  std::vector<ENodeB *> *enodebs = GetNetworkManager()->GetENodeBContainer();
  std::vector<DownlinkPacketScheduler *> schedulers;
  // initialization
  int counter = 0;
  for (auto it = enodebs->begin(); it != enodebs->end(); it++) {
    ENodeB *enb = *it;
    DownlinkPacketScheduler *scheduler =
        (DownlinkPacketScheduler *)enb->GetDLScheduler();
    assert(counter == enb->GetIDNetworkNode());
    schedulers.push_back(scheduler);
    counter += 1;
  }
  // set up schedulers
  bool available_flow = false;
  for (size_t j = 0; j < schedulers.size(); j++) {
    schedulers[j]->UpdateAverageTransmissionRate();
    schedulers[j]->SelectFlowsToSchedule();
    if (schedulers[j]->GetFlowsToSchedule()->size() > 0) {
      available_flow = true;
    }
    auto flows = schedulers[j]->GetFlowsToSchedule();
    if (GetTTICounter() < 102)
      std::cout << "cell: " << j << " n_flows: " << flows->size() << std::endl;
  }
  // if no flow is available in any slice, stop schedulers
  if (!available_flow) {
    for (size_t j = 0; j < schedulers.size(); j++)
      schedulers[j]->StopSchedule();
    return;
  }
  // Assume that every eNB has same number of RBs
  int nb_of_rbs = schedulers[0]
                      ->GetMacEntity()
                      ->GetDevice()
                      ->GetPhy()
                      ->GetBandwidthManager()
                      ->GetDlSubChannels()
                      .size();
  // int nb_of_rbs = no_of_schedulers * 4;
  // cout << "Number of RBS: " << nb_of_rbs << endl;
  // bool enable_comp = schedulers[0]->enable_comp_;

  if (schedulers[0]->enable_tune_weights_) {
    // ReassignUserToSlice(schedulers);
    schedulers[0]->enable_tune_weights_ = false;
  }

  if (schedulers[0]->enable_sliceserve_) {
    CalcSliceServedQuota(schedulers);
    int rb_id = 0;
    // for (int j = 0; j < schedulers.size(); j++){
    //   cout << "Cell: " << j << endl;
    //   for (int i = 0; i < schedulers[j]->rbs_to_slice_.size(); i++) {
    //     cout << "RB: " << i << " Slice: " << schedulers[0]->rbs_to_slice_[i]
    //     << endl;
    //   }
    // }

    // Assertion to check Cross Cell RB Linkage. But not ideal to use it since
    // in the case of unequal weights across cells, SS disables muting and
    // allows for RB to be allocated to different slices across cells. for (int
    // i = 0; i < nb_of_rbs; i++){
    //   int assigned_slice = schedulers[0]->rbs_to_slice_[i];
    //   for (int j = 0; j < schedulers.size(); j++){
    //     assert(schedulers[j]->rbs_to_slice_[i] == assigned_slice);
    // cout << "Assigned Slice at C0: " << assigned_slice << " C"
    // << j << ": " << schedulers[j]->rbs_to_slice_[i] << endl;
    // }
    // }

    while (rb_id < nb_of_rbs) {
      SliceServedAllocateOneRBExhaustMute(schedulers, rb_id);
      rb_id += RBG_SIZE;
    }

    for (int j = 0; j < schedulers.size(); j++) {
      RadioSaberDownlinkScheduler *scheduler =
          (RadioSaberDownlinkScheduler *)schedulers[j];
      std::vector<int> slice_rbg_count(scheduler->slice_ctx_.num_slices_, 0);
      std::vector<int> slice_rb_count(scheduler->slice_ctx_.num_slices_, 0);

      std::vector<int> rb_slice_map = scheduler->rbs_to_slice_;
      for (int i = 0; i < nb_of_rbs; i += 4) {
        slice_rbg_count[rb_slice_map[i]] += 4;
      }
      for (int i = 0; i < nb_of_rbs; i++) {
        slice_rb_count[rb_slice_map[i]] += 1;
      }

      for (int i = 0; i < scheduler->slice_ctx_.num_slices_; i++) {
        double rightful_share_rbs = 0;
        if (GetTTICounter() == 101) {
          rightful_share_rbs = scheduler->slice_ctx_.weights_[i] * nb_of_rbs;
        } else {
          rightful_share_rbs = slice_rb_count[i];
        }
        double assigned_share_rbs = slice_rb_count[i];
        double allocated_share_rbs = slice_rbg_count[i];
        // CARRY IT OVER HERE
        scheduler->rollover_slice_quota_[i] =
            rightful_share_rbs - allocated_share_rbs;
      }
    }
  } else {
    for (auto it = schedulers.begin(); it != schedulers.end(); it++) {
      // cout << "Scheduler Szie: " << schedulers.size() << endl;
      RadioSaberDownlinkScheduler *scheduler =
          (RadioSaberDownlinkScheduler *)(*it);
      scheduler->CalculateSliceQuota();
      if (schedulers[0]->current_tti_cost_) {
        scheduler->CalculateMetricsPerRB(GetTTICounter(), 0);
      }
    }
    int rb_id = 0;
    std::map<int, double> per_slice_metric_map;
    while (rb_id < nb_of_rbs) { // Allocate in the form of PRBG of size 4
      if (!schedulers[0]->enable_comp_) {
        SingleObjMutingExhaust(schedulers, rb_id);
      } else if (schedulers[0]->muting_system_ == 0) {
        SingleObjMutingExhaust(schedulers, rb_id);
      } else if (schedulers[0]->muting_system_ == 3) {
        MultiObjMuting(schedulers, rb_id);
      } else if (schedulers[0]->muting_system_ == 4) {
        std::map<int, double> per_slice_metric_map_per_rb =
            MultiObjMutingExhaustiveRealloc(schedulers, rb_id,
                                            per_slice_metric_map);
        // add past metric map to metric map
        for (auto it = per_slice_metric_map_per_rb.begin();
             it != per_slice_metric_map_per_rb.end(); it++) {
          per_slice_metric_map[it->first] += it->second;
        }
      } else {
        SingleObjMutingExhaust(schedulers, rb_id);
      }
      rb_id += RBG_SIZE;
    }

    std::map<int, double> slice_rollover_quotas;
    for (size_t j = 0; j < schedulers.size(); j++) {
      RadioSaberDownlinkScheduler *rs_scheduler =
          (RadioSaberDownlinkScheduler *)schedulers[j];
      for (int k = 0; k < schedulers[j]->slice_ctx_.num_slices_; k++) {
        rs_scheduler->slice_rbgs_quota_[k] =
            round(rs_scheduler->slice_rbgs_quota_[k] * 100000.) / 100000.;
        // cout << "Rolling over " << rs_scheduler->slice_rbgs_quota_[k] << "
        // for slice " << k << endl;
        slice_rollover_quotas[k] += rs_scheduler->slice_rbgs_quota_[k];
        rs_scheduler->rollover_slice_quota_[k] =
            rs_scheduler->slice_rbgs_quota_[k];
      }
    }
  }
  for (size_t j = 0; j < schedulers.size(); j++) {
    schedulers[j]->FinalizeScheduledFlows();
    schedulers[j]->StopSchedule();
  }
}

inline int provision_metric(double weight, double ideal_weight) {
  double weight_diff = weight - ideal_weight;
  double delta = 0.01;
  if (abs(weight_diff) < delta) {
    return 0;
  }
  if (weight_diff >= delta) {
    return 1;
  } else {
    return -1;
  }
}

inline bool within_range(double v) { return v >= 0 && v <= 1; }

void FrameManager::ReassignUserToSlice(
    std::vector<DownlinkPacketScheduler *> &schedulers) {
  // Seed with a real random value, if available
  std::random_device rd;

  // Independent random number generator for 6 or 7
  std::mt19937 gen1(rd());
  std::uniform_int_distribution<> dist1(6, 7);

  // Independent random number generator for 0 to 5
  std::mt19937 gen2(rd());
  std::uniform_int_distribution<> dist2(0, 5);

  for (size_t j = 0; j < schedulers.size(); j++) {
    int num_slices = schedulers[j]->slice_ctx_.num_slices_;
    auto flows = schedulers[j]->GetFlowsToSchedule();
    if (GetTTICounter() < 102)
      std::cout << "cell: " << j << " n_flows: " << flows->size() << std::endl;
    int slice_id = 0;
    for (auto it = flows->begin(); it != flows->end(); it++) {
      FlowToSchedule *flow = *it;
      int random1 = dist1(gen1);
      int random2 = dist2(gen2);
      // CODE TO FORCEFULLY ASSIGN GOOD OR BAD USERS TO A CERTAIN SLICE
      int bs_id = flow->GetBearer()->GetSource()->GetIDNetworkNode();
      CartesianCoordinates *ue_pos = flow->GetBearer()
                                         ->GetDestination()
                                         ->GetMobilityModel()
                                         ->GetAbsolutePosition();
      CartesianCoordinates *bs_pos = flow->GetBearer()
                                         ->GetSource()
                                         ->GetMobilityModel()
                                         ->GetAbsolutePosition();
      double distance = bs_pos->GetDistance(ue_pos);
      // cout << "Flow: " <<
      // flow->GetBearer()->GetDestination()->GetIDNetworkNode()
      //    << " Distance: " << distance << " BS ID: " << bs_id << endl;
      if ((bs_id == 0 && distance < 150) || (bs_id > 0 && distance < 50)) {
        flow->SetSliceID(random1);
        flow->GetBearer()->GetDestination()->SetSliceID(random1);
      } else {
        flow->SetSliceID(random2);
        flow->GetBearer()->GetDestination()->SetSliceID(random2);
        slice_id += 1;
      }
      cout << "Assigning Flow: "
           << flow->GetBearer()->GetDestination()->GetIDNetworkNode()
           << " of Cell " << flow->GetBearer()->GetSource()->GetIDNetworkNode()
           << " Index " << j << " to Slice " << flow->GetSliceID() << endl;
    }
  }
}

void FrameManager::UpdateNoiseInterferenceWithMute(
    std::vector<DownlinkPacketScheduler *> &schedulers, int rb_id,
    int mute_id) {
  if (mute_id == -1)
    return;
  for (int j = 0; j < (int)schedulers.size(); j++) {
    if (j != mute_id) {
      FlowsToSchedule *flows = schedulers[j]->GetFlowsToSchedule();
      for (auto it = flows->begin(); it != flows->end(); it++) {
        auto &rsrp_report = (*it)->GetRSRPReport();
        for (int i = rb_id; i < RBG_SIZE + rb_id; i++) {
          rsrp_report.noise_interfere_watt[i] -=
              pow(10., rsrp_report.rsrp_interference[mute_id][i] / 10.);
        }
      }
    }
  }
}

Allocation
FrameManager::DoAllocation(std::vector<DownlinkPacketScheduler *> &schedulers,
                           int rb_id, int mute_id, bool mute_cell,
                           std::map<int, int> slice_map,
                           std::vector<int> global_cells_muted,
                           std::vector<FlowToSchedule *> prior_cell_flow) {
  std::vector<FlowToSchedule *> cell_flow(schedulers.size(), nullptr);
  AMCModule *amc = schedulers[0]->GetMacEntity()->GetAmcModule();
  std::map<int, int> per_cell_slice_map;
  std::map<int, double> per_cell_metric_map;
  std::map<int, double> per_slice_metric_map;
  // std::vector<int> empty_cells_;
  double sum_metric = 0;
  double sum_tbs = 0;
  if (global_cells_muted.size() > 0) {
    assert(prior_cell_flow.size() > 0);
  }
  for (size_t i = 0; i < schedulers.size(); i++) {
    if (i == mute_id ||
        std::find(global_cells_muted.begin(), global_cells_muted.end(), i) !=
            global_cells_muted.end()) {
      continue;
    }
    FlowToSchedule *selected_flow = nullptr;
    RadioSaberDownlinkScheduler *scheduler =
        (RadioSaberDownlinkScheduler *)schedulers[i];
    FlowsToSchedule *flows = scheduler->GetFlowsToSchedule();
    int num_slice = scheduler->slice_ctx_.num_slices_;
    std::vector<FlowToSchedule *> slice_flow(num_slice, nullptr);
    std::vector<double> slice_spectraleff(num_slice, 0);
    std::vector<double> max_metrics(num_slice, 0);
    double max_slice_spectraleff = 0;
    double max_metric = 0;
    bool no_user_realloc = false;
    if (global_cells_muted.size() > 0) {
      // SET NO USER REALLOC TO FALSE PERMANENTLY IF YOU WANT TO ALLOW FOR USER
      // REALLOCATION
      no_user_realloc = true;
    }
    if (no_user_realloc) {
      assert(prior_cell_flow[i] != nullptr);
      selected_flow = prior_cell_flow[i];
      auto &rsrp_report = selected_flow->GetRSRPReport();
      double spectraleff_rbg = 0.0;
      if (mute_cell) {
        for (int j = rb_id; j < RBG_SIZE + rb_id; j++) {
          double in_under_mute = rsrp_report.noise_interfere_watt[j];
          in_under_mute -=
              pow(10.0, rsrp_report.rsrp_interference[mute_id][j] / 10.);
          int cqi_under_mute = amc->GetCQIFromSinr(rsrp_report.rx_power[j] -
                                                   10. * log10(in_under_mute));
          spectraleff_rbg += amc->GetEfficiencyFromCQI(cqi_under_mute);
        }
      } else {
        for (int j = rb_id; j < RBG_SIZE + rb_id; j++) {
          double int_noise = rsrp_report.noise_interfere_watt[j];
          int cqi = amc->GetCQIFromSinr(rsrp_report.rx_power[j] -
                                        10. * log10(int_noise));
          selected_flow->GetCqiFeedbacks()[j] = cqi;         // **
          spectraleff_rbg += amc->GetEfficiencyFromCQI(cqi); // This returns TBS
        }
      }
      double metric = scheduler->ComputeSchedulingMetric(
          selected_flow->GetBearer(), spectraleff_rbg, rb_id);
      max_slice_spectraleff = spectraleff_rbg;
      max_metric = metric;
    } else {
      for (auto it = flows->begin(); it != flows->end(); it++) {
        FlowToSchedule *flow = *it;
        auto &rsrp_report = flow->GetRSRPReport();
        double spectraleff_rbg = 0.0;
        if (mute_cell) {
          assert(mute_id != -1);
          for (int j = rb_id; j < RBG_SIZE + rb_id; j++) {
            double in_under_mute = rsrp_report.noise_interfere_watt[j];
            in_under_mute -=
                pow(10.0, rsrp_report.rsrp_interference[mute_id][j] / 10.);
            int cqi_under_mute = amc->GetCQIFromSinr(
                rsrp_report.rx_power[j] - 10. * log10(in_under_mute));
            spectraleff_rbg += amc->GetEfficiencyFromCQI(cqi_under_mute);
          }
        } else {
          for (int j = rb_id; j < RBG_SIZE + rb_id; j++) {
            double int_noise = rsrp_report.noise_interfere_watt[j];
            int cqi = amc->GetCQIFromSinr(rsrp_report.rx_power[j] -
                                          10. * log10(int_noise));
            flow->GetCqiFeedbacks()[j] = cqi; // **
            spectraleff_rbg +=
                amc->GetEfficiencyFromCQI(cqi); // This returns TBS
          }
        }
        double metric = scheduler->ComputeSchedulingMetric(
            flow->GetBearer(), spectraleff_rbg, rb_id);
        int slice_id = flow->GetSliceID();
        if (metric > max_metrics[slice_id]) {
          max_metrics[slice_id] = metric;
          slice_flow[slice_id] = flow;
          slice_spectraleff[slice_id] = spectraleff_rbg;
        }
      }
      max_slice_spectraleff = 0;
      max_metric = 0;
      for (int k = 0; k < slice_spectraleff.size(); k++) {
        if (slice_spectraleff[k] == 0.0) {
          // std::cout << "T: " << GetTTICounter()
          // << " RB: " << rb_id
          // << " Warn: slice " << k
          // << " has no user in cell " << i << std::endl;
        }
      }
      if (slice_map.size() == 0) {
        for (int j = 0; j < num_slice; j++) {
          if (slice_spectraleff[j] > max_slice_spectraleff &&
              scheduler->slice_rbgs_quota_[j] > 0) {
            max_slice_spectraleff = slice_spectraleff[j];
            max_metric = max_metrics[j];
            selected_flow = slice_flow[j];
          }
        }
        if (selected_flow == nullptr) {
          // cout << "Insufficient quota for all slices at Cell: " << i << endl;
          for (int j = 0; j < num_slice; j++) {
            if (slice_spectraleff[j] > max_slice_spectraleff) {
              max_slice_spectraleff = slice_spectraleff[j];
              max_metric = max_metrics[j];
              selected_flow = slice_flow[j];
            }
          }
          // cout << "Allocted to Flow: " <<
          // selected_flow->GetBearer()->GetDestination()->GetIDNetworkNode() <<
          // endl; empty_cells_.push_back(i);
        }
      } else {
        max_slice_spectraleff = slice_spectraleff[slice_map[i]];
        max_metric = max_metrics[slice_map[i]];
        selected_flow = slice_flow[slice_map[i]];
      }
    }

    assert(selected_flow);
    cell_flow[i] = selected_flow;
    int slice_policy =
        schedulers[i]->slice_ctx_.algo_params_[selected_flow->GetSliceID()].psi;
    if (slice_policy == 0) { // Max TP
      per_cell_metric_map[i] = max_slice_spectraleff;
    } else {
      per_cell_metric_map[i] = max_metric;
    }
    sum_metric += max_metric;
    sum_tbs += max_slice_spectraleff;
  }

  for (int i = 0; i < cell_flow.size(); i++) {
    if (cell_flow[i] != nullptr) {
      per_cell_slice_map[i] = cell_flow[i]->GetSliceID();
      per_slice_metric_map[cell_flow[i]->GetSliceID()] +=
          per_cell_metric_map[i];
    } else {
      assert(i == mute_id);
      per_cell_slice_map[i] = -1;
    }
  }
  if (slice_map.size() > 0) {
    for (auto &t : per_cell_metric_map) {
      int cell_id = t.first;
      double metric = t.second;
      assert(per_cell_slice_map[cell_id] == slice_map[cell_id]);
    }
  }

  Allocation result;
  result.cell_flow = cell_flow;
  result.sum_metric = sum_metric;
  result.sum_tbs = sum_tbs;
  result.per_cell_metric_map = per_cell_metric_map;
  result.per_cell_slice_map = per_cell_slice_map;
  result.per_slice_metric_map = per_slice_metric_map;
  // result.empty_cells = empty_cells_;
  return result;
}

void FrameManager::CalcSliceServedQuota(
    std::vector<DownlinkPacketScheduler *> &schedulers) {
  // assume only the macrocell(0th) and other cells are interfered
  const int num_slices = schedulers[0]->slice_ctx_.num_slices_;
  std::vector<std::pair<int, double>> linking_index(num_slices, {0, 0});
  for (int i = 0; i < num_slices; i++) {
    double total_index = 0;
    double macro_weight = schedulers[0]->slice_ctx_.weights_[i];
    for (size_t j = 1; j < schedulers.size(); j++) {
      double small_weight = schedulers[j]->slice_ctx_.weights_[i];
      total_index +=
          (macro_weight > small_weight ? small_weight : macro_weight);
    }
    linking_index[i].first = i;
    linking_index[i].second = total_index;
  }
  sort(
      linking_index.begin(), linking_index.end(),
      [](const auto &a, const auto &b) -> bool { return a.second > b.second; });
  int nb_of_rbs = schedulers[0]
                      ->GetMacEntity()
                      ->GetDevice()
                      ->GetPhy()
                      ->GetBandwidthManager()
                      ->GetDlSubChannels()
                      .size();
  for (size_t i = 0; i < schedulers.size(); i++) {
    schedulers[i]->rbs_to_slice_.resize(nb_of_rbs, 0);
  }
  std::vector<int> cells_begin_id(schedulers.size(), 0);
  for (size_t i = 0; i != linking_index.size(); i++) {
    int sid = linking_index[i].first;
    for (size_t cid = 0; cid != schedulers.size(); cid++) {
      DownlinkPacketScheduler *scheduler = schedulers[cid];
      double slice_rbs =
          ((double)nb_of_rbs * scheduler->slice_ctx_.weights_[sid]);
      RadioSaberDownlinkScheduler *rs_scheduler =
          (RadioSaberDownlinkScheduler *)scheduler;
      slice_rbs += rs_scheduler->rollover_slice_quota_[sid];
      rs_scheduler->rollover_slice_quota_[sid] = 0;
      for (int i = 0; i < slice_rbs; i++) {
        scheduler->rbs_to_slice_[cells_begin_id[cid] + i] = sid;
      }
      cells_begin_id[cid] += slice_rbs;
      if (i == (linking_index.size() - 1)) {
        // The quota RBs of every slice can be fractional, round it up in the
        // last slice
        int sid = GetTTICounter() % scheduler->slice_ctx_.num_slices_;
        for (int i = cells_begin_id[cid]; i < nb_of_rbs; i++) {
          scheduler->rbs_to_slice_[i] = sid;
        }
      }
    }
  }
}

void FrameManager::SliceServedAllocateOneRBExhaustMute(
    std::vector<DownlinkPacketScheduler *> &schedulers, int rb_id) {
  const int slice_serve = schedulers[0]->rbs_to_slice_[rb_id];
  bool enable_comp = schedulers[0]->enable_comp_;
  for (auto it = schedulers.begin(); it != schedulers.end(); it++) {
    if ((*it)->rbs_to_slice_[rb_id] != slice_serve) {
      enable_comp = false;
    }
  }
  AMCModule *amc = schedulers[0]->GetMacEntity()->GetAmcModule();
  std::unordered_set<int> global_cells_muted;
  std::vector<FlowToSchedule *> global_cell_flow;
  const int id_no_mute = schedulers.size();
  while (true) {
    int final_mute_id = -1;
    int mute_id;
    if (enable_comp) {
      mute_id = 0;
    } else {
      mute_id = id_no_mute;
    }
    std::vector<int> mute_ids;
    std::vector<double> sum_metric_under_mute;
    std::vector<std::vector<FlowToSchedule *>> cell_flow_under_mute;
    for (; mute_id <= (int)schedulers.size(); mute_id++) {
      if (global_cells_muted.count(mute_id)) {
        continue;
      }
      // the aggregate tbs or pf-metric across cells
      double sum_metric = 0;
      std::vector<FlowToSchedule *> cell_flow(schedulers.size(), nullptr);
      for (size_t i = 0; i < schedulers.size(); i++) {
        int cell_id = i;
        RadioSaberDownlinkScheduler *scheduler =
            (RadioSaberDownlinkScheduler *)schedulers[cell_id];
        int cell_slice = scheduler->rbs_to_slice_[rb_id];
        if (cell_id == mute_id || global_cells_muted.count(cell_id)) {
          continue;
        }
        FlowsToSchedule *flows = scheduler->GetFlowsToSchedule();
        double max_metric = -1;
        FlowToSchedule *chosen_flow = nullptr;
        for (auto it = flows->begin(); it != flows->end(); it++) {
          FlowToSchedule *flow = *it;
          // exclusively for this slice, no inter-slice scheduler
          if (flow->GetSliceID() != cell_slice) {
            continue;
          }
          auto &rsrp_report = flow->GetRSRPReport();
          double tbs = 0.0;
          for (int i = rb_id; i < RBG_SIZE + rb_id; i++) {
            double in_under_mute = rsrp_report.noise_interfere_watt[i];
            if (mute_id != id_no_mute) {
              in_under_mute -=
                  pow(10.0, rsrp_report.rsrp_interference[mute_id][i] / 10.);
            }
            int cqi_under_mute = amc->GetCQIFromSinr(
                rsrp_report.rx_power[i] - 10. * log10(in_under_mute));
            tbs += amc->GetEfficiencyFromCQI(cqi_under_mute);
          }
          double metric =
              scheduler->ComputeSchedulingMetric(flow->GetBearer(), tbs, rb_id);
          if (metric > max_metric) {
            max_metric = metric;
            chosen_flow = flow;
          }
        }
        if (chosen_flow) {
          cell_flow[cell_id] = chosen_flow;
          sum_metric += max_metric;
        } else {
          enable_comp = false;
        }
      }
      mute_ids.push_back(mute_id);
      sum_metric_under_mute.push_back(sum_metric);
      cell_flow_under_mute.push_back(cell_flow);
    }
    auto it = find(mute_ids.begin(), mute_ids.end(), id_no_mute);
    int no_mute_index = it - mute_ids.begin();
    double no_mute_metric = sum_metric_under_mute[no_mute_index];
    // Calculate the percentage improvement over the no muting scenario
    std::vector<double> percentage_improvements;
    for (size_t i = 0; i < mute_ids.size(); i++) {
      double metric_percentage_improvement =
          (double)(sum_metric_under_mute[i] - no_mute_metric) / no_mute_metric;
      percentage_improvements.push_back(metric_percentage_improvement);
    }
    double max = *max_element(percentage_improvements.begin(),
                              percentage_improvements.end());
    auto it_2 = find(percentage_improvements.begin(),
                     percentage_improvements.end(), max);
    int id = it_2 - percentage_improvements.begin();
    final_mute_id = mute_ids[id];
    if (final_mute_id == id_no_mute) {
      global_cell_flow = cell_flow_under_mute[id];
      break;
    } else if (enable_comp) {
      global_cell_flow = cell_flow_under_mute[id];
      global_cells_muted.insert(final_mute_id);
      UpdateNoiseInterferenceWithMute(schedulers, rb_id, final_mute_id);
#ifdef MUTING_FREQ_GAIN_DEBUG
      std::cout << "Muting Decision: " << rb_id
                << " - Muting Cell: " << final_mute_id
                << " Percentage Improvement: " << it_2[0] << std::endl;
#endif
      break;
    }
  }
  for (size_t j = 0; j < schedulers.size(); j++) {
    if (global_cells_muted.count(j)) {
      continue;
    }
    RadioSaberDownlinkScheduler *scheduler =
        (RadioSaberDownlinkScheduler *)schedulers[j];
    FlowToSchedule *flow = global_cell_flow[j];
    if (!flow) {
      scheduler->slice_rbgs_quota_[scheduler->rbs_to_slice_[rb_id]] -= 1;
      continue;
    }
    auto &rsrp_report = flow->GetRSRPReport();
    // under the current muting assumption
    for (int i = rb_id; i < RBG_SIZE + rb_id; i++) {
      double in_under_mute = rsrp_report.noise_interfere_watt[i];
      int cqi_under_mute = amc->GetCQIFromSinr(rsrp_report.rx_power[i] -
                                               10. * log10(in_under_mute));
      flow->GetCqiFeedbacks()[i] = cqi_under_mute;
    }
    scheduler->slice_rbgs_quota_[flow->GetSliceID()] -= 1;
    for (int i = rb_id; i < RBG_SIZE + rb_id; i++) {
      flow->GetListOfAllocatedRBs()->push_back(i);
    }
  }
}

std::vector<int> GetMutingOrder(int num_cells, bool random) {
  vector<int> cell_muting_order;
  for (int i = 0; i < num_cells; i++) {
    cell_muting_order.push_back(i);
  }
  if (random) {
    std::random_device rd;
    std::default_random_engine rng(1);
    std::shuffle(cell_muting_order.begin(), cell_muting_order.end(), rng);
  }
  return cell_muting_order;
}

std::map<int, double>
FrameManager::GetMetricsOnMutedCell(DownlinkPacketScheduler *muted_cell,
                                    std::map<int, double> &rbgs_deduction,
                                    int rb_id) {
  RadioSaberDownlinkScheduler *scheduler =
      (RadioSaberDownlinkScheduler *)muted_cell;
  FlowsToSchedule *flows = scheduler->GetFlowsToSchedule();
  const int num_slice = scheduler->slice_ctx_.num_slices_;
  AMCModule *amc = scheduler->GetMacEntity()->GetAmcModule();
  int nb_of_rbs = scheduler->GetMacEntity()
                      ->GetDevice()
                      ->GetPhy()
                      ->GetBandwidthManager()
                      ->GetDlSubChannels()
                      .size();
  std::vector<double> slice_sum_metrics(num_slice, 0);
  std::vector<double> slice_rbgs_quota(scheduler->slice_rbgs_quota_);
  for (auto it = rbgs_deduction.begin(); it != rbgs_deduction.end(); it++) {
    slice_rbgs_quota[it->first] -= it->second;
  }
  while (rb_id < nb_of_rbs) {
    std::vector<FlowToSchedule *> slice_flow(num_slice, nullptr);
    std::vector<double> slice_spectraleff(num_slice, -1);
    std::vector<double> max_metrics(num_slice, -1);
    for (auto it = flows->begin(); it != flows->end(); it++) {
      FlowToSchedule *flow = *it;
      auto &rsrp_report = flow->GetRSRPReport();
      // under the current muting assumption
      double spectraleff_rbg = 0.0;
      for (int j = rb_id; j < RBG_SIZE + rb_id; j++) {
        double int_noise =
            rsrp_report.noise_interfere_watt[j]; // Interference under Muting
        int cqi = amc->GetCQIFromSinr(rsrp_report.rx_power[j] -
                                      10. * log10(int_noise));
        spectraleff_rbg += amc->GetEfficiencyFromCQI(cqi); // This returns TBS
      }
      double metric = scheduler->ComputeSchedulingMetric(
          flow->GetBearer(), spectraleff_rbg, rb_id);
      int slice_id = flow->GetSliceID();
      // enterprise schedulers
      if (metric > max_metrics[slice_id]) {
        max_metrics[slice_id] = metric;
        slice_flow[slice_id] = flow;
        slice_spectraleff[slice_id] = spectraleff_rbg;
      }
    }
    double max_slice_spectraleff = -1;
    int selected_slice = -1;
    double slice_metric = -1;
    for (int j = 0; j < num_slice; j++) {
      if (slice_spectraleff[j] > max_slice_spectraleff &&
          slice_rbgs_quota[j] >= 1) {
        max_slice_spectraleff = slice_spectraleff[j];
        selected_slice = j;
        slice_metric = max_metrics[j];
      }
    }
    // no slice has more than one RB quota, allocate to a slice with fractional
    if (selected_slice == -1) {
      for (int j = 0; j < num_slice; j++) {
        if (slice_spectraleff[j] > max_slice_spectraleff &&
            slice_rbgs_quota[j] >= 0) {
          max_slice_spectraleff = slice_spectraleff[j];
          selected_slice = j;
          slice_metric = max_metrics[j];
        }
      }
    }
    slice_rbgs_quota[selected_slice] -= 1;
    slice_sum_metrics[selected_slice] += slice_metric;
    rb_id += RBG_SIZE;
  }
  std::map<int, double> benefit_slice_metrics;
  for (const auto &pair : rbgs_deduction) {
    benefit_slice_metrics[pair.first] = slice_sum_metrics[pair.first];
  }
  return benefit_slice_metrics;
}

void FrameManager::SingleObjMutingExhaust(
    std::vector<DownlinkPacketScheduler *> &schedulers, int rb_id) {
  std::vector<FlowToSchedule *> cell_flow(schedulers.size(), nullptr);
  AMCModule *amc = schedulers[0]->GetMacEntity()->GetAmcModule();
  std::unordered_set<int> global_cells_muted;
  std::vector<FlowToSchedule *>
      global_cell_flow; // WHich flow will be scheduled for this RB
  // std::vector<int> global_empty_cells;
  const int index_no_mute = schedulers.size();
  bool do_realloc = schedulers[0]->do_reallocation_;

  double tbs_weight = schedulers[0]->max_tp_weight_;
  double pf_metric_weight = 1 - tbs_weight;
  double total_quota = 0.;

  while (true) {
    int final_mute_id = -1;
    double max_sum_tbs = 0;
    double max_sum_metric = 0;
    std::vector<FlowToSchedule *> max_cell_flow;
    // std::vector<int> max_empty_cells;
    int mute_id;
    if (schedulers[0]->enable_comp_) { // if enable_comp, start from every
                                       // possible muting cell
      mute_id = 0;
    } else { // if not enable_comp, start from schedulers.size() which means no
             // muting
      if (GetTTICounter() < 102) {
        cout << "No Muting\n";
      }
      mute_id = index_no_mute;
    }
    std::vector<int> mute_ids;
    std::vector<double> sum_tbs_under_mute;
    std::vector<double> sum_metric_under_mute;
    std::vector<std::vector<FlowToSchedule *>> cell_flow_under_mute;
    // std::vector<std::vector<int>> empty_cells_under_mute;
    for (; mute_id <= (int)schedulers.size(); mute_id++) {
      // already muted
      // cout << "Testing Muting: " << mute_id << endl;
      if (global_cells_muted.count(mute_id)) {
        continue;
      }
      double sum_tbs = 0;
      double sum_metric = 0;
      std::vector<FlowToSchedule *> cell_flow(schedulers.size(), nullptr);
      // std::vector<int> empty_cells;
      // capture the slice that benefits most from muting(across all cells)
      for (int j = 0; j < (int)schedulers.size(); j++) {
        RadioSaberDownlinkScheduler *scheduler =
            (RadioSaberDownlinkScheduler *)schedulers[j];
        // skip the muted cell
        if (j == mute_id || global_cells_muted.count(j)) {
          continue;
        }
        FlowsToSchedule *flows = scheduler->GetFlowsToSchedule();
        int num_slice = scheduler->slice_ctx_.num_slices_;
        // enterprise scheduling for every slice
        // RadioSaber Logic Starts Here
        std::vector<FlowToSchedule *> slice_flow(num_slice, nullptr);
        std::vector<double> slice_spectraleff(num_slice, -1);
        std::vector<double> max_metrics(num_slice, -1);
        std::vector<double> max_pf_metrics(num_slice, -1);
        // Radiosaber Logic
        for (auto it = flows->begin(); it != flows->end(); it++) {
          FlowToSchedule *flow = *it;
          auto &rsrp_report = flow->GetRSRPReport();
          // under the current muting assumption
          double spectraleff_rbg = 0.0;
          for (int i = rb_id; i < RBG_SIZE + rb_id; i++) {
            double in_under_mute =
                rsrp_report
                    .noise_interfere_watt[i]; // Interference under Muting
            if (mute_id != index_no_mute) {
              // Muting one cell would change the SINR of every user in every
              // cell
              in_under_mute -=
                  pow(10., rsrp_report.rsrp_interference[mute_id][i] / 10.);
            }
            int cqi_under_mute = amc->GetCQIFromSinr(
                rsrp_report.rx_power[i] - 10. * log10(in_under_mute));
            spectraleff_rbg +=
                amc->GetEfficiencyFromCQI(cqi_under_mute); // This returns TBS
          }
          double metric = scheduler->ComputeSchedulingMetric(
              flow->GetBearer(), spectraleff_rbg, rb_id);
          double historical =
              (flow->GetBearer()->GetAverageTransmissionRate()) / 1000.;
          double instantaneous = spectraleff_rbg;
          double pf_metric_for_flow = instantaneous / historical;
          if (!schedulers[0]->enable_comp_) {
            pf_metric_for_flow = metric;
          }
          if (scheduler->user_weights) {
            int slice_id = flow->GetSliceID();
            SchedulerAlgo param = scheduler->slice_ctx_.algo_params_[slice_id];
            if (flow->GetBearer()->GetPriority() > 1) {
              if (param.psi == 0) {
                spectraleff_rbg *= flow->GetBearer()->GetPriority();
              } else {
                pf_metric_for_flow *= flow->GetBearer()->GetPriority();
              }
            }
          }
          int slice_id = flow->GetSliceID();
          // enterprise schedulers
          if (metric > max_metrics[slice_id]) {
            max_metrics[slice_id] = metric;
            slice_flow[slice_id] = flow;
            slice_spectraleff[slice_id] = spectraleff_rbg;
            max_pf_metrics[slice_id] = pf_metric_for_flow;
          }
        }
        double max_slice_spectraleff = -1;
        double max_pf_metric = -1;
        FlowToSchedule *selected_flow = nullptr;
        for (int i = 0; i < num_slice; i++) {
          if (slice_spectraleff[i] <= 0) {
            // std::cout << "Warn: slice " << std::to_string(i)
            // << " has no user in cell " << std::to_string(j) << std::endl;
          }
          if (slice_spectraleff[i] > max_slice_spectraleff &&
              scheduler->slice_rbgs_quota_[i] >= 1) {
            max_slice_spectraleff = slice_spectraleff[i];
            // max_pf_metric = max_metrics[i];
            max_pf_metric = max_pf_metrics[i];
            selected_flow = slice_flow[i];
          }
        }
        if (selected_flow == nullptr) {
          // cout << "Insufficient quota for all slices at Cell: " << j << " RB:
          // " << rb_id << endl;
          vector<int> eligible_slices;
          for (int i = 0; i < scheduler->slice_ctx_.num_slices_; i++) {
            if (scheduler->slice_rbgs_quota_[i] > 0) {
              eligible_slices.push_back(i);
            }
          }
          for (int k = 0; k < eligible_slices.size(); k++) {
            int selected_slice = eligible_slices[k];
            selected_flow = slice_flow[selected_slice];
            max_slice_spectraleff = slice_spectraleff[selected_slice];
            max_pf_metric = max_pf_metrics[selected_slice];
            if (selected_flow != nullptr) {
              break;
            }
          }
          if (selected_flow == nullptr) {
            cout << "No eligible slice found\n";
            while (selected_flow == nullptr) {
              int selected_slice = rand() % scheduler->slice_ctx_.num_slices_;
              selected_flow = slice_flow[selected_slice];
              max_slice_spectraleff = slice_spectraleff[selected_slice];
              max_pf_metric = max_pf_metrics[selected_slice];
            }
          }
          // empty_cells.push_back(j);
          // cout << "Allocated to Flow: " <<
          // selected_flow->GetBearer()->GetDestination()->GetIDNetworkNode() <<
          // endl;
        }
        assert(selected_flow);
        cell_flow[j] = selected_flow;
        sum_tbs += max_slice_spectraleff;
        sum_metric += max_pf_metric;
      }
      mute_ids.push_back(mute_id);
      sum_tbs_under_mute.push_back(sum_tbs);
      sum_metric_under_mute.push_back(sum_metric);
      cell_flow_under_mute.push_back(cell_flow);
      // empty_cells_under_mute.push_back(empty_cells);
    }
    // Calculate the TP, PF with No Muting/No further muting scenario.
    auto it = find(mute_ids.begin(), mute_ids.end(), index_no_mute);
    int no_mute_index = it - mute_ids.begin();
    double no_mute_tbs = sum_tbs_under_mute[no_mute_index];
    double no_mute_pf = sum_metric_under_mute[no_mute_index];

    // Calculate the percentage improvement over the no muting scenario
    std::vector<double> percentage_improvements;
    for (size_t i = 0; i < mute_ids.size(); i++) {
      max_sum_tbs = sum_tbs_under_mute[i];
      max_sum_metric = sum_metric_under_mute[i];
      double tbs_percentage_improvement =
          (double)(sum_tbs_under_mute[i] - no_mute_tbs) / no_mute_tbs;
      double pf_percentage_improvement =
          (double)(sum_metric_under_mute[i] - no_mute_pf) / no_mute_pf;
      percentage_improvements.push_back(
          (tbs_weight * tbs_percentage_improvement) +
          (pf_metric_weight * pf_percentage_improvement));
    }

    // Find the index of the maximum percentage improvement
    double max = *max_element(percentage_improvements.begin(),
                              percentage_improvements.end());
    auto it_2 = find(percentage_improvements.begin(),
                     percentage_improvements.end(), max);
    int index_max_percentage_improvement =
        it_2 - percentage_improvements.begin();
    final_mute_id = mute_ids[index_max_percentage_improvement];

    max_cell_flow = cell_flow_under_mute[index_max_percentage_improvement];
    max_sum_tbs = sum_tbs_under_mute[index_max_percentage_improvement];
    max_sum_metric = sum_metric_under_mute[index_max_percentage_improvement];
    // max_empty_cells =
    // empty_cells_under_mute[index_max_percentage_improvement];

    if (final_mute_id == index_no_mute) { // We stop muting any further
      global_cell_flow = max_cell_flow;
      // terminate the loop since no more benefit by muting a cell
      break;
    } else { // We continue muting the next cell and update the noise and
             // interfernce for every user
      global_cell_flow = max_cell_flow;
      // global_empty_cells = max_empty_cells;
      global_cell_flow[final_mute_id] = nullptr;
#ifdef REALLOCATION_DEBUG
      cout << "Initial Allocation: " << global_cell_flow.size() << endl;
      for (int i = 0; i < global_cell_flow.size(); i++) {
        if (global_cell_flow[i] == nullptr) {
          cout << i << " is muted\n";
        } else {
          cout << "RB ID: " << rb_id << " Cell " << i << " UE: "
               << global_cell_flow[i]
                      ->GetBearer()
                      ->GetDestination()
                      ->GetIDNetworkNode()
               << endl;
        }
      }
#endif

      global_cells_muted.insert(final_mute_id);
#ifdef MUTING_FREQ_GAIN_DEBUG
      std::cout << "Muting Decision: " << rb_id
                << " - Muting Cell: " << final_mute_id
                << " Percentage Improvement: "
                << percentage_improvements[index_max_percentage_improvement]
                << endl;
#endif
      // update the noise_interference report of all users in all cells
      UpdateNoiseInterferenceWithMute(schedulers, rb_id, final_mute_id);
    }
  }
  for (size_t j = 0; j < schedulers.size(); j++) {
    if (global_cells_muted.count(j)) {
      continue;
    }
    RadioSaberDownlinkScheduler *scheduler =
        (RadioSaberDownlinkScheduler *)schedulers[j];
    FlowToSchedule *flow = global_cell_flow[j];
    auto &rsrp_report = flow->GetRSRPReport();
    for (int i = rb_id; i < RBG_SIZE + rb_id; i++) {
      double in_under_mute = rsrp_report.noise_interfere_watt[i];
      int cqi_under_mute = amc->GetCQIFromSinr(rsrp_report.rx_power[i] -
                                               10. * log10(in_under_mute));
      flow->GetCqiFeedbacks()[i] = cqi_under_mute;
    }
    // if j in global_empty_cells, no quota deduction
    // if (find(global_empty_cells.begin(), global_empty_cells.end(), j) ==
    // global_empty_cells.end()){
    scheduler->slice_rbgs_quota_[flow->GetSliceID()] -= 1;
    // } else {
    // cout << "Cell: " << j
    // << " allocted for Free to Flow: " <<
    // flow->GetBearer()->GetDestination()->GetIDNetworkNode()
    // << " Slice: " << flow->GetSliceID() << endl;
    // }
    // cout << "Cell " << j << " allocated to Slice: " << flow->GetSliceID() <<
    // " Deducting -1" << endl;
    for (int i = rb_id; i < RBG_SIZE + rb_id; i++) {
      flow->GetListOfAllocatedRBs()->push_back(i);
    }
  }

#ifdef REALLOCATION_DEBUG
  cout << "\nFinal Allocation: " << endl;
  for (int i = 0; i < global_cell_flow.size(); i++) {
    if (global_cell_flow[i] == nullptr) {
      cout << i << " is muted\n";
    } else {
      cout << "RB ID: " << rb_id << " Cell " << i << " UE: "
           << global_cell_flow[i]
                  ->GetBearer()
                  ->GetDestination()
                  ->GetIDNetworkNode()
           << endl;
    }
  }
#endif

  for (auto it = global_cells_muted.begin(); it != global_cells_muted.end();
       it++) {
    int cell_id = *it;
    RadioSaberDownlinkScheduler *scheduler =
        (RadioSaberDownlinkScheduler *)schedulers[cell_id];
    double deduction = 1. / scheduler->slice_ctx_.num_slices_;
    // cout << "Deduction: " << deduction << endl;
    for (int i = 0; i < scheduler->slice_ctx_.num_slices_; i++) {
      // cout << "Deducting " << deduction <<
      // " from Slice: " << i << " at Cell " <<
      // scheduler->GetMacEntity()->GetDevice()->GetIDNetworkNode() << endl;
      scheduler->slice_rbgs_quota_[i] -= deduction;
    }
  }
}

std::map<int, double>
GetPerSliceDeduction(std::map<int, double> per_slice_metric_no_mute,
                     std::map<int, double> per_slice_metric_mute) {
  bool proportionate_to_benefit_deduction = false;
  double total_percentage_gain = 0.0;
  std::map<int, double> per_slice_percentage_gain_map;
  std::map<int, double> per_slice_cost_map;

  for (auto const pair : per_slice_metric_no_mute) {
    double per_slice_percentage_gain = (per_slice_metric_mute[pair.first] -
                                        per_slice_metric_no_mute[pair.first]) /
                                       per_slice_metric_no_mute[pair.first];
    if (per_slice_percentage_gain > 0) {
      per_slice_percentage_gain_map[pair.first] = per_slice_percentage_gain;
      total_percentage_gain += per_slice_percentage_gain;
    }
  }

  if (proportionate_to_benefit_deduction) {
    double total = 0;
    for (auto const pair : per_slice_percentage_gain_map) {
      per_slice_cost_map[pair.first] = pair.second / total_percentage_gain;
      total += pair.second / total_percentage_gain;
    }
    assert(total - 1 < 0.001);
  } else {
    double mean_cost = 1.0 / per_slice_percentage_gain_map.size();

    for (auto const pair : per_slice_percentage_gain_map) {
      per_slice_cost_map[pair.first] = mean_cost;
    }
  }

  // cout << "Per Slice Cost Map\n";
  // for (auto const &t: per_slice_cost_map){
  //   cout << t.first << " " << t.second << endl;
  // }
  return per_slice_cost_map;
}

double GetMutingScore(std::map<int, double> per_slice_metric_no_mute,
                      std::map<int, double> per_slice_metric_mute,
                      RadioSaberDownlinkScheduler *muted_cell) {
  std::map<int, double> per_slice_metric_gain_map;
  double muting_score = 0;

  std::vector<int> keys_to_remove;
  for (auto const pair : per_slice_metric_no_mute) {
    double per_slice_metric_gain = per_slice_metric_mute[pair.first] -
                                   per_slice_metric_no_mute[pair.first];
    assert(per_slice_metric_gain >= 0);
    if (per_slice_metric_gain > 0) {
      per_slice_metric_gain_map[pair.first] = per_slice_metric_gain;
    } else {
      keys_to_remove.push_back(pair.first);
    }
  }

  for (int i = 0; i < keys_to_remove.size(); i++) {
    per_slice_metric_no_mute.erase(keys_to_remove[i]);
    per_slice_metric_mute.erase(keys_to_remove[i]);
  }
  double mean_cost = 1.0 / per_slice_metric_gain_map.size();
  std::map<int, double> per_slice_cost_map =
      GetPerSliceDeduction(per_slice_metric_no_mute, per_slice_metric_mute);

  // cout << "Map Size: " << per_slice_metric_gain_map.size() << " Mean Cost: "
  // << mean_cost << endl;
  int counter = 0;
  size_t size1 = per_slice_metric_gain_map.size();
  size_t size2 = per_slice_metric_no_mute.size();
  size_t size3 = per_slice_metric_mute.size();
  size_t size4 = per_slice_cost_map.size();

  bool all_sizes_equal =
      (size1 == size2) && (size2 == size3) && (size3 == size4);

  while (counter < per_slice_metric_gain_map.size()) {
    if (!all_sizes_equal) {
      cout << "Per Slice Metric Gain: " << per_slice_metric_gain_map.size()
           << " Per Slice Metric No Mute: " << per_slice_metric_no_mute.size()
           << " Per Slice Metric Mute: " << per_slice_metric_mute.size()
           << " Per Slice Cost: " << per_slice_cost_map.size() << endl;
      assert(0 == 1);
    }
    for (auto &pair : per_slice_metric_gain_map) {
      // if (per_slice_cost_map[pair.first] != mean_cost){
      //   cout << "Per Slice Cost: " << per_slice_cost_map[pair.first] << "
      //   Mean Cost: " << mean_cost << endl; assert(0==1);
      // }
      // double slice_loss = (muted_cell->metrics_perrbg_slice_[pair.first]) *
      // per_slice_cost_map[pair.first];
      double slice_gain = pair.second;
      // cout << "Gaining Slice: " << pair.first << " Gain: " << pair.second <<
      // " Cost: " << per_slice_cost_map[pair.first] << endl;
      double slice_loss = muted_cell->GetLossMetric(
          pair.first, per_slice_cost_map[pair.first], false);
      if (slice_loss == 0) {
        // Meaning Slice has not Quota on this Cell, then slice_score = 0
        slice_gain = 0;
      }
      // double metric_per_rbg = muted_cell->GetSliceRollingAverage(pair.first);
      // double slice_loss = metric_per_rbg * mean_cost;
      double slice_score = slice_gain / slice_loss;
      if (slice_score > 1) {
        muting_score += slice_score;
        counter++;
      } else {
        per_slice_metric_gain_map.erase(pair.first);
        mean_cost = 1.0 / per_slice_metric_gain_map.size();
        per_slice_metric_mute.erase(pair.first);
        per_slice_metric_no_mute.erase(pair.first);
        per_slice_cost_map = GetPerSliceDeduction(per_slice_metric_no_mute,
                                                  per_slice_metric_mute);
        // cout << "Map Size: " << per_slice_metric_gain_map.size() << " Mean
        // Cost: " << mean_cost << endl;
        counter = 0;
        muting_score = 0;
        break;
      }
    }
  }

  return muting_score;
}

std::vector<int> FrameManager::GetExhaustiveMuteOrder(
    std::vector<DownlinkPacketScheduler *> &schedulers, int rb_id) {
  AMCModule *amc = schedulers[0]->GetMacEntity()->GetAmcModule();
  std::map<int, int> per_cell_slice_map;
std:
  map<int, double> per_slice_metric;
  std::map<int, double> per_slice_metric_under_mute;

  int aggressor_cell = -1;
  RadioSaberDownlinkScheduler *muted_cell = nullptr;

  std::vector<int> aggressor_cells = GetMutingOrder(schedulers.size(), false);
  std::map<int, double> muting_scores;
  for (int i = 0; i < aggressor_cells.size(); i++) {
    aggressor_cell = aggressor_cells[i];
    Allocation allocation_under_mute =
        DoAllocation(schedulers, rb_id, aggressor_cell, true);
    per_cell_slice_map = allocation_under_mute.per_cell_slice_map;
    per_slice_metric_under_mute = allocation_under_mute.per_slice_metric_map;
    Allocation allocation_no_mute = DoAllocation(
        schedulers, rb_id, aggressor_cell, false, per_cell_slice_map);
    per_slice_metric = allocation_no_mute.per_slice_metric_map;
    muted_cell = (RadioSaberDownlinkScheduler *)schedulers[aggressor_cell];
    Allocation default_allocation = DoAllocation(schedulers, rb_id);
    int victim_slice =
        default_allocation.cell_flow[aggressor_cell]->GetSliceID();
    double muted_cell_metric =
        default_allocation.per_cell_metric_map[aggressor_cell];

    double muted_cell_average_metric =
        muted_cell->GetLossMetric(victim_slice, 1, false);
    double losing_slice_ratio = 0;
    if (muted_cell_average_metric == 0) {
      losing_slice_ratio = 0;
    } else {
      losing_slice_ratio = muted_cell_metric / muted_cell_average_metric;
    }
    double muting_score = GetMutingScore(
        per_slice_metric, per_slice_metric_under_mute, muted_cell);
    muting_score -= losing_slice_ratio;
    if (muting_score > 1.0) {
      muting_scores.emplace(aggressor_cell, muting_score);
    }
  }

  std::vector<std::pair<int, double>> mapVector(muting_scores.begin(),
                                                muting_scores.end());
  std::sort(mapVector.begin(), mapVector.end(),
            [](const auto &lhs, const auto &rhs) {
              return lhs.second >
                     rhs.second; // Change '<' to '>' for descending order
            });

  std::vector<int> sorted_keys;
  for (const auto &entry : mapVector) {
    sorted_keys.push_back(entry.first);
  }
  return sorted_keys;
}

void RecordMetrics(std::vector<DownlinkPacketScheduler *> &schedulers,
                   Allocation alloc, int rb_id) {
  // print alloc per cell slice map
  // cout << "Recording for RB: " << rb_id << endl;
  for (int i = 0; i < schedulers.size(); i++) {
    if (alloc.per_cell_slice_map.find(i) == alloc.per_cell_slice_map.end() ||
        alloc.per_cell_slice_map[i] == -1) {
      // cout << "Skipping Cell: " << i << endl;
      continue;
    }
    int scheduled_slice = alloc.per_cell_slice_map[i];
    double metric_achieved = alloc.per_cell_metric_map[i];
    // get the radiosaber scheduler object
    RadioSaberDownlinkScheduler *rs_scheduler =
        (RadioSaberDownlinkScheduler *)schedulers[i];
    rs_scheduler->per_slice_past_performance[scheduled_slice].push_back(
        metric_achieved);
    // if (rs_scheduler->per_slice_past_performance[scheduled_slice].size() >
    // 10){
    // rs_scheduler->per_slice_past_performance[scheduled_slice].erase(rs_scheduler->per_slice_past_performance[scheduled_slice].begin());
    // }
    // assert(rs_scheduler->per_slice_past_performance[scheduled_slice].size()
    // <= 10);
  }
}

std::vector<int>
FrameManager::MultiObjMuting(std::vector<DownlinkPacketScheduler *> &schedulers,
                             int rb_id) {
  AMCModule *amc = schedulers[0]->GetMacEntity()->GetAmcModule();
  if (GetTTICounter() < 102) {
    cout << "Running Multiobjective Muting for RB: " << rb_id
         << " at TTI: " << GetTTICounter() << endl;
  }
  std::map<int, int> per_cell_slice_map;
  std::map<int, double> per_slice_metric;
  std::map<int, double> per_slice_metric_under_mute;
  std::vector<FlowToSchedule *> cell_flow(schedulers.size(), nullptr);
  // std::vector<int> empty_cells_;
  bool enable_comp = schedulers[0]->enable_comp_;
  vector<int> per_slice_mute_counter =
      vector<int>(schedulers[0]->slice_ctx_.num_slices_, 0);

  bool mute_aggressor = false;
  int aggressor_cell = -1;
  // map<int, double> slice_benefit;
  const int num_slice = schedulers[0]->slice_ctx_.num_slices_;
  RadioSaberDownlinkScheduler *muted_cell = nullptr;
  std::map<int, double> per_slice_cost_map;
  Allocation allocation_under_mute;
  Allocation allocation_no_mute;
  std::vector<int> global_cells_muted;
  std::vector<int> aggressor_cells = GetExhaustiveMuteOrder(schedulers, rb_id);
  if (aggressor_cells.size() == 0) {
    // cout << "No Candidate Cells for RB: " << rb_id << " in T: " <<
    // GetTTICounter() << endl;
  }

  double total_quota = 0.0;
  for (int i = 0; i < schedulers.size(); i++) {
    for (int j = 0; j < schedulers[i]->slice_ctx_.num_slices_; j++) {
      RadioSaberDownlinkScheduler *rs_scheduler =
          (RadioSaberDownlinkScheduler *)schedulers[i];
      double quota_remaining = rs_scheduler->slice_rbgs_quota_[j];
      // cout << "Cell: " << i << " Slice: " << j << " Quota: " <<
      // quota_remaining << endl;
      total_quota += quota_remaining;
    }
    double expected_quota = (496 - rb_id) / 8.;
    double diff = abs(total_quota - expected_quota);
    // cout << "Total Quota: " << total_quota << endl;
    // cout << "Expected Quota: " << expected_quota << endl;
    // cout << "Diff: " << diff << endl;
    assert(diff < 0.01);
    total_quota = 0.0;
  }

  if (enable_comp) {
    while (true) {
      mute_aggressor = false;
      for (int i = 0; i < aggressor_cells.size(); i++) {
        if (std::find(global_cells_muted.begin(), global_cells_muted.end(),
                      aggressor_cells[i]) != global_cells_muted.end()) {
          continue;
        }
        assert(global_cells_muted.size() == 0);
        aggressor_cell = aggressor_cells[i];
        allocation_under_mute =
            DoAllocation(schedulers, rb_id, aggressor_cell, true, {},
                         global_cells_muted, cell_flow);
        per_cell_slice_map = allocation_under_mute.per_cell_slice_map;
        per_slice_metric_under_mute =
            allocation_under_mute.per_slice_metric_map;

        allocation_no_mute =
            DoAllocation(schedulers, rb_id, aggressor_cell, false,
                         per_cell_slice_map, global_cells_muted, cell_flow);
        per_slice_metric = allocation_no_mute.per_slice_metric_map;

        vector<int> scheduled_cells_no_mute;
        vector<int> scheduled_cells_under_mute;
        for (auto &t : allocation_no_mute.per_cell_slice_map) {
          scheduled_cells_no_mute.push_back(t.first);
        }
        for (auto &t : allocation_under_mute.per_cell_slice_map) {
          scheduled_cells_under_mute.push_back(t.first);
        }
        assert(scheduled_cells_no_mute.size() ==
               scheduled_cells_under_mute.size());
        for (int i = 0; i < scheduled_cells_no_mute.size(); i++) {
          assert(allocation_no_mute.per_cell_slice_map[i] ==
                 allocation_under_mute.per_cell_slice_map[i]);
        }

        // cout << "Considering Agg C: " << aggressor_cell << endl;
        per_slice_cost_map = {};
        per_slice_cost_map =
            GetPerSliceDeduction(per_slice_metric, per_slice_metric_under_mute);
        // cout << "Per Slice Cost Map Below: " << endl;
        for (const auto &slice_cost : per_slice_cost_map) {
          // cout << "Slice: " << slice_cost.first << " Cost: " <<
          // slice_cost.second << endl;
        }

        muted_cell = (RadioSaberDownlinkScheduler *)schedulers[aggressor_cell];
        // make a deep copy of per_slice_metric_under_mute and per_slice_metric
        // std::map<int, double> per_slice_metric_under_mute_copy =
        // per_slice_metric_under_mute; std::map<int, double>
        // per_slice_metric_copy = per_slice_metric;

        while (per_slice_cost_map.size() >= 1) {
          for (const auto &slice_cost : per_slice_cost_map) {
            // mute_aggressor is always initialized to false for every flow
            int slice_id = slice_cost.first;
            mute_aggressor = false;
            double gain = per_slice_metric_under_mute[slice_id] -
                          per_slice_metric[slice_id];
            assert(gain >= 0);
            if (muted_cell->slice_rbgs_quota_[slice_id] >= slice_cost.second) {
              // Should only be able to mute aggressor if it has quota left on
              // the aggressor double metric_per_rbg =
              // muted_cell->metrics_perrbg_slice_[slice_id]; double
              // metric_loss_penalty = slice_cost.second * metric_per_rbg;
              double metric_loss_penalty =
                  muted_cell->GetLossMetric(slice_id, slice_cost.second, false);
              // cout << "Loss: " << metric_loss_penalty << endl;
              if (metric_loss_penalty < gain &&
                  (gain / metric_loss_penalty) > 1.001) {
                mute_aggressor = true;
              } else {
                // std::cout << "Slice Backs out: "
                // << " Slice: " << slice_id
                // << " Cost: " << slice_cost.second
                // << " Penalty: " << metric_loss_penalty
                // << " Gain: " << gain
                // << " MuteCell: " << aggressor_cell
                // << " Time: " << GetTTICounter()
                // << " RB: " << rb_id
                // << endl;
              }
            }
            if (!mute_aggressor) {
              per_slice_metric_under_mute.erase(slice_id);
              per_slice_metric.erase(slice_id);
              per_slice_cost_map = GetPerSliceDeduction(
                  per_slice_metric, per_slice_metric_under_mute);
              break;
            }
          }
          if (mute_aggressor) {
            break;
          }
        }
        if (mute_aggressor) {
          break;
        }
      }
      double total_metric_under_mute = 0.0;
      for (size_t i = 0; i < per_slice_metric_under_mute.size(); i++) {
        if (per_slice_metric_under_mute[i] > 0) {
          total_metric_under_mute += per_slice_metric_under_mute[i];
        }
      }
      double total_metric = 0.0;
      for (size_t i = 0; i < per_slice_metric.size(); i++) {
        if (per_slice_metric[i] > 0) {
          total_metric += per_slice_metric[i];
        }
      }
      if (mute_aggressor) {
        assert(aggressor_cell != -1);
        for (const auto &slice_cost : per_slice_cost_map) {

          // Choose Metric
          double metric_loss_penalty = muted_cell->GetLossMetric(
              slice_cost.first, slice_cost.second, true);
          // cout << "Aggressor Cell: " << aggressor_cell << " Final Cost: " <<
          // metric_loss_penalty << endl; double metric_per_rbg =
          // muted_cell->metrics_perrbg_slice_[slice_cost.first]; double
          // metric_loss_penalty = slice_cost.second * metric_per_rbg;
          double gain = per_slice_metric_under_mute[slice_cost.first] -
                        per_slice_metric[slice_cost.first];
          // if (slice_cost.first == 0){
          // cout << "Slice: " << slice_cost.first
          // << " Cost: " << slice_cost.second
          // << " Penalty: " << metric_loss_penalty
          // << " Gain: " << gain;
          // << " PercentageGain: " << gain / per_slice_metric[slice_cost.first]
          // << " Gain:Loss: " << gain/metric_loss_penalty
          // << " MuteCell: " << aggressor_cell
          // << " Time: " << GetTTICounter()
          // << " RB: " << rb_id
          // << endl;
          // }

          muted_cell->slice_rbgs_quota_[slice_cost.first] -= slice_cost.second;
          // cout << " Muted Cell: " << aggressor_cell
          //  << " Slice: " << slice_cost.first
          //  << " Quota: " << muted_cell->slice_rbgs_quota_[slice_cost.first]
          //  << endl;
          // if
          // (muted_cell->sorted_metrics_per_rbg_slice_[slice_cost.first].size()
          // >= 1){
          //   muted_cell->sorted_metrics_per_rbg_slice_[slice_cost.first].pop_back();
          // }
          per_slice_mute_counter[slice_cost.first] += 1;
        }
#ifdef MUTING_FREQ_GAIN_DEBUG
        std::cout << "Muting Decision: " << rb_id
                  << " - Muting Cell: " << aggressor_cell
                  << " TTI: " << GetTTICounter() << " Percentage Improvement: "
                  << (total_metric_under_mute - total_metric) / total_metric
                  << endl;
#endif
        allocation_under_mute.success = true;
        allocation_under_mute.percentage_improvement =
            (total_metric_under_mute - total_metric) / total_metric;
        UpdateNoiseInterferenceWithMute(schedulers, rb_id, aggressor_cell);
        cell_flow = allocation_under_mute.cell_flow;
        // empty_cells_ = allocation_under_mute.empty_cells;
        global_cells_muted.push_back(aggressor_cell);
        RecordMetrics(schedulers, allocation_under_mute, rb_id);
        // REMOVE THE FOLLOWING BREAK IN ORDER TO ENABLE MULTI MUTE
        break;
        if (global_cells_muted.size() == aggressor_cells.size()) {
          break;
        }
      } else {
        RecordMetrics(schedulers, allocation_no_mute, rb_id);
        break;
      }
    }
  }

  // REMOVE INTERFERNCE FROM ALL FLOWS ONCE DECISION IS FINAL
  if (global_cells_muted.size() == 0) {
    allocation_no_mute = DoAllocation(schedulers, rb_id);
    cell_flow = allocation_no_mute.cell_flow;
    // empty_cells_ = allocation_no_mute.empty_cells;
  }
  if (global_cells_muted.size() > 1) {
    cout << "Multi Mute:"
         << " RB ID: " << rb_id << " Muted Cells: " << global_cells_muted.size()
         << " Muted Cells: ";
    for (int i = 0; i < global_cells_muted.size(); i++) {
      cout << global_cells_muted[i] << " ";
    }
    cout << endl;
  }
  for (int j = 0; j < (int)schedulers.size(); j++) {
    if (std::find(global_cells_muted.begin(), global_cells_muted.end(), j) !=
        global_cells_muted.end()) {
      continue;
    }
    RadioSaberDownlinkScheduler *scheduler =
        (RadioSaberDownlinkScheduler *)schedulers[j];
    FlowToSchedule *flow = cell_flow[j];
    auto &rsrp_report = flow->GetRSRPReport();
    for (int i = rb_id; i < RBG_SIZE + rb_id; i++) {
      double int_noise = rsrp_report.noise_interfere_watt[i];
      int cqi =
          amc->GetCQIFromSinr(rsrp_report.rx_power[i] - 10. * log10(int_noise));
      flow->GetCqiFeedbacks()[i] = cqi;
    }
    // if j in empty cells, then we should not deduct quota
    // if (std::find(empty_cells_.begin(), empty_cells_.end(), j) ==
    // empty_cells_.end()) {
    scheduler->slice_rbgs_quota_[flow->GetSliceID()] -= 1;
    // } else {
    // cout << "Cell: " << j
    // << " allocted for Free to Flow: " <<
    // flow->GetBearer()->GetDestination()->GetIDNetworkNode()
    // << " Slice: " << flow->GetSliceID() << endl;
    // }
    for (int i = rb_id; i < RBG_SIZE + rb_id; i++) {
      flow->GetListOfAllocatedRBs()->push_back(i);
    }
  }

  if (rb_id == 488) {
    // cout << "Final RB: " << rb_id << endl;
    // print the object in Radiosaber Scheduler per_slice_past_performance
    for (int i = 0; i < schedulers.size(); i++) {
      RadioSaberDownlinkScheduler *rs_scheduler =
          (RadioSaberDownlinkScheduler *)schedulers[i];
      for (int j = 0; j < rs_scheduler->slice_ctx_.num_slices_; j++) {
        // print the average of the vector in the object
        double sum = 0;
        for (int k = 0; k < rs_scheduler->per_slice_past_performance[j].size();
             k++) {
          sum += rs_scheduler->per_slice_past_performance[j][k];
        }
        if (rs_scheduler->per_slice_past_performance[j].size() == 0) {
          assert(sum == 0);
        }
        // check if the average is nan then replace it with 0
        double achieved_performance =
            sum / rs_scheduler->per_slice_past_performance[j].size();
        if (isnan(achieved_performance)) {
          achieved_performance = 0;
        }
        if (rs_scheduler->per_slice_past_cost_multiplier[j] <= 0) {
          rs_scheduler->per_slice_past_cost_multiplier[j] = 1;
        }
        double predicted_performance = rs_scheduler->metrics_perrbg_slice_[j];
        // cout << "Predicted Metrics - Cell ID: " << i << " slice " << j
        // << " estimated: " << predicted_performance
        // << " estimated-wo-multiplier: " << predicted_performance/
        // rs_scheduler->per_slice_past_cost_multiplier[j]
        // << " achieved: " << achieved_performance
        // << " ratio: " << achieved_performance / predicted_performance
        // << " multiplier-used: " <<
        // rs_scheduler->per_slice_past_cost_multiplier[j]
        // << endl;
        rs_scheduler->per_slice_past_cost_multiplier[j] =
            achieved_performance / predicted_performance;
      }
    }
  }

  return per_slice_mute_counter;
}

std::map<int, double> FrameManager::MultiObjMutingExhaustiveRealloc(
    std::vector<DownlinkPacketScheduler *> &schedulers, int rb_id,
    std::map<int, double> past_metric_map) {
  // Print past Metric Map
  // cout << "***---RB ID: " << rb_id
  // << " T: " << GetTTICounter()
  // << " Past Metric Map - Size: " << past_metric_map.size()
  // << "---***" << endl;
  // for (auto const& pair: past_metric_map){
  //   cout << "\tSlice: " << pair.first << " Metric: " << pair.second
  //   << endl;
  // }
  AMCModule *amc = schedulers[0]->GetMacEntity()->GetAmcModule();
  std::map<int, int> per_cell_slice_map;
  std::vector<FlowToSchedule *> cell_flow(schedulers.size(), nullptr);
  std::map<int, double> metric_map;
  // std::vector<int> empty_cells_;
  bool enable_comp = schedulers[0]->enable_comp_;
  bool current_tti_cost = schedulers[0]->current_tti_cost_;
  bool consider_all_cells_for_int = schedulers[0]->consider_all_cells_for_int_;
  vector<int> per_slice_mute_counter =
      vector<int>(schedulers[0]->slice_ctx_.num_slices_, 0);

  bool mute_aggressor = false;
  int aggressor_cell = -1;
  int muted_flow_slice_id = -1;
  // map<int, double> slice_benefit;
  const int num_slice = schedulers[0]->slice_ctx_.num_slices_;
  double per_slice_quota_deduction;
  RadioSaberDownlinkScheduler *muted_cell = nullptr;
  std::map<int, double> per_slice_cost_map;
  Allocation allocation_under_mute;
  Allocation default_allocation;
  std::vector<int> global_cells_muted;
  std::map<int, double> per_slice_metric_under_mute;
  std::map<int, double> per_slice_metric;
  std::vector<int> aggressor_cells = GetExhaustiveMuteOrder(schedulers, rb_id);

  double total_quota = 0.0;
  for (int i = 0; i < schedulers.size(); i++) {
    for (int j = 0; j < schedulers[i]->slice_ctx_.num_slices_; j++) {
      RadioSaberDownlinkScheduler *rs_scheduler =
          (RadioSaberDownlinkScheduler *)schedulers[i];
      double quota_remaining = rs_scheduler->slice_rbgs_quota_[j];
      total_quota += quota_remaining;
    }
    int total_quota_int = static_cast<int>(round(total_quota));
    int expected_quota_int = (496 - rb_id) / 8;
    double expected_quota = (496 - rb_id) / 8.;
    double diff = abs(total_quota - expected_quota);
    assert(diff < 0.01);
    total_quota = 0.0;
  }

  // cout << "RB ID: " << rb_id << " Aggressor Cells: " <<
  // aggressor_cells.size() << endl;
  if (enable_comp) {
    for (int i = 0; i < aggressor_cells.size(); i++) {
      aggressor_cell = aggressor_cells[i];
      muted_cell = (RadioSaberDownlinkScheduler *)schedulers[aggressor_cell];

      // Run the Exhaustive Allocation Under No Mute
      default_allocation = DoAllocation(schedulers, rb_id);
      // for (auto const& t: default_allocation.per_cell_slice_map){
      //   cout << "Cell: " << t.first << " Slice: " << t.second
      //   << " Metric: " << default_allocation.per_cell_metric_map[t.first] <<
      //   endl;
      // }
      // cout << endl;
      std::vector<int> benefitting_slices;
      for (auto const &pair : default_allocation.per_slice_metric_map) {
        assert(pair.second > 0);
        // cout << "In Default Alloc - Slice: " << pair.first << " Metric: " <<
        // pair.second << endl;
        benefitting_slices.push_back(pair.first);
      }
      // cout << "Num Benefitting Slices - No Mute: " <<
      // benefitting_slices.size() << endl;
      per_slice_metric = DoExhaustiveAllocation(
          schedulers, rb_id + RBG_SIZE, default_allocation.per_cell_slice_map);
      // cout << "RB: " << rb_id << " Per Slice Metric after Exhaustive Alloc: "
      // << per_slice_metric.size()
      // << endl;
      // for (int i = 0; i < per_slice_metric.size(); i++){
      //   cout << "\tSlice ID: " << i << " Metric: " << per_slice_metric[i] <<
      //   endl;
      // }

      // iterate per_cell_metric map and add it to per_slice_metric
      for (auto const &pair : default_allocation.per_slice_metric_map) {
        per_slice_metric[pair.first] += pair.second;
      }
      for (auto const &pair : past_metric_map) {
        per_slice_metric[pair.first] += pair.second;
      }

      // cout << "RB: " << rb_id << " Total Per Slice Metric: " <<
      // per_slice_metric.size() << endl;
      assert(per_slice_metric.size() == num_slice);
      for (int i = 0; i < per_slice_metric.size(); i++) {
        assert(per_slice_metric[i] > 0);
        // cout << "\tSlice ID: " << i << " Metric: " << per_slice_metric[i] <<
        // endl;
      }

      // Now Run the Exhaustive Allocation Under Mute Until at Ben_slices is
      // empty or all Slices Benefit. cout << "\n\nNow Muting Aggressor Cell: "
      // << aggressor_cell << endl;
      allocation_under_mute =
          DoAllocation(schedulers, rb_id, aggressor_cell, true, {},
                       global_cells_muted, cell_flow);
      // for (auto const& t: allocation_under_mute.per_cell_slice_map){
      //   cout << "Cell: " << t.first << " Slice: " << t.second
      //   << " Metric: " << allocation_under_mute.per_cell_metric_map[t.first]
      //   << endl;
      // }
      std::vector<int> benefitting_slices_under_mute;
      for (auto const &pair : allocation_under_mute.per_slice_metric_map) {
        assert(pair.second > 0);
        // cout << "In Mute Alloc - Slice: " << pair.first << " Metric: " <<
        // pair.second << endl;
        if (muted_cell->slice_rbgs_quota_[pair.first] > 0) {
          benefitting_slices_under_mute.push_back(pair.first);
        }
      }
      while (benefitting_slices_under_mute.size() > 0) {
        mute_aggressor = false;
        per_slice_metric_under_mute = DoExhaustiveAllocation(
            schedulers, rb_id + RBG_SIZE,
            allocation_under_mute.per_cell_slice_map, aggressor_cell,
            benefitting_slices_under_mute);
        for (auto const &pair : allocation_under_mute.per_slice_metric_map) {
          per_slice_metric_under_mute[pair.first] += pair.second;
        }
        for (auto const &pair : past_metric_map) {
          per_slice_metric_under_mute[pair.first] += pair.second;
        }
        // cout << "RB: " << rb_id
        // << " Per Slice Metric under Mute: " <<
        // per_slice_metric_under_mute.size() << endl;
        for (int i = 0; i < per_slice_metric_under_mute.size(); i++) {
          assert(per_slice_metric_under_mute[i] > 0);
          // cout << "\tSlice ID: " << i << " Metric: " <<
          // per_slice_metric_under_mute[i] << endl;
        }
        assert(per_slice_metric_under_mute.size() == num_slice);
        assert(per_slice_metric.size() == num_slice);

        double deduction = 1.0 / benefitting_slices_under_mute.size();
        for (auto const &slice_id : benefitting_slices_under_mute) {
          if (per_slice_metric_under_mute[slice_id] >
                  per_slice_metric[slice_id] &&
              muted_cell->slice_rbgs_quota_[slice_id] >= deduction) {
            mute_aggressor = true;
          } else {
            // cout << "Slice ID: " << slice_id
            // << " Metric: " << per_slice_metric[slice_id]
            // << " Metric Under Mute: " <<
            // per_slice_metric_under_mute[slice_id]
            // << " Slice Does Not Benefit OR does Not have enough Quota to
            // induce Muting."
            // << " Remove From Benefitting Slices and Run Again."
            // << endl;
            // cout << "Size before removing: " <<
            // benefitting_slices_under_mute.size() << endl;
            benefitting_slices_under_mute.erase(
                std::remove(benefitting_slices_under_mute.begin(),
                            benefitting_slices_under_mute.end(), slice_id),
                benefitting_slices_under_mute.end());
            // cout << "Size after removing: " <<
            // benefitting_slices_under_mute.size() << endl;
            mute_aggressor = false;
            break;
          }
        }
        if (mute_aggressor) {
          // cout << "Benefitting Slices: " <<
          // benefitting_slices_under_mute.size() << endl;
          double deduction = 1.0 / benefitting_slices_under_mute.size();
          // cout << "Deduction: " << deduction << endl;
          for (auto &slice_id : benefitting_slices_under_mute) {
            // cout << "Deducting from Cell: " << aggressor_cell << " Slice: "
            // << slice_id << " Quota: " <<
            // muted_cell->slice_rbgs_quota_[slice_id] << endl;
            muted_cell->slice_rbgs_quota_[slice_id] -= deduction;
          }
          break;
        }
      }

      // ---------------------------------------------------------------------------------------------------------------------------
      // //
      if (mute_aggressor) {
        break;
      }
    }
    double total_metric_under_mute = 0.0;
    for (size_t i = 0; i < per_slice_metric_under_mute.size(); i++) {
      if (per_slice_metric_under_mute[i] > 0) {
        total_metric_under_mute += per_slice_metric_under_mute[i];
      }
    }
    double total_metric = 0.0;
    for (size_t i = 0; i < per_slice_metric.size(); i++) {
      if (per_slice_metric[i] > 0) {
        total_metric += per_slice_metric[i];
      }
    }
    if (mute_aggressor) {
      assert(aggressor_cell != -1);
#ifdef MUTING_FREQ_GAIN_DEBUG
      std::cout << "Muting Decision: " << rb_id
                << " - Muting Cell: " << aggressor_cell
                << " TTI: " << GetTTICounter() << " Percentage Improvement: "
                << (total_metric_under_mute - total_metric) / total_metric
                << endl;
#endif
      allocation_under_mute.success = true;
      allocation_under_mute.percentage_improvement =
          (total_metric_under_mute - total_metric) / total_metric;
      UpdateNoiseInterferenceWithMute(schedulers, rb_id, aggressor_cell);
      cell_flow = allocation_under_mute.cell_flow;
      metric_map = allocation_under_mute.per_slice_metric_map;
      global_cells_muted.push_back(aggressor_cell);
    }
  }

  // REMOVE INTERFERNCE FROM ALL FLOWS ONCE DECISION IS FINAL
  if (global_cells_muted.size() == 0) {
    default_allocation = DoAllocation(schedulers, rb_id);
    cell_flow = default_allocation.cell_flow;
    metric_map = default_allocation.per_slice_metric_map;
  }
  for (int j = 0; j < (int)schedulers.size(); j++) {
    if (std::find(global_cells_muted.begin(), global_cells_muted.end(), j) !=
        global_cells_muted.end()) {
      continue;
    }
    RadioSaberDownlinkScheduler *scheduler =
        (RadioSaberDownlinkScheduler *)schedulers[j];
    FlowToSchedule *flow = cell_flow[j];
    auto &rsrp_report = flow->GetRSRPReport();
    for (int i = rb_id; i < RBG_SIZE + rb_id; i++) {
      double int_noise = rsrp_report.noise_interfere_watt[i];
      int cqi =
          amc->GetCQIFromSinr(rsrp_report.rx_power[i] - 10. * log10(int_noise));
      flow->GetCqiFeedbacks()[i] = cqi;
    }
    scheduler->slice_rbgs_quota_[flow->GetSliceID()] -= 1;
    for (int i = rb_id; i < RBG_SIZE + rb_id; i++) {
      flow->GetListOfAllocatedRBs()->push_back(i);
    }
  }
  return metric_map;
}

std::map<int, double> FrameManager::DoExhaustiveAllocation(
    std::vector<DownlinkPacketScheduler *> &schedulers, int rb_id,
    std::map<int, int> per_cell_slice_map, int aggressor_cell,
    std::vector<int> benefitting_slices) {
  int per_cell_rb_id = rb_id;

  std::map<int, std::map<int, double>> per_cell_per_slice_quota;
  std::map<int, double> per_slice_total_metric;
  int nb_of_rbs = schedulers[0]
                      ->GetMacEntity()
                      ->GetDevice()
                      ->GetPhy()
                      ->GetBandwidthManager()
                      ->GetDlSubChannels()
                      .size();
  int num_slice = schedulers[0]->slice_ctx_.num_slices_;

  for (int i = 0; i < schedulers.size(); i++) {
    RadioSaberDownlinkScheduler *scheduler =
        (RadioSaberDownlinkScheduler *)schedulers[i];
    for (int j = 0; j < num_slice; j++) {
      per_cell_per_slice_quota[i][j] = scheduler->slice_rbgs_quota_[j];
    }
  }

  for (auto &pair : per_cell_slice_map) {
    int cell_id = pair.first;
    int slice_id = pair.second;
    per_cell_per_slice_quota[cell_id][slice_id] -= 1;
  }

  if (benefitting_slices.size() > 0 && aggressor_cell != -1) {
    // cout << "Aggressor Cell: " << aggressor_cell << " RB: " << rb_id << "
    // Benefitting Slices: " << benefitting_slices.size() << " \nSlices: "; for
    // (int i = 0; i < benefitting_slices.size(); i++){
    //   cout << benefitting_slices[i] << " ";
    // }
    // cout << endl;
    double muting_cost = 1.0 / benefitting_slices.size();
    for (auto const &slice_id : benefitting_slices) {
      if (std::find(benefitting_slices.begin(), benefitting_slices.end(),
                    slice_id) != benefitting_slices.end()) {
        // cout << "Deducting Muting Cost: " << muting_cost << " from Cell: " <<
        // aggressor_cell << " Slice: " << slice_id << endl;
        per_cell_per_slice_quota[aggressor_cell][slice_id] -= muting_cost;
      }
    }
  }
  // cout << endl;

  // print quotas
  // for (int i = 0; i < schedulers.size(); i++){
  //   cout << "Cell: " << i << " Quotas: ";
  //   for (int j = 0; j < num_slice; j++){
  //     cout << per_cell_per_slice_quota[i][j] << " ";
  //   }
  //   cout << endl;
  // }

  // Run RS Allocation
  // cout << "Num Cells: " << schedulers.size() << endl;
  for (int i = 0; i < schedulers.size(); i++) {
    RadioSaberDownlinkScheduler *scheduler =
        (RadioSaberDownlinkScheduler *)schedulers[i];
    // cout << "Scheduler ID: " << i << " RB ID: " << rb_id << endl;
    std::vector<double> slice_rbgs_quota(num_slice, 0);
    for (int j = 0; j < num_slice; j++) {
      slice_rbgs_quota[j] = per_cell_per_slice_quota[i][j];
    }
    while (per_cell_rb_id < nb_of_rbs) {
      std::vector<FlowToSchedule *> slice_flow(num_slice, nullptr);
      std::vector<double> slice_spectraleff(num_slice, -1);
      std::vector<double> max_metrics(num_slice, -1);
      FlowsToSchedule *flows = scheduler->GetFlowsToSchedule();
      AMCModule *amc = scheduler->GetMacEntity()->GetAmcModule();
      for (auto it = flows->begin(); it != flows->end(); it++) {
        FlowToSchedule *flow = *it;
        auto &rsrp_report = flow->GetRSRPReport();
        double spectraleff_rbg = 0.0;
        for (int j = per_cell_rb_id; j < RBG_SIZE + per_cell_rb_id; j++) {
          double int_noise = rsrp_report.noise_interfere_watt[j];
          int cqi = amc->GetCQIFromSinr(rsrp_report.rx_power[j] -
                                        10. * log10(int_noise));
          spectraleff_rbg += amc->GetEfficiencyFromCQI(cqi);
        }
        double metric = scheduler->ComputeSchedulingMetric(
            flow->GetBearer(), spectraleff_rbg, per_cell_rb_id);
        int slice_id = flow->GetSliceID();
        if (metric > max_metrics[slice_id]) {
          max_metrics[slice_id] = metric;
          slice_flow[slice_id] = flow;
          slice_spectraleff[slice_id] = spectraleff_rbg;
        }
      }
      double max_slice_spectraleff = -1;
      int selected_slice = -1;
      double slice_metric = -1;
      FlowToSchedule *selected_flow = nullptr;
      for (int j = 0; j < num_slice; j++) {
        if (slice_spectraleff[j] > max_slice_spectraleff &&
            slice_rbgs_quota[j] >= 1) {
          max_slice_spectraleff = slice_spectraleff[j];
          selected_slice = j;
          slice_metric = max_metrics[j];
          selected_flow = slice_flow[j];
        }
      }
      // *************** no slice has more than one RB quota, allocate to a
      // slice with fractional ***************
      // *************** no slice has more than one RB quota, allocate to a
      // slice with fractional ***************
      // *************** no slice has more than one RB quota, allocate to a
      // slice with fractional ***************
      // *************** no slice has more than one RB quota, allocate to a
      // slice with fractional ***************
      if (selected_slice == -1) {
        // cout << "Special Case\n";
        // Still to be handled
        // Multiple slices have quotas left
        for (int j = 0; j < num_slice; j++) {
          if (slice_spectraleff[j] > max_slice_spectraleff &&
              slice_rbgs_quota[j] >= 0) {
            max_slice_spectraleff = slice_spectraleff[j];
            selected_slice = j;
            slice_metric = max_metrics[j];
            selected_flow = slice_flow[j];
          }
        }

        // for (int j = 0; j < num_slice; j++){
        //   if (slice_rbgs_quota[j] > 0){
        //     selected_slice = -2;
        //     per_slice_total_metric[j] += slice_rbgs_quota[j] *
        //     scheduler->metrics_perrbg_slice_[j]; slice_rbgs_quota[j] -=
        //     slice_rbgs_quota[j];
        //   }
        // }
      }
      assert(selected_slice != -1);
      assert(selected_slice == selected_flow->GetSliceID());
      // cout << "In Exhaustive Alloc: " << " Cell: " << i
      // << " Slice: " << selected_slice
      // << " RB: " << per_cell_rb_id
      // << " Metric: " << slice_metric
      // << endl;
      if (selected_slice != -2) {
        per_slice_total_metric[selected_slice] += slice_metric;
        slice_rbgs_quota[selected_slice] -= 1;
      }
      per_cell_rb_id += RBG_SIZE;
    }
    per_cell_rb_id = rb_id;
  }
  // cout << "TOTAL EXHAUST METRIC:\n";
  // for (auto const &t: per_slice_total_metric){
  //   cout << "Slice: " << t.first << " Metric: " << t.second << endl;
  // }
  // cout << endl;
  return per_slice_total_metric;
}
