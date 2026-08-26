#include "nvs-downlink-scheduler.h"
#include "../mac-entity.h"
#include "../../packet/Packet.h"
#include "../../packet/packet-burst.h"
#include "../../../device/NetworkNode.h"
#include "../../../flows/radio-bearer.h"
#include "../../../protocolStack/rrc/rrc-entity.h"
#include "../../../flows/application/Application.h"
#include "../../../device/ENodeB.h"
#include "../../../protocolStack/mac/AMCModule.h"
#include "../../../phy/lte-phy.h"
#include "../../../core/spectrum/bandwidth-manager.h"
#include "../../../core/idealMessages/ideal-control-messages.h"

NVSDownlinkScheduler::NVSDownlinkScheduler(std::string config_fname)
: DownlinkPacketScheduler(config_fname) {
  SetMacEntity (0);
  CreateFlowsToSchedule ();
  std::cout << "construct NVS Downlink Scheduler." << std::endl;
}

NVSDownlinkScheduler::~NVSDownlinkScheduler()
{
  Destroy ();
}

void
NVSDownlinkScheduler::SelectFlowsToSchedule()
{
  current_slice_ = SelectSliceToServe();

  ClearFlowsToSchedule ();
  RrcEntity *rrc = GetMacEntity ()->GetDevice ()->GetProtocolStack ()->GetRrcEntity ();
  RrcEntity::RadioBearersContainer* bearers = rrc->GetRadioBearerContainer ();

  for (auto it = bearers->begin (); it != bearers->end (); it++)
	{
	  //SELECT FLOWS TO SCHEDULE
	  RadioBearer *bearer = (*it);
    int slice_id = bearer->GetDestination()->GetSliceID();
    if (slice_id != current_slice_)
      continue;
    // int user_id = bearer->GetUserID();
    // if (slice_ctx_.user_to_slice_[user_id] != current_slice_)
    //   continue;

	  if (bearer->HasPackets () &&
      bearer->GetDestination ()->GetNodeState () == NetworkNode::STATE_ACTIVE)
		{
		  //compute data to transmit
		  int dataToTransmit;
		  if (bearer->GetApplication()->GetApplicationType()
        == Application::APPLICATION_TYPE_INFINITE_BUFFER) {
			  dataToTransmit = 100000000;
			}
		  else {
			  dataToTransmit = bearer->GetQueueSize ();
			}

		  //compute spectral efficiency
		  ENodeB *enb = (ENodeB*) GetMacEntity ()->GetDevice ();
		  ENodeB::UserEquipmentRecord *ueRecord = enb->GetUserEquipmentRecord(
        bearer->GetDestination ()->GetIDNetworkNode ());
		  std::vector<double> spectralEfficiency;
      std::vector<int>& cqi_feedbacks = ueRecord->GetCQI();
      std::vector<CqiReport>& cqi_withmute_feedbacks = ueRecord->GetCQIWithMute();
		  int numberOfCqi = cqi_feedbacks.size ();
		  for (int i = 0; i < numberOfCqi; i++)
			{
			  double sEff = GetMacEntity()->GetAmcModule()->GetEfficiencyFromCQI(cqi_feedbacks.at (i));
			  spectralEfficiency.push_back (sEff);
			}

		  //create flow to scheduler record
		  InsertFlowToSchedule(bearer, dataToTransmit, cqi_feedbacks, cqi_withmute_feedbacks);
		}
	  else
	    {}
	}
}

int NVSDownlinkScheduler::SelectSliceToServe()
{
  int slice_id = 0;
  double max_score = 0;

  RrcEntity *rrc = GetMacEntity ()->GetDevice ()->GetProtocolStack ()->GetRrcEntity ();
  RrcEntity::RadioBearersContainer* bearers = rrc->GetRadioBearerContainer ();
  std::vector<bool> slice_with_queue(slice_ctx_.num_slices_, false);
  for (auto it = bearers->begin(); it != bearers->end(); it++) {
    RadioBearer *bearer = (*it);
    // int user_id = bearer->GetUserID();
    int ue_slice = bearer->GetDestination()->GetSliceID();

    if (bearer->HasPackets () && 
      bearer->GetDestination()->GetNodeState () == NetworkNode::STATE_ACTIVE)
		{
		  int dataToTransmit;
		  if (bearer->GetApplication ()->GetApplicationType()
        == Application::APPLICATION_TYPE_INFINITE_BUFFER) {
			  dataToTransmit = 100000000;
			}
		  else {
			  dataToTransmit = bearer->GetQueueSize ();
			}
      if (dataToTransmit > 0) {
        // slice_with_queue[slice_ctx_.user_to_slice_[user_id]] = true;
        slice_with_queue[ue_slice] = true;
      }
    }
  }
  for (int i = 0; i < slice_ctx_.num_slices_; ++i) {
    if (! slice_with_queue[i]) continue;
    if (slice_ctx_.ewma_time_[i] == 0) {
      slice_id = i;
      break;
    }
    else {
      double score = slice_ctx_.weights_[i] / slice_ctx_.ewma_time_[i];
      if (score >= max_score) {
        max_score = score;
        slice_id = i;
      }
    }
  }
  for (int i = 0; i < slice_ctx_.num_slices_; ++i) {
    if (! slice_with_queue[i]) continue;
    slice_ctx_.ewma_time_[i] = (1-beta_) * slice_ctx_.ewma_time_[i];
    if (i == slice_id) {
      slice_ctx_.ewma_time_[i] += beta_ * 1;
    }
  }
  return slice_id;
}

double
NVSDownlinkScheduler::ComputeSchedulingMetric (RadioBearer *bearer, double spectralEfficiency, int subChannel)
{
  /*
   * For the PF scheduler the metric is computed
   * as follows:
   *
   * metric = spectralEfficiency / averageRate
   */

  double metric = (spectralEfficiency * 180000.)
					  /
					  bearer->GetAverageTransmissionRate();

  return metric;
}

