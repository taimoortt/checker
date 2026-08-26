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



#ifndef FRAMEMANAGER_H_
#define FRAMEMANAGER_H_

#include <iostream>
#include "NetworkManager.h"
#include "../core/eventScheduler/simulator.h"
#include "../protocolStack/mac/packet-scheduler/downlink-packet-scheduler.h"
#include "TDDConfiguration.h"


 /*
   * LTE Frame Structure:
   *
   *  ** Frame structure type 1 (FS1): FDD
   *    ...
   *  ** Frame structure type 2 (FS2): TDD
   *    ...
   */
struct Allocation {
  std::vector<FlowToSchedule*> cell_flow;
  double sum_metric;
  double sum_tbs;
  std::map<int, double> per_cell_metric_map;
  std::map<int, int> per_cell_slice_map;
  std::map<int, double> per_slice_metric_map;
  bool success = false;
  std::vector<int> empty_cells;
  double percentage_improvement = 0;
};

class FrameManager {
public:
	static const int RBG_SIZE = 8;
	enum FrameStructure
	  {
		FRAME_STRUCTURE_FDD,
		FRAME_STRUCTURE_TDD
	  };
private:
	FrameStructure m_frameStructure;

	int m_TDDFrameConfiguration;

	int m_nbFrames;
	int m_nbSubframes;
	unsigned long m_TTICounter;

	FrameManager();
	static FrameManager *ptr;

public:
	//FrameManager();
	virtual ~FrameManager();

	static FrameManager*
	Init (void)
	  {
		if (ptr==NULL)
	      {
		    ptr = new FrameManager;
	   	  }
		return ptr;
	  }

	void
	SetFrameStructure (FrameStructure frameStructure);
	FrameStructure
	GetFrameStructure (void) const;

	void
	SetTDDFrameConfiguration (int configuration);
	int
	GetTDDFrameConfiguration (void) const;

	int
	GetSubFrameType (int nbSubFrame);

	void
	UpdateNbFrames (void);
	int
	GetNbFrames (void) const;
	void
	UpdateNbSubframes (void);
	void
	ResetNbSubframes (void);
	int
	GetNbSubframes (void) const;
	void
	UpdateTTIcounter (void);
	unsigned long
	GetTTICounter (void) const;

	void
	Start (void);
	void
	StartFrame (void);
	void
	StopFrame (void);
	void
	StartSubframe (void);
	void
	StopSubframe (void);

	NetworkManager* GetNetworkManager (void);

	void UpdateUserPosition(void);
	void ResourceAllocation(void);

  void CentralResourceAllocation(void);

  void CentralDownlinkRBsAllocation(void);
  void TuneWeightsAcrossCells(std::vector<DownlinkPacketScheduler*>&);
  void ReassignUserToSlice(std::vector<DownlinkPacketScheduler*>&);

	void UpdateUserPriorityPerApp(std::vector<DownlinkPacketScheduler*>& schedulers, int priority);
  	void UpdateUserPriorityPerUser(std::vector<DownlinkPacketScheduler*>& schedulers,
  	int cell_id, int slice_id,int priority);

  /*
   * Baseline helper:
   * Computes per-cell, per-slice RB quota ordering for Slice-Served baseline.
   */
  void CalcSliceServedQuota(std::vector<DownlinkPacketScheduler*>&);

  /*
   * Baseline helper:
   * Performs one-RBG exhaustive muting decision for Slice-Served baseline.
   */
  void SliceServedAllocateOneRBExhaustMute(
	  std::vector<DownlinkPacketScheduler*>& schedulers,
	  int rb_id
  );
  void SliceServedAllocateOneRBHeuristicMute(
	  std::vector<DownlinkPacketScheduler*>& schedulers,
	  int rb_id
  );

  void RadioSaberAllocateOneRBMute(
    std::vector<DownlinkPacketScheduler*>& schedulers,
    int rb_id
  );

  /*
  This function updates the noise_interference_watt of all flows
  due to muting the @mute_id cell
  @param rb_id: the allocated resource block id
  @param mute_id: the index of the muted cell
  */
  void UpdateNoiseInterferenceWithMute(
    std::vector<DownlinkPacketScheduler*>& schedulers,
    int rb_id, int mute_id = -1
  );

  Allocation DoAllocation (
    std::vector<DownlinkPacketScheduler*>& schedulers,
    int rb_id, int mute_id = -1, bool mute_cell = false,
	std::map<int, int> slice_map = std::map<int, int>(),
	std::vector<int> global_cells_muted = std::vector<int>(),
	std::vector<FlowToSchedule*> prior_cell_flow = std::vector<FlowToSchedule*>()
  );

  /*
   * Research baseline:
   * Single-objective exhaustive muting (used for comparison).
   */
  void SingleObjMutingExhaust(
    std::vector<DownlinkPacketScheduler*>& schedulers,
    int rb_id
  );

  void SingleObjMutingOneCell(
    std::vector<DownlinkPacketScheduler*>& schedulers, 
    int rb_id
  );

  void MultiObjMutingOld(
    std::vector<DownlinkPacketScheduler*>& schedulers,
    int rb_id
  );

  /*
   * Research contribution:
   * Multi-objective muting policy used in the artifact.
   * Returns ordered muted-cell IDs for this RB allocation step.
   */
  std::vector<int> MultiObjMuting(
	std::vector<DownlinkPacketScheduler*>& schedulers,
	int rb_id
  );


  std::map<int, double> MultiObjMutingExhaustiveRealloc(
  std::vector<DownlinkPacketScheduler*>& schedulers,
    int rb_id, std::map<int, double> per_slice_metric_map
  );

  std::map<int, double> DoExhaustiveAllocation(
  std::vector<DownlinkPacketScheduler*>& schedulers,
    int rb_id, std::map<int, int> per_cell_slice_map,
    int aggressor_cell = -1, std::vector<int> benefitting_slices = std::vector<int>()
  );
  
  std::vector<int> GetExhaustiveMuteOrder(
  std::vector<DownlinkPacketScheduler*>& schedulers,
	int rb_id);
  
  bool EvaluateTradeoff(
	  DownlinkPacketScheduler* muted_cell,
	  double cost_in_rbs, int slice_id, double gain_in_metric
  );

  /*
  Deprecated function
  This function calculates the utility metrics of those slices that benefit
  from muting cell on the muted cell
  @param muted_cell: the muted cell id
  @param rbgs_deduction: key: id of those slices which benefit; value: rbgs quota deduction
  @return: key: id of those slice; value: sum of utility metrics on the muted cell
  */
  std::map<int, double> GetMetricsOnMutedCell(
	  DownlinkPacketScheduler* muted_cell,
	  std::map<int, double>& rbgs_deduction,
	  int rb_id
  );
};

#endif /* FRAMEMANAGER_H_ */
