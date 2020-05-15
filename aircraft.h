#ifndef AIRCRAFT_H
#define AIRCRAFT_H

uint32_t aircraftHash(uint32_t addr);
struct aircraft *aircraftGet(uint32_t addr);
struct aircraft *aircraftCreate(struct modesMessage *mm);




#endif
