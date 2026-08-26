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


#ifndef CELLPOSITION_H_
#define CELLPOSITION_H_

#include "../core/cartesianCoodrdinates/CartesianCoordinates.h"
#include <math.h>
#include <vector>

static double cellPosition[19][2] = {

		//Radius = 500 m

		//Center
		{0., 0.},				// cellID = 0

		//First Loop
		{0.,866.},				// cellID = 1
		{-750.,433.},			// cellID = 2
		{-750,-433},			// cellID = 3
		{0.,-866.},				// cellID = 4
		{750.,-433.},			// cellID = 5
		{750.,433.},			// cellID = 6

		//Second Loop
		{0.,1732.},				// cellID = 7
		{-750.,1299.},				// cellID = 8
		{-1500.,866.},				// cellID = 9
		{-1500.,0.},				// cellID = 10
		{-1500.,-866.},				// cellID = 11
		{-750.,-1299.},				// cellID = 12
		{0.,-1732},				// cellID = 13
		{750.,-1299.},				// cellID = 14
		{1500.,-866.},				// cellID = 15
		{1500.,0},				// cellID = 16
		{1500.,866.},				// cellID = 17
		{750.,1299.},				// cellID = 18
};

static double
GetMaxDistanceFromCenter (int nbCell, double radius)
{
  double maxPosition;

  switch (nbCell)
    {
      case 1:
        maxPosition = radius;
        break;
      case 7:
    	maxPosition = (radius * (sqrt(3)/2)) * 3;
    	break;
      case 19:
    	maxPosition = (radius * (sqrt(3)/2)) * 5;
    	break;
      default:
    	maxPosition = radius;
    	break;
    }
  return maxPosition;
}

static CartesianCoordinates
GetCartesianCoordinatesHex(int idCell, double radius)
{
	double x, y;
  switch(idCell) {
    case 0:
      x = 0;
      y = radius;
      break;
    case 1:
      x = -sqrt(3)/2 * radius;
      y = radius / 2;
      break;
    case 2:
      x = -sqrt(3)/2 * radius;
      y = -radius / 2;
      break;
    case 3:
      x = 0;
      y = -radius;
      break;
    case 4:
      x = sqrt(3)/2 * radius;
      y = -radius / 2;
      break;
    case 5:
      x = sqrt(3)/2 * radius;
      y = radius / 2;
      break;
  }
  CartesianCoordinates coordinates;
  coordinates.SetCoordinates(x,y);
  return coordinates;
}

static CartesianCoordinates
GetCartesianCoordinatesForCell (int idCell, double radius)
{
  double x;
  double y;

  switch (idCell)
    {
      case 0:
    	  x = 0;
    	  y = 0;
    	  break;

      case 1:
    	  x = 0;
    	  y = radius * sqrt(3);
          break;

      case 2:
    	  x = - (radius + (radius/2));
    	  y = radius * (sqrt(3)/2);
          break;

      case 3:
    	  x = - (radius + (radius/2));
    	  y = - (radius * (sqrt(3)/2));
          break;

      case 4:
    	  x = 0;
    	  y = - (radius * sqrt(3));
          break;

      case 5:
    	  x = radius + (radius/2);
    	  y = - (radius * (sqrt(3)/2));
          break;

      case 6:
    	   x = radius + (radius/2);
    	   y = radius * (sqrt(3)/2);
           break;

      case 7:
    	  x = 0;
    	  y = 2 * (radius * sqrt(3));
          break;

      case 8:
    	  x = 2 * - (radius + (radius/2));
    	  y = 2 * radius * (sqrt(3)/2);
          break;

      case 9:
    	  x = 2 * - (radius + (radius/2));
    	  y = 2 * - (radius * (sqrt(3)/2));
          break;

      case 10:
    	  x = 2 * 0;
    	  y = 2 * - (radius * sqrt(3));
          break;

      case 11:
    	  x = 2 * radius + (radius/2);
    	  y = 2 * - (radius * (sqrt(3)/2));
          break;

      case 12:
    	   x = 2 * radius + (radius/2);
    	   y = 2 * radius * (sqrt(3)/2);
           break;

      // case 7:
    	//    x = 0;
    	//    y = 2 * (radius * sqrt(3));
      //     	  break;

      // case 8:
    	//    x = - (radius + (radius/2));
    	//    y = 3 * (radius * (sqrt(3)/2));
      //     	  break;

      // case 9:
    	//    x = - (3 * radius);
    	//    y = radius * sqrt(3);
      //     	  break;

      // case 10:
    	//   x = - (3 * radius);
    	//   y = 0;
      //     break;

      // case 11:
    	//    x = - (3 * radius);
    	//    y = - (radius * sqrt(3));
      //     	  break;

      // case 12:
    	//   x = - (radius + (radius/2));
    	//    y = - (3 * (radius * (sqrt(3)/2)));
      //     	  break;

      case 13:
    	   x = 0;
    	   y = - (2 * (radius * sqrt(3)));
           break;

      case 14:
    	  x = radius + (radius/2);
    	  y = - (3 * (radius * (sqrt(3)/2)));
          break;

      case 15:
    	   x = 3 * radius;
    	   y = - (radius * sqrt(3));
           break;

      case 16:
    	  x = 3 * radius;
    	  y = 0;
          break;

      case 17:
    	   x = 3 * radius;
    	   y = radius * sqrt(3);
           break;

      case 18:
    	  x = radius + (radius/2);
    	  y = 3 * (radius * (sqrt(3)/2));
		  break;


    }

  CartesianCoordinates coordinates;
  coordinates.SetCoordinates(x,y);
  return coordinates;
}

std::vector<CartesianCoordinates*>* GetCartesianCoordinatesForMicro(CartesianCoordinates* macro, int n, int radius, int inter_micro_dist) {

  double cell_x = macro->GetCoordinateX();
  double cell_y = macro->GetCoordinateY();
  cout << "Macro ID: " << macro->GetCellID() << "\n";
  vector<CartesianCoordinates*> *vectorOfCoordinates = new vector<CartesianCoordinates*>;
  

  int mod = 0;
  double macro_distance;
  bool isFarEnough;
  int above_bin = 0;
  int below_bin = 0;
  int half_cells = n/2;
  cout << "Half: " << half_cells << "\n";
  cout << "Starting Loop: \n";

  while (vectorOfCoordinates->size() < n){
    CartesianCoordinates *newCoordinates;
    // double random_num_x = static_cast<double>(rand() % (int)radius);
    // double random_num_y = static_cast<double>(rand() % (int)radius);
    int bin_val = 400;
    double random_num_x = 500 / sqrt(2);
    double random_num_y = 500 / sqrt(2);
    bool check_bins = false;

    mod = rand() % 4;
    
    if(mod == 0){
      newCoordinates = new CartesianCoordinates(cell_x + random_num_x, cell_y + random_num_y);
    } else if (mod == 1){
      newCoordinates = new CartesianCoordinates(cell_x - random_num_x, cell_y + random_num_y);
    } else if (mod == 2){
      newCoordinates = new CartesianCoordinates(cell_x + random_num_x, cell_y - random_num_y);
    } else if (mod == 3){
      newCoordinates = new CartesianCoordinates(cell_x - random_num_x, cell_y - random_num_y);
    }
    macro_distance = macro->GetDistance(newCoordinates);
    isFarEnough = true;
    std::cout << "New CDS: " << newCoordinates->GetCoordinateX() << " " << newCoordinates->GetCoordinateY() << "\n";
    if(macro_distance < 300.0 || macro_distance > 900.0){
      isFarEnough = false;
    } else {
      for (int j = 0; j < vectorOfCoordinates->size(); j++){
        int micro_distance = newCoordinates->GetDistance(vectorOfCoordinates->at(j));
        std::cout << "New CDS D from Micro " << j << ": " << micro_distance << "\n";
        if (micro_distance < inter_micro_dist){
          isFarEnough = false;
          break;
        }
      }
    }
    if (isFarEnough && check_bins){
      cout << "Checking Bins:\n";
      if (macro_distance > bin_val && above_bin < half_cells) {
        cout << "Above Bin: " << above_bin << "\n";
        above_bin++;
        cout << "Incrementing Above Bin: " << above_bin << "\n";
      } else if (macro_distance < bin_val && below_bin < half_cells) {
        cout << "Below Bin: " << below_bin << "\n";
        below_bin++;
        cout << "Incrementing Below Bin: " << below_bin << "\n";
      } else {
        cout << "Not acceptable\n";
        isFarEnough = false;
      }
    }
    cout << "Size of Coordinates:" << vectorOfCoordinates->size() << "\n";

    if (isFarEnough){
      vectorOfCoordinates->push_back(newCoordinates);
      cout << "Adding: " << newCoordinates->GetCoordinateX() << " " << newCoordinates->GetCoordinateY() << "\n";
      if (vectorOfCoordinates->size() == n){
        cout << "Breaking\n";
        break;
      }
    }

    cout << "\n";
  }
  return vectorOfCoordinates;
}

static double
GetTopology_Border (void)
{
  int nbCell = NetworkManager::Init()->GetNbCell();

  switch (nbCell)
    {
      case 1:
    	  return (NetworkManager::Init()->GetCellByID (0)->GetRadius () * 1000);
    	  break;
      case 7:
    	  return ((2.6 * NetworkManager::Init()->GetCellByID (0)->GetRadius ()) * 1000);
    	  break;
      case 19:
          return ((4. * NetworkManager::Init()->GetCellByID (0)->GetRadius ()) * 1000);
          break;
      default:
    	  return 1000.0;
    	  break;
    }
}

#endif /* CELLPOSITION_H_ */
