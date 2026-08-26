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

#ifndef CQIMANAGER_H_
#define CQIMANAGER_H_

#include <vector>
#include <map>
#include <cmath>

class NetworkNode;

struct SinrReport {
//   double sinr;
//   double sinr_with_mute;
//   int neighbor_cell;  // if neighbor_cell is -1, we don't mute

//   SinrReport(double _sinr, double _sinr_with_mute, int _neighbor_cell)
//   : sinr(_sinr), sinr_with_mute(_sinr_with_mute),
//     neighbor_cell(_neighbor_cell) {}
	double sinr;
	double sinr_mute_one;
	double sinr_mute_two;
	double sinr_mute_both;
	int cell_one;
	int cell_two;
	SinrReport(double _sinr, double _sinr_mute_one,
		double _sinr_mute_two, double _sinr_mute_both,
		int _cell_one, int _cell_two) {
		sinr = _sinr;
		sinr_mute_one = _sinr_mute_one;
		sinr_mute_two = _sinr_mute_two;
		sinr_mute_both = _sinr_mute_both;
		cell_one = _cell_one;
		cell_two = _cell_two;
	}
};

struct RSRPReport {
	// for every RB, the received power
	std::vector<double> rx_power;
	std::map<int, std::vector<double>> rsrp_interference;
	double device_noise;
	// for every RB, the final noise plus interference
	std::vector<double> noise_interfere_watt;
	std::vector<int> final_cqi;
	int serve_node;
	RSRPReport() {}
	
	RSRPReport(std::vector<double>& _rx_power,
		std::map<int, std::vector<double>>& _rsrp_interference,
		double _device_noise, int _serve_node) {
		rx_power = _rx_power;
		rsrp_interference = _rsrp_interference;
		device_noise = _device_noise;
		serve_node = _serve_node;
		std::vector<double>noise_interfere_tot(_rx_power.size(), pow(10., _device_noise/10));
		for (int i = 0; i < _rx_power.size(); i++) {
			for(auto it = rsrp_interference.begin(); it != rsrp_interference.end(); it++) {
				if (it->first != _serve_node){
					noise_interfere_tot[i] += pow(10, it->second[i]/10);
				}
			}
		}
		noise_interfere_watt = noise_interfere_tot;
		final_cqi = std::vector<int>(
			_rx_power.size(), 0);
	}
};

class CqiManager {
public:

    enum CQIReportingMode
      {
        PERIODIC,
        APERIODIC
      };

	CqiManager();
	virtual ~CqiManager();

	void SetDevice (NetworkNode* d);
	NetworkNode* GetDevice (void);

	void SetCqiReportingMode (CQIReportingMode m);
	CQIReportingMode GetCqiReportingMode (void);

	void SetSendCqi (bool b);
	bool GetSendCqi (void);

	void SetReportingInterval (int i);
	int GetReportingInterval (void);

	void SetLastSent ();
	long int GetLastSent (void);

	virtual void CreateCqiFeedbacks (std::vector<double> sinr) = 0;
  	virtual void CreateCqiFeedbacks (std::vector<SinrReport> cqi_records) = 0;
	virtual void CreateCqiFeedbacks (RSRPReport rsrp_record) = 0;

	bool NeedToSendFeedbacks (void);

private:
	CQIReportingMode m_reportingMode;

	bool m_sendCqi; //Used with aperiodic reporting. It is set to true by the eNB !

	int m_reportingInterval;
	long int m_lastSent;

	NetworkNode* m_device;

  std::map<int, double> neighbor_rsrp;

};

#endif /* CQIMANAGER_H_ */
