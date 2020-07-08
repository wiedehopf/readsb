#ifndef AIRCRAFT_H
#define AIRCRAFT_H

#define API_INDEX_MAX 32000

uint32_t aircraftHash(uint32_t addr);
struct aircraft *aircraftGet(uint32_t addr);
struct aircraft *aircraftCreate(struct modesMessage *mm);

void apiClear();
void apiAdd(struct aircraft *a, uint64_t now);
void apiSort();
void apiReq(double latMin, double latMax, double lonMin, double lonMax, uint32_t *scratch);

struct av {
    uint32_t addr;
    int32_t value;
};

struct range {
    int from; // inclusive
    int to; // exclusive
};




#endif
