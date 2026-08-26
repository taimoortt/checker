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
 *         Lukasz Rajewski <lukasz.rajewski@gmail.com> (optimized PRB
 * allocation)
 */

#include "downlink-packet-scheduler.h"
#include "../../../core/spectrum/bandwidth-manager.h"
#include "../../../device/ENodeB.h"
#include "../../../device/NetworkNode.h"
#include "../../../flows/MacQueue.h"
#include "../../../flows/application/Application.h"
#include "../../../flows/radio-bearer.h"
#include "../../../phy/lte-phy.h"
#include "../../../protocolStack/mac/AMCModule.h"
#include "../../../protocolStack/rrc/rrc-entity.h"
#include "../../../utility/eesm-effective-sinr.h"
#include "../../packet/Packet.h"
#include "../../packet/packet-burst.h"
#include "../mac-entity.h"
#include <cassert>
#include <fstream>
#include <jsoncpp/json/json.h>
#define WINDOW_SIZE 100

DownlinkPacketScheduler::DownlinkPacketScheduler(std::string config_fname) {
  cout << "Config File Name: " << config_fname << endl;
  if (config_fname == "")
    return;
  std::ifstream ifs(config_fname);
  if (!ifs.is_open()) {
    throw std::runtime_error("Fail to open configuration file.");
  }
  Json::Reader reader;
  Json::Value obj;
  reader.parse(ifs, obj);
  ifs.close();
  const Json::Value &slice_schemes = obj["slices"];
  slice_ctx_.num_slices_ = 0;
  for (int i = 0; i < (int)slice_schemes.size(); i++) {
    int n_slices = slice_schemes[i]["n_slices"].asInt();
    slice_ctx_.num_slices_ += n_slices;
    for (int j = 0; j < n_slices; j++) {
      cout << " Slice Weight: " << slice_schemes[i]["weight"].asDouble()
           << endl;
      slice_ctx_.weights_.push_back(slice_schemes[i]["weight"].asDouble());
      slice_ctx_.algo_params_.emplace_back(
          slice_schemes[i]["algo_alpha"].asInt(),
          slice_schemes[i]["algo_beta"].asInt(),
          slice_schemes[i]["algo_epsilon"].asInt(),
          slice_schemes[i]["algo_psi"].asInt());
    }
  }
  for (int i = 0; i < slice_ctx_.algo_params_.size(); i++) {
    cout << "Slice " << i << " Alpha: " << slice_ctx_.algo_params_[i].alpha
         << " Beta: " << slice_ctx_.algo_params_[i].beta
         << " Epsilon: " << slice_ctx_.algo_params_[i].epsilon
         << " Psi: " << slice_ctx_.algo_params_[i].psi << endl;
  }
  enable_comp_ = true;
  enable_tune_weights_ = true;
  enable_sliceserve_ = false;
  muting_system_ = 0;
  max_tp_weight_ = 0;
  do_reallocation_ = true;
  current_tti_cost_ = true;
  consider_all_cells_for_int_ = false;
  user_weights = true;
  if (obj.isMember("enable_comp"))
    enable_comp_ = obj["enable_comp"].asBool();
  if (obj.isMember("enable_tune_weights"))
    enable_tune_weights_ = obj["enable_tune_weights"].asBool();
  if (obj.isMember("enable_sliceserve"))
    enable_sliceserve_ = obj["enable_sliceserve"].asBool();
  if (obj.isMember("muting_system"))
    muting_system_ = obj["muting_system"].asInt();
  if (obj.isMember("max_tp_weight"))
    max_tp_weight_ = obj["max_tp_weight"].asDouble();
  if (obj.isMember("do_reallocation"))
    do_reallocation_ = obj["do_reallocation"].asBool();
  if (obj.isMember("current_tti_cost"))
    current_tti_cost_ = obj["current_tti_cost"].asBool();
  if (obj.isMember("consider_all_cells_for_int"))
    consider_all_cells_for_int_ = obj["consider_all_cells_for_int"].asBool();
  if (obj.isMember("user_weights"))
    user_weights = obj["user_weights"].asBool();
  slice_ctx_.priority_.resize(slice_ctx_.num_slices_, 0);
  slice_ctx_.ewma_time_.resize(slice_ctx_.num_slices_, 0);
  assert(slice_ctx_.num_slices_ == (int)slice_ctx_.weights_.size());

  cout << "enable_comp: " << enable_comp_ << endl;
  cout << "enable_tune_weights: " << enable_tune_weights_ << endl;
  cout << "enable_sliceserve: " << enable_sliceserve_ << endl;
  cout << "muting_system: " << muting_system_ << endl;
  cout << "max_tp_weight: " << max_tp_weight_ << endl;
  cout << "do_reallocation: " << do_reallocation_ << endl;
  cout << "current_tti_cost: " << current_tti_cost_ << endl;
  cout << "consider_all_cells_for_int: " << consider_all_cells_for_int_ << endl;
  cout << "user_weights: " << user_weights << endl;
  cout << "num_slices: " << slice_ctx_.num_slices_ << endl;
  cout << "weights: ";
  for (int i = 0; i < slice_ctx_.num_slices_; i++)
    cout << slice_ctx_.weights_[i] << " ";
  cout << endl;
  cout << "algo_params: ";
  for (int i = 0; i < slice_ctx_.num_slices_; i++) {
    cout << slice_ctx_.algo_params_[i].alpha << " "
         << slice_ctx_.algo_params_[i].beta << " "
         << slice_ctx_.algo_params_[i].epsilon << " "
         << slice_ctx_.algo_params_[i].psi << " ";
  }
  cout << endl;
}

DownlinkPacketScheduler::~DownlinkPacketScheduler() { Destroy(); }

void DownlinkPacketScheduler::SelectFlowsToSchedule() {
#ifdef SCHEDULER_DEBUG
  // std::cerr << "\t Select Flows to schedule" << std::endl;
#endif

  ClearFlowsToSchedule();

  RrcEntity *rrc =
      GetMacEntity()->GetDevice()->GetProtocolStack()->GetRrcEntity();
  RrcEntity::RadioBearersContainer *bearers = rrc->GetRadioBearerContainer();

  for (std::vector<RadioBearer *>::iterator it = bearers->begin();
       it != bearers->end(); it++) {
    // SELECT FLOWS TO SCHEDULE
    RadioBearer *bearer = (*it);
    if (bearer->HasPackets() &&
        bearer->GetDestination()->GetNodeState() == NetworkNode::STATE_ACTIVE) {
      // compute data to transmit
      int dataToTransmit;
      if (bearer->GetApplication()->GetApplicationType() ==
          Application::APPLICATION_TYPE_INFINITE_BUFFER) {
        dataToTransmit = 100000000;
      } else {
        dataToTransmit = bearer->GetQueueSize();
      }

      // compute spectral efficiency
      ENodeB *enb = (ENodeB *)GetMacEntity()->GetDevice();
      ENodeB::UserEquipmentRecord *ueRecord = enb->GetUserEquipmentRecord(
          bearer->GetDestination()->GetIDNetworkNode());
      std::vector<int> &cqi_feedbacks = ueRecord->GetCQI();
      std::vector<CqiReport> &cqi_withmute_feedbacks =
          ueRecord->GetCQIWithMute();

      // create flow to scheduler record
      InsertFlowToSchedule(bearer, dataToTransmit, cqi_feedbacks,
                           cqi_withmute_feedbacks, ueRecord->GetRSRP());
    } else {
    }
  }
}

void DownlinkPacketScheduler::DoSchedule(void) {
#ifdef SCHEDULER_DEBUG
  std::cout << "\nStart DL packet scheduler for eNodeB "
            << GetMacEntity()->GetDevice()->GetIDNetworkNode() << std::endl;
#endif

  UpdateAverageTransmissionRate();
  SelectFlowsToSchedule();

  if (GetFlowsToSchedule()->size() == 0) {
  } else {
    RBsAllocation();
  }

  StopSchedule();
}

void DownlinkPacketScheduler::DoStopSchedule(void) {
  // #ifdef SCHEDULER_DEBUG
  //   std::cout << "\t Creating Packet Burst" << std::endl;
  // #endif

  PacketBurst *pb = new PacketBurst();

  // Create Packet Burst
  FlowsToSchedule *flowsToSchedule = GetFlowsToSchedule();

  for (FlowsToSchedule::iterator it = flowsToSchedule->begin();
       it != flowsToSchedule->end(); it++) {
    FlowToSchedule *flow = (*it);

    int availableBytes = flow->GetAllocatedBits() / 8;

    if (availableBytes > 0) {

      flow->GetBearer()->UpdateTransmittedBytes(availableBytes);

      // #ifdef SCHEDULER_DEBUG
      // 	      std::cout << "\t  --> add packets for flow "
      // 	    		  << flow->GetBearer ()->GetApplication
      // ()->GetApplicationID () << std::endl; #endif

      RlcEntity *rlc = flow->GetBearer()->GetRlcEntity();
      if (flow->GetBearer()->GetApplication()->GetApplicationType() == 1) {
        cout << "Flow ID: "
             << flow->GetBearer()->GetDestination()->GetIDNetworkNode()
             << " hol_delay: " << flow->GetBearer()->GetHeadOfLinePacketDelay()
             << endl;
        int flows_completed = rlc->getFlowsCompleted();
        if (flows_completed >= 10000) {
          cout << "All Video Flows Complete. Exiting\n";
          cerr << "All Video Flows Complete. Exiting\n";
          std::exit(0);
        }
        rlc->setFlowsCompleted(flows_completed + 1);
      }
      PacketBurst *pb2 = rlc->TransmissionProcedure(availableBytes);

      // #ifdef SCHEDULER_DEBUG
      // 	      std::cout << "\t\t  nb of packets: " << pb2->GetNPackets
      // () << std::endl; #endif

      if (pb2->GetNPackets() > 0) {
        std::list<Packet *> packets = pb2->GetPackets();
        std::list<Packet *>::iterator it;
        for (it = packets.begin(); it != packets.end(); it++) {
          // #ifdef SCHEDULER_DEBUG
          // 	    		  std::cout << "\t\t  added packet of bytes " <<
          // (*it)->GetSize () << std::endl; #endif

          Packet *p = (*it);
          pb->AddPacket(p->Copy());
        }
      }
      delete pb2;
    } else {
    }
  }

  // UpdateAverageTransmissionRate ();

  // SEND PACKET BURST

#ifdef SCHEDULER_DEBUG
  if (pb->GetNPackets() == 0)
    std::cout << "\t Send only reference symbols" << std::endl;
#endif

  GetMacEntity()->GetDevice()->SendPacketBurst(pb);
}

void DownlinkPacketScheduler::RBsAllocation() {
#ifdef SCHEDULER_DEBUG
  std::cout << " ---- DownlinkPacketScheduler::RBsAllocation";
#endif

  FlowsToSchedule *flows = GetFlowsToSchedule();
  int nbOfRBs = GetMacEntity()
                    ->GetDevice()
                    ->GetPhy()
                    ->GetBandwidthManager()
                    ->GetDlSubChannels()
                    .size();

  // create a matrix of flow metrics
  double metrics[nbOfRBs][flows->size()];
  for (int i = 0; i < nbOfRBs; i++) {
    for (size_t j = 0; j < flows->size(); j++) {
      metrics[i][j] = ComputeSchedulingMetric(
          flows->at(j)->GetBearer(),
          flows->at(j)->GetSpectralEfficiency().at(i), i);
    }
  }

#ifdef SCHEDULER_DEBUG
  // std::cout << ", available RBs " << nbOfRBs << ", flows " << flows->size()
  //           << std::endl;
#endif

  AMCModule *amc = GetMacEntity()->GetAmcModule();
  int l_dAllocatedRBCounter = 0;

  int l_iNumberOfUsers = ((ENodeB *)this->GetMacEntity()->GetDevice())
                             ->GetNbOfUserEquipmentRecords();

  bool *l_bFlowScheduled = new bool[flows->size()];
  int l_iScheduledFlows = 0;
  std::vector<double> *l_bFlowScheduledSINR =
      new std::vector<double>[flows->size()];
  for (size_t k = 0; k < flows->size(); k++)
    l_bFlowScheduled[k] = false;

  // RBs allocation
  for (int s = 0; s < nbOfRBs; s++) {
    if (l_iScheduledFlows == (int)flows->size())
      break;

    double targetMetric = -1;
    bool RBIsAllocated = false;
    FlowToSchedule *scheduledFlow;
    int l_iScheduledFlowIndex = 0;

    for (size_t k = 0; k < flows->size(); k++) {
      if (metrics[s][k] >= targetMetric && !l_bFlowScheduled[k]) {
        targetMetric = metrics[s][k];
        RBIsAllocated = true;
        scheduledFlow = flows->at(k);
        l_iScheduledFlowIndex = k;
      }
    }

    if (RBIsAllocated) {
      l_dAllocatedRBCounter++;

      scheduledFlow->GetListOfAllocatedRBs()->push_back(
          s); // the s RB has been allocated to that flow!

      double sinr = amc->GetSinrFromCQI(scheduledFlow->GetCqiFeedbacks().at(s));
      l_bFlowScheduledSINR[l_iScheduledFlowIndex].push_back(sinr);

      double effectiveSinr =
          GetEesmEffectiveSinr(l_bFlowScheduledSINR[l_iScheduledFlowIndex]);
      int mcs = amc->GetMCSFromCQI(amc->GetCQIFromSinr(effectiveSinr));
      int transportBlockSize = amc->GetTBSizeFromMCS(
          mcs, scheduledFlow->GetListOfAllocatedRBs()->size());
      if (transportBlockSize >= scheduledFlow->GetDataToTransmit() * 8) {
        l_bFlowScheduled[l_iScheduledFlowIndex] = true;
        l_iScheduledFlows++;
      }
    }
  }

  delete[] l_bFlowScheduled;
  delete[] l_bFlowScheduledSINR;

#ifdef SCHEDULER_DEBUG
  for (size_t ii = 0; ii < flows->size(); ii++) {
    FlowToSchedule *flow = flows->at(ii);
    std::cout << "Flow("
              << flow->GetBearer()->GetApplication()->GetApplicationID()
              << ") allocated RBs:";
    for (size_t i = 0; i < flow->GetListOfAllocatedRBs()->size(); i++) {
      int rbid = flow->GetListOfAllocatedRBs()->at(i);
      fprintf(stdout, " %d(%d, %.3f, %.3f)", rbid,
              flow->GetCqiFeedbacks().at(rbid),
              flow->GetSpectralEfficiency().at(rbid), metrics[rbid][ii]);
    }
    std::cout << std::endl;
  }
#endif

  FinalizeScheduledFlows();
  //   PdcchMapIdealControlMessage *pdcchMsg = new PdcchMapIdealControlMessage
  //   ();

  //   for (FlowsToSchedule::iterator it = flows->begin (); it != flows->end ();
  //   it++)
  //   {
  //     FlowToSchedule *flow = (*it);
  //     if (flow->GetListOfAllocatedRBs ()->size () > 0)
  //     {
  //       //this flow has been scheduled
  //       std::vector<double> estimatedSinrValues;
  //       for (int rb = 0; rb < flow->GetListOfAllocatedRBs ()->size (); rb++ )

  //       {
  //         double sinr = amc->GetSinrFromCQI (
  //             flow->GetCqiFeedbacks ().at (flow->GetListOfAllocatedRBs ()->at
  //             (rb)));

  //         estimatedSinrValues.push_back (sinr);
  //       }

  //       //compute the effective sinr
  //       double effectiveSinr = GetEesmEffectiveSinr (estimatedSinrValues);
  //       //get the MCS for transmission
  //       int mcs = amc->GetMCSFromCQI (amc->GetCQIFromSinr (effectiveSinr));
  //       //define the amount of bytes to transmit
  //       int transportBlockSize = amc->GetTBSizeFromMCS (mcs,
  //       flow->GetListOfAllocatedRBs ()->size ()); flow->UpdateAllocatedBits
  //       (transportBlockSize);

  // #ifdef SCHEDULER_DEBUG
  //       std::cout << "\t\t --> flow "
  //         << flow->GetBearer ()->GetApplication ()->GetApplicationID ()
  //         << " has been scheduled:" <<
  //         " nb_of_RBs: " << flow->GetListOfAllocatedRBs ()->size () <<
  //         " effective_sinr: " << effectiveSinr <<
  //         " tbs_size: " << transportBlockSize
  //         << std::endl;
  // #endif
  //       //create PDCCH messages
  //       for (int rb = 0; rb < flow->GetListOfAllocatedRBs ()->size (); rb++ )
  //       {
  //         pdcchMsg->AddNewRecord (PdcchMapIdealControlMessage::DOWNLINK,
  //             flow->GetListOfAllocatedRBs ()->at (rb),
  //             flow->GetBearer ()->GetDestination (),
  //             mcs);
  //       }
  //     }
  //   }

  //   if (pdcchMsg->GetMessage()->size () > 0)
  //     {
  //       GetMacEntity ()->GetDevice ()->GetPhy ()->SendIdealControlMessage
  //       (pdcchMsg);
  //     }
  //   delete pdcchMsg;
}

int DownlinkPacketScheduler::FinalizeScheduledFlows(void) {
  int available_rbs = 0;
  FlowsToSchedule *flows = GetFlowsToSchedule();
  for (auto it = flows->begin(); it != flows->end(); it++) {
    std::vector<int> *allocated_rbs = (*it)->GetListOfAllocatedRBs();
    available_rbs += allocated_rbs->size();
  }
#ifdef SCHEDULER_DEBUG
  std::cout << "RBsAllocation";
  std::cout << ", available RBs " << available_rbs << ", flows "
            << flows->size() << std::endl;
#endif
  PdcchMapIdealControlMessage *pdcchMsg = new PdcchMapIdealControlMessage();
  AMCModule *amc = GetMacEntity()->GetAmcModule();
  int total_tbs = 0;
  for (auto it = flows->begin(); it != flows->end(); it++) {
    FlowToSchedule *flow = (*it);
    std::vector<int> *allocated_rbs = flow->GetListOfAllocatedRBs();
    if (allocated_rbs->size() > 0) {
      std::vector<double> sinr_values;
      std::vector<double> individual_sinr_values =
          flow->GetRSRPReport().rx_power;

      double linear_tbs = 0;
      for (auto it = allocated_rbs->begin(); it != allocated_rbs->end(); it++) {
        int rb_id = *it;
        int cqi = flow->GetCqiFeedbacks().at(rb_id);
        double sinr = amc->GetSinrFromCQI(cqi);
        sinr_values.push_back(sinr);
        linear_tbs += amc->GetEfficiencyFromCQI(cqi);
      }
      double effective_sinr = GetEesmEffectiveSinr(sinr_values);
      int cqi = amc->GetCQIFromSinr(effective_sinr);
      int mcs = amc->GetMCSFromCQI(cqi);
      int tbs_size = amc->GetTBSizeFromMCS(mcs, allocated_rbs->size());
      // int tbs_size = linear_tbs;
      // cout << "MCS: " << mcs << endl;
      flow->UpdateAllocatedBits(tbs_size);
      total_tbs += tbs_size;
      // cout <<  "Total TBS for Cell: " << total_tbs << endl << endl;

#ifdef SCHEDULER_DEBUG
      std::cout << "\t\tflow "
                << flow->GetBearer()->GetApplication()->GetApplicationID()
                << " cell: " << GetMacEntity()->GetDevice()->GetIDNetworkNode()
                << " slice: "
                << flow->GetBearer()->GetDestination()->GetSliceID()
                << " nb_of_rbs: " << flow->GetListOfAllocatedRBs()->size()
                << " eff_sinr: " << effective_sinr << " tbs_size: " << tbs_size
                << std::endl;
#endif
      for (auto it = allocated_rbs->begin(); it != allocated_rbs->end(); it++) {
        pdcchMsg->AddNewRecord(PdcchMapIdealControlMessage::DOWNLINK, *it,
                               flow->GetBearer()->GetDestination(), mcs);
      }
    }
  }
  if (pdcchMsg->GetMessage()->size() > 0) {
    GetMacEntity()->GetDevice()->GetPhy()->SendIdealControlMessage(pdcchMsg);
  }
  delete pdcchMsg;
  return total_tbs;
}

void DownlinkPacketScheduler::UpdatePerSlicePerformance(int slice_id,
                                                        double metric) {
  // Calculate Rolling Average
  // cout << "Slice ID: " << slice_id << " Metric: " << metric << endl;
  per_slice_performance[slice_id].push_back(metric);
  if (per_slice_performance[slice_id].size() > WINDOW_SIZE) {
    per_slice_performance[slice_id].erase(
        per_slice_performance[slice_id].begin());
  }

  double sum = 0;
  for (double value : per_slice_performance[slice_id]) {
    sum += value;
  }

  double mean = sum / per_slice_performance[slice_id].size();
  // cout << "Mean: " << mean << endl << endl;
  per_slice_mean_performance[slice_id] = mean;
}

double DownlinkPacketScheduler::GetSliceRollingAverage(int slice_id) {
  return per_slice_mean_performance[slice_id];
}

void DownlinkPacketScheduler::UpdateAverageTransmissionRate(void) {
  RrcEntity *rrc =
      GetMacEntity()->GetDevice()->GetProtocolStack()->GetRrcEntity();
  RrcEntity::RadioBearersContainer *bearers = rrc->GetRadioBearerContainer();

  for (std::vector<RadioBearer *>::iterator it = bearers->begin();
       it != bearers->end(); it++) {
    RadioBearer *bearer = (*it);
    bearer->UpdateAverageTransmissionRate();
  }
}
