#include "readsb.h"

#define MAX_DIFF 35.0

uint32_t receiverHash(uint64_t id) {
    uint64_t h = 0x30732349f7810465ULL ^ (4 * 0x2127599bf4325c37ULL);
    h ^= mix_fasthash(id);

    h -= (h >> 32);
    h &= (1ULL << 32) - 1;
    h -= (h >> RECEIVER_TABLE_HASH_BITS);

    return h & (RECEIVER_TABLE_SIZE - 1);
}

struct receiver *receiverGet(uint64_t id) {
    struct receiver *r = Modes.receiverTable[receiverHash(id)];

    while (r && r->id != id) {
        r = r->next;
    }
    return r;
}
struct receiver *receiverCreate(uint64_t id) {
    if (Modes.receiverCount > 4 * RECEIVER_TABLE_SIZE)
        return NULL;
    struct receiver *r = receiverGet(id);
    if (r)
        return r;
    uint32_t hash = receiverHash(id);
    r = malloc(sizeof(struct receiver));
    *r = (struct receiver) {0};
    r->id = id;
    r->next = Modes.receiverTable[hash];
    Modes.receiverTable[hash] = r;
    Modes.receiverCount++;
    if (((Modes.receiverCount * 16) & (RECEIVER_TABLE_SIZE - 1)) == 0)
        fprintf(stderr, "receiverTable fill: %0.8f\n", Modes.receiverCount / (double) RECEIVER_TABLE_SIZE);
    return r;
}
void receiverTimeout(int part, int nParts) {
    int stride = RECEIVER_TABLE_SIZE / nParts;
    int start = stride * part;
    int end = start + stride;
    //fprintf(stderr, "START: %8d END: %8d\n", start, end);
    uint64_t now = mstime();
    for (int i = start; i < end; i++) {
        struct receiver **r = &Modes.receiverTable[i];
        struct receiver *del;
        while (*r) {
            /*
            receiver *b = *r;
            fprintf(stderr, "%016"PRIx64" %9"PRu64" %4.0f %4.0f %4.0f %4.0f\n",
                    b->id, b->positionCounter,
                    b->latMin, b->latMax, b->lonMin, b->lonMax);
            */
            if (
                    (Modes.receiverCount > RECEIVER_TABLE_SIZE && (*r)->lastSeen < now - 20 * MINUTES)
                    || ((*r)->lastSeen < now - 1 * HOURS)
                    || Modes.exit
               ) {
                del = *r;
                *r = (*r)->next;
                Modes.receiverCount--;
                free(del);
            } else {
                r = &(*r)->next;
            }
        }
    }
}
void receiverCleanup() {
    for (int i = 0; i < RECEIVER_TABLE_SIZE; i++) {
        struct receiver *r = Modes.receiverTable[i];
        struct receiver *next;
        while (r) {
            next = r->next;
            free(r);
            r = next;
        }
    }
}
void receiverPositionReceived(struct aircraft *a, uint64_t id, double lat, double lon, uint64_t now) {
    if (bogus_lat_lon(lat, lon))
        return;
    if (lat > 85.0 || lat < -85.0 || lon < -175 || lon > 175)
        return;
    struct receiver *r = receiverGet(id);

    if (!r) {
        r = receiverCreate(id);
        if (!r)
            return;
        r->lonMin = lon;
        r->lonMax = lon;
        r->latMin = lat;
        r->latMax = lat;

    } else {
        struct receiver before = *r;

        r->lonMin = fmin(r->lonMin, lon);
        r->latMin = fmin(r->latMin, lat);

        r->lonMax = fmax(r->lonMax, lon);
        r->latMax = fmax(r->latMax, lat);

        r->lastSeen = now;
        r->positionCounter++;

        double latDiff = before.latMax - before.latMin;
        double lonDiff = before.lonMax - before.lonMin;

        double latDiff2 = r->latMax - r->latMin;
        double lonDiff2 = r->lonMax - r->lonMin;

        int debug = 0;

        if (Modes.debug_receiver && (lonDiff2 > MAX_DIFF || latDiff2 > MAX_DIFF) && !(lonDiff > MAX_DIFF || latDiff > MAX_DIFF))
            debug = 1;
        if (Modes.debug_receiver && id == 0x33174156975948af)
            debug = 1;
        if (debug)
            fprintf(stderr, "hex: %06x id: %016"PRIx64" #pos: %9"PRIu64" %12.5f %12.5f %4.0f %4.0f %4.0f %4.0f\n",
                    a->addr, r->id, r->positionCounter,
                    lat, lon,
                    before.latMin, before.latMax,
                    before.lonMin, before.lonMax);
    }
}

struct receiver *receiverGetReference(uint64_t id, double *lat, double *lon, struct aircraft *a) {
    struct receiver *r = receiverGet(id);
    if (!r) {
        if (a->addr == Modes.cpr_focus)
            fprintf(stderr, "%06x: no associated receiver found.\n", a->addr);
        return NULL;
    }
    double latDiff = r->latMax - r->latMin;
    double lonDiff = r->lonMax - r->lonMin;


    if (r->positionCounter < 500)
        return NULL;
    if (lonDiff > MAX_DIFF || latDiff > MAX_DIFF) {
        if (0 && Modes.debug_receiver)
            fprintf(stderr, "%06x: receiver ref invalid: %016"PRIx64" %9"PRIu64" %4.0f %4.0f %4.0f %4.0f %4.0f %4.0f\n",
                    a->addr,
                    r->id, r->positionCounter,
                    r->latMin, *lat, r->latMax,
                    r->lonMin, *lon, r->lonMax);
        return NULL;
    }

    // all checks good, set reference latitude and return 1

    *lat = r->latMin + latDiff / 2;
    *lon = r->lonMin + lonDiff / 2;

    /*
       if (Modes.debug_receiver || a->addr == Modes.cpr_focus)
       fprintf(stderr, "id:%016"PRIx64" #pos:%9"PRIu64" lat min:%4.0f mid:%4.0f max:%4.0f lon min:%4.0f mid:%4.0f max:%4.0f\n",
       r->id, r->positionCounter,
       r->latMin, *lat, r->latMax,
       r->lonMin, *lon, r->lonMax);
       }
       */

    return r;
    }
void receiverTest() {
    uint64_t now = mstime();
    for (uint64_t i = 0; i < (1<<22); i++) {
        uint64_t id = i << 22;
        receiver *r = receiverGet(id);
        if (!r)
            r = receiverCreate(id);
        if (r)
            r->lastSeen = now;
    }
    printf("%"PRIu64"\n", Modes.receiverCount);
    for (int i = 0; i < (1<<22); i++) {
        receiver *r = receiverGet(i);
        if (!r)
            r = receiverCreate(i);
    }
    printf("%"PRIu64"\n", Modes.receiverCount);
    receiverTimeout(0, 1);
    printf("%"PRIu64"\n", Modes.receiverCount);
}
