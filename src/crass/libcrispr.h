// File: libcrispr.h
// Original Author: Michael Imelfort 2011
// --------------------------------------------------------------------
//
// OVERVIEW:
// 
// Header file for the "crispr toolbox"
//
// --------------------------------------------------------------------
//  Copyright  2011 Michael Imelfort and Connor Skennerton
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.
// --------------------------------------------------------------------
//
//                        A
//                       A B
//                      A B R
//                     A B R A
//                    A B R A C
//                   A B R A C A
//                  A B R A C A D
//                 A B R A C A D A
//                A B R A C A D A B 
//               A B R A C A D A B R  
//              A B R A C A D A B R A 
//

#ifndef libcrispr_h
#define libcrispr_h

// system includes
#include <iostream>
#include <string>
#include <vector>
#include <zlib.h> 
#include <map>
#include <ctime>

// local includes
#include "crassDefines.h"
#include "WuManber.h"
#include "PatternMatcher.h"
#include "kseq.h"
#include "ReadHolder.h"
#include "SeqUtils.h"
#include "StringCheck.h"
#include "Types.h"
#if SEARCH_SINGLETON
#include "SearchChecker.h"
#endif





typedef std::vector<WuManber*> MultiSearchVector;
typedef WuManber::DataFound multiSearchData;

enum READ_TYPE {
    LONG_READ,
    SHORT_READ
};

enum side{rightSide, leftSide};


//**************************************
// search functions
//**************************************
int decideWhichSearch(const char *inputFile, 
                      const options &opts, 
                      ReadMap * mReads, 
                      StringCheck * mStringCheck, 
                      lookupTable& patternsHash, 
                      lookupTable& readsFound,
                      time_t& startTime);

int longReadSearch(ReadHolder& seq, 
                   const options &opts, 
                   ReadMap * mReads, 
                   StringCheck * mStringCheck, 
                   lookupTable &patterns_hash, 
                   lookupTable &readsFound);

int shortReadSearch(ReadHolder&  seq, 
                    const options &opts, 
                    ReadMap * mReads, 
                    StringCheck * mStringCheck, 
                    lookupTable &patterns_hash, 
                    lookupTable &readsFound);

void findSingletons2(const char *inputFastq, 
                    const options &opts, 
                    std::vector<std::string> * nonRedundantPatterns, 
                    lookupTable &readsFound, 
                    ReadMap * mReads, 
                    StringCheck * mStringCheck,
                    time_t& startTime);

void findSingletons(const char *inputFastq, 
                    const options &opts, 
                    std::vector<std::string> * nonRedundantPatterns, 
                    lookupTable &readsFound, 
                    ReadMap * mReads, 
                    StringCheck * mStringCheck,
                    time_t& startTime);

void findSingletonsMultiVector(const char *inputFastq, 
                               const options &opts, 
                               std::vector<std::vector<std::string> *> &patterns, 
                               lookupTable &readsFound, 
                               ReadMap * mReads, 
                               StringCheck * mStringCheck,
                               time_t& startTime);

int scanRight(ReadHolder& tmp_holder, 
              std::string& pattern, 
              unsigned int minSpacerLength, 
              unsigned int scanRange);

unsigned int extendPreRepeat(ReadHolder& tmp_holder, 
                             int searchWindowLength,
                             int minSpacerLength);

bool qcFoundRepeats(ReadHolder& tmp_holder, 
                    int minSpacerLength, 
                    int maxSpacerLength);

bool isRepeatLowComplexity(std::string& repeat);

bool drHasHighlyAbundantKmers(std::string& directRepeat, float& max_count);

bool drHasHighlyAbundantKmers(std::string& directRepeat);

void addReadHolder(ReadMap * mReads, 
                   StringCheck * mStringCheck, 
                   ReadHolder& tmp_holder);

//
//
//
//void map2Vector(lookupTable &patterns_hash, std::vector<std::string> &patterns);

#endif //libcrispr_h
