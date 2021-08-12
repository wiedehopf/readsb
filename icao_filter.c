// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// icao_filter.c: hashtable for ICAO addresses
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

#include "readsb.h"

// Open-addressed hash table with linear probing.
// We store each address twice to handle Data/Parity
// which need to match on a partial address (top 16 bits only).

// Maintain two tables and switch between them to age out entries.

#define filterBits (16)
#define filterBuckets (1LL << filterBits)
static uint32_t icao_filter_a[filterBuckets];
static uint32_t icao_filter_b[filterBuckets];
static uint32_t *icao_filter_active;

static inline uint32_t filterHash(uint32_t addr) {
    return addrHash(addr, filterBits);
}

#define EMPTY 0xFFFFFFFF

void icaoFilterInit() {
    memset(icao_filter_a, 0xFF, sizeof(icao_filter_a));
    memset(icao_filter_b, 0xFF, sizeof(icao_filter_b));
    icao_filter_active = icao_filter_a;
}

void icaoFilterAdd(uint32_t addr) {
    uint32_t h, h0;
    h0 = h = filterHash(addr);
    while (icao_filter_active[h] != EMPTY && icao_filter_active[h] != addr) {
        h = (h + 1) & (filterBuckets - 1);
        if (h == h0) {
            fprintf(stderr, "ICAO hash table full, increase AIRCRAFT_HASH_BITS\n");
            return;
        }
    }
    if (icao_filter_active[h] == EMPTY)
        icao_filter_active[h] = addr;

    /* disable as it's not being used
    // also add with a zeroed top byte, for handling DF20/21 with Data Parity
    h0 = h = filterHash(addr & 0x00ffff);
    while (icao_filter_active[h] != EMPTY && (icao_filter_active[h] & 0x00ffff) != (addr & 0x00ffff)) {
        h = (h + 1) & (filterBuckets - 1);
        if (h == h0) {
            fprintf(stderr, "ICAO hash table full, increase filterBuckets\n");
            return;
        }
    }
    if (icao_filter_active[h] == EMPTY)
        icao_filter_active[h] = addr;
    */
}

int icaoFilterTest(uint32_t addr) {
    uint32_t h, h0;

    h0 = h = filterHash(addr);
    while (icao_filter_a[h] != EMPTY && icao_filter_a[h] != addr) {
        h = (h + 1) & (filterBuckets - 1);
        if (h == h0)
            break;
    }
    if (icao_filter_a[h] == addr)
        return 1;

    h = h0;
    while (icao_filter_b[h] != EMPTY && icao_filter_b[h] != addr) {
        h = (h + 1) & (filterBuckets - 1);
        if (h == h0)
            break;
    }
    if (icao_filter_b[h] == addr)
        return 1;

    return 0;
}

// call this periodically:
void icaoFilterExpire() {
    int occupied = 0;
    for (int i = 0; i < filterBuckets; i++) {
        if (icao_filter_active[i] != EMPTY)
            occupied++;
    }
    //fprintf(stderr, "icao_filter fill %.2f %%\n", occupied * 100.0 / filterBuckets);
    if (icao_filter_active == icao_filter_a) {

        memset(icao_filter_b, 0xFF, sizeof(icao_filter_b));
        icao_filter_active = icao_filter_b;
    } else {
        memset(icao_filter_a, 0xFF, sizeof(icao_filter_a));
        icao_filter_active = icao_filter_a;
    }
}
