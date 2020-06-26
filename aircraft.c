#include "readsb.h"

uint32_t aircraftHash(uint32_t addr) {
    uint64_t h = 0x30732349f7810465ULL ^ (4 * 0x2127599bf4325c37ULL);
    uint64_t in = addr;
    uint64_t v = in << 48;
    v ^= in << 24;
    v ^= in;
    h ^= mix_fasthash(v);

    h -= (h >> 32);
    h &= (1ULL << 32) - 1;
    h -= (h >> AIRCRAFT_HASH_BITS);

    return h & (AIRCRAFT_BUCKETS - 1);
}
struct aircraft *aircraftGet(uint32_t addr) {
    struct aircraft *a = Modes.aircraft[aircraftHash(addr)];

    while (a && a->addr != addr) {
        a = a->next;
    }
    return a;
}

struct aircraft *aircraftCreate(struct modesMessage *mm) {
    uint32_t addr = mm->addr;
    if (Modes.aircraftCount > 8 * AIRCRAFT_BUCKETS) {
        fprintf(stderr, "ERROR ERROR, aircraft hash table overfilled!");
        return NULL;
    }
    struct aircraft *a = aircraftGet(addr);
    if (a)
        return a;
    a = (struct aircraft *) aligned_alloc(64, sizeof(struct aircraft));

    // Default everything to zero/NULL
    memset(a, 0, sizeof (struct aircraft));

    a->size_struct_aircraft = sizeof(struct aircraft);

    // Now initialise things that should not be 0/NULL to their defaults
    a->addr = mm->addr;
    a->addrtype = ADDR_UNKNOWN;
    for (int i = 0; i < 8; ++i) {
        a->signalLevel[i] = fmax(1e-5, mm->signalLevel);
    }
    a->signalNext = 0;

    // defaults until we see a message otherwise
    a->adsb_version = -1;
    a->adsb_hrd = HEADING_MAGNETIC;
    a->adsb_tah = HEADING_GROUND_TRACK;

    // Copy the first message so we can emit it later when a second message arrives.
    a->first_message = malloc(sizeof(struct modesMessage));
    memcpy(a->first_message, mm, sizeof(struct modesMessage));

    if (Modes.json_globe_index) {
        a->globe_index = -5;
    }

    if (pthread_mutex_init(&a->trace_mutex, NULL)) {
        fprintf(stderr, "Unable to initialize trace mutex!\n");
        exit(1);
    }

    // initialize data validity ages
    //adjustExpire(a, 58);
    Modes.stats_current.unique_aircraft++;


    uint32_t hash = aircraftHash(addr);
    a->next = Modes.aircraft[hash];
    Modes.aircraft[hash] = a;
    Modes.aircraftCount++;
    //if (((Modes.aircraftCount * 4) & (AIRCRAFT_BUCKETS - 1)) == 0)
    //    fprintf(stderr, "aircraft table fill: %0.1f\n", Modes.aircraftCount / (double) AIRCRAFT_BUCKETS );

    return a;
}
void apiAdd(struct aircraft *a) {
    if (Modes.iAddrLen > API_INDEX_MAX) {
        fprintf(stderr, "too many aircraft!.\n");
        return;
    }
    struct iAddr byLat;
    struct iAddr byLon;

    byLat.addr = a->addr;
    byLat.index = (int32_t) (a->lat * 1E6);

    byLon.addr = a->addr;
    byLon.index = (int32_t) (a->lon * 1E6);

    Modes.byLat[Modes.iAddrLen] = byLat;
    Modes.byLon[Modes.iAddrLen] = byLon;

    Modes.iAddrLen++;
}
static int compareIndex(const void *p1, const void *p2) {
    struct iAddr *a1 = (struct iAddr*) p1;
    struct iAddr *a2 = (struct iAddr*) p2;
    if (a1->index > a2->index)
        return 1;

    if (a1->index < a2->index)
        return -1;

    return 0;
}
void apiSort() {
    qsort(Modes.byLat, sizeof(struct iAddr), Modes.iAddrLen, compareIndex);
    qsort(Modes.byLon, sizeof(struct iAddr), Modes.iAddrLen, compareIndex);
}
