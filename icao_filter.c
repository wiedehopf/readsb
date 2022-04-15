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

// Maintain two tables and switch between them to age out entries.

static uint32_t filterBits;
static uint32_t filterBuckets;
static size_t filterSize;
static uint32_t *icao_filter_a;
static uint32_t *icao_filter_b;
static uint32_t *icao_filter_active;

static uint32_t occupied;

static inline uint32_t filterHash(uint32_t addr) {
    return addrHash(addr, filterBits);
}

#define EMPTY 0xFFFFFFFF
#define MINBITS 8
#define MAXBITS 20

void icaoFilterInit() {
    filterBits = MINBITS;
    filterBuckets = 1ULL << filterBits;
    filterSize = filterBuckets * sizeof(uint32_t);
    occupied = 0;
    sfree(icao_filter_a);
    sfree(icao_filter_b);
    icao_filter_a = cmalloc(filterSize);
    icao_filter_b = cmalloc(filterSize);
    memset(icao_filter_a, 0xFF, filterSize);
    memset(icao_filter_b, 0xFF, filterSize);
    icao_filter_active = icao_filter_a;
}
void icaoFilterDestroy() {
    sfree(icao_filter_a);
    sfree(icao_filter_b);
}

static void icaoFilterResize(uint32_t bits) {
    uint32_t oldBuckets = filterBuckets;
    uint32_t *oldActive = icao_filter_active;
    uint32_t *oldA = icao_filter_a;
    uint32_t *oldB = icao_filter_b;

    filterBits = bits;
    filterBuckets = 1ULL << filterBits;
    filterSize = filterBuckets * sizeof(uint32_t);

    if (filterBuckets > 256000)
        fprintf(stderr, "icao_filter: changing size to %d!\n", (int) filterBuckets);

    icao_filter_a = cmalloc(filterSize);
    icao_filter_b = cmalloc(filterSize);
    memset(icao_filter_a, 0xFF, filterSize);
    memset(icao_filter_b, 0xFF, filterSize);

    // reset occupied count
    occupied = 0;
    icao_filter_active = icao_filter_a;
    for (uint32_t i = 0; i < oldBuckets; i++) {
        if (oldActive[i] != EMPTY) {
            icaoFilterAdd(oldActive[i]);
        }
    }
    sfree(oldA);
    sfree(oldB);
}

// call this periodically:
void icaoFilterExpire() {
    if (occupied < filterBuckets / 9 && filterBits > MINBITS) {
        icaoFilterResize(filterBits - 1);
    }
    // reset occupied count
    occupied = 0;
    if (icao_filter_active == icao_filter_a) {

        memset(icao_filter_b, 0xFF, filterSize);
        icao_filter_active = icao_filter_b;
    } else {
        memset(icao_filter_a, 0xFF, filterSize);
        icao_filter_active = icao_filter_a;
    }
}

void icaoFilterAdd(uint32_t addr) {
    uint32_t h, h0;
    h0 = h = filterHash(addr);
    while (icao_filter_active[h] != EMPTY && icao_filter_active[h] != addr) {
        h = (h + 1) & (filterBuckets - 1);
        if (h == h0) {
            fprintf(stderr, "ICAO hash table full, this shouldn't happen\n");
            return;
        }
    }
    if (icao_filter_active[h] == EMPTY) {
        occupied++;
        icao_filter_active[h] = addr;
    }

    if (occupied > filterBuckets / 3 && filterBits < 20) {
        icaoFilterResize(filterBits + 1);
    }
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
