// File: libcrispr.cpp
// Original Author: Michael Imelfort 2011  :)
// --------------------------------------------------------------------
//
// OVERVIEW:
// 
// This file wraps all the crispr associated functions we'll need.
// The "crispr toolbox"
//
// --------------------------------------------------------------------
//  Copyright  2011, 2012 Michael Imelfort and Connor Skennerton
//  Copyright  2016       Connor Skennerton
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
// system includes
#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <sstream>
#include <zlib.h>  
#include <fstream>
#include <cmath>
#include <fcntl.h>
#include <stdlib.h>
#include <exception>
#include "StlExt.h"
#include "Exception.h"

// local includes
#include "libcrispr.h"
#include "LoggerSimp.h"
#include "crassDefines.h"
#include "PatternMatcher.h"
#include "SeqUtils.h"
#include "kseq.h"
#include "config.h"

extern "C" {
#include "../aho-corasick/msutil.h"
#include "../aho-corasick/acism.h"
}

#define longReadCut(d,s) ((4 * d) + (3 * s))
#define shortReadCut(d,s) ((2 * d) + s)

int decideWhichSearch(const char *inputFastq, 
                      const options& opts, 
                      ReadMap * mReads, 
                      StringCheck * mStringCheck, 
                      lookupTable& patternsHash, 
                      lookupTable& readsFound,
                      time_t& time_start
                      )

{
    //-----
    // Wrapper used for searching reads for DRs
    // depending on the length of the read. 
	// this funciton may use the boyer moore algorithm
    // or the CRT search algorithm
    //
    gzFile fp = getFileHandle(inputFastq);
    kseq_t * seq;

    // initialize seq
    seq = kseq_init(fp);
    
    int l, log_counter, max_read_length;
    log_counter = max_read_length = 0;
    static int read_counter = 0;
    time_t time_current;
    
    int long_read_cutoff =  shortReadCut(opts.lowDRsize, opts.lowSpacerSize);
    int short_read_cutoff = shortReadCut(opts.lowDRsize, opts.lowSpacerSize);
    // read sequence  
    while ( (l = kseq_read(seq)) >= 0 ) 
    {
        max_read_length = (l > max_read_length) ? l : max_read_length;
        if (log_counter == CRASS_DEF_READ_COUNTER_LOGGER) 
        {
            time(&time_current);
            double diff = difftime(time_current, time_start);
            //time_start = time_current;

            std::cout<<"\r["<<PACKAGE_NAME<<"_patternFinder]: "
					 << "Processed "<<read_counter<<" ...";
            std::cout<<diff<<" sec"<<std::flush;
            log_counter = 0;
        }
        try {
            // grab a readholder
            ReadHolder tmp_holder;
            tmp_holder.setSequence(seq->seq.s);tmp_holder.setHeader( seq->name.s);
#if SEARCH_SINGLETON
            SearchCheckerList::iterator debug_iter = debugger->find(seq->name.s);
            if (debug_iter != debugger->end()) {
                changeLogLevel(10);
                std::cout<<"Processing interesting read: "<<debug_iter->first<<std::endl;
            } else {
                changeLogLevel(opts.logLevel);
            }
#endif
            // test if it has a comment entry and a quality entry (fastq input file)
            if (seq->comment.s) 
            {
                tmp_holder.setComment(seq->comment.s);
            }
            if (seq->qual.s) 
            {
                tmp_holder.setQual(seq->qual.s);
            }
            
            if (opts.removeHomopolymers)
            {
                // RLE is necessary...
                tmp_holder.encode();
                // update the value of l based on if we arre removing homopolymers
                l = static_cast<int>(tmp_holder.getSeq().length());
            }

            longReadSearch(tmp_holder, 
                           opts, 
                           mReads, 
                           mStringCheck, 
                           patternsHash, 
                           readsFound);

        } catch (crispr::exception& e) {
            std::cerr<<e.what()<<std::endl;
            kseq_destroy(seq);
            gzclose(fp);
            throw crispr::exception(__FILE__, 
                                    __LINE__, 
                                    __PRETTY_FUNCTION__,
                                    "Fatal error in search algorithm!");
        }
        log_counter++;
        read_counter++;
    }
    
    kseq_destroy(seq); // destroy seq
    gzclose(fp);
    
    logInfo("finished processing file:"<<inputFastq, 1);    
    time(&time_current);
    double diff = difftime(time_current, time_start);
    //time_start = time_current;
    std::cout<<"\r["<<PACKAGE_NAME<<"_patternFinder]: "<< "Processed "<<read_counter<<" ...";
    std::cout<<diff<<" sec"<<std::flush;
    logInfo("So far " << mReads->size()<<" direct repeat variants have been found from " << read_counter << " reads", 2);

    return max_read_length;
}


// CRT search
int scanRight(ReadHolder&  tmp_holder, 
              std::string& pattern, 
              unsigned int minSpacerLength, 
              unsigned int scanRange)
{
#ifdef DEBUG
    logInfo("Scanning Right for more repeats:", 9);
#endif
    unsigned int start_stops_size = tmp_holder.getStartStopListSize();
    
    unsigned int pattern_length = static_cast<unsigned int>(pattern.length());
    
    // final start index
    unsigned int last_repeat_index = tmp_holder.getRepeatAt(start_stops_size - 2);
    
    //second to final start index
    unsigned int second_last_repeat_index = tmp_holder.getRepeatAt(start_stops_size - 4);
    
    unsigned int repeat_spacing = last_repeat_index - second_last_repeat_index;
    
#ifdef DEBUG
    logInfo(start_stops_size<<" : "<<pattern_length<<" : "<<last_repeat_index<<" : "<<second_last_repeat_index<<" : "<<repeat_spacing, 9);
#endif
    
    int candidate_repeat_index, position;
    
    unsigned int begin_search, end_search;
    
    unsigned int read_length = static_cast<unsigned int>(tmp_holder.getSeqLength());
    bool more_to_search = true;
    while (more_to_search)
    {
        candidate_repeat_index = last_repeat_index + repeat_spacing;
        begin_search = candidate_repeat_index - scanRange;
        end_search = candidate_repeat_index + pattern_length + scanRange;
        #ifdef DEBUG
        logInfo(candidate_repeat_index<<" : "<<begin_search<<" : "<<end_search, 9);
        #endif
        /******************** range checks ********************/
        //check that we do not search too far within an existing repeat when scanning right
        unsigned int scanRightMinBegin = last_repeat_index + pattern_length + minSpacerLength;
        
        if (begin_search < scanRightMinBegin)
        {
            begin_search = scanRightMinBegin;
        }
        if (begin_search > read_length - 1)
        {
            #ifdef DEBUG
            logInfo("returning... "<<begin_search<<" > "<<read_length - 1, 9);
            #endif
            return read_length - 1;
        }
        if (end_search > read_length)
        {
            end_search = read_length;
        }
        
        if ( begin_search >= end_search)
        {
            #ifdef DEBUG
            logInfo("Returning... "<<begin_search<<" >= "<<end_search, 9);
            #endif
            return end_search;
        }
        /******************** end range checks ********************/
        
        std::string text = tmp_holder.substr(begin_search, (end_search - begin_search));
        
        #ifdef DEBUG
        logInfo(pattern<<" : "<<text, 9);
        #endif
        position = PatternMatcher::bmpSearch(text, pattern);
        
        
        if (position >= 0)
        {
            tmp_holder.startStopsAdd(begin_search + position, begin_search + position + pattern_length);
            second_last_repeat_index = last_repeat_index;
            last_repeat_index = begin_search + position;
            repeat_spacing = last_repeat_index - second_last_repeat_index;
            if (repeat_spacing < (minSpacerLength + pattern_length))
            {
                more_to_search = false;
            }
        }
        else
        {
            more_to_search = false;
        }
    }
    
    return begin_search + position;
}

int longReadSearch(ReadHolder& tmpHolder, 
                   const options& opts, 
                   ReadMap * mReads, 
                   StringCheck * mStringCheck, 
                   lookupTable& patternsHash, 
                   lookupTable& readsFound)
{
    //-----
    // Code lifted from CRT, ported by Connor and hacked by Mike.
    // Should do well at finding crisprs in long reads
    //
    
    //bool match_found = false;

    std::string read = tmpHolder.getSeq();
    
    // get the length of this sequence
    unsigned int seq_length = static_cast<unsigned int>(read.length());
    

    //the mumber of bases that can be skipped while we still guarantee that the entire search
    //window will at some point in its iteration thru the sequence will not miss a any repeat
    unsigned int skips = opts.lowDRsize - (2 * opts.searchWindowLength - 1);
    if (skips < 1)
    {
        skips = 1;
    }

    int searchEnd = seq_length - opts.lowDRsize - opts.lowSpacerSize - opts.searchWindowLength - 1;
    
    if (searchEnd < 0) 
    {
        logError("Read: "<<tmpHolder.getHeader()<<" length is less than "<<opts.lowDRsize + opts.lowSpacerSize + opts.searchWindowLength + 1<<"bp"<<
                " lDR: "<< opts.lowDRsize << " lS: "<< opts.lowSpacerSize << " SW: "<< opts.searchWindowLength << " SL: "<< seq_length);
        //delete tmpHolder;
        return 1;
    }
    
    for (unsigned int j = 0; j <= static_cast<unsigned int>(searchEnd); j = j + skips)
    {
                    
        unsigned int beginSearch = j + opts.lowDRsize + opts.lowSpacerSize;
        unsigned int endSearch = j + opts.highDRsize + opts.highSpacerSize + opts.searchWindowLength;
        
        if (endSearch >= seq_length)
        {
            endSearch = seq_length - 1;
        }
        //should never occur
        if (endSearch < beginSearch)
        {
            endSearch = beginSearch;
        }
        
        std::string text;
        std::string pattern;
        try {
            text = read.substr(beginSearch, (endSearch - beginSearch));

        } catch (std::exception& e) {
            throw crispr::substring_exception(e.what(), 
                                              text.c_str(), 
                                              beginSearch, 
                                              (endSearch - beginSearch), 
                                              __FILE__, 
                                              __LINE__, 
                                              __PRETTY_FUNCTION__);
        }
        try {
            pattern = read.substr(j, opts.searchWindowLength);
        } catch (std::exception& e) {
            throw crispr::substring_exception(e.what(), 
                                              read.c_str(), 
                                              j, 
                                              opts.searchWindowLength, 
                                              __FILE__, 
                                              __LINE__, 
                                              __PRETTY_FUNCTION__);
        }

        //if pattern is found, add it to candidate list and scan right for additional similarly spaced repeats
        int pattern_in_text_index = -1;
            pattern_in_text_index = PatternMatcher::bmpSearch(text, pattern);

        if (pattern_in_text_index >= 0)
        {
            tmpHolder.startStopsAdd(j,  j + opts.searchWindowLength);
            unsigned int found_pattern_start_index = beginSearch + static_cast<unsigned int>(pattern_in_text_index);
            
            tmpHolder.startStopsAdd(found_pattern_start_index, found_pattern_start_index + opts.searchWindowLength);
            scanRight(tmpHolder, pattern, opts.lowSpacerSize, 24);
        }

        if ( (tmpHolder.numRepeats() >= opts.minNumRepeats) ) //tmp_holder->numRepeats is half the size of the StartStopList
        {
#ifdef DEBUG
            logInfo(tmpHolder.getHeader(), 8);
            logInfo("\tPassed test 1. At least "<<opts.minNumRepeats<< " ("<<tmpHolder.numRepeats()<<") repeated kmers found", 8);
#endif

            unsigned int actual_repeat_length = extendPreRepeat(tmpHolder, opts.searchWindowLength, opts.lowSpacerSize);

            if ( (actual_repeat_length >= opts.lowDRsize) && (actual_repeat_length <= opts.highDRsize) )
            {
#ifdef DEBUG

                logInfo("\tPassed test 2. The repeat length is "<<opts.lowDRsize<<" >= "<< actual_repeat_length <<" <= "<<opts.highDRsize, 8);
#endif
			// Declare a tmp string here to hold the encoded DR if 
			// removeHolopolymers is in affect.  Later if the read passes all
			// the tests then add in this encoded string since that is the 
			// version that the singleton finder should be looking for
			std::string encoded_repeat;
			if(opts.removeHomopolymers) {
				encoded_repeat = tmpHolder.repeatStringAt(0);
				tmpHolder.decode();

			}
                
                // drop partials
                //tmpHolder.dropPartials();
                // After we remove the partial repeats we need to check again to make sure that
                // we're still above the minimum needed
                //if (tmpHolder.numRepeats() < opts.minNumRepeats) {
                //    break;
                //}
                if (qcFoundRepeats(tmpHolder, opts.lowSpacerSize, opts.highSpacerSize))
                {
#ifdef DEBUG
                    logInfo("Passed all tests!", 8);
                    logInfo("Potential CRISPR containing read found: "<<tmpHolder.getHeader(), 7);
                    logInfo(tmpHolder.getSeq(), 9);
                    logInfo("-------------------", 7)
#endif                            

                    //ReadHolder * candidate_read = new ReadHolder();
                    //*candidate_read = *tmp_holder;
                    //addReadHolder(mReads, mStringCheck, candidate_read);
                    addReadHolder(mReads, mStringCheck, tmpHolder);
                    //match_found = true;
					if(opts.removeHomopolymers) {
							patternsHash[encoded_repeat] = true;
						} else {
							patternsHash[tmpHolder.repeatStringAt(0)] = true;
						}
                    readsFound[tmpHolder.getHeader()] = true;
                    break;
                }
            }
#ifdef DEBUG                
            else
            {
                logInfo("\tFailed test 2. Repeat length: "<<tmpHolder.getRepeatLength()/* << " : " << match_found*/, 8); 
            }
#endif
            j = tmpHolder.back() - 1;
        }
        tmpHolder.clearStartStops();
    }
    return 0;
}



typedef struct _multisearch_payload {
    ReadMap * mReads;
    StringCheck * mStringCheck;
    kseq_t * read;
    lookupTable *readsFound;
    MEMREF * pattv;
} MultisearchPayload;


static int on_match(int strnum, int textpos, MultisearchPayload *payload)
{
    //if (matchfp) fprintf(matchfp, "%9d %7d '%.*s'\n", textpos, strnum, (int)pattv[strnum].len, pattv[strnum].ptr);
    if (payload->readsFound->find(payload->read->name.s) == payload->readsFound->end())
    {

#ifdef DEBUG
        logInfo("new read recruited: "<<payload->read->name.s, 9);
        logInfo(payload->read->seq.s, 10);
#endif
        // The index is one past the end of the match but crass stores 
        // it's position at the end of the match
        unsigned int DR_end = static_cast<unsigned int>(textpos - 1); //static_cast<unsigned int>(search_data.iFoundPosition) + static_cast<unsigned int>(search_data.sDataFound.length()) - 1;
        if(DR_end >= static_cast<unsigned int>(payload->read->seq.l))
        {
            DR_end = static_cast<unsigned int>(payload->read->seq.l) - 1;
        }
        ReadHolder tmp_holder;
        tmp_holder.setSequence(payload->read->seq.s);
        tmp_holder.setHeader( payload->read->name.s);
        if (payload->read->comment.s) 
        {
            tmp_holder.setComment(payload->read->comment.s);
        }
        if (payload->read->qual.s) 
        {
            tmp_holder.setQual(payload->read->qual.s);
        }
        //logInfo("textpos: "<<textpos<<" DR_end: "<<DR_end<<" start: "<<DR_end << " len: "<< payload->pattv[strnum].len, 1)
        tmp_holder.startStopsAdd(DR_end - (payload->pattv[strnum].len - 1), DR_end);
        addReadHolder(payload->mReads, payload->mStringCheck, tmp_holder);
    }

    return 1;
}

void findSingletons2(const char *inputFastq, 
                    const options &opts, 
                    std::vector<std::string> * nonRedundantPatterns, 
                    lookupTable &readsFound, 
                    ReadMap * mReads, 
                    StringCheck * mStringCheck,
                    time_t& startTime)
{
    std::string conc;
    std::vector<std::string>::iterator iter;
    for( iter = nonRedundantPatterns->begin(); iter != nonRedundantPatterns->end(); ++iter) {
        conc += *iter + "\n";
    }

    // this is a hack as refsplit creates an extra blank record, which stuffs
    // up the search if the string ends with a new line character. Basically
    // here I'm replacing the last newline with null to prevent this
    char * concstr = new char[conc.size() + 1];
    std::copy(conc.begin(), conc.end(), concstr);
    concstr[conc.size()] = '\0';
    concstr[conc.size()-1] = '\0';
    int npatts;

    MEMREF *pattv = refsplit(concstr, '\n', &npatts);

    ACISM *psp = acism_create(pattv, npatts);

    gzFile fp = getFileHandle(inputFastq);
    kseq_t *seq;
    seq = kseq_init(fp);

    int l;
    int log_counter = 0;
    static int read_counter = 0;

    time_t time_current;

    MultisearchPayload payload;
    payload.mReads = mReads;
    payload.mStringCheck = mStringCheck;
    payload.pattv = pattv;
    payload.readsFound = &readsFound;
    
    while ( (l = kseq_read(seq)) >= 0 ) 
    {
        // seq is a read what we love
        // search it for the patterns until found
        if (log_counter == CRASS_DEF_READ_COUNTER_LOGGER) 
        {
            time(&time_current);
            double diff = difftime(time_current, startTime);
            std::cout<<"\r["<<PACKAGE_NAME<<"_singletonFinder]: "<<"Processed "<<read_counter<<" ...";
            std::cout<<diff<<" sec"<<std::flush;
            log_counter = 0;
        }

        payload.read = seq;
        MEMREF tmp = {seq->seq.s, seq->seq.l};

        (void)acism_scan(psp, tmp, (ACISM_ACTION*)on_match, &payload);
    
        log_counter++;
        read_counter++;
    }

    gzclose(fp);
    kseq_destroy(seq); // destroy seq
    delete[] concstr;

    time(&time_current);
    double diff = difftime(time_current, startTime);
    std::cout<<"\r["<<PACKAGE_NAME<<"_singletonFinder]: "<<"Processed "<<read_counter<<" ...";
    std::cout<<diff<<" sec"<<std::flush;
    
}

unsigned int extendPreRepeat(ReadHolder&  tmp_holder, int searchWindowLength, int minSpacerLength)
{
#ifdef DEBUG
    tmp_holder.logContents(9);
#endif
#ifdef DEBUG
    logInfo("Extending Prerepeat...", 9);
#endif
    //-----
    // Extend a preliminary repeat - return the final repeat size
    //
    // LOOK AT ME
    //!!!!!!!!!!!!!!!
    // the number of repeats
    // equal to half the size of the start stop list
    //!!!!!!!!!!!!!!
    unsigned int num_repeats = tmp_holder.numRepeats();
    tmp_holder.setRepeatLength(searchWindowLength);
    int cut_off = (int)(CRASS_DEF_TRIM_EXTEND_CONFIDENCE * num_repeats);
    
    // make sure that we don't go below 2
    if (2 > cut_off) 
    {
        cut_off = 2;
    }
#ifdef DEBUG
    logInfo("cutoff: "<<cut_off, 9);
#endif
    
    
    // the index in the read of the first DR kmer
    unsigned int first_repeat_start_index = tmp_holder.getFirstRepeatStart();
    
    // the index in the read of the last DR kmer
    unsigned int last_repeat_start_index = tmp_holder.getLastRepeatStart();
    
    // the length between the first two DR kmers
    unsigned int shortest_repeat_spacing = tmp_holder.startStopsAt(2) - tmp_holder.startStopsAt(0);
    // loop througth all remaining members of mRepeats
    unsigned int end_index = tmp_holder.getStartStopListSize();
    
    for (unsigned int i = 4; i < end_index; i+=2)
    {
        
        // get the repeat spacing of this pair of DR kmers
        unsigned int curr_repeat_spacing = tmp_holder.startStopsAt(i) - tmp_holder.startStopsAt(i - 2);
#ifdef DEBUG
        logInfo(i<<" : "<<curr_repeat_spacing, 10);
#endif
        
        // if it is shorter than what we already have, make it the shortest
        if (curr_repeat_spacing < shortest_repeat_spacing)
        {
            shortest_repeat_spacing = curr_repeat_spacing;
        }
    }
#ifdef DEBUG
    logInfo("shortest repeat spacing: "<<shortest_repeat_spacing, 9);
#endif
    unsigned int right_extension_length = 0;
    // don't search too far  
    unsigned int max_right_extension_length = shortest_repeat_spacing - minSpacerLength;
#ifdef DEBUG
    logInfo("max right extension length: "<<max_right_extension_length, 9);
#endif
    unsigned int DR_index_end = end_index;
    
    // Sometimes we shouldn't use the far right DR. (it may lie too close to the end)
    /*unsigned int dist_to_end = tmp_holder.getSeqLength() - last_repeat_start_index - 1;
    if(dist_to_end < max_right_extension_length)
    {
#ifdef DEBUG
        logInfo("removing end partial: "<<tmp_holder.getLastRepeatStart()<<" ("<<dist_to_end<<" < "<<max_right_extension_length<<")", 9);
#endif
        DR_index_end -= 2;
        cut_off = (int)(CRASS_DEF_TRIM_EXTEND_CONFIDENCE * (num_repeats - 1));
        if (2 > cut_off) 
        {
            cut_off = 2;
        }
#ifdef DEBUG
        logInfo("minimum number of repeats needed with same nucleotide for extension: "<<cut_off, 9);
#endif
    }*/

    std::string curr_repeat;
    int char_count_A, char_count_C, char_count_T, char_count_G;
    char_count_A = char_count_C = char_count_T = char_count_G = 0;
    //(from the right side) extend the length of the repeat to the right as long as the last base of all repeats are at least threshold
#ifdef DEBUG
    int iteration_counter = 0;
#endif
    while (max_right_extension_length > 0)
    {
        if((last_repeat_start_index + searchWindowLength + right_extension_length) >= static_cast<int>(tmp_holder.getSeqLength())) {
            DR_index_end -= 2;
        }
        for (unsigned int k = 0; k < DR_index_end; k+=2 )
        {
#ifdef DEBUG
            logInfo("Iteration "<<iteration_counter++, 10);
            logInfo(k<<" : "<<tmp_holder.getRepeatAt(k) + tmp_holder.getRepeatLength(), 10);
#endif
            // look at the character just past the end of the last repeat
            // make sure our indicies make some sense!
            if((tmp_holder.getRepeatAt(k) + tmp_holder.getRepeatLength()) >= static_cast<unsigned int>(tmp_holder.getSeqLength()))
            {    
                k = DR_index_end;
            }
            else
            {
                switch( tmp_holder.getSeqCharAt(tmp_holder.getRepeatAt(k) + tmp_holder.getRepeatLength()))
                {
                    case 'A':
                        char_count_A++;
                        break;
                    case 'C':
                        char_count_C++;
                        break;
                    case 'G':
                        char_count_G++;
                        break;
                    case 'T':
                        char_count_T++;
                        break;
                }
            }
        }
        
#ifdef DEBUG
        logInfo("R: " << char_count_A << " : " << char_count_C << " : " << char_count_G << " : " << char_count_T << " : " << tmp_holder.getRepeatLength() << " : " << max_right_extension_length, 9);
#endif
        if ( (char_count_A >= cut_off) || (char_count_C >= cut_off) || (char_count_G >= cut_off) || (char_count_T >= cut_off) )
        {
#ifdef DEBUG
            logInfo("R: SUCCESS! count above "<< cut_off, 9);
#endif
            tmp_holder.incrementRepeatLength();
            max_right_extension_length--;
            right_extension_length++;
            char_count_A = char_count_C = char_count_T = char_count_G = 0;
        }
        else
        {
#ifdef DEBUG
            logInfo("R: FAILURE! count below "<< cut_off, 9);
#endif
            break;
        }
    }
    
    char_count_A = char_count_C = char_count_T = char_count_G = 0;
    
    // again, not too far
    unsigned int left_extension_length = 0;
    int test_for_negative = shortest_repeat_spacing - tmp_holder.getRepeatLength();// - minSpacerLength;
    unsigned int max_left_extension_length = (test_for_negative >= 0)? static_cast<unsigned int>(test_for_negative) : 0;

#ifdef DEBUG
    logInfo("max left extension length: "<<max_left_extension_length, 9);
#endif
    // and again, we may not wat to use the first DR
    unsigned int DR_index_start = 0;
    /*if(max_left_extension_length > first_repeat_start_index)
    {
#ifdef DEBUG
        logInfo("removing start partial: "<<max_left_extension_length<<" > "<<first_repeat_start_index, 9);
#endif
        DR_index_start+=2;
        cut_off = static_cast<int>(CRASS_DEF_TRIM_EXTEND_CONFIDENCE * (num_repeats - 1));
        if (2 > cut_off) 
        {
            cut_off = 2;
        }
#ifdef DEBUG
        logInfo("new cutoff: "<<cut_off, 9);
#endif
    }*/
    //(from the left side) extends the length of the repeat to the left as long as the first base of all repeats is at least threshold
    while (left_extension_length < max_left_extension_length)
    {
        if(first_repeat_start_index - left_extension_length < 0) {
            logInfo("first repeat can't be extended anymore, dorpping", 10);
            DR_index_start += 2;
        }
        for (unsigned int k = DR_index_start; k < end_index; k+=2 )
        {
#ifdef DEBUG
            logInfo(k<<" : "<<tmp_holder.getRepeatAt(k) - left_extension_length - 1, 10);
#endif
            switch(tmp_holder.getSeqCharAt(tmp_holder.getRepeatAt(k) - left_extension_length - 1))
            {
                case 'A':
                    char_count_A++;
                    break;
                case 'C':
                    char_count_C++;
                    break;
                case 'G':
                    char_count_G++;
                    break;
                case 'T':
                    char_count_T++;
                    break;
            }
        }
#ifdef DEBUG
        logInfo("L:" << char_count_A << " : " << char_count_C << " : " << char_count_G << " : " << char_count_T << " : " << tmp_holder.getRepeatLength() << " : " << left_extension_length, 9);
#endif
        
        if ( (char_count_A >= cut_off) || (char_count_C >= cut_off) || (char_count_G >= cut_off) || (char_count_T >= cut_off) )
        {
            tmp_holder.incrementRepeatLength();
            left_extension_length++;
            char_count_A = char_count_C = char_count_T = char_count_G = 0;
        }
        else
        {
            break;
        }
    }
    StartStopListIterator repeat_iter = tmp_holder.begin();
#ifdef DEBUG    
    logInfo("Repeat positions:", 9);
#endif
    while (repeat_iter < tmp_holder.end()) 
    {
        if(*repeat_iter < static_cast<unsigned int>(left_extension_length))
        {
            *repeat_iter = 0;
            *(repeat_iter + 1) += right_extension_length - 1;
        }
        else
        {
            *repeat_iter -= left_extension_length;
            *(repeat_iter+1) += right_extension_length - 1;
        }
#ifdef DEBUG    
        logInfo("\t"<<*repeat_iter<<","<<*(repeat_iter+1), 9);
#endif
        repeat_iter += 2;
    }

    return static_cast<unsigned int>(tmp_holder.getRepeatLength());
    
}


//need at least two elements
bool qcFoundRepeats(ReadHolder& tmp_holder, int minSpacerLength, int maxSpacerLength)
{

    if (tmp_holder.numRepeats() < 2) 
    {
        tmp_holder.logContents(1);
        throw crispr::exception(__FILE__,
                                __LINE__,
                                __PRETTY_FUNCTION__,
                                "The vector holding the repeat indexes has less than 2 repeats!");
    }

    
    std::string repeat = tmp_holder.repeatStringAt(0);
    
    if (isRepeatLowComplexity(repeat)) 
    {
#ifdef DEBUG
        logInfo("\tFailed test 3. The repeat is low complexity", 8);
#endif
        return false;
    }
    
#ifdef DEBUG
    logInfo("\tPassed test 3. The repeat is not low complexity", 8);
#endif
    // test for a long or short read
    int single_compare_index = 0;
    bool is_short = (2 > tmp_holder.numSpacers()); 
    if(!is_short) 
    {
        // for holding stats
        float ave_spacer_to_spacer_len_difference = 0.0;
        float ave_repeat_to_spacer_len_difference = 0.0;
        float ave_spacer_to_spacer_difference = 0.0;
        float ave_repeat_to_spacer_difference = 0.0;
        int min_spacer_length = 10000000;
        int max_spacer_length = 0;
        int num_compared = 0;
        
        // now go through the spacers and check for similarities
        std::vector<std::string> spacer_vec;
        
        //get all the spacer strings

        tmp_holder.getAllSpacerStrings(spacer_vec);
        
        std::vector<std::string>::iterator spacer_iter = spacer_vec.begin();
        std::vector<std::string>::iterator spacer_last = spacer_vec.end();


        spacer_last--;
        while (spacer_iter != spacer_last) 
        {

            num_compared++;
            try {
            	ave_repeat_to_spacer_difference += PatternMatcher::getStringSimilarity(repeat, *spacer_iter);
            } catch (std::exception& e) {
                std::cerr<<"Failed to compare similarity between: "<<repeat<<" : "<<*spacer_iter<<std::endl;
                throw crispr::exception(__FILE__,
                                        __LINE__,
                                        __PRETTY_FUNCTION__,
                                        e.what());
            }
            float ss_diff = 0;
            try {
            	ss_diff += PatternMatcher::getStringSimilarity(*spacer_iter, *(spacer_iter + 1));
			} catch (std::exception& e) {
                std::cerr<<"Failed to compare similarity between: "<<*spacer_iter<<" : "<<*(spacer_iter+1)<<std::endl;
                throw crispr::exception(__FILE__,
                                        __LINE__,
                                        __PRETTY_FUNCTION__,
                                        e.what());
			}
            ave_spacer_to_spacer_difference += ss_diff;

            //MI std::cout << ss_diff << " : " << *spacer_iter << " : " << *(spacer_iter + 1) << std::endl;
            ave_spacer_to_spacer_len_difference += (static_cast<float>(spacer_iter->size()) - static_cast<float>((spacer_iter + 1)->size()));
            ave_repeat_to_spacer_len_difference +=  (static_cast<float>(repeat.size()) - static_cast<float>(spacer_iter->size()));
            spacer_iter++;
        }

        // now look for max and min lengths!
        spacer_iter = spacer_vec.begin();
        spacer_last = spacer_vec.end();
        while (spacer_iter != spacer_last) 
        {
            if(static_cast<int>(spacer_iter->length()) < min_spacer_length)
            {
                min_spacer_length = static_cast<int>(spacer_iter->length()); 
            }
            if(static_cast<int>(spacer_iter->length()) > max_spacer_length)
            {
                max_spacer_length = static_cast<int>(spacer_iter->length()); 
            }
            spacer_iter++;
        }

        // we may not have compared anything...
        if(num_compared == 0)
        {
            // there are only three spacers in this read and the read begins and ends on a spacer
            // we will still need to do some comparisons
            is_short = true;
            single_compare_index = 1;
        }
        else
        {
            // we can keep going!
            ave_spacer_to_spacer_difference /= static_cast<float>(num_compared);
            ave_repeat_to_spacer_difference /= static_cast<float>(num_compared);
            ave_spacer_to_spacer_len_difference = abs(ave_spacer_to_spacer_len_difference /= static_cast<float>(num_compared));
            ave_repeat_to_spacer_len_difference = abs(ave_repeat_to_spacer_len_difference /= static_cast<float>(num_compared));
            
            /*
             * MAX AND MIN SPACER LENGTHS
             */
            if (min_spacer_length < minSpacerLength) 
            {
#ifdef DEBUG
                logInfo("\tFailed test 4a. Min spacer length out of range: "<<min_spacer_length<<" < "<<minSpacerLength, 8);
#endif
                return false;
            }
#ifdef DEBUG
            logInfo("\tPassed test 4a. Min spacer length within range: "<<min_spacer_length<<" > "<<minSpacerLength, 8);
#endif
            if (max_spacer_length > maxSpacerLength) 
            {
#ifdef DEBUG
                logInfo("\tFailed test 4b. Max spacer length out of range: "<<max_spacer_length<<" > "<<maxSpacerLength, 8);
#endif
                return false;
            }
#ifdef DEBUG
            logInfo("\tPassed test 4b. Max spacer length within range: "<<max_spacer_length<<" < "<<maxSpacerLength, 8);
#endif
            
            /*
             * REPEAT AND SPACER CONTENT SIMILARITIES
             */
            if (ave_spacer_to_spacer_difference > CRASS_DEF_SPACER_OR_REPEAT_MAX_SIMILARITY) 
            {
#ifdef DEBUG
                logInfo("\tFailed test 5a. Spacers are too similar: "<<ave_spacer_to_spacer_difference<<" > "<<CRASS_DEF_SPACER_OR_REPEAT_MAX_SIMILARITY, 8);
#endif
                return false;
            }
#ifdef DEBUG
            logInfo("\tPassed test 5a. Spacers are not too similar: "<<ave_spacer_to_spacer_difference<<" < "<<CRASS_DEF_SPACER_OR_REPEAT_MAX_SIMILARITY, 8);
#endif    
            if (ave_repeat_to_spacer_difference > CRASS_DEF_SPACER_OR_REPEAT_MAX_SIMILARITY)
            {
#ifdef DEBUG
                logInfo("\tFailed test 5b. Spacers are too similar to the repeat: "<<ave_repeat_to_spacer_difference<<" > "<<CRASS_DEF_SPACER_OR_REPEAT_MAX_SIMILARITY, 8);
#endif
                return false;
            }
#ifdef DEBUG
            logInfo("\tPassed test 5b. Spacers are not too similar to the repeat: "<<ave_repeat_to_spacer_difference<<" < "<<CRASS_DEF_SPACER_OR_REPEAT_MAX_SIMILARITY, 8);
#endif    
            
            /*
             * REPEAT AND SPACER LENGTH SIMILARITIES
             */
            if (ave_spacer_to_spacer_len_difference > CRASS_DEF_SPACER_TO_SPACER_LENGTH_DIFF) 
            {
#ifdef DEBUG 
                logInfo("\tFailed test 6a. Spacer lengths differ too much: "<<ave_spacer_to_spacer_len_difference<<" > "<<CRASS_DEF_SPACER_TO_SPACER_LENGTH_DIFF, 8);
#endif
                return false;
            }
#ifdef DEBUG 
            logInfo("\tPassed test 6a. Spacer lengths do not differ too much: "<<ave_spacer_to_spacer_len_difference<<" < "<<CRASS_DEF_SPACER_TO_SPACER_LENGTH_DIFF, 8);
#endif    
            if (ave_repeat_to_spacer_len_difference > CRASS_DEF_SPACER_TO_REPEAT_LENGTH_DIFF) 
            {
#ifdef DEBUG
                logInfo("\tFailed test 6b. Repeat to spacer lengths differ too much: "<<ave_repeat_to_spacer_len_difference<<" > "<<CRASS_DEF_SPACER_TO_REPEAT_LENGTH_DIFF, 8);
#endif
                return false;
            }
#ifdef DEBUG
            logInfo("\tPassed test 6b. Repeat to spacer lengths do not differ too much: "<<ave_repeat_to_spacer_len_difference<<" < "<<CRASS_DEF_SPACER_TO_REPEAT_LENGTH_DIFF, 8);
#endif
            
        }
    }
    
    // Are we testing a short read or only one spacer?
    if(is_short)
    {
        std::string spacer = tmp_holder.spacerStringAt(single_compare_index);
        float similarity = PatternMatcher::getStringSimilarity(repeat, spacer);
        if (similarity > CRASS_DEF_SPACER_OR_REPEAT_MAX_SIMILARITY) 
        {
            /*
             * MAX AND MIN SPACER LENGTHS
             */
            if (static_cast<int>(spacer.length()) < minSpacerLength) 
            {
#ifdef DEBUG
                logInfo("\tFailed test 4a. Min spacer length out of range: "<<spacer.length()<<" < "<<minSpacerLength, 8);
#endif
                return false;
            }
#ifdef DEBUG
            logInfo("\tPassed test 4a. Min spacer length within range: "<<spacer.length()<<" > "<<minSpacerLength, 8);
#endif
            if (static_cast<int>(spacer.length()) > maxSpacerLength) 
            {
#ifdef DEBUG
                logInfo("\tFailed test 4b. Max spacer length out of range: "<<spacer.length()<<" > "<<maxSpacerLength, 8);
#endif
                return false;
            }
#ifdef DEBUG
             logInfo("\tPassed test 4b. Max spacer length within range: "<<spacer.length()<<" < "<<maxSpacerLength, 8);
#endif
            /*
             * REPEAT AND SPACER CONTENT SIMILARITIES
             */ 
#ifdef DEBUG
            logInfo("\tFailed test 5. Spacer is too similar to the repeat: "<<similarity<<" > "<<CRASS_DEF_SPACER_OR_REPEAT_MAX_SIMILARITY, 8);
#endif
            return false;
        }
#ifdef DEBUG
        logInfo("\tPassed test 5. Spacer is not too similar to the repeat: "<<similarity<<" < "<<CRASS_DEF_SPACER_OR_REPEAT_MAX_SIMILARITY, 8);
#endif        
        
        /*
         * REPEAT AND SPACER LENGTH SIMILARITIES
         */
        if (abs(static_cast<int>(spacer.length()) - static_cast<int>(repeat.length())) > CRASS_DEF_SPACER_TO_REPEAT_LENGTH_DIFF) 
        {
#ifdef DEBUG
            logInfo("\tFailed test 6. Repeat to spacer length differ too much: "<<abs((int)spacer.length() - (int)repeat.length())<<" > "<<CRASS_DEF_SPACER_TO_REPEAT_LENGTH_DIFF, 8);
#endif
            return false;
        }
#ifdef DEBUG
        logInfo("\tPassed test 6. Repeat to spacer length do not differ too much: "<<abs((int)spacer.length() - (int)repeat.length())<<" < "<<CRASS_DEF_SPACER_TO_REPEAT_LENGTH_DIFF, 8);
#endif
    }
    
    return true;
}

bool isRepeatLowComplexity(std::string& repeat)
{
    int c_count = 0;
    int g_count = 0;
    int a_count = 0;
    int t_count = 0;
    int n_count = 0;
    
    int curr_repeat_length = static_cast<int>(repeat.length());
    
    int cut_off = static_cast<int>(curr_repeat_length * CRASS_DEF_LOW_COMPLEXITY_THRESHHOLD);
    
    std::string::iterator dr_iter;
    for (dr_iter = repeat.begin(); dr_iter != repeat.end();++dr_iter)
    {
        switch (*dr_iter) 
        {
            case 'c':
            case 'C':
                c_count++; break;
            case 't': 
            case 'T':
                t_count++; break;
            case 'a':
            case 'A':
                a_count++; break;
            case 'g':
            case 'G':
                g_count++; break;
            default: n_count++; break;
        }
    }
    if (a_count > cut_off) return true; 
    else if (t_count > cut_off) return true; 
    else if (g_count > cut_off) return true;
    else if (c_count > cut_off) return true;
    else if (n_count > cut_off) return true;   
    return false;
}


bool drHasHighlyAbundantKmers(std::string& directRepeat) {
    float max;
    return drHasHighlyAbundantKmers(directRepeat, max);
}

bool drHasHighlyAbundantKmers(std::string& directRepeat, float& maxFrequency)
{
    // cut kmers from the direct repeat to test whether
    // a particular kmer is vastly over represented
    std::map<std::string, int> kmer_counter;
    size_t kmer_length = 3;
    size_t max_index = (directRepeat.length() - kmer_length);
    std::string kmer;
    int total_count = 0;
    try {
        for (size_t i = 0; i < max_index; i++) {
            kmer = directRepeat.substr(i, kmer_length);
            //std::cout << kmer << ", ";
            addOrIncrement(kmer_counter, kmer);
            total_count++;
        }
    } catch (std::exception& e) {
        throw crispr::runtime_exception(__FILE__, 
                                        __LINE__, 
                                        __PRETTY_FUNCTION__, 
                                        e.what());
    }
    //std::cout << std::endl;
    
    std::map<std::string, int>::iterator iter;
    //std::cout << directRepeat << std::endl;
    int max_count = 0;
    for (iter = kmer_counter.begin(); iter != kmer_counter.end(); ++iter) {
        if (iter->second > max_count) {
            max_count = iter->second;
        }
        //std::cout << "{" << iter->first << ", " << iter->second << ", " << (float(iter->second)/float(total_count)) << "}, ";
    }
    //std::cout << std::endl;
    maxFrequency = static_cast<float>(max_count)/static_cast<float>(total_count);
    if (maxFrequency > CRASS_DEF_KMER_MAX_ABUNDANCE_CUTOFF) {
        return true;
    } else {
        return false;
    }
}

void addReadHolder(ReadMap * mReads, 
                   StringCheck * mStringCheck, 
                   ReadHolder& tmpReadholder)
{

    ReadHolder * candidate = new ReadHolder(tmpReadholder);
    std::string dr_lowlexi;
	try {
		dr_lowlexi = candidate->DRLowLexi();
	} catch(crispr::exception& e) {
		std::cerr<<e.what()<<std::endl;
		throw crispr::exception(__FILE__,
		                        __LINE__,
		                        __PRETTY_FUNCTION__,
		                        "Cannot obtain read in lowlexi form"
		                        );
	}

    StringToken st = mStringCheck->getToken(dr_lowlexi);
    if(0 == st)
    {
        // new guy
        st = mStringCheck->addString(dr_lowlexi);
        (*mReads)[st] = new ReadList();
    }

#ifdef DEBUG
    logInfo("Direct repeat: "<<dr_lowlexi<<" String Token: "<<st, 10);
#endif
    
#ifdef SEARCH_SINGLETON
    SearchCheckerList::iterator debug_iter = debugger->find(tmpReadholder.getHeader());
    if (debug_iter != debugger->end()) {
        // our read got through to this stage

        std::cout<< tmpReadholder.splitApart();
        tmpReadholder.printContents();
        debug_iter->second.token(st);
        std::cout<<debug_iter->first<<" " <<st<<std::endl;
    }
#endif

    (*mReads)[st]->push_back(candidate);
}

