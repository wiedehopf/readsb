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
    a = malloc(sizeof(struct aircraft));

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
void apiClear() {
    Modes.avLen = 0;
}

void apiAdd(struct aircraft *a, uint64_t now) {
    // don't include stale aircraft in the API index
    if (a->position_valid.source != SOURCE_JAERO && now > a->seen + 60 * 1000)
        return;
    if (a->messages < 2)
        return;

    if (Modes.avLen > API_INDEX_MAX) {
        fprintf(stderr, "too many aircraft!.\n");
        return;
    }
    struct av byLat;
    struct av byLon;

    byLat.addr = a->addr;
    byLat.value = (int32_t) (a->lat * 1E6);

    byLon.addr = a->addr;
    byLon.value = (int32_t) (a->lon * 1E6);

    Modes.byLat[Modes.avLen] = byLat;
    Modes.byLon[Modes.avLen] = byLon;

    Modes.avLen++;
}

static int compareValue(const void *p1, const void *p2) {
    struct av *a1 = (struct av*) p1;
    struct av *a2 = (struct av*) p2;
    return (a1->value > a2->value) - (a1->value < a2->value);
}

void apiSort() {
    qsort(Modes.byLat, Modes.avLen, sizeof(struct av), compareValue);
    qsort(Modes.byLon, Modes.avLen, sizeof(struct av), compareValue);
}

static struct range findRange(int32_t ref_from, int32_t ref_to, struct av *list, int len) {
    struct range res = {0, 0};
    if (len == 0 || ref_from > ref_to)
        return res;

    // get lower bound
    int i = 0;
    int j = len - 1;
    while (j > i + 1) {

        int pivot = (i + j) / 2;

        if (list[pivot].value < ref_from)
            i = pivot;
        else
            j = pivot;
    }
    if (list[j].value < ref_from)
        res.from = j;
    else
        res.from = i;

    // get upper bound (exclusive)
    i = res.from;
    j = len - 1;
    while (j > i + 1) {

        int pivot = (i + j) / 2;

        if (list[pivot].value <= ref_to)
            i = pivot;
        else
            j = pivot;
    }
    if (list[j].value > ref_to)
        res.to = j + 1;
    else
        res.to = i + 1;

    return res;
}

static int compareUint32(const void *p1, const void *p2) {
    uint32_t *a1 = (uint32_t *) p1;
    uint32_t *a2 = (uint32_t *) p2;
    return (*a1 > *a2) - (*a1 < *a2);
}

void apiReq(double latMin, double latMax, double lonMin, double lonMax, uint32_t *scratch) {

    int32_t lat1 = (int32_t) (latMin * 1E6);
    int32_t lat2 = (int32_t) (latMax * 1E6);
    int32_t lon1 = (int32_t) (lonMin * 1E6);
    int32_t lon2 = (int32_t) (lonMax * 1E6);

    struct range rangeLat = findRange(lat1, lat2, Modes.byLat, Modes.avLen);
    struct range rangeLon = findRange(lon1, lon2, Modes.byLon, Modes.avLen);

    int allocLat = rangeLat.to - rangeLat.from;
    int allocLon = rangeLon.to - rangeLon.from;

    fprintf(stderr, "%d %d %d %d %d %d\n", allocLat, allocLon, rangeLat.from, rangeLat.to, rangeLon.from, rangeLon.to);

    if (!allocLat || !allocLon) {
        scratch[0] = 0;
        return;
    }

    uint32_t *listLat = &scratch[1 * API_INDEX_MAX];
    uint32_t *listLon = &scratch[2 * API_INDEX_MAX];

    for (int i = 0; i < allocLat; i++) {
        listLat[i] = Modes.byLat[rangeLat.from + i].addr;
    }
    qsort(listLat, allocLat, sizeof(uint32_t), compareUint32);

    for (int i = 0; i < allocLon; i++) {
        listLon[i] = Modes.byLon[rangeLon.from + i].addr;
    }
    qsort(listLon, allocLon, sizeof(uint32_t), compareUint32);

    int i = 0;
    int j = 0;
    int k = 0;
    while (j < allocLat && k < allocLon) {
        if (listLat[j] < listLon[k]) {
            j++;
            continue;
        }
        if (listLat[j] > listLon[k]) {
            k++;
            continue;
        }

        scratch[i] = listLat[j];
        fprintf(stderr, "%06x %06x\n", listLat[j], listLon[k]);
        i++;
        j++;
        k++;
    }
    scratch[i] = 0;
    return;
}
