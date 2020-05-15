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
    h -= (h >> AIRCRAFTS_HASH_BITS);

    return h & (AIRCRAFTS_BUCKETS - 1);
}
struct aircraft *aircraftGet(uint32_t addr) {
    struct aircraft *a = Modes.aircrafts[aircraftHash(addr)];

    while (a && a->addr != addr) {
        a = a->next;
    }
    return a;
}

struct aircraft *aircraftCreate(struct modesMessage *mm) {
    uint32_t addr = mm->addr;
    if (Modes.aircraftCount > 8 * AIRCRAFTS_BUCKETS)
        return NULL;
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
    for (int i = 0; i < 8; ++i)
        a->signalLevel[i] = 1e-5;
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
    a->next = Modes.aircrafts[hash];
    Modes.aircrafts[hash] = a;
    Modes.aircraftCount++;
    if (((Modes.aircraftCount * 4) & (AIRCRAFTS_BUCKETS - 1)) == 0)
        fprintf(stderr, "aircraft table fill: %0.1f\n", Modes.aircraftCount / (double) AIRCRAFTS_BUCKETS );

    return a;
}
