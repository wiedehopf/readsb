#ifndef AIRCRAFT_H
#define AIRCRAFT_H

#define API_INDEX_MAX 32000

uint32_t aircraftHash(uint32_t addr);
struct aircraft *aircraftGet(uint32_t addr);
struct aircraft *aircraftCreate(struct modesMessage *mm);

void apiClear();
void apiAdd(struct aircraft *a);
void apiSort();

struct iAddr {
    int32_t index;
    uint32_t addr;
};




#endif
