#include "readsb.h"

uint32_t receiverHash(uint64_t id) {
    uint64_t h = 0x30732349f7810465ULL ^ (4 * 0x2127599bf4325c37ULL);
    h ^= mix_fasthash(id);

    h -= (h >> 32);
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
    if (((Modes.receiverCount * 4) & (RECEIVER_TABLE_SIZE - 1)) == 0)
        fprintf(stderr, "receiverTable fill: %0.1f\n", Modes.receiverCount / (double) RECEIVER_TABLE_SIZE);
    return r;
}
void receiverTimeout(int part, int nParts) {
    int stride = RECEIVER_TABLE_SIZE / nParts;
    int start = stride * part;
    int end = start + stride;
    uint64_t now = mstime();
    for (int i = start; i < end; i++) {
        struct receiver **r = &Modes.receiverTable[i];

        while (*r) {
            /*
            receiver *b = *r;
            fprintf(stderr, "%016lx %9lu %4.0f %4.0f %4.0f %4.0f\n",
                    b->id, b->positionCounter,
                    b->latMin, b->latMax, b->lonMin, b->lonMax);
            */
            if (
                    (Modes.receiverCount > RECEIVER_TABLE_SIZE && (*r)->lastSeen < now - 1 * HOUR)
                    || ((*r)->lastSeen < now - 24 * HOUR)
               ) {
                *r = (*r)->next;
                Modes.receiverCount--;
            } else {
                r = &(*r)->next;
            }
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
}

int receiverGetReference(uint64_t id, double *lat, double *lon) {
    struct receiver *r = receiverGet(id);
    if (!r)
        return 0;
    double latDiff = r->latMax - r->latMin;
    double lonDiff = r->lonMax - r->lonMin;
    if (lonDiff > 30 || latDiff > 30 || r->positionCounter < 10)
        return 0;
    *lat = r->latMin + latDiff / 2;
    *lon = r->lonMin + lonDiff / 2;
    if (0)
        fprintf(stderr, "%016lx %9lu %4.0f %4.0f %4.0f %4.0f %4.0f %4.0f\n",
                r->id, r->positionCounter,
                r->latMin, *lat, r->latMax,
                r->lonMin, *lon, r->lonMax);
    return 1;
}
