#include "radiosaber-downlink-scheduler.h"
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
#include "../../../utility/eesm-effective-sinr.h"
#include <cassert>
#include <unordered_set>
#include <algorithm>
#include <cmath>

using SchedulerAlgo = SliceContext::SchedulerAlgo;

RadioSaberDownlinkScheduler::RadioSaberDownlinkScheduler(std::string config_fname)
: DownlinkPacketScheduler(config_fname) {
  SetMacEntity (0);
  CreateFlowsToSchedule ();
  slice_rbs_share_.resize(slice_ctx_.num_slices_, 0);
  slice_rbs_offset_.resize(slice_ctx_.num_slices_, 0);
  slice_rbgs_quota_.resize(slice_ctx_.num_slices_, 0);
  metrics_perrbg_slice_.resize(slice_ctx_.num_slices_, 0);
  sorted_metrics_per_rbg_slice_.resize(slice_ctx_.num_slices_, std::vector<double>(0));
  rollover_slice_quota_.resize(slice_ctx_.num_slices_, 0);
  actual_slice_rbgs_metric_sum_.resize(slice_ctx_.num_slices_, 0);
  cost_indices_.resize(slice_ctx_.num_slices_, 0);
  remaining_portions_.resize(slice_ctx_.num_slices_, 1);
  per_slice_lower_priority_count.resize(slice_ctx_.num_slices_, 0);
  per_slice_higher_priority_count.resize(slice_ctx_.num_slices_, 0);
  avg_lower_priority_metric.resize(slice_ctx_.num_slices_, 0);
  avg_higher_priority_metric.resize(slice_ctx_.num_slices_, 0);
  per_slice_clusters.resize(slice_ctx_.num_slices_, std::map<int, std::pair<int, double>>());
  cluster_indices_.resize(slice_ctx_.num_slices_, std::pair<int, double>(0, 0.0));
  initial_rbg_quota_.resize(slice_ctx_.num_slices_, 0);
  for (int i = 0; i < slice_ctx_.num_slices_; i++) {
    per_slice_past_cost_multiplier[i] = -1.0;  // Initialize each key with an empty vector
  }

  for (int i = 0; i < slice_ctx_.num_slices_; ++i) {
    per_slice_past_performance[i] = std::vector<double>();  // Initialize each key with an empty vector
  }
  std::cout << "construct RadioSaber Downlink Scheduler." << std::endl;
}

RadioSaberDownlinkScheduler::~RadioSaberDownlinkScheduler()
{
  Destroy ();
}

void RadioSaberDownlinkScheduler::CalculateSliceQuota()
{
  int nb_rbgs = GetMacEntity()->GetDevice()->GetPhy()
    ->GetBandwidthManager()->GetDlSubChannels().size() / RBG_SIZE;
  for (int i = 0; i < slice_ctx_.num_slices_; i++) {
    slice_rbgs_quota_[i] = nb_rbgs * slice_ctx_.weights_[i];
  }
  // now we reallocate the RBGs of slices with no traffic to slices with traffic
  // step1: find those slices with data
  FlowsToSchedule* flows = GetFlowsToSchedule();
  std::unordered_set<int> slice_with_data;
  double extra_rbgs = nb_rbgs;
  for (auto it = flows->begin(); it != flows->end(); ++it) {
    assert(*it != nullptr);
    int slice_id = (*it)->GetSliceID();
    if (slice_with_data.find(slice_id) != slice_with_data.end()) {
      continue;
    }
    slice_with_data.insert(slice_id);
    extra_rbgs -= slice_rbgs_quota_[slice_id];
  }
  assert(slice_with_data.size() > 0);
  // step2: update slice rbgs quota(set 0 to slice without data, and increase a random slice with extra_rbgs)
  int rand_idx = rand() % slice_with_data.size();
  for (int i = 0; i < slice_ctx_.num_slices_; i++) {
    if (slice_with_data.find(i) == slice_with_data.end()) {
      slice_rbgs_quota_[i] = 0;
    }
    else {
      if (rand_idx == 0)
        slice_rbgs_quota_[i] += extra_rbgs;
      rand_idx -= 1;
    }
  }

  for (int i = 0; i < slice_rbgs_quota_.size(); i++) {
    initial_rbg_quota_[i] = slice_rbgs_quota_[i];
  }
  // cluster_indices_ = std::vector<double>(slice_ctx_.num_slices_, 0);
  // std::cout << "slice quota: ";
  // for (int i = 0; i < slice_ctx_.num_slices_; i++) {
  //   std::cout << slice_rbgs_quota_[i] << ", ";
  // }
  // std::cout << std::endl;

  // std::cout << "rolled over: ";
  //   for (int i = 0; i < slice_ctx_.num_slices_; i++) {
  //   std::cout << rollover_slice_quota_[i] << ", ";
  // }
  // std::cout << std::endl;

  // for (int i = 0; i < slice_ctx_.num_slices_; i++) {
  //   slice_rbgs_quota_[i] += rollover_slice_quota_[i];
  // }

  // std::cout << "after rollover: ";
  // for (int i = 0; i < slice_ctx_.num_slices_; i++) {
  //   std::cout << slice_rbgs_quota_[i] << ", ";
  // }
  // std::cout << std::endl;
}

double
RadioSaberDownlinkScheduler::ComputeSchedulingMetric(
  RadioBearer *bearer, double spectralEfficiency, int subChannel)
{
  double metric = 0;
  double averageRate = bearer->GetAverageTransmissionRate();
  int slice_id = bearer->GetDestination()->GetSliceID();
  averageRate /= 1000.0; // set the unit of both params to kbps
  SchedulerAlgo param = slice_ctx_.algo_params_[slice_id];
  if (param.alpha == 0) {
    metric = pow(spectralEfficiency, param.epsilon) / pow(averageRate, param.psi);
    // cout << "IN RS Scheduler: " 
    // << averageRate 
    // << " Inst: " << spectralEfficiency << " His: " << averageRate
    // << " " << param.alpha << " " << param.beta << " " << param.epsilon << " " << param.psi 
    // << " Metric: " << metric << endl;
  }
  else {
    if (param.beta) {
      double HoL = bearer->GetHeadOfLinePacketDelay();
      metric = HoL * pow(spectralEfficiency, param.epsilon)
        / pow(averageRate, param.psi);
    }
    else {
      metric = pow(spectralEfficiency, param.epsilon)
        / pow(averageRate, param.psi);
    }
  }
  metric *= bearer->GetPriority();
  return metric;
}

// Function to calculate the mean of a vector
double calculateMean(const std::vector<double>& data) {
    double sum = 0.0;
    for (auto num : data) {
        sum += num;
    }
    return sum / data.size();
}

// Function to calculate the standard deviation of a vector
double calculateStdDev(const std::vector<double>& data, double mean) {
    double sum = 0.0;
    for (auto num : data) {
        sum += (num - mean) * (num - mean);
    }
    return std::sqrt(sum / data.size());
}

// Function to classify numbers into clusters and return cluster sizes and means
std::map<int, std::pair<int, double>> classifyAndCluster(const std::vector<double>& data) {
    double mean = calculateMean(data);
    double stdDev = calculateStdDev(data, mean);
    // cout << "Mean: " << mean << endl;
    // cout << "StdDev: " << stdDev << endl;
    double two_std_dev_below_mean = mean - 2 * stdDev;
    double one_std_dev_below_mean = mean - stdDev;
    double one_std_dev_above_mean = mean + stdDev;
    double two_std_dev_above_mean = mean + 2 * stdDev;

    std::map<int, std::pair<int, double>> clusters; // {clusterId, {count, sum}}

    for (auto num : data) {
        int clusterId;
        if (num <= two_std_dev_below_mean) {
            clusterId = 0;
        } else if (num > two_std_dev_below_mean && num <= one_std_dev_below_mean) {
            clusterId = 1;
        } else if (num > one_std_dev_below_mean && num <= one_std_dev_above_mean) {
            clusterId = 2;
        } else if (num > one_std_dev_above_mean && num <= two_std_dev_above_mean) {
            clusterId = 3;
        } else {
            clusterId = 4;
        }

        clusters[clusterId].first++;  // Increment count
        clusters[clusterId].second += num; // Add to sum
    }

    for (auto& cluster : clusters) {
      // if (cluster.second.first > 16){
        // cout << "WTF:::\n";
        // cout 
        // << "Cluster ID: " << cluster.first 
        // << " Cluster Portion: " << cluster.second.first 
        // << " Cluster Metric: " << cluster.second.second
        // << " Data Len: " << data.size()
        // << endl;
        
      // }
        if (cluster.second.first > 0) { // To avoid division by zero
            cluster.second.second /= cluster.second.first;
        } else {
            assert(0==1);
            cluster.second.second = 0; // Set mean to 0 if no data points in cluster
        }
    }
    return clusters;
}


void
RadioSaberDownlinkScheduler::CalculateMetricsPerRB(int tti, int rb_id)
{
  // cout << "At Cell: " << GetMacEntity()->GetDevice()->GetIDNetworkNode()
  // << " - Initializing to 0" << endl;
  cluster_indices_.resize(slice_ctx_.num_slices_, std::pair<int, double>(0, 0.0));
  per_slice_clusters.resize(slice_ctx_.num_slices_, std::map<int, std::pair<int, double>>());
  for (int i = 0; i < slice_ctx_.num_slices_; ++i) {
    per_slice_past_performance[i] = std::vector<double>();  // Initialize each key with an empty vector
  }
  for (int i = 0; i < slice_ctx_.num_slices_; i++) {
    cluster_indices_[i] = std::pair<int, double>(0, 0.0);
    per_slice_clusters[i] = std::map<int, std::pair<int, double>>();
    // cout << "Slice: " << i << " Quota: " << slice_rbgs_quota_[i] << endl;
  }
  
  FlowsToSchedule* flows = GetFlowsToSchedule();
  const int num_slice = slice_ctx_.num_slices_;
  AMCModule* amc = GetMacEntity()->GetAmcModule();
  int nb_of_rbs = GetMacEntity()->GetDevice()->GetPhy()
    ->GetBandwidthManager()->GetDlSubChannels().size ();
  std::vector<double> slice_sum_metrics(num_slice, 0);
  std::vector<double> slice_rbgs_quota(slice_rbgs_quota_);
  std::map<int, std::vector<FlowToSchedule*>> per_slice_user_allocation;
  rb_id = 0;
  // initialized the 2d vector sorted_metrics_per_rbg_slice_ with empty vectors
  sorted_metrics_per_rbg_slice_ = std::vector<std::vector<double>>(num_slice, std::vector<double>(0));
  cost_indices_ = std::vector<int>(num_slice, 0);
  remaining_portions_ = std::vector<double>(num_slice, 1.0);
  while (rb_id < nb_of_rbs) {
    std::vector<FlowToSchedule*> slice_flow(num_slice, nullptr);
    std::vector<double> slice_spectraleff(num_slice, -1);
    std::vector<double> max_metrics(num_slice, -1);
    for (auto it = flows->begin(); it != flows->end(); it++) {
      FlowToSchedule* flow = *it;
      auto& rsrp_report = flow->GetRSRPReport();
      // under the current muting assumption
      double spectraleff_rbg = 0.0;
      // std::vector<double> sinr_values;
      for (int j = rb_id; j < RBG_SIZE+rb_id; j++) {
        double int_noise = rsrp_report.noise_interfere_watt[j]; // Interference under Muting
        int cqi = amc->GetCQIFromSinr(rsrp_report.rx_power[j] - 10. * log10(int_noise));
        spectraleff_rbg += amc->GetEfficiencyFromCQI(cqi); // This returns TBS
      }
      double metric = ComputeSchedulingMetric(
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
    FlowToSchedule* selected_flow = nullptr;
    for (int j = 0; j < num_slice; j++) {
      if (slice_spectraleff[j] > max_slice_spectraleff
          && slice_rbgs_quota[j] >= 1) {
        max_slice_spectraleff = slice_spectraleff[j];
        selected_slice = j;
        slice_metric = max_metrics[j];
        selected_flow = slice_flow[j];
      }
    }
    // no slice has more than one RB quota, allocate to a slice with fractional
    if (selected_slice == -1) {
      for (int j = 0; j < num_slice; j++) {
        if (slice_spectraleff[j] > max_slice_spectraleff
            && slice_rbgs_quota[j] >= 0) {
          max_slice_spectraleff = slice_spectraleff[j];
          selected_slice = j;
          slice_metric = max_metrics[j];
          selected_flow = slice_flow[j];
        }
      }
      // cout << "Cell: " << GetMacEntity()->GetDevice()->GetIDNetworkNode() 
      //  << "No Flow Selected: Selecting Slice: " << selected_slice << endl;
    }
    assert(selected_slice != -1);
    assert(selected_slice == selected_flow->GetSliceID());
    per_slice_user_allocation[selected_slice].push_back(selected_flow);
    sorted_metrics_per_rbg_slice_[selected_slice].push_back(slice_metric);
    slice_rbgs_quota[selected_slice] -= 1;
    slice_sum_metrics[selected_slice] += slice_metric;
    rb_id += RBG_SIZE;
  }

  // cout << "CELL: " << GetMacEntity()->GetDevice()->GetIDNetworkNode() << endl;

  for (int i = 0;  i < num_slice; i++){
    // std::vector<double>& per_slice_metrics_vector = sorted_metrics_per_rbg_slice_[i];
    // cout << "Slice Allocated: " << sorted_metrics_per_rbg_slice_[i].size() << endl;
    // if (sorted_metrics_per_rbg_slice_[i].size() > 16){
    //   cout << "WTF::: " << " Slice: " << i << " Size: " << sorted_metrics_per_rbg_slice_[i].size() << endl;
    // }
    std::map<int, std::pair<int, double>> clusters = classifyAndCluster(sorted_metrics_per_rbg_slice_[i]);
    per_slice_clusters[i] = clusters;
    // if (clusters.size() == 0){
    //   cout << "Slice: " << i << " has no clusters\n";
    // }
    for (auto &p: clusters) {
      // cout << " Cluster ID: " << p.first
      // << " Cluster Count: " << p.second.first
      // << " Cluster Metric: " << p.second.second
      // << endl;
      cluster_indices_[i] = std::pair<int, double>(p.first, p.second.first);
      break;
    }
    // cout << " Slice " << i
    // << " Start Cluster ID: " << cluster_indices_[i].first
    // << " Start Cluster Portion: " << cluster_indices_[i].second
    // << endl;
    // for (auto &p: clusters) {
    //   cout << " Cluster ID: " << p.first 
    //   << " Cluster Count: " << p.second.first 
    //   << " Cluster Metric: " << p.second.second 
    //   << endl;
    // }
  }
  // cout << endl;

  // for (int k = 0; k < per_slice_user_allocation.size(); k++) {
  //   int priority_mismatch = 0;
  //   cout << "Cell: " << GetMacEntity()->GetDevice()->GetIDNetworkNode()
  //   << " Slice: " << k
  //   << " Size: " << per_slice_user_allocation[k].size();
  //   for (int l = 0; l < per_slice_user_allocation[k].size(); l++){
  //     auto flow = per_slice_user_allocation[k][l];
  //     cout << "\tMetric: " << sorted_metrics_per_rbg_slice_[k][l] 
  //     << " Priority:" 
  //     << flow->GetBearer()->GetPriority() << "\n";
  //     if (flow->GetBearer()->GetPriority() != per_slice_user_allocation[k][0]->GetBearer()->GetPriority()) {
  //       priority_mismatch++;
  //     }
  //   }
  //   if (priority_mismatch > 0){
  //     cout << "Priority Mismatch: " << priority_mismatch;
  //   }
  //   cout << endl;
  // }
  // cout << "Sorted Size: " << sorted_metrics_per_rbg_slice_.size() << endl;



  // for (int i = 0; i < per_slice_user_allocation.size(); i++) {
  //   int lower_priority_count = 0;
  //   int higher_priority_count = 0;
  //   double total_lower_priority_metric = 0;
  //   double total_higher_priority_metric = 0;
  //   for (int j = 0; j < per_slice_user_allocation[i].size(); j++) {
  //     if (per_slice_user_allocation[i][j]->GetBearer()->GetPriority() == 1) {
  //       lower_priority_count++;
  //       total_lower_priority_metric += sorted_metrics_per_rbg_slice_[i][j];
  //     }
  //     else {
  //       higher_priority_count++;
  //       total_higher_priority_metric += sorted_metrics_per_rbg_slice_[i][j];
  //     }
  //   }

  //   per_slice_lower_priority_count[i] = lower_priority_count;
  //   per_slice_higher_priority_count[i] = higher_priority_count;
  //   if (lower_priority_count){
  //     avg_lower_priority_metric[i] = total_lower_priority_metric / lower_priority_count;
  //   } else {
  //     avg_lower_priority_metric[i] = -1;
  //   }
  //   if (higher_priority_count){
  //     avg_higher_priority_metric[i] = total_higher_priority_metric / higher_priority_count;
  //   } else {
  //     avg_higher_priority_metric[i] = -1;
  //   }
  // }




  // sort each of the vector in the 2d vector sorted_metrics_per_rbg_slice_
  // Sorted in a descending order so that we can just pop the min value from the back, instead of having 
  // to remove the value from the front of the vector
  // for (int i = 0; i < sorted_metrics_per_rbg_slice_.size(); i++){
  //     std::vector<double>& per_slice_metrics_vector = sorted_metrics_per_rbg_slice_[i];
  //     std::sort(per_slice_metrics_vector.begin(), per_slice_metrics_vector.end());
  // }
  for (int j = 0; j < num_slice; j++) {
    // cout << "Slice: " << j 
    // << " Size: " << sorted_metrics_per_rbg_slice_[j].size()
    // << " Metrics: " << slice_sum_metrics[j]
    // << " Quota: " << slice_rbgs_quota_[j]
    // << endl;
    if (slice_rbgs_quota_[j] == 0){
      metrics_perrbg_slice_[j] = 0;
    } else{
      metrics_perrbg_slice_[j] = slice_sum_metrics[j] / sorted_metrics_per_rbg_slice_[j].size();      
    }
  }
  // for (int i = 0; i < num_slice; i++){
  //   cout << "Initial Metrics - Cell ID: " << GetMacEntity()->GetDevice()->GetIDNetworkNode()
  //   << " slice " << i 
  //   << " avg: " << metrics_perrbg_slice_[i] << endl;
    // << " size: " << sorted_metrics_per_rbg_slice_[i].size()
    // << " metrics: ";
    //   std::vector<double>& per_slice_metrics_vector = sorted_metrics_per_rbg_slice_[i];
    //   for (int j = 0; j < per_slice_metrics_vector.size(); j++){
    //       std::cout << per_slice_metrics_vector[j] << " ";
    //   }
  // }

  //   for (int i = 0; i < num_slice; i++){
  //   cout << "Slice: " << i << endl;
  //   cout << "Lower Priority Count: " << per_slice_lower_priority_count[i] << endl;
  //   cout << "Higher Priority Count: " << per_slice_higher_priority_count[i] << endl;
  //   cout << "Lower Priority Metric: " << avg_lower_priority_metric[i] << endl;
  //   cout << "Higher Priority Metric: " << avg_higher_priority_metric[i] << endl;
  // }
}

// double RadioSaberDownlinkScheduler:: GetHistoricalAvg (int slice_id){
//   if (per_slice_past_performance[slice_id].size() == 0){
//     return -1;
//   } else {
//     return calculateMean(per_slice_past_performance[slice_id]);
//   }
// }

double RadioSaberDownlinkScheduler:: GetLossMetric(int slice_id, double cost_in_rbg=1, bool update_metrics=false){
    double calculated_cost = 0;

    // ---------------------------------------------------------------
    // // Choose Average

    double per_rbg_metrics = metrics_perrbg_slice_[slice_id];
    per_rbg_metrics *= cost_in_rbg;
    calculated_cost = per_rbg_metrics;

    // ---------------------------------------------------------------
    // // Choose Clustering

    // std::pair<int, double> cluster_usage_info = cluster_indices_[slice_id];
    // int cluster_id = cluster_usage_info.first;
    // double remaining_portion = cluster_usage_info.second;
    // std::map<int, std::pair<int, double>> slice_cluster = per_slice_clusters[slice_id];
    // int allowed_portion = slice_cluster[cluster_id].first;
    // double avg_metric = slice_cluster[cluster_id].second;
    // calculated_cost = cost_in_rbg * avg_metric;
    // if (update_metrics){
    //   remaining_portion -= cost_in_rbg;
    //   if (remaining_portion <= 0){
    //     cluster_id++;
    //     while (per_slice_clusters[slice_id][cluster_id].first <= 0 && cluster_id < 3){
    //       cluster_id++;
    //     }
    //     remaining_portion = slice_cluster[cluster_id].first;
    //   }
    //   cluster_indices_[slice_id] = std::pair<int, double>(cluster_id, remaining_portion);
    // }


    if (per_slice_past_cost_multiplier[slice_id] != -1){
      calculated_cost *= per_slice_past_cost_multiplier[slice_id];
    }
    // cout << "Cost used: " << calculated_cost << endl;

    return calculated_cost;
}