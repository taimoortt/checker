#ifndef RADIOSABERDOWNLINKSCHEDULER_H_
#define RADIOSABERDOWNLINKSCHEDULER_H_

#include "downlink-packet-scheduler.h"
#include <vector>

class RadioSaberDownlinkScheduler : public DownlinkPacketScheduler {
private:
// the ewma beta for inter-slice scheduling
  const double beta_ = 0.01;
  int current_slice_ = -1;
  

public:
	RadioSaberDownlinkScheduler(std::string config_fname);
	virtual ~RadioSaberDownlinkScheduler();
	virtual double ComputeSchedulingMetric (RadioBearer *bearer, double spectralEfficiency, int subChannel);
  double GetLossMetric(int slice_id, double cost_in_rbg, bool upate_metrics);
  
  std::vector<double> slice_rbgs_quota_;
  std::vector<double> slice_rbs_share_;
  std::vector<double> slice_rbs_offset_;
  std::vector<double> metrics_perrbg_slice_;
  std::vector<std::vector<double>> sorted_metrics_per_rbg_slice_;
  std::vector<double> rollover_slice_quota_;

  // For per user metric tracking
  std::vector<int> cost_indices_;
  std::vector<double> remaining_portions_;

  // For clusters
  std::vector<std::pair<int,double>> cluster_indices_;
  std::vector<int> per_slice_lower_priority_count;
  std::vector<int> per_slice_higher_priority_count;
  std::vector<double> avg_lower_priority_metric;
  std::vector<double> avg_higher_priority_metric;
  std::vector<double> initial_rbg_quota_;
  std::map<int, std::vector<double>> per_slice_past_performance;
  std::vector<std::map<int, std::pair<int, double>>> per_slice_clusters;
  std::map<int, double> per_slice_past_cost_multiplier;


  void CalculateSliceQuota();
  void CalculateMetricsPerRB(int tti, int rb_id = 0);
  double GetHistoricalAvg (int slice_id);
  std::vector<double> actual_slice_rbgs_metric_sum_;
};

#endif
