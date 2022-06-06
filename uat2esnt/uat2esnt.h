//
// Copyright 2015, Oliver Jowett <oliver@mutability.co.uk>
//

// This file is free software: you may copy, redistribute and/or modify it  
// under the terms of the GNU General Public License as published by the
// Free Software Foundation, either version 2 of the License, or (at your  
// option) any later version.  
//
// This file is distributed in the hope that it will be useful, but  
// WITHOUT ANY WARRANTY; without even the implied warranty of  
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU  
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License  
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#ifndef UAT2ESNT_H
#define UAT2ESNT_H

#include <stdint.h>

// call once before calling other functions
void uat2esnt_initCrcTables();

//
// p is the start of the UAT message starting with + or -
// end points one byte after the last message byte
//
// allocate 1024 bytes of output buffer and pass as out variable
// pass pointer past end of buffer as out_end
//
// 1 uat message can result in the output of multiple AVR type messages, each message is newline terminated
//
// using AVR / raw bytes as ascii as output, using this fromat with timestamp and signal: <+beast_ts+sigL+raw+;\n
// <FF004D4C4155FF96AD723390159118FA27A9BE53EA;
//
void uat2esnt_convert_message(char *p, char *end, char *out, char *out_end);
#endif
