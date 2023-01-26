// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// net_io.h: network handling.
//
// Copyright (c) 2019 Michael Wolf <michael@mictronics.de>
//
// This code is based on a detached fork of dump1090-fa.
//
// Copyright (c) 2014,2015 Oliver Jowett <oliver@mutability.co.uk>
//
// This file is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// any later version.
//
// This file is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#ifndef JSON_OUT_H
#define JSON_OUT_H

typedef struct traceBuffer traceBuffer;


int includeAircraftJson(int64_t now, struct aircraft *a);

void printACASInfoShort(uint32_t addr, unsigned char *MV, struct aircraft *a, struct modesMessage *mm, int64_t now);
void logACASInfoShort(uint32_t addr, unsigned char *MV, struct aircraft *a, struct modesMessage *mm, int64_t now);

char *sprintACASInfoShort(char *p, char *end, uint32_t addr, unsigned char *MV, struct aircraft *a, struct modesMessage *mm, int64_t now);
char *sprintAircraftObject(char *p, char *end, struct aircraft *a, int64_t now, int printMode, struct modesMessage *mm, bool includeSeenByList);
char *sprintAircraftRecent(char *p, char *end, struct aircraft *a, int64_t now, int printMode, struct modesMessage *mm, int64_t recent);
size_t calculateSeenByListJsonSize(struct aircraft *a, int64_t now);
struct char_buffer generateAircraftJson(int64_t onlyRecent);
struct char_buffer generateAircraftBin(threadpool_buffer_t *pbuffer);
struct char_buffer generateTraceJson(struct aircraft *a, traceBuffer tb, int start, int last, threadpool_buffer_t *buffer, int64_t startStamp);
struct char_buffer generateGlobeBin(int globe_index, int mil, threadpool_buffer_t *buffer);
struct char_buffer generateGlobeJson(int globe_index, threadpool_buffer_t *buffer);
struct char_buffer generateReceiverJson ();
struct char_buffer generateHistoryJson ();
struct char_buffer generateClientsJson();
struct char_buffer generateOutlineJson();
struct char_buffer generateVRS(int part, int n_parts, int reduced_data);

struct char_buffer writeJsonToFile (const char* dir, const char *file, struct char_buffer cb);
struct char_buffer writeJsonToGzip (const char* dir, const char *file, struct char_buffer cb, int gzip);

__attribute__ ((format(printf, 3, 4))) static inline char *safe_snprintf(char *p, char *end, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    p += vsnprintf(p < end ? p : NULL, p < end ? (size_t) (end - p) : 0, format, ap);
    if (p > end)
        p = end;
    va_end(ap);
    return p;
}

const char *nav_modes_flags_string(nav_modes_t flags);

#endif
