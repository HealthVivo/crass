/*
 *  crass.h is part of the CRisprASSembler project
 *  
 *  Created by Connor Skennerton.
 *  Copyright 2011 Connor Skennerton & Michael Imelfort. All rights reserved. 
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 *
 *                     A B R A K A D A B R A
 *                      A B R A K A D A B R
 *                       A B R A K A D A B
 *                        A B R A K A D A       	
 *                         A B R A K A D
 *                          A B R A K A
 *                           A B R A K
 *                            A B R A
 *                             A B R
 *                              A B
 *                               A
 */

#ifndef __CRASS_H
    #define __CRASS_H

// system includes
#include <sstream>
#include <vector>
#include <map>
#include <fstream>
#include <iostream>

// local includes
#include "crassDefines.h"
#include "kseq.h"
#include "CrisprNode.h"
#include "WorkHorse.h"

//**************************************
// user input + system
//**************************************



static struct option long_options [] = {

    {"layoutAlgorithm",required_argument,NULL,'a'},
    {"numBins",required_argument,NULL,'b'},
    {"graphColour",required_argument,NULL,'c'},
    {"minDR", required_argument, NULL, 'd'},
    {"maxDR", required_argument, NULL, 'D'},
#ifdef DEBUG
    {"noDebugGraph",no_argument,NULL,'e'},
#endif
    {"covCutoff",required_argument,NULL,'f'},
    {"logToScreen", no_argument, NULL, 'g'},
    {"showSingltons",no_argument,NULL,'G'},
    {"help", no_argument, NULL, 'h'},
    //{"removeHomopolymers",no_argument,NULL,'H'},
    {"kmerCount", required_argument, NULL, 'k'},
    {"graphNodeLen",required_argument,NULL,'K'},
    {"logLevel", required_argument, NULL, 'l'},
    {"longDescription",no_argument,NULL,'L'},
    {"minNumRepeats", required_argument, NULL, 'n'},
    {"outDir", required_argument, NULL, 'o'},
#ifdef RENDERING
    {"noRendering",no_argument,NULL,'r'},
#endif
    {"minSpacer", required_argument, NULL, 's'},
    {"maxSpacer", required_argument, NULL, 'S'},
    {"version", no_argument, NULL, 'V'},
    {"windowLength", required_argument, NULL, 'w'},
    {"spacerScalling",required_argument,NULL,'x'},
    {"repeatScalling",required_argument,NULL,'y'},
    {"noScalling",no_argument,NULL,'z'},
#ifdef SEARCH_SINGLETON
    {"searchChecker", required_argument, NULL, 0},
#endif
    {NULL, no_argument, NULL, 0}
};

void  usage(void);

void  versionInfo(void);

//void  recursiveMkdir(std::string dir);

int   processOptions(int argc, char *argv[], options *opts);





#endif
