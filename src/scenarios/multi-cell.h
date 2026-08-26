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

#include "../channel/LteChannel.h"
#include "../phy/enb-lte-phy.h"
#include "../phy/ue-lte-phy.h"
#include "../core/spectrum/bandwidth-manager.h"
#include "../networkTopology/Cell.h"
#include "../device/ENodeB.h"
#include "../protocolStack/packet/packet-burst.h"
#include "../protocolStack/packet/Packet.h"
#include "../core/eventScheduler/simulator.h"
#include "../flows/application/InfiniteBuffer.h"
#include "../flows/application/TraceBased.h"
#include "../flows/application/InternetFlow.h"
#include "../device/IPClassifier/ClassifierParameters.h"
#include "../flows/QoS/QoSParameters.h"
#include "../flows/QoS/QoSForEXP.h"
#include "../flows/QoS/QoSForFLS.h"
#include "../flows/QoS/QoSForM_LWDF.h"
#include "../componentManagers/FrameManager.h"
#include "../utility/seed.h"
#include "../utility/RandomVariable.h"
#include "../utility/UsersDistribution.h"
#include "../channel/propagation-model/macrocell-urban-area-channel-realization.h"
#include "../channel/propagation-model/macrocell-rural-area-channel-realization.h"
#include "../load-parameters.h"
#include <iostream>
#include <queue>
#include <fstream>
#include <stdlib.h>
#include <cstring>
#include <string>
#include <cassert>
#include <jsoncpp/json/json.h>
#include <algorithm>

struct SliceConfig {
  int num_ues;
  int nb_video;
  int nb_internetflow;
  int nb_backlogflow;
  double user_priority;
  std::vector<int>      video_bitrate;    // the video bitrate of a single user
  std::vector<double>   if_bitrate;       // the aggregate data rate of the slice

  SliceConfig(int _nb_video, int _nb_internetflow,
              int _nb_backlogflow)
  : nb_video(_nb_video),
    nb_internetflow(_nb_internetflow),
    nb_backlogflow(_nb_backlogflow),
    user_priority(1.0) {}
};

static void MultiCell (int nbCell, double radius,
                       int nbUE,
                       int nbVoIP, int nbVideo, int nbBE, int nbCBR,
                       int sched_type,
                       int frame_struct,
                       int speed,
		                   double maxDelay, int videoBitRate,
                       std::string config_fname,
                       int num_macro, int num_micro, int inter_micro_distance,
                       double _duration, int _bandwidth,
                       int seed)
{
  // define simulation times
  double duration = _duration;
  double flow_duration = _duration;
  int cluster = 1;
	double bandwidth = _bandwidth;
  cout << "Duration: " << duration << endl;

  // CREATE COMPONENT MANAGER
  Simulator *simulator = Simulator::Init();
  FrameManager *frameManager = FrameManager::Init();
  NetworkManager* nm = NetworkManager::Init();

  // CONFIGURE SEED
  if (seed >= 0)
  {
    int commonSeed = GetCommonSeed (seed);
    srand (commonSeed);
    std::mt19937 gen(commonSeed); // Initialize Mersenne Twister random number 
  }
  else
  {
    srand (time(NULL));
  }
  std::cout << "Simulation with SEED = " << seed << std::endl;
  cout << "Inter Micro distance: " << inter_micro_distance << endl;

  // SET SCHEDULING ALLOCATION SCHEME
  ENodeB::DLSchedulerType downlink_scheduler_type;
  switch (sched_type)
  {
    case 1:
      downlink_scheduler_type = ENodeB::DLScheduler_TYPE_PROPORTIONAL_FAIR;
      std::cout << "Scheduler PF "<< std::endl;
      break;
    case 2:
      downlink_scheduler_type = ENodeB::DLScheduler_TYPE_MLWDF;
      std::cout << "Scheduler MLWDF "<< std::endl;
      break;
    case 3:
      downlink_scheduler_type = ENodeB::DLScheduler_TYPE_EXP;
      std::cout << "Scheduler EXP "<< std::endl;
      break;
    case 4:
      downlink_scheduler_type = ENodeB::DLScheduler_TYPE_FLS;
      std::cout << "Scheduler FLS "<< std::endl;
      break;
    case 5:
      downlink_scheduler_type = ENodeB::DLScheduler_EXP_RULE;
      std::cout << "Scheduler EXP_RULE "<< std::endl;
      break;
    case 6:
      downlink_scheduler_type = ENodeB::DLScheduler_LOG_RULE;
      std::cout << "Scheduler LOG RULE "<< std::endl;
      break;
    case 7:
      downlink_scheduler_type = ENodeB::DLScheduler_NVS;
      std::cout << "Scheduler NVS in multi cells" << std::endl;
      break;
    case 8:
      downlink_scheduler_type = ENodeB::DLScheduler_RADIOSABER;
      std::cout << "Scheduler RadioSaber in multi cells" << std::endl;
      break;
    default:
      downlink_scheduler_type = ENodeB::DLScheduler_TYPE_PROPORTIONAL_FAIR;
      break;
  }

  // SET FRAME STRUCTURE
  FrameManager::FrameStructure frame_structure;
  switch (frame_struct)
  {
    case 1:
      frame_structure = FrameManager::FRAME_STRUCTURE_FDD;
      break;
    case 2:
      frame_structure = FrameManager::FRAME_STRUCTURE_TDD;
      break;
    default:
      frame_structure = FrameManager::FRAME_STRUCTURE_FDD;
      break;
  }
  frameManager->SetFrameStructure(FrameManager::FRAME_STRUCTURE_FDD);

  //Define Application Container
  vector<InfiniteBuffer*> BEApplication;
  vector<TraceBased*> VideoApplication;
  vector<InternetFlow*> IFApplication;

  int destinationPort = 101;
  int applicationID = 0;

  //Create GW
  Gateway *gw = new Gateway ();
  nm->GetGatewayContainer ()->push_back (gw);

  vector<int> user_to_slice;
  vector<int> slice_users;
  vector<SliceConfig> slice_configs;
  int total_ues = 0;

  std::ifstream ifs(config_fname);
  Json::Reader reader;
  Json::Value obj;
  if (!reader.parse(ifs, obj)) {
    throw std::runtime_error("Error, failed to parse the json config file.");
  }
  const Json::Value& ues_per_slice = obj["ues_per_slice"];
  int num_slices = ues_per_slice.size();
  for (int i = 0; i < num_slices; i++) {
    int num_ue = ues_per_slice[i].asInt();
    for (int j = 0; j < num_ue; j++) {
      user_to_slice.push_back(i);
    }
    slice_users.push_back(num_ue);
    total_ues += num_ue;
  }
  const Json::Value& slice_schemes = obj["slices"];
  for (int i = 0; i < slice_schemes.size(); i++) {
    int n_slices = slice_schemes[i]["n_slices"].asInt();
    for (int j = 0; j < n_slices; j++) {
      // Assume a one flow per user setting
      int num_ue = slice_schemes[i]["num_ues"].asInt();
      int video_apps = slice_schemes[i]["video_app"].asInt();
      int internet_flows = slice_schemes[i]["internet_flow"].asInt();
      int backlog_flows = slice_schemes[i]["backlog_flow"].asInt();
      SliceConfig config(video_apps, internet_flows, backlog_flows);
      config.num_ues = num_ue;
      if (slice_schemes[i].isMember("user_priority")) {
        config.user_priority = slice_schemes[i]["user_priority"].asDouble();
      }
      cout 
      // << "Num UE: " << num_ue 
      << " Video Apps: " << video_apps 
      << " Internet Flows: " << internet_flows 
      << " Backlog Flows: " << backlog_flows << endl;
      assert(num_ue == slice_users[i]);
      assert(num_ue == video_apps + internet_flows + backlog_flows);


      const Json::Value& video_bitrate = slice_schemes[i]["video_bitrate"];
      for (int k = 0; k < video_bitrate.size(); k++) {
        config.video_bitrate.push_back(video_bitrate[k].asInt());
      }
      const Json::Value& if_bitrate = slice_schemes[i]["if_bitrate"];
      for (int k = 0; k < if_bitrate.size(); k++) {
        config.if_bitrate.push_back(if_bitrate[k].asDouble());
      }
      slice_configs.push_back(config);
    }
  }

  cout << "Total Slices: " << slice_configs.size() << endl;



  //create cells
  assert(num_macro * num_micro + num_macro == nbCell);
  nm->SetNbMacroCell(num_macro);
  std::vector <Cell*> *cells = new std::vector <Cell*>;
  int cell_id = 0;
  int i = 0;
  for (; i < num_macro; i++)
  {
    CartesianCoordinates center =
      GetCartesianCoordinatesForCell(i, radius * 1000.0); //Multiply with 1000 to convert to metres

    Cell *c = new Cell (cell_id, radius, 0.035, center.GetCoordinateX (), center.GetCoordinateY ());
    cells->push_back (c);
    nm->GetCellContainer ()->push_back (c);
    cell_id++;

    std::cout << "Created Cell, id " << c->GetIdCell ()
      <<", position: " << c->GetCellCenterPosition ()->GetCoordinateX ()
      << " " << c->GetCellCenterPosition ()->GetCoordinateY () << std::endl;
  }
  #ifndef HOTSPOT_DEPLOYMENT
  for (; i < num_micro + num_macro; i++)
  {
    CartesianCoordinates center =
      GetCartesianCoordinatesForCell(i, (radius/5) * 1000.0); //Multiply with 1000 to convert to metres

    Cell *c = new Cell (i, radius/5, 0.035, center.GetCoordinateX (), center.GetCoordinateY ());
    cells->push_back (c);
    nm->GetCellContainer ()->push_back (c);

    std::cout << "Created Micro, id " << c->GetIdCell ()
      <<", position: " << c->GetCellCenterPosition ()->GetCoordinateX ()
      << " " << c->GetCellCenterPosition ()->GetCoordinateY () << std::endl;
  }
  #endif
  #ifdef HOTSPOT_DEPLOYMENT
  for (int i = 0; i < num_macro; i++){
    Cell *c = cells->at(i);
    vector<CartesianCoordinates*>* micro_coordinates =
      GetCartesianCoordinatesForMicro(c->GetCellCenterPosition(), num_micro, radius*1000, inter_micro_distance);
    for (int k = 0; k < micro_coordinates->size(); k++){
      CartesianCoordinates *micro_cell = micro_coordinates->at(k);
      Cell *c = new Cell (cell_id, radius/5, 0.035, micro_cell->GetCoordinateX (), micro_cell->GetCoordinateY ());
      cells->push_back (c);
      nm->GetCellContainer ()->push_back (c);
      cell_id++;
      cout << "Created Micro, id " << c->GetIdCell ()
        <<", position: " << c->GetCellCenterPosition ()->GetCoordinateX ()
        << " " << c->GetCellCenterPosition ()->GetCoordinateY () << endl;
    }
  }
  #endif

  // std::vector <BandwidthManager*> spectrum_macro = RunFrequencyReuseTechniques (num_macro, cluster, 50);
  // std::vector <BandwidthManager*> spectrum_micro = RunFrequencyReuseTechniques (num_micro, cluster, 50, 252);
  std::vector <BandwidthManager*> spectrums = RunFrequencyReuseTechniques (nbCell, cluster, bandwidth);

  //Create a set of a couple of channels
  std::vector <LteChannel*> *dlChannels = new std::vector <LteChannel*>;
  std::vector <LteChannel*> *ulChannels = new std::vector <LteChannel*>;
  for (int i = 0; i < nbCell; i++) {
    LteChannel *dlCh = new LteChannel ();
    dlCh->SetChannelId (i);
    dlChannels->push_back (dlCh);

    LteChannel *ulCh = new LteChannel ();
    ulCh->SetChannelId (i);
    ulChannels->push_back (ulCh);
  }

  //create eNBs
  std::vector <ENodeB*> *eNBs = new std::vector <ENodeB*>;
  i = 0;
  for (; i < num_macro; i++) {
    ENodeB* enb = new ENodeB (i, cells->at (i));
    enb->GetPhy()->SetTxPower(49);
    enb->GetPhy ()->SetDlChannel (dlChannels->at (i));
    enb->GetPhy ()->SetUlChannel (ulChannels->at (i));
    enb->SetDLScheduler (downlink_scheduler_type, config_fname);
    enb->GetPhy ()->SetBandwidthManager (spectrums.at (i));

    std::cout << "Created Macro enb, id " << enb->GetIDNetworkNode()
      << ", cell id " << enb->GetCell ()->GetIdCell ()
      <<", position: " << enb->GetMobilityModel ()->GetAbsolutePosition ()->GetCoordinateX ()
      << " " << enb->GetMobilityModel ()->GetAbsolutePosition ()->GetCoordinateY ()
      << ", channels id " << enb->GetPhy ()->GetDlChannel ()->GetChannelId ()
      << enb->GetPhy ()->GetUlChannel ()->GetChannelId ()  << std::endl;

    spectrums.at (i)->Print ();
    ulChannels->at (i)->AddDevice((NetworkNode*) enb);
    nm->GetENodeBContainer ()->push_back (enb);
    eNBs->push_back (enb);
  }

  #ifndef HOTSPOT_DEPLOYMENT
  for (;i<num_micro + num_macro; i++){
  #endif
  #ifdef HOTSPOT_DEPLOYMENT
  for (;i<(num_micro * num_macro)+num_macro; i++){
  #endif
    ENodeB* enb = new ENodeB (i, cells->at (i));
    enb->GetPhy()->SetTxPower(35);
    enb->GetPhy ()->SetDlChannel (dlChannels->at (i));
    enb->GetPhy ()->SetUlChannel (ulChannels->at (i));
    enb->SetDLScheduler (downlink_scheduler_type, config_fname);
    enb->GetPhy ()->SetBandwidthManager (spectrums.at (i));

    std::cout << "Created Micro enb, id " << enb->GetIDNetworkNode()
      << ", cell id " << enb->GetCell ()->GetIdCell ()
      <<", position: " << enb->GetMobilityModel ()->GetAbsolutePosition ()->GetCoordinateX ()
      << " " << enb->GetMobilityModel ()->GetAbsolutePosition ()->GetCoordinateY ()
      << ", channels id " << enb->GetPhy ()->GetDlChannel ()->GetChannelId ()
      << enb->GetPhy ()->GetUlChannel ()->GetChannelId ()  << std::endl;

    spectrums.at (i)->Print ();
    ulChannels->at (i)->AddDevice((NetworkNode*) enb);
    nm->GetENodeBContainer ()->push_back (enb);
    eNBs->push_back (enb);
  }

  //nbUE is the number of users that are into each cell at the beginning of the simulation
  int idUE = 0;
  #ifndef HOTSPOT_DEPLOYMENT
  vector<CartesianCoordinates*> *positions = GetUniformUsersDistribution (j, total_ues);
  #endif
  #ifdef HOTSPOT_DEPLOYMENT
  vector<CartesianCoordinates*> *positions = GetHotspotUsersDistribution (total_ues);
  // std::vector<CartesianCoordinates*> *positions = new vector<CartesianCoordinates*>;  // Initialize the vector

  // CartesianCoordinates* x = new CartesianCoordinates(50., 0.);
  // x->SetCellID(0);
  // positions->push_back(x);

  // x = new CartesianCoordinates(-350., 450.);
  // x->SetCellID(1);
  // positions->push_back(x);

  // x = new CartesianCoordinates(-350., 100.);
  // x->SetCellID(1);
  // positions->push_back(x);

  // x = new CartesianCoordinates(-70., 550.);
  // x->SetCellID(0);
  // positions->push_back(x);

  // Get User Mobility Patterns
  const Json::Value& mobility_speed = obj["mobility_speed"];
  const Json::Value& mobility_distribution = obj["mobility_distribution"];

  std::vector<int> speeds;
  std::vector<double> proportions;

  // print the mobility speed and distribution
  std::cout << "Mobility Speeds: ";
  for (const auto& speed : mobility_speed) {
      std::cout << speed.asInt() << " ";
  }
  std::cout << std::endl;

  std::cout << "Mobility Distribution: ";
  for (const auto& proportion : mobility_distribution) {
      std::cout << proportion.asDouble() << " ";
  }
  std::cout << std::endl;
  

  for (const auto& speed : mobility_speed) {
      speeds.push_back(speed.asInt());
  }

  for (const auto& proportion : mobility_distribution) {
      proportions.push_back(proportion.asDouble());
  }

  // assert that sum of proportions is 1
  double sum = 0;
  for (double proportion : proportions) {
      sum += proportion;
  }
  cout << "Sum: " << sum << endl;
  const double epsilon = 1e-9; // Adjust this based on the required precision
  assert(std::abs(sum - 1.0) < epsilon);
  // assert that number of speeds and proportions match
  assert(speeds.size() == proportions.size());

  // Generate the mobility array
  std::vector<int> mobility_array;

  for (size_t i = 0; i < speeds.size(); i++) {
      int count = std::round(proportions[i] * total_ues); // Calculate count based on proportion
      mobility_array.insert(mobility_array.end(), count, speeds[i]); // Add 'count' elements of speeds[i]
  }

  // Shuffle the array using a local random number generator
  std::mt19937 rng(seed); // Use a fixed seed value
  std::shuffle(mobility_array.begin(), mobility_array.end(), rng);

  // Ensure the array has exactly total_ues elements
  while (mobility_array.size() > total_ues) mobility_array.pop_back();
  while (mobility_array.size() < total_ues) mobility_array.push_back(speeds[0]); // Fill with the first speed if needed

  // Print the result
  std::cout << "Generated Mobility Array: ";
  for (int speed : mobility_array) {
      std::cout << speed << " ";
  }
  std::cout << std::endl;

  // print counts of each speed value in the array
  std::map<int, int> counts;
  for (int speed : mobility_array) {
      counts[speed]++;
  }

  std::cout << "Speed Counts: ";
  for (const auto& pair : counts) {
      std::cout << pair.first << ": " << pair.second << " ";
  }
  std::cout << std::endl;


  #endif
  double start_time = 0.1;
  double duration_time = start_time + flow_duration;
  std::vector<UserEquipment*> ues;
  //Create UEs
  for (int i = 0; i < total_ues; i++) {
    //ue's random position
    double posX = positions->at (i)->GetCoordinateX ();
    double posY = positions->at (i)->GetCoordinateY ();
    double speedDirection;
    if (speed == 0) {
      speedDirection = 0;
    }
    else {
      speedDirection = (double)(rand() %360) * ((2*3.14)/360);
    }
    // int serve_cell = positions->at(i)->GetCellID();
    int serve_cell = 0;
    speed = mobility_array[i];
    enum Mobility::MobilityModel mobility;
    if (speed == 0) {
      mobility = Mobility::CONSTANT_POSITION;
    } else {
      mobility = Mobility::RANDOM_WALK;
    }
    
    if (speed == 0){}
    UserEquipment* ue = new UserEquipment (idUE,
        posX, posY, speed, speedDirection,
        cells->at (serve_cell),
        eNBs->at (serve_cell),
        1, //HO activated!
        mobility);
    //Mobility::RANDOM_DIRECTION);
    cout << "Setting user " << i << " to mobility: " << speed << endl;
    ue->SetSliceID(user_to_slice[idUE]);
    cout << "Setting User: " << ue->GetIDNetworkNode() << " to Slice: " << user_to_slice[idUE] << endl;
    ue->SetNbCells(nm->GetCellContainer ()->size());
    #ifdef USE_REAL_TRACE
    // cout << "Using Real Trace\n";
    ue->CalculateUpdatedRSRP();
    #endif
    std::cout << "Created UE - id " << idUE << " position " << posX << " " << posY
      << ", cell " <<  ue->GetCell ()->GetIdCell ()
      << ", target enb " << ue->GetTargetNode ()->GetIDNetworkNode () << std::endl;
    ue->GetPhy ()->SetDlChannel (eNBs->at (serve_cell)->GetPhy ()->GetDlChannel ());
    ue->GetPhy ()->SetUlChannel (eNBs->at (serve_cell)->GetPhy ()->GetUlChannel ());
    FullbandCqiManager *cqiManager = new FullbandCqiManager ();
    cqiManager->SetCqiReportingMode (CqiManager::PERIODIC);
    cqiManager->SetReportingInterval (1);
    cqiManager->SetDevice (ue);
    ue->SetCqiManager (cqiManager);
    nm->GetUserEquipmentContainer ()->push_back (ue);
    // register ue to the enb
    eNBs->at (serve_cell)->RegisterUserEquipment (ue);
    // define the channel realization
    MacroCellUrbanAreaChannelRealization* c_dl = new MacroCellUrbanAreaChannelRealization (eNBs->at (serve_cell), ue);
    eNBs->at (serve_cell)->GetPhy ()->GetDlChannel ()->GetPropagationLossModel ()->AddChannelRealization (c_dl);
    MacroCellUrbanAreaChannelRealization* c_ul = new MacroCellUrbanAreaChannelRealization (ue, eNBs->at (serve_cell));
    eNBs->at (serve_cell)->GetPhy ()->GetUlChannel ()->GetPropagationLossModel ()->AddChannelRealization (c_ul);
    ues.push_back(ue);
    // CREATE DOWNLINK APPLICATION FOR THIS UE
        
    SliceConfig config = slice_configs[user_to_slice[idUE]];
    ue->UpdateUserPosition(start_time);
    idUE++;
  }
  int slice_counter = 0;
  ENodeB* enb;
  int counter = 0;
  for (int i = 0; i < eNBs->size(); i++) {
    enb = eNBs->at(i);
    std::vector<ENodeB::UserEquipmentRecord*> ue_records = *enb->GetUserEquipmentRecords();
    cout << "Cell: " << enb->GetIDNetworkNode() << " has " << ue_records.size() << " UEs" << endl;
    for (int j = 0; j < ue_records.size(); j++){
      UserEquipment* ue = ue_records[j]->GetUE();
      // cout << "UE: " << ue->GetIDNetworkNode() << " Cell: " << enb->GetIDNetworkNode() << " Slice: " << user_to_slice[ue->GetIDNetworkNode()] << endl;
      user_to_slice[ue->GetIDNetworkNode()] = slice_counter % num_slices;
      ue->SetSliceID(slice_counter % num_slices);
      cout << "Counter: "<< counter << " Reassigning UE: " << ue->GetIDNetworkNode() << " to Slice: " << user_to_slice[ue->GetIDNetworkNode()] << endl;
      slice_counter++;
      counter++;
    }
    slice_counter = 0;
  }

  for (int i = 0; i < slice_configs.size(); i++){
    int slice_ues = 0;
    int expected_ues = slice_configs[i].num_ues;
    for (int j = 0; j < total_ues; j++){
      if (user_to_slice[j] == i){
        slice_ues++;
      }
    }
    cout << "Slice " << i << " has " << slice_ues << " UEs with Expected: " << expected_ues << endl;
    // assert(slice_ues == expected_ues);
  }

  for (int i = 0; i < slice_configs.size(); i++){
    SliceConfig config = slice_configs[i];
    cout << "Slice config: " << i << " has " << config.num_ues << " UEs" << endl;
    int nb_video = config.nb_video;
    int nb_internetflow = config.nb_internetflow;
    int nb_backlogflow = config.nb_backlogflow;
    int num_ues = config.num_ues;
    std::vector<char> slice_flows;
    for (int j = 0; j < config.nb_video; j++){
      slice_flows.push_back('v');
    }
    for (int j = 0; j < config.nb_internetflow; j++){
      slice_flows.push_back('i');
    }
    for (int j = 0; j < config.nb_backlogflow+5; j++){
      slice_flows.push_back('b');
    }
    cout << "Total Flow Apps: " << slice_flows.size() << endl;
    // print slice_flows
    cout << "Slice Flows Array: " << endl;
    for (int j = 0; j < slice_flows.size(); j++){
      cout << slice_flows[j] << " ";
    }
    // cout << endl;
    // assert(slice_flows.size() == num_ues);

    int flow_iterator = 0;

    for (int j = 0; j < nbUE; j++){
      UserEquipment* ue = ues[j];
      int idUE = ue->GetIDNetworkNode();
      if (ue->GetSliceID() == i){
        cout << "UE: " << idUE << " at Cell: " << ue->GetCell()->GetIdCell() << " is in Slice: " << i << endl;
        char app = slice_flows[flow_iterator];
        cout << "Flow iterator: " << flow_iterator << " UE: " << idUE << " is getting app: " << app << endl;

        if (app == 'v'){
          // *** video application
          // create application
          TraceBased* video_app = new TraceBased();
          VideoApplication.push_back(video_app);
          video_app->SetSource(gw);
          video_app->SetDestination(ue);
          video_app->SetApplicationID(idUE);
          video_app->SetStartTime(start_time);
          video_app->SetStopTime(duration_time);
          string video_trace ("foreman_H264_");
          //string video_trace ("highway_H264_");
          //string video_trace ("mobile_H264_");
          switch (videoBitRate)
          {
            case 128:
              {
                string _file (path + "src/flows/application/Trace/" + video_trace + "128k.dat");
                cout << "File Name: " << _file << endl;
                video_app->SetTraceFile(_file);
                std::cout << "		selected video @ 128k"<< std::endl;
                break;
              }
            case 242:
              {
                string _file (path + "src/flows/application/Trace/" + video_trace + "242k.dat");
                video_app->SetTraceFile(_file);
                std::cout << "		selected video @ 242k"<< std::endl;
                break;
              }
            case 440:
              {
                string _file (path + "src/flows/application/Trace/" + video_trace + "440k.dat");
                video_app->SetTraceFile(_file);
                std::cout << "		selected video @ 440k"<< std::endl;
                break;
              }
            default:
              {
                string _file (path + "src/flows/application/Trace/" + video_trace + "128k.dat");
                video_app->SetTraceFile(_file);
                std::cout << "		selected video @ 128k as default"<< std::endl;
                break;
              }
          }
          //create classifier parameters
          ClassifierParameters *cp = new ClassifierParameters(
            gw->GetIDNetworkNode(),
            ue->GetIDNetworkNode(),
            0,
            destinationPort,
            TransportProtocol::TRANSPORT_PROTOCOL_TYPE_UDP);
          video_app->SetClassifierParameters(cp);
          std::cout << "CREATED VIDEO APPLICATION, ID " << idUE << std::endl;
          //update counter
          destinationPort++;
          applicationID++;
        } else if (app == 'i'){ 
          // heavy tail distributed internet flows with const bitrate
          double flow_rate = config.if_bitrate[0] / slice_users[user_to_slice[idUE]];
          InternetFlow* ip_app = new InternetFlow(seed+idUE);
          IFApplication.push_back(ip_app);
          ip_app->SetSource(gw);
          ip_app->SetDestination(ue);
          ip_app->SetApplicationID(idUE);
          ip_app->SetStartTime(start_time);
          ip_app->SetStopTime(duration_time);
          ip_app->SetAvgRate(flow_rate);
          ip_app->SetPriority(config.user_priority);

          QoSParameters* qosParameters = new QoSParameters();
          ip_app->SetQoSParameters(qosParameters);

          ClassifierParameters* cp = new ClassifierParameters(
            gw->GetIDNetworkNode(),
            ue->GetIDNetworkNode(),
            0,
            destinationPort,
            TransportProtocol::TRANSPORT_PROTOCOL_TYPE_UDP
          );
          ip_app->SetClassifierParameters(cp);

          std::cout << "CREATED InternetFlow APPLICATION, ID " << idUE
            << " Flow Rate: " << flow_rate << " Mbps"
            << " Priority: " << config.user_priority << std::endl;

          destinationPort ++;
          applicationID ++;
        } else if (app == 'b'){
      // *** be application
        // create application
        InfiniteBuffer* be_app = new InfiniteBuffer();
        BEApplication.push_back(be_app);
        be_app->SetSource(gw);
        be_app->SetDestination(ue);
        be_app->SetApplicationID(idUE);
        be_app->SetStartTime(start_time);
        be_app->SetStopTime(duration_time);
        // create qos parameters
        QoSParameters *qosParameters = new QoSParameters ();
        be_app->SetQoSParameters (qosParameters);
        //create classifier parameters
        ClassifierParameters *cp = new ClassifierParameters(
          gw->GetIDNetworkNode(),
          ue->GetIDNetworkNode(),
          0,
          destinationPort,
          TransportProtocol::TRANSPORT_PROTOCOL_TYPE_UDP);
        be_app->SetClassifierParameters (cp);
        std::cout << "CREATED BE APPLICATION, ID " << idUE << std::endl;
        //update counter
        destinationPort++;
        applicationID++;
      }

        flow_iterator = (flow_iterator + 1) % slice_flows.size();
      }
    }
  }
  simulator->SetStop(duration);
  // simulator->Schedule(duration-10, &Simulator::PrintMemoryUsage, simulator);
  simulator->Run ();

  //Delete created objects
  cells->clear ();
  delete cells;
  eNBs->clear ();
  delete eNBs;
  delete frameManager;
  //delete nm;
  delete simulator;
  delete dlChannels;
  delete ulChannels;

  for (auto it = VideoApplication.begin(); it != VideoApplication.end(); ++it)
    delete *it;
  for (auto it = BEApplication.begin(); it != BEApplication.end(); ++it)
    delete *it;
  for (auto it = IFApplication.begin(); it != IFApplication.end(); ++it)
    delete *it;
}
