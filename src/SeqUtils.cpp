/*
 *  SeqUtils.cpp is part of the CRisprASSembler project
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

#include "SeqUtils.h"
#include <string>
#include <algorithm>
#include <iostream>

using namespace std;
string reverseComplement(string str)
{
    string revcomp_string = "";
    string::reverse_iterator rit;
    cout<<str<<endl;
    for ( rit = str.rbegin() ; rit < str.rend(); rit++ )
    {
		switch ((*rit)) 
        {
            case 'A':
            case 'a':
                revcomp_string += 'T';
                break;
            case 'C':
            case 'c':
                revcomp_string += 'G';
                break;
            case 'G':
            case 'g':
                revcomp_string += 'C';
                break;
            case 'T':
            case 't':
            case 'U':
            case 'u':
                revcomp_string += 'A';
                break;
            case 'M':
            case 'm':
                revcomp_string +='K'; 
                break;
            case 'R':
            case 'r':
                revcomp_string += 'Y';
                break;
            case 'W':
            case 'w':
                revcomp_string += 'W';
                break;
            case 'S':
            case 's':
                revcomp_string += 'S';
                break;
            case 'Y':
            case 'y':
                revcomp_string += 'R';
                break;
            case 'K':
            case 'k':
                revcomp_string += 'M';
                break;
            case 'V':
            case 'v':
                revcomp_string += 'B';
                break;
            case 'H':
            case 'h':
                revcomp_string += 'D';
                break;
            case 'D':
            case 'd':
                revcomp_string += 'H';
                break;
            case 'B':
            case 'b':
                revcomp_string += 'V';
                break;
            case 'N':
            case 'n':
                revcomp_string += 'N';
                break;
            default:
                revcomp_string += 'N';
                break;
		}
	}
    cout<<revcomp_string<<endl;
    return revcomp_string;
}

string laurenize (string seq1)
{
    string seq2 = reverseComplement(seq1);
    if (lexicographical_compare(seq1.begin(), seq1.end(), seq2.begin(), seq2.end()))
    {
        return seq1;
    }
    return seq2;
}