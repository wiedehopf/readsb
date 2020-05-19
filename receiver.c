#include "readsb.h"

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
                    (Modes.receiverCount > RECEIVER_TABLE_SIZE && (*r)->lastSeen < now - 20 * MINUTE)
                    || ((*r)->lastSeen < now - 1 * HOUR)
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
void receiverPositionReceived(uint64_t id, double lat, double lon, uint64_t now) {
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
        r->lonMin = fmin(r->lonMin, lon);
        r->lonMax = fmax(r->lonMax, lon);
        r->latMin = fmin(r->latMin, lat);
        r->latMax = fmax(r->latMax, lat);
    }
    r->lastSeen = now;
    r->positionCounter++;

    if (0 && Modes.debug_receiver)
        fprintf(stderr, "%016"PRIx64" %9"PRIu64" %4.0f %4.0f %4.0f %4.0f %4.0f %4.0f\n",
                r->id, r->positionCounter,
                lat, lon,
                r->latMin, r->latMax,
                r->lonMin, r->lonMax);
}

int receiverGetReference(uint64_t id, double *lat, double *lon, struct aircraft *a) {
    struct receiver *r = receiverGet(id);
    if (!r) {
        if (a->addr == Modes.cpr_focus)
            fprintf(stderr, "%06x: no associated receiver found.\n", a->addr);
        return 0;
    }
    double latDiff = r->latMax - r->latMin;
    double lonDiff = r->lonMax - r->lonMin;
    *lat = r->latMin + latDiff / 2;
    *lon = r->lonMin + lonDiff / 2;

    if (a->addr == Modes.cpr_focus)
        fprintf(stderr, "%06x: receiver ref invalid: %016"PRIx64" %9"PRIu64" %4.0f %4.0f %4.0f %4.0f %4.0f %4.0f\n",
                a->addr,
                r->id, r->positionCounter,
                r->latMin, *lat, r->latMax,
                r->lonMin, *lon, r->lonMax);

    if (lonDiff > 30 || latDiff > 30 || r->positionCounter < 50)
        return 0;

    /*
    if (Modes.debug_receiver || a->addr == Modes.cpr_focus)
        fprintf(stderr, "%016"PRIx64" %9"PRIu64" %4.0f %4.0f %4.0f %4.0f %4.0f %4.0f\n",
                r->id, r->positionCounter,
                r->latMin, *lat, r->latMax,
                r->lonMin, *lon, r->lonMax);
                */
    return 1;
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
