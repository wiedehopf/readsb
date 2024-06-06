#include "readsb.h"

#define RECEIVER_MAX_RANGE 800e3

uint32_t receiverHash(uint64_t id) {
    uint64_t h = 0x30732349f7810465ULL ^ (4 * 0x2127599bf4325c37ULL);
    h ^= mix_fasthash(id);

    h -= (h >> 32);
    h &= (1ULL << 32) - 1;
    h -= (h >> Modes.receiver_table_hash_bits);

    return h & (Modes.receiver_table_size - 1);
}

struct receiver *receiverGet(uint64_t id) {
    if (!Modes.receiverTable) {
        return NULL;
    }
    struct receiver *r = Modes.receiverTable[receiverHash(id)];

    while (r && r->id != id) {
        r = r->next;
    }
    return r;
}
struct receiver *receiverCreate(uint64_t id) {
    if (!Modes.receiverTable) {
        return NULL;
    }
    struct receiver *r = receiverGet(id);
    if (r)
        return r;
    if (Modes.receiverCount > Modes.receiver_table_size)
        return NULL;
    uint32_t hash = receiverHash(id);
    r = cmalloc(sizeof(struct receiver));
    *r = (struct receiver) {0};
    r->id = id;
    r->next = Modes.receiverTable[hash];
    r->firstSeen = r->lastSeen = mstime();
    Modes.receiverTable[hash] = r;
    Modes.receiverCount++;
    if (Modes.receiverCount % (Modes.receiver_table_size / 4) == 0)
        fprintf(stderr, "receiverTable fill: %0.8f\n", Modes.receiverCount / (double) Modes.receiver_table_size);
    if (Modes.debug_receiver && Modes.receiverCount % 128 == 0)
        fprintf(stderr, "receiverCount: %"PRIu64"\n", Modes.receiverCount);
    return r;
}
static void receiverDebugPrint(struct receiver *r, char *message) {
    if (1) {
        return;
    }
    fprintf(stderr, "%016"PRIx64" %9lld %6.1f %6.1f %6.1f %6.1f %s\n",
            r->id, (long long) r->positionCounter,
            r->latMin, r->latMax, r->lonMin, r->lonMax,
            message);
}
static void receiverMaintenance(struct receiver *r) {
    double decay = 0.005 * RECEIVER_MAINTENANCE_INTERVAL / SECONDS;

    receiverDebugPrint(r, "beforeDecay");

    // only decay if extent is pretty large

    if (r->latMax - r->latMin > 10) {
        r->latMax -= decay;
        r->latMin += decay;
    }

    if (r->lonMax - r->lonMin > 10) {
        r->lonMax -= decay;
        r->lonMin += decay;
    }

    receiverDebugPrint(r, "afterDecay");

}
void receiverTimeout(int part, int nParts, int64_t now) {
    if (!Modes.receiverTable) {
        return;
    }
    int stride = Modes.receiver_table_size / nParts;
    int start = stride * part;
    int end = start + stride;
    //fprintf(stderr, "START: %8d END: %8d\n", start, end);
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
                    (Modes.receiverCount > Modes.receiver_table_size && (*r)->lastSeen < now - 20 * MINUTES)
                    || (now > (*r)->lastSeen + 24 * HOURS)
                    || ((*r)->badExtent && now > (*r)->badExtent + 30 * MINUTES)
               ) {

                del = *r;
                *r = (*r)->next;
                Modes.receiverCount--;
                free(del);
            } else {
                receiverMaintenance(*r);
                r = &(*r)->next;
            }
        }
    }
}
void receiverInit() {
    if (Modes.netReceiverId || Modes.netIngest || Modes.debug_no_discard || Modes.viewadsb) {
        Modes.receiver_table_hash_bits = 16;
    } else {
        Modes.receiver_table_hash_bits = 8;
    }

    Modes.receiver_table_size = 1 << Modes.receiver_table_hash_bits;

    Modes.receiverTable = cmalloc(Modes.receiver_table_size * sizeof(struct receiver));
    memset(Modes.receiverTable, 0x0,  Modes.receiver_table_size * sizeof(struct receiver));
}
void receiverCleanup() {
    if (!Modes.receiverTable) {
        return;
    }
    for (int i = 0; i < Modes.receiver_table_size; i++) {
        struct receiver *r = Modes.receiverTable[i];
        struct receiver *next;
        while (r) {
            next = r->next;
            free(r);
            r = next;
        }
    }
    sfree(Modes.receiverTable);
}
int receiverPositionReceived(struct aircraft *a, struct modesMessage *mm, double lat, double lon, int64_t now) {
    uint64_t id = mm->receiverId;
    if (id == 0 || lat > 85.0 || lat < -85.0 || lon < -179.9 || lon > 179.9) {
        return RECEIVER_RANGE_UNCLEAR;
    }
    int reliabilityRequired = Modes.position_persistence * 3 / 4;
    if (Modes.viewadsb || Modes.receiver_focus) {
        reliabilityRequired = imin(2, Modes.position_persistence);
    }

    // we only use ADS-B positions in the air with sufficient reliability to affect the receiver position / extent
    int noModifyReceiver = (mm->source != SOURCE_ADSB || mm->cpr_type == CPR_SURFACE
            || a->pos_reliable_odd < reliabilityRequired || a->pos_reliable_even < reliabilityRequired);

    struct receiver *r = receiverGet(id);

    if (!r || r->positionCounter == 0) {
        if (noModifyReceiver) {
            return RECEIVER_RANGE_UNCLEAR;
        }
        r = receiverCreate(id);
        if (!r) {
            return RECEIVER_RANGE_UNCLEAR;
        }
        r->lonMin = lon;
        r->lonMax = lon;
        r->latMin = lat;
        r->latMax = lat;
    }

    // diff before applying new position (we'll just get distance zero for a new receiver, this is fine)
    struct receiver before = *r;
    double latDiff = before.latMax - before.latMin;
    double lonDiff = before.lonMax - before.lonMin;

    double rlat = r->latMin + latDiff / 2;
    double rlon = r->lonMin + lonDiff / 2;

    double distance = greatcircle(rlat, rlon, lat, lon, 1);

    if (!noModifyReceiver) {
        if (distance < RECEIVER_MAX_RANGE) {
            r->lonMin = fmin(r->lonMin, lon);
            r->latMin = fmin(r->latMin, lat);

            r->lonMax = fmax(r->lonMax, lon);
            r->latMax = fmax(r->latMax, lat);
            if (
                    before.latMin != r->latMin
                    || before.latMax != r->latMax
                    || before.lonMin != r->lonMin
                    || before.lonMax != r->lonMax
               ) {
                //receiverDebugPrint(r, "growingExtent");
            }
            r->goodCounter++;
            r->badCounter = fmax(0, r->badCounter - 0.5);
        }

        if (!r->badExtent && distance > RECEIVER_MAX_RANGE) {
            int badExtent = 1;
            for (int i = 0; i < RECEIVER_BAD_AIRCRAFT; i++) {
                struct bad_ac *bad = &r->badAircraft[i];
                if (bad->addr == a->addr) {
                    badExtent = 0;
                    break;
                }
            }
            for (int i = 0; i < RECEIVER_BAD_AIRCRAFT; i++) {
                struct bad_ac *bad = &r->badAircraft[i];
                if (now - bad->ts > 3 * MINUTES) {
                    // new entry
                    bad->ts = now;
                    bad->addr = a->addr;
                    badExtent = 0;
                    break;
                }
            }
            if (badExtent) {
                r->badExtent = now;

                if (Modes.debug_receiver) {
                    char uuid[32]; // needs 18 chars and null byte
                    sprint_uuid1(r->id, uuid);
                    fprintf(stderr, "receiverBadExtent: %0.0f nmi hex: %06x id: %s #pos: %9"PRIu64" %12.5f %12.5f %4.0f %4.0f %4.0f %4.0f\n",
                            distance / 1852.0, a->addr, uuid, r->positionCounter,
                            lat, lon,
                            before.latMin, before.latMax,
                            before.lonMin, before.lonMax);
                }
            }
        }
    }

    if (!noModifyReceiver) {
        r->positionCounter++;
        r->lastSeen = now;
    }

    if (distance > RECEIVER_MAX_RANGE) {
        return RECEIVER_RANGE_BAD;
    }

    return RECEIVER_RANGE_GOOD;
}

struct receiver *receiverGetReference(uint64_t id, double *lat, double *lon, struct aircraft *a, int noDebug) {
    if (!Modes.receiverTable) {
        return NULL;
    }
    struct receiver *r = receiverGet(id);
    if (!(Modes.debug_receiver && a && a->addr == Modes.cpr_focus)) {
        noDebug = 1;
    }
    if (!r) {
        if (!noDebug) {
            fprintf(stderr, "id:%016"PRIx64" NOREF: receiverId not known\n", id);
        }
        return NULL;
    }


    double latDiff = r->latMax - r->latMin;
    double lonDiff = r->lonMax - r->lonMin;

    *lat = r->latMin + latDiff / 2;
    *lon = r->lonMin + lonDiff / 2;

    uint32_t positionCounterRequired = (Modes.viewadsb || Modes.receiver_focus) ? 4 : 100;
    if (r->positionCounter < positionCounterRequired || r->badExtent) {
        if (!noDebug) {
            fprintf(stderr, "id:%016"PRIx64" NOREF: #posCounter:%9"PRIu64" refLoc: %4.0f,%4.0f lat: %4.0f to %4.0f lon: %4.0f to %4.0f\n",
                    r->id, r->positionCounter,
                    *lat, *lon,
                    r->latMin, r->latMax,
                    r->lonMin, r->lonMax);
        }
        return NULL;
    }

    if (!noDebug) {
        fprintf(stderr, "id:%016"PRIx64" #posCounter:%9"PRIu64" refLoc: %4.0f,%4.0f lat: %4.0f to %4.0f lon: %4.0f to %4.0f\n",
                r->id, r->positionCounter,
                *lat, *lon,
                r->latMin, r->latMax,
                r->lonMin, r->lonMax);
    }

    return r;
}
void receiverTest() {
    int64_t now = mstime();
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
    receiverTimeout(0, 1, mstime());
    printf("%"PRIu64"\n", Modes.receiverCount);
}

int receiverCheckBad(uint64_t id, int64_t now) {
    struct receiver *r = receiverGet(id);
    if (r && now < r->timedOutUntil)
        return 1;
    else
        return 0;
}

struct receiver *receiverBad(uint64_t id, uint32_t addr, int64_t now) {
    if (!Modes.receiverTable) {
        return NULL;
    }
    struct receiver *r = receiverGet(id);

    if (!r)
        r = receiverCreate(id);

    int64_t timeout = 12 * SECONDS;

    if (r && now + (timeout * 2 / 3) > r->timedOutUntil) {
        r->lastSeen = now;
        r->badCounter++;
        if (r->badCounter > 5.99) {
            r->timedOutCounter++;
            if (Modes.debug_garbage) {
                char uuid[32]; // needs 18 chars and null byte
                sprint_uuid1(r->id, uuid);
                fprintf(stderr, "timeout receiverId: %s hex: %06x #good: %6d #bad: %5.0f #timeouts: %u\n",
                        uuid, addr, r->goodCounter, r->badCounter, r->timedOutCounter);
            }
            r->timedOutUntil = now + timeout;
            r->goodCounter = 0;
            r->badCounter = 0;
        }
        return r;
    } else {
        return NULL;
    }
}

struct char_buffer generateReceiversJson() {
    struct char_buffer cb;
    int64_t now = mstime();

    size_t buflen = 1*1024*1024; // The initial buffer is resized as needed
    char *buf = (char *) cmalloc(buflen), *p = buf, *end = buf + buflen;

    p = safe_snprintf(p, end, "{ \"now\" : %.1f,\n", now / 1000.0);

    //p = safe_snprintf(p, end, "  \"columns\" : [ \"receiverId\", \"\"],\n");
    p = safe_snprintf(p, end, "  \"receivers\" : [\n");

    struct receiver *r;

    if (Modes.receiverTable) {
        for (int j = 0; j < Modes.receiver_table_size; j++) {
            for (r = Modes.receiverTable[j]; r; r = r->next) {

                // check if we have enough space
                if ((p + 1000) >= end) {
                    int used = p - buf;
                    buflen *= 2;
                    buf = (char *) realloc(buf, buflen);
                    p = buf + used;
                    end = buf + buflen;
                }

                char uuid[64];
                sprint_uuid1(r->id, uuid);

                double elapsed = (r->lastSeen - r->firstSeen) / 1000.0 + 1.0;
                p = safe_snprintf(p, end, "    [ \"%s\", %6.2f, %6.2f, %6.2f, %6.2f, %7.2f, %7.2f, %d, %0.2f,%0.2f ],\n",
                        uuid,
                        r->positionCounter / elapsed,
                        r->timedOutCounter * 3600.0 / elapsed,
                        r->latMin,
                        r->latMax,
                        r->lonMin,
                        r->lonMax,
                        r->badExtent ? 1 : 0,
                        r->latMin + (r->latMax - r->latMin) / 2.0,
                        r->lonMin + (r->lonMax - r->lonMin) / 2.0);

                if (p >= end)
                    fprintf(stderr, "buffer overrun client json\n");
            }
        }
    }

    if (*(p-2) == ',')
        *(p-2) = ' ';

    p = safe_snprintf(p, end, "  ]\n}\n");

    cb.len = p - buf;
    cb.buffer = buf;
    return cb;
}
