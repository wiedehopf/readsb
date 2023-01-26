#include "readsb.h"

#define API_HASH_BITS (16)
#define API_BUCKETS (1 << API_HASH_BITS)

static int apiUpdate();
static inline uint32_t hexHash(uint32_t addr) {
    return addrHash(addr, API_HASH_BITS);
}

static inline uint32_t regHash(char *reg) {
    const uint64_t seed = 0x30732349f7810465ULL;
    uint64_t h = fasthash64(reg, 12, seed);

    uint32_t bits = API_HASH_BITS;
    uint64_t res = h ^ (h >> 32);

    if (bits < 16)
        res ^= (res >> 16);

    res ^= (res >> bits);

    // mask to fit the requested bit width
    res &= (((uint64_t) 1) << bits) - 1;

    return (uint32_t) res;
}

static inline uint32_t callsignHash(char *callsign) {
    const uint64_t seed = 0x30732349f7810465ULL;
    uint64_t h = fasthash64(callsign, 8, seed);

    uint32_t bits = API_HASH_BITS;
    uint64_t res = h ^ (h >> 32);

    if (bits < 16)
        res ^= (res >> 16);

    res ^= (res >> bits);

    // mask to fit the requested bit width
    res &= (((uint64_t) 1) << bits) - 1;

    return (uint32_t) res;
}

static int antiSpam(int64_t *nextPrint, int64_t interval) {
    int64_t now = mstime();
    if (now > *nextPrint) {
        *nextPrint = now + interval;
        return 1;
    } else {
        return 0;
    }
}

static int compareLon(const void *p1, const void *p2) {
    struct apiEntry *a1 = (struct apiEntry*) p1;
    struct apiEntry *a2 = (struct apiEntry*) p2;
    return (a1->bin.lon > a2->bin.lon) - (a1->bin.lon < a2->bin.lon);
}

static struct range findLonRange(int32_t ref_from, int32_t ref_to, struct apiEntry *list, int len) {
    struct range res;
    memset(&res, 0, sizeof(res));
    if (len == 0 || ref_from > ref_to)
        return res;

    // get lower bound
    int i = 0;
    int j = len - 1;
    while (j > i + 1) {

        int pivot = (i + j) / 2;

        if (list[pivot].bin.lon < ref_from)
            i = pivot;
        else
            j = pivot;
    }

    if (list[j].bin.lon < ref_from) {
        res.from = j + 1;
    } else if (list[i].bin.lon < ref_from) {
        res.from = i + 1;
    } else {
        res.from = i;
    }


    // get upper bound (exclusive)
    i = imin(res.from, len - 1);
    j = len - 1;
    while (j > i + 1) {

        int pivot = (i + j) / 2;
        if (list[pivot].bin.lon <= ref_to)
            i = pivot;
        else
            j = pivot;
    }

    if (list[j].bin.lon <= ref_to) {
        res.to = j + 1;
    } else if (list[i].bin.lon <= ref_to) {
        res.to = i + 1;
    } else {
        res.to = i;
    }

    return res;
}

static int filter_alt_baro(struct apiEntry *haystack, int haylen, struct apiEntry *matches, size_t *alloc, struct apiOptions *options) {
    int count = 0;
    float reverse_alt_factor = 1.0f / BINCRAFT_ALT_FACTOR;
    for (int i = 0; i < haylen; i++) {
        struct apiEntry *e = &haystack[i];
        int32_t alt = INT32_MIN;
        if (e->bin.baro_alt_valid) {
            alt = e->bin.baro_alt * reverse_alt_factor;
        } else if (e->bin.airground == AG_GROUND) {
            alt = 0;
        }
        if (alt >= options->above_alt_baro && alt <= options->below_alt_baro && alt != INT32_MIN) {
            matches[count++] = *e;
            *alloc += e->jsonOffset.len;
        }
    }
    return count;
}

static int filter_dbFlags(struct apiEntry *haystack, int haylen, struct apiEntry *matches, size_t *alloc, struct apiOptions *options) {
    int count = 0;
    for (int i = 0; i < haylen; i++) {
        struct apiEntry *e = &haystack[i];
        if (
                (options->filter_mil && (e->bin.dbFlags & 1))
                || (options->filter_interesting && (e->bin.dbFlags & 2))
                || (options->filter_pia && (e->bin.dbFlags & 4))
                || (options->filter_ladd && (e->bin.dbFlags & 8))
           ) {
            matches[count++] = *e;
            *alloc += e->jsonOffset.len;
        }
    }
    return count;
}

static int filterWithPos(struct apiEntry *haystack, int haylen, struct apiEntry *matches, size_t *alloc)  {
    int count = 0;

    for (int i = 0; i < haylen; i++) {
        struct apiEntry *e = &haystack[i];
        if (e->bin.position_valid) {
            matches[count++] = *e;
            *alloc += e->jsonOffset.len;
        }
    }
    return count;
}
static int filterSquawk(struct apiEntry *haystack, int haylen, struct apiEntry *matches, size_t *alloc, unsigned squawk)  {
    int count = 0;

    for (int i = 0; i < haylen; i++) {
        struct apiEntry *e = &haystack[i];
        //fprintf(stderr, "%04x %04x\n", options->squawk, e->bin.squawk);
        if (e->bin.squawk == squawk && e->bin.squawk_valid) {
            matches[count++] = *e;
            *alloc += e->jsonOffset.len;
        }
    }
    return count;
}

static int filterCallsignPrefix(struct apiEntry *haystack, int haylen, struct apiEntry *matches, size_t *alloc, char *callsign_prefix) {
    int count = 0;
    int prefix_len = strlen(callsign_prefix);
    for (int j = 0; j < haylen; j++) {
        struct apiEntry *e = &haystack[j];
        if (e->bin.callsign_valid && strncmp(e->bin.callsign, callsign_prefix, prefix_len) == 0) {
            matches[count++] = *e;
            *alloc += e->jsonOffset.len;
        }
    }
    return count;
}

static int filterCallsignExact(struct apiEntry *haystack, int haylen, struct apiEntry *matches, size_t *alloc, char *callsign) {
    // replace null padding with space padding
    for (int i = 0; i < 8; i++) {
        if (callsign[i] == '\0') {
            callsign[i] = ' ';
        }
    }
    int count = 0;
    for (int j = 0; j < haylen; j++) {
        struct apiEntry *e = &haystack[j];
        if (e->bin.callsign_valid && strncmp(e->bin.callsign, callsign, 8) == 0) {
            matches[count++] = *e;
            *alloc += e->jsonOffset.len;
        }
    }
    return count;
}

static int filterTypeList(struct apiEntry *haystack, int haylen, char *typeList, int typeCount, struct apiEntry *matches, size_t *alloc) {
    int count = 0;
    for (int k = 0; k < typeCount; k++) {
        char *typeCode = typeList + 4 * k;
        // upper case typeCode
        for (int i = 0; i < 4; i++) {
            typeCode[i] = toupper(typeCode[i]);
        }
    }
    for (int j = 0; j < haylen; j++) {
        struct apiEntry *e = &haystack[j];
        for (int k = 0; k < typeCount; k++) {
            char *typeCode = typeList + 4 * k;
            if (strncmp(e->bin.typeCode, typeCode, 4) == 0) {
                //fprintf(stderr, "typeCode: %.4s %.4s alloc increase by %d\n", e->bin.typeCode, typeCode, e->jsonOffset.len);
                matches[count++] = *e;
                *alloc += e->jsonOffset.len;
                // break inner loop
                break;
            }
        }
    }
    return count;
}

static int inLatRange(struct apiEntry *e, int32_t lat1, int32_t lat2, struct apiOptions *options) {
    return (e->bin.lat >= lat1 && e->bin.lat <= lat2 && (e->bin.position_valid || options->binCraft));
}

static int findInBox(struct apiEntry *haystack, int haylen, struct apiOptions *options, struct apiEntry *matches, size_t *alloc) {
    double *box = options->box;
    struct range r[2];
    memset(r, 0, sizeof(r));
    int count = 0;


    int32_t lat1 = (int32_t) (box[0] * 1E6);
    int32_t lat2 = (int32_t) (box[1] * 1E6);
    int32_t lon1 = (int32_t) (box[2] * 1E6);
    int32_t lon2 = (int32_t) (box[3] * 1E6);

    if (lon1 <= lon2) {
        r[0] = findLonRange(lon1, lon2, haystack, haylen);
    } else if (lon1 > lon2) {
        r[0] = findLonRange(lon1, 180E6, haystack, haylen);
        r[1] = findLonRange(-180E6, lon2, haystack, haylen);
        //fprintf(stderr, "%.1f to 180 and -180 to %1.f\n", lon1 / 1E6, lon2 / 1E6);
    }
    for (int k = 0; k < 2; k++) {
        for (int j = r[k].from; j < r[k].to; j++) {
            struct apiEntry *e = &haystack[j];
            if (inLatRange(e, lat1, lat2, options)) {
                matches[count++] = *e;
                *alloc += e->jsonOffset.len;
            }
        }
    }
    //fprintf(stderr, "box: lat %.1f to %.1f, lon %.1f to %.1f, count: %d\n", box[0], box[1], box[2], box[3], count);
    return count;
}
static int findRegList(struct apiEntry **hashList, char *regList, int regCount, struct apiEntry *matches, size_t *alloc) {
    int count = 0;
    for (int k = 0; k < regCount; k++) {
        char *reg = &regList[k * 12];
        // upper case reg
        for (int i = 0; i < 12; i++) {
            reg[i] = toupper(reg[i]);
        }
        //fprintf(stderr, "reg: %s\n", reg);
        uint32_t hash = regHash(reg);
        struct apiEntry *e = hashList[hash];
        while (e) {
            if (strncmp(e->bin.registration, reg, 12) == 0) {
                matches[count++] = *e;
                *alloc += e->jsonOffset.len;
                break;
            }
            e = e->nextReg;
        }
    }
    return count;
}
static int findCallsignList(struct apiEntry **hashList, char *callsignList, int callsignCount, struct apiEntry *matches, size_t *alloc) {
    int count = 0;
    for (int k = 0; k < callsignCount; k++) {
        char *callsign = &callsignList[k * 8];
        // replace null padding with space padding, upper case input
        for (int i = 0; i < 8; i++) {
            callsign[i] = toupper(callsign[i]);
            if (callsign[i] == '\0') {
                callsign[i] = ' ';
            }
        }
        uint32_t hash = callsignHash(callsign);
        //fprintf(stderr, "callsign: %8s hash: %u\n", callsign, hash);
        struct apiEntry *e = hashList[hash];
        while (e) {
            //fprintf(stderr, "callsign: %8s\n", e->bin.callsign);
            if (strncmp(e->bin.callsign, callsign, 8) == 0) {
                matches[count++] = *e;
                *alloc += e->jsonOffset.len;
                break;
            }
            e = e->nextCallsign;
        }
    }
    return count;
}
static int findHexList(struct apiEntry **hashList, uint32_t *hexList, int hexCount, struct apiEntry *matches, size_t *alloc) {
    int count = 0;
    for (int k = 0; k < hexCount; k++) {
        uint32_t addr = hexList[k];
        uint32_t hash = hexHash(addr);
        struct apiEntry *e = hashList[hash];
        while (e) {
            if (e->bin.hex == addr) {
                matches[count++] = *e;
                *alloc += e->jsonOffset.len;
                break;
            }
            e = e->nextHex;
        }
    }
    return count;
}
static int findInCircle(struct apiEntry *haystack, int haylen, struct apiOptions *options, struct apiEntry *matches, size_t *alloc) {
    struct apiCircle *circle = &options->circle;
    struct range r[2];
    memset(r, 0, sizeof(r));
    int count = 0;
    double lat = circle->lat;
    double lon = circle->lon;
    double radius = circle->radius; // in meters
    bool onlyClosest = circle->onlyClosest;

    double circum = 40075e3; // earth circumference is 40075km
    double fudge = 1.002; // make the box we check a little bigger

    double londiff = fudge * radius / (cos(lat * M_PI / 180.0) * circum + 1) * 360;
    double o1 = lon - londiff;
    double o2 = lon + londiff;
    o1 = o1 < -180 ? o1 + 360: o1;
    o2 = o2 > 180 ? o2 - 360 : o2;
    if (londiff >= 180) {
        // just check all lon
        o1 = -180;
        o2 = 180;
    }

    double latdiff = fudge * radius / (circum / 2) * 180.0;
    double a1 = lat - latdiff;
    double a2 = lat + latdiff;
    if (a1 < -90 || a2 > 90) {
        // going over a pole, just check all lon
        o1 = -180;
        o2 = 180;
    }
    int32_t lat1 = (int32_t) (a1 * 1E6);
    int32_t lat2 = (int32_t) (a2 * 1E6);

    int32_t lon1 = (int32_t) (o1 * 1E6);
    int32_t lon2 = (int32_t) (o2 * 1E6);

    //fprintf(stderr, "radius:%8.0f latdiff: %8.0f londiff: %8.0f\n", radius, greatcircle(a1, lon, lat, lon), greatcircle(lat, o1, lat, lon, 0));
    if (lon1 <= lon2) {
        r[0] = findLonRange(lon1, lon2, haystack, haylen);
    } else if (lon1 > lon2) {
        r[0] = findLonRange(lon1, 180E6, haystack, haylen);
        r[1] = findLonRange(-180E6, lon2, haystack, haylen);
        //fprintf(stderr, "%.1f to 180 and -180 to %1.f\n", lon1 / 1E6, lon2 / 1E6);
    }
    if (onlyClosest) {
        bool found = false;
        double minDistance = 300E6; // larger than any distances we encounter, also how far light travels in a second
        for (int k = 0; k < 2; k++) {
            for (int j = r[k].from; j < r[k].to; j++) {
                struct apiEntry *e = &haystack[j];
                if (inLatRange(e, lat1, lat2, options)) {
                    double dist = greatcircle(lat, lon, e->bin.lat / 1E6, e->bin.lon / 1E6, 0);
                    if (dist < radius && dist < minDistance) {
                        // first match is overwritten repeatedly
                        matches[count] = *e;
                        *alloc += e->jsonOffset.len;
                        matches[count].distance = (float) dist;
                        minDistance = dist;
                        found = true;
                    }
                }
            }
        }
        if (found)
            count = 1;
    }
    if (!onlyClosest) {
        for (int k = 0; k < 2; k++) {
            for (int j = r[k].from; j < r[k].to; j++) {
                struct apiEntry *e = &haystack[j];
                if (inLatRange(e, lat1, lat2, options)) {
                    double dist = greatcircle(lat, lon, e->bin.lat / 1E6, e->bin.lon / 1E6, 0);
                    if (dist < radius) {
                        matches[count] = *e;
                        matches[count].distance = (float) dist;
                        matches[count].direction = (float) bearing(lat, lon, e->bin.lat / 1E6, e->bin.lon / 1E6);
                        *alloc += e->jsonOffset.len;
                        count++;
                    }
                }
            }
        }
    }
    //fprintf(stderr, "circle count: %d\n", count);
    return count;
}


static struct apiEntry *apiAlloc(int count) {
    struct apiEntry *buf = cmalloc(count * sizeof(struct apiEntry));
    if (!buf) {
        fprintf(stderr, "FATAL: apiAlloc malloc fail\n");
        setExit(2);
    }
    return buf;
}

static struct char_buffer apiReq(struct apiThread *thread, struct apiOptions *options) {

    int flip = atomic_load(&Modes.apiFlip[thread->index]);

    struct apiBuffer *buffer = &Modes.apiBuffer[flip];
    struct apiEntry *haystack;
    int haylen;
    struct range pos_range;
    struct range all_range;
    if (options->filter_dbFlag) {
        haystack = buffer->list_flag;
        haylen = buffer->len_flag;

        pos_range = buffer->list_flag_pos_range;

        all_range.from = 0;
        all_range.to = haylen;
    } else {
        haystack = buffer->list;
        haylen = buffer->len;

        pos_range = buffer->list_pos_range;

        all_range.from = 0;
        all_range.to = haylen;
    }

    struct char_buffer cb = { 0 };
    struct apiEntry *matches = NULL;

    size_t alloc_base = API_REQ_PADSTART + 1024;
    size_t alloc = alloc_base;
    int count = 0;

    int doFree = 0;

    if (options->is_box) {
        int combined_len = haylen;
        if (options->is_hexList) {
            // this is a special case, in addition to the box, also return results for the hexList
            // we don't bother deduplicating, so this can return results more than once
            // thus allocate haylen and then also the number of hexes queried in addition
            combined_len += options->hexCount;
        }

        doFree = 1; matches = apiAlloc(combined_len); if (!matches) { return cb; };

        // first get matches for the box
        count = findInBox(haystack, haylen, options, matches, &alloc);

        if (options->is_hexList) {
            // optionally add matches for &find_hex
            count += findHexList(buffer->hexHash, options->hexList, options->hexCount, matches + count, &alloc);
        }
    } else if (options->is_circle) {
        doFree = 1; matches = apiAlloc(haylen); if (!matches) { return cb; };

        count = findInCircle(haystack, haylen, options, matches, &alloc);

        alloc += count * 30; // adding 27 characters per entry: ,"dst":1000.000, "dir":357
    } else if (options->is_hexList) {
        doFree = 1; matches = apiAlloc(options->hexCount); if (!matches) { return cb; };

        count = findHexList(buffer->hexHash, options->hexList, options->hexCount, matches, &alloc);
    } else if (options->is_regList) {
        doFree = 1; matches = apiAlloc(options->regCount); if (!matches) { return cb; };

        count = findRegList(buffer->regHash, options->regList, options->regCount, matches, &alloc);
    } else if (options->is_callsignList) {
        doFree = 1; matches = apiAlloc(options->callsignCount); if (!matches) { return cb; };

        count = findCallsignList(buffer->callsignHash, options->callsignList, options->callsignCount, matches, &alloc);
    } else if (options->is_typeList) {
        doFree = 1; matches = apiAlloc(haylen); if (!matches) { return cb; };

        count = filterTypeList(haystack, haylen, options->typeList, options->typeCount, matches, &alloc);
    } else if (options->all || options->all_with_pos) {
        struct range range;
        if (options->all) {
            range = all_range;
        } else if ( options->all_with_pos) {
            range = pos_range;
        } else {
            fprintf(stderr, "FATAL: unreachablei ahchoh8R\n");
            setExit(2);
            return cb;
        }
        count = range.to - range.from;
        if (count > 0) {
            struct apiEntry *first = &haystack[range.from];
            struct apiEntry *last = &haystack[range.to - 1];
            // assume continuous allocation from generation of api buffer
            alloc += last->jsonOffset.offset + last->jsonOffset.len - first->jsonOffset.offset;
            doFree = 0;
            matches = first;
        } else {
            doFree = 0;
            matches = NULL;
        }
    }

    if (options->filter_squawk) {
        struct apiEntry *filtered = apiAlloc(count); if (!filtered) { return cb; }

        size_t alloc = alloc_base;
        count = filterSquawk(matches, count, filtered, &alloc, options->squawk);

        if (doFree) { sfree(matches); }; doFree = 1; matches = filtered;
    }
    // filter all_with_pos as pos_range unreliable due do gpsOkBefore f***ery
    if (options->filter_with_pos || options->all_with_pos) {
        struct apiEntry *filtered = apiAlloc(count); if (!filtered) { return cb; }

        size_t alloc = alloc_base;
        count = filterWithPos(matches, count, filtered, &alloc);

        if (doFree) { sfree(matches); }; doFree = 1; matches = filtered;
    }
    if (options->filter_dbFlag) {
        struct apiEntry *filtered = apiAlloc(count); if (!filtered) { return cb; }

        size_t alloc = alloc_base;
        count = filter_dbFlags(matches, count, filtered, &alloc, options);

        if (doFree) { sfree(matches); }; doFree = 1; matches = filtered;
    }
    if (options->filter_alt_baro) {
        struct apiEntry *filtered = apiAlloc(count); if (!filtered) { return cb; }

        size_t alloc = alloc_base;
        count = filter_alt_baro(matches, count, filtered, &alloc, options);

        if (doFree) { sfree(matches); }; doFree = 1; matches = filtered;
    }
    if (options->filter_callsign_prefix) {
        struct apiEntry *filtered = apiAlloc(count); if (!filtered) { return cb; }

        size_t alloc = alloc_base;
        count = filterCallsignPrefix(matches, count, filtered, &alloc, options->callsign_prefix);

        if (doFree) { sfree(matches); }; doFree = 1; matches = filtered;
    }
    if (options->filter_callsign_exact) {
        struct apiEntry *filtered = apiAlloc(count); if (!filtered) { return cb; }

        size_t alloc = alloc_base;
        count = filterCallsignExact(matches, count, filtered, &alloc, options->callsign_exact);

        if (doFree) { sfree(matches); }; doFree = 1; matches = filtered;
    }
    if (options->filter_typeList) {
        struct apiEntry *filtered = apiAlloc(count); if (!filtered) { return cb; }

        size_t alloc = alloc_base;
        count = filterTypeList(matches, count, options->typeList, options->typeCount, filtered, &alloc);

        if (doFree) { sfree(matches); }; doFree = 1; matches = filtered;
    }

    // elementSize only applies to binCraft output
    uint32_t elementSize = sizeof(struct binCraft);
    if (options->binCraft) {
        alloc = API_REQ_PADSTART + 2 * elementSize + count * elementSize;
    }

    cb.buffer = cmalloc(alloc);
    if (!cb.buffer)
        return cb;

    char *payload = cb.buffer + API_REQ_PADSTART;
    char *p = payload;
    char *end = cb.buffer + alloc;


    if (options->binCraft) {
        memset(p, 0, elementSize);

#define memWrite(p, var) do { if (p + sizeof(var) > end) { break; }; memcpy(p, &var, sizeof(var)); p += sizeof(var); } while(0)

        int64_t now = buffer->timestamp;
        memWrite(p, now);

        memWrite(p, elementSize);

        uint32_t ac_count_pos = Modes.globalStatsCount.readsb_aircraft_with_position;
        memWrite(p, ac_count_pos);

        uint32_t index = 0;
        memWrite(p, index);

        int16_t south = -90;
        int16_t west = -180;
        int16_t north = 90;
        int16_t east = 180;
        if (options->is_box) {
            south = nearbyint(options->box[0]);
            north = nearbyint(options->box[1]);
            west = nearbyint(options->box[2]);
            east = nearbyint(options->box[3]);
        }

        memWrite(p, south);
        memWrite(p, west);
        memWrite(p, north);
        memWrite(p, east);

        uint32_t messageCount = Modes.stats_current.messages_total + Modes.stats_alltime.messages_total;
        memWrite(p, messageCount);

        uint32_t resultCount = count;
        memWrite(p, resultCount);

        int32_t dummy = 0;
        memWrite(p, dummy);

        memWrite(p, Modes.binCraftVersion);

#undef memWrite
        if (p - payload > (int) elementSize) {
            fprintf(stderr, "apiBin: too many details in first element\n");
        }

        p = payload + elementSize;

        for (int i = 0; i < count; i++) {
            if (unlikely(p + elementSize > end)) {
                fprintf(stderr, "search code deeK9OoR: count: %d need: %ld alloc: %ld\n", count, (long) ((count + 1) * elementSize), (long) alloc);
                break;
            }
            struct apiEntry *e = &matches[i];
            memcpy(p, &e->bin, elementSize);
            p += elementSize;
        }

    } else {
        if (options->jamesv2) {
            p = safe_snprintf(p, end, "{\"ac\":[");
        } else {
            p = safe_snprintf(p, end, "{\"now\": %.3f", buffer->timestamp / 1000.0);
            p = safe_snprintf(p, end, "\n,\"aircraft\":[");
        }

        char *json = buffer->json;

        for (int i = 0; i < count; i++) {
            struct apiEntry *e = &matches[i];
            struct offset off = e->jsonOffset; // READ-ONLY here
            if (unlikely(p + off.len + 100 >= end)) {
                fprintf(stderr, "search code ieva2aeV: count: %d need: %ld alloc: %ld\n", count, (long) ((p + off.len + 100) - payload), (long) alloc);
                break;
            }
            memcpy(p, json + off.offset, off.len);
            p += off.len;
            if (options->is_circle) {
                // json objects in cache are terminated by a comma: \n{ .... },
                p -= 2; // remove \} and , and make sure printf puts those back
                p = safe_snprintf(p, end, ",\"dst\":%.3f,\"dir\":%.1f},", e->distance / 1852.0, e->direction);
            }
        }

        // json objects in cache are terminated by a comma: \n{ .... },
        if (*(p - 1) == ',')
            p--; // remove trailing comma if necessary

        options->request_processed = microtime();
        p = safe_snprintf(p, end, "\n]");

        if (options->jamesv2) {
            p = safe_snprintf(p, end, "\n,\"msg\": \"No error\"");
            p = safe_snprintf(p, end, "\n,\"now\": %lld", (long long) buffer->timestamp);
            p = safe_snprintf(p, end, "\n,\"total\": %d", count);
            p = safe_snprintf(p, end, "\n,\"ctime\": %lld", (long long) buffer->timestamp);
            p = safe_snprintf(p, end, "\n,\"ptime\": %lld", (long long) nearbyint((options->request_processed - options->request_received) / 1000.0));
        } else {
            p = safe_snprintf(p, end, "\n,\"resultCount\": %d", count);
            p = safe_snprintf(p, end, "\n,\"ptime\": %.3f", (options->request_processed - options->request_received) / 1000.0);
        }
        p = safe_snprintf(p, end, "\n}\n");
    }

    cb.len = p - cb.buffer;
    size_t payload_len = p - payload;

    if (cb.len > alloc) {
        fprintf(stderr, "apiReq buffer insufficient\n");
    }


    if (doFree) {
        sfree(matches);
    }

    if (options->zstd) {
        struct char_buffer new = { 0 };
        size_t new_alloc = API_REQ_PADSTART + ZSTD_compressBound(alloc);
        new.buffer = cmalloc(new_alloc);
        memset(new.buffer, 0x0, new_alloc);

        struct char_buffer dst;
        dst.buffer = new.buffer + API_REQ_PADSTART;
        dst.len = new_alloc - API_REQ_PADSTART;

        //fprintf(stderr, "payload_len %ld\n", (long) payload_len);

        size_t compressedSize = ZSTD_compressCCtx(thread->cctx,
                dst.buffer, dst.len,
                payload, payload_len,
                API_ZSTD_LVL);

        dst.len = compressedSize;
        new.len = API_REQ_PADSTART + compressedSize;
        ident(dst);

        //free uncompressed buffer
        sfree(cb.buffer);

        cb = new;

        if (ZSTD_isError(compressedSize)) {
            fprintf(stderr, "API zstd error: %s\n", ZSTD_getErrorName(compressedSize));
            sfree(cb.buffer);
            cb.buffer = NULL;
            cb.len = 0;
            return cb;
        }
        //fprintf(stderr, "first 4 bytes: %08x len: %ld\n", *((uint32_t *) cb.buffer), (long) cb.len);
    }

    return cb;
}

static inline void apiAdd(struct apiBuffer *buffer, struct aircraft *a, int64_t now) {
    if (!(includeAircraftJson(now, a)))
        return;

    struct apiEntry *entry = &(buffer->list[buffer->len]);
    memset(entry, 0, sizeof(struct apiEntry));

    toBinCraft(a, &entry->bin, now);

    if (trackDataValid(&a->pos_reliable_valid)) {
        // position valid
        // else if (trackDataAge(now, &a->pos_reliable_valid) < 30 * MINUTES)
    } else if (a->nogpsCounter >= NOGPS_SHOW && now - a->seenAdsbReliable < NOGPS_DWELL) {
        // keep in box
    } else {
        // change lat / lon for sorting purposes
        entry->bin.lat = INT32_MAX;
        entry->bin.lon = INT32_MAX;
    }

    buffer->aircraftJsonCount++;

    entry->globe_index = a->globe_index;

    buffer->len++;
}

static inline void apiGenerateJson(struct apiBuffer *buffer, int64_t now) {
    sfree(buffer->json);
    buffer->json = NULL;

    size_t alloc = buffer->len * 1024 + 4096; // The initial buffer is resized as needed
    buffer->json = (char *) cmalloc(alloc);
    char *p = buffer->json;
    char *end = buffer->json + alloc;

    for (int i = 0; i < buffer->len; i++) {
        struct apiEntry *entry = &buffer->list[i];
        struct aircraft *a = aircraftGet(entry->bin.hex);

        if (!a) {
            fprintf(stderr, "FATAL: apiGenerateJson: aircraft missing, this shouldn't happen.");
            setExit(2);
            entry->jsonOffset.offset = 0;
            entry->jsonOffset.len = 0;
            continue;
        }

        // Make sure
        size_t seenByListSize = 0;
        if (Modes.aircraft_json_seen_by_list)
            // Better double the size to account for any new entries that are added between
            // size calculation and the call to sprintAircraftObject()
            seenByListSize = calculateSeenByListJsonSize(a, now) * 2;
        if (p + 2000 + seenByListSize >= end) {
            int used = p - buffer->json;
            alloc *= 2;
            if (used + 2000 + seenByListSize > alloc)
                alloc += seenByListSize;
            buffer->json = (char *) realloc(buffer->json, alloc);
            p = buffer->json + used;
            end = buffer->json + alloc;
        }

        uint32_t hash;

        hash = hexHash(entry->bin.hex);
        entry->nextHex = buffer->hexHash[hash];
        buffer->hexHash[hash] = entry;

        hash = regHash(entry->bin.registration);
        entry->nextReg = buffer->regHash[hash];
        buffer->regHash[hash] = entry;

        hash = callsignHash(entry->bin.callsign);
        entry->nextCallsign = buffer->callsignHash[hash];
        buffer->callsignHash[hash] = entry;
        //fprintf(stderr, "callsign: %8s hash: %u\n", entry->bin.callsign, hash);

        char *start = p;

        *p++ = '\n';
        p = sprintAircraftObject(p, end, a, now, 0, NULL, true);
        *p++ = ',';


        entry->jsonOffset.offset = start - buffer->json;
        entry->jsonOffset.len = p - start;
    }

    buffer->jsonLen = p - buffer->json;

    if (p >= end) {
        fprintf(stderr, "FATAL: buffer full apiAdd\n");
        setExit(2);
    }
}


static int apiUpdate() {
    struct craftArray *ca = &Modes.aircraftActive;

    // always clear and update the inactive apiBuffer
    int flip = (atomic_load(&Modes.apiFlip[0]) + 1) % 2;
    struct apiBuffer *buffer = &Modes.apiBuffer[flip];

    // reset buffer lengths
    buffer->len = 0;
    buffer->len_flag = 0;

    int acCount = ca->len;
    if (buffer->alloc < acCount) {
        if (acCount > 100000) {
            fprintf(stderr, "<3> this is strange, too many aircraft!\n");
        }
        buffer->alloc = acCount + 128;
        sfree(buffer->list);
        sfree(buffer->list_flag);
        buffer->list = cmalloc(buffer->alloc * sizeof(struct apiEntry));
        buffer->list_flag = cmalloc(buffer->alloc * sizeof(struct apiEntry));
        if (!buffer->list || !buffer->list_flag) {
            fprintf(stderr, "apiList alloc: out of memory!\n");
            exit(1);
        }
    }

    // reset hashList to NULL
    memset(buffer->hexHash, 0x0, API_BUCKETS * sizeof(struct apiEntry*));
    memset(buffer->regHash, 0x0, API_BUCKETS * sizeof(struct apiEntry*));
    memset(buffer->callsignHash, 0x0, API_BUCKETS * sizeof(struct apiEntry*));

    // reset api list, just in case we don't set the entries completely due to oversight
    memset(buffer->list, 0x0, buffer->alloc * sizeof(struct apiEntry));
    memset(buffer->list_flag, 0x0, buffer->alloc * sizeof(struct apiEntry));

    buffer->aircraftJsonCount = 0;

    int64_t now = mstime();
    ca_lock_read(ca);
    for (int i = 0; i < ca->len; i++) {
        struct aircraft *a = ca->list[i];

        if (a == NULL)
            continue;

        apiAdd(buffer, a, now);
    }
    ca_unlock_read(ca);

    // sort api lists
    qsort(buffer->list, buffer->len, sizeof(struct apiEntry), compareLon);

    apiGenerateJson(buffer, now);

    for (int i = 0; i < buffer->len; i++) {
        struct apiEntry entry = buffer->list[i];
        if (entry.bin.dbFlags) {
            // copy entry into flags list (only contains aircraft with at least one dbFlag set
            buffer->list_flag[buffer->len_flag++] = entry;
        }
    }
    // sort not needed as order is maintained copying from main list

    buffer->list_pos_range = findLonRange(-180 * 1E6, 180 * 1E6, buffer->list, buffer->len);
    buffer->list_flag_pos_range = findLonRange(-180 * 1E6, 180 * 1E6, buffer->list_flag, buffer->len_flag);

    buffer->timestamp = now;

    // doesn't matter which of the 2 buffers the api req will use they are both pretty current
    for (int i = 0; i < Modes.apiThreadCount; i++) {
        atomic_store(&Modes.apiFlip[i], flip);
    }

    pthread_cond_signal(&Threads.json.cond);
    pthread_cond_signal(&Threads.globeJson.cond);

    return buffer->len;
}

static int shutClose(int fd) {
    if (shutdown(fd, SHUT_RDWR) < 0) { // Secondly, terminate the reliable delivery
        if (errno != ENOTCONN && errno != EINVAL) { // SGI causes EINVAL
            fprintf(stderr, "API: Shutdown client socket failed.\n");
        }
    }
    return close(fd);
}

static void apiCloseCon(struct apiCon *con, struct apiThread *thread) {
    if (!con->open) {
        fprintf(stderr, "apiCloseCon double close!\n");
        return;
    }

    int fd = con->fd;
    if (con->events && epoll_ctl(thread->epfd, EPOLL_CTL_DEL, fd, NULL)) {
        fprintf(stderr, "apiCloseCon: EPOLL_CTL_DEL %d: %s\n", fd, strerror(errno));
    }
    con->events = 0;

    if (shutClose(fd) != 0) {
        perror("apiCloseCon: close:");
    }

    if (Modes.debug_api) {
        fprintf(stderr, "%d %d apiCloseCon()\n", thread->index, fd);
    }

    sfree(con->request.buffer);
    con->request.len = 0;
    con->request.alloc = 0;

    struct char_buffer *reply = &con->reply;

    thread->responseBytesBuffered -= reply->len;

    sfree(reply->buffer);
    reply->len = 0;
    reply->alloc = 0;

    con->open = 0;
    thread->conCount--;
    // put it back on the stack of free connection structs
    thread->stack[thread->stackCount++] = con;
    //fprintf(stderr, "%2d %5d\n", thread->index, thread->conCount);
}

static void apiResetCon(struct apiCon *con, struct apiThread *thread) {
    if (!con->open) {
        fprintf(stderr, "apiResetCon called on closed connection!\n");
        return;
    }
    if (!con->keepalive) {
        apiCloseCon(con, thread);
        return;
    }

    if (Modes.debug_api) {
        fprintf(stderr, "%d %d apiResetCon\n", thread->index, con->fd);
    }

    // not freeing request buffer, rather reusing it
    con->request.len = 0;

    con->bytesSent = 0;

    struct char_buffer *reply = &con->reply;

    thread->responseBytesBuffered -= reply->len;

    // free reply buffer
    sfree(reply->buffer);
    reply->len = 0;
    reply->alloc = 0;

    con->lastReset = mstime();
}

static void sendStatus(int fd, int keepalive, const char *http_status) {
    char buf[256];
    char *p = buf;
    char *end = buf + sizeof(buf);

    p = safe_snprintf(p, end,
    "HTTP/1.1 %s\r\n"
    "server: readsb/3.1442\r\n"
    "connection: %s\r\n"
    "cache-control: no-store\r\n"
    "content-length: 0\r\n\r\n",
    http_status,
    keepalive ? "keep-alive" : "close");

    int res = send(fd, buf, strlen(buf), 0);
    MODES_NOTUSED(res);
}

static void send200(int fd, int keepalive) {
    sendStatus(fd, keepalive, "200 OK");
}
static void send400(int fd, int keepalive) {
    sendStatus(fd, keepalive, "400 Bad Request");
}
static void send405(int fd, int keepalive) {
    sendStatus(fd, keepalive, "405 Method Not Allowed");
}
static void send505(int fd, int keepalive) {
    sendStatus(fd, keepalive, "505 HTTP Version Not Supported");
}
static void send503(int fd, int keepalive) {
    sendStatus(fd, keepalive, "503 Service Unavailable");
}
static void send500(int fd, int keepalive) {
    sendStatus(fd, keepalive, "500 Internal Server Error");
}


static int parseDoubles(char *start, char *end, double *results, int max) {
    int count = 0;
    char *sot;
    char *eot = start - 1;
    char *endptr = NULL;
    //fprintf(stderr, "%s\n", start);
    while ((sot = eot + 1) < end) {
        eot = memchr(sot, ',', end - sot);
        if (!eot) {
            eot = end; // last token memchr returns NULL and eot is set to end
        }
        *eot = '\0';

        results[count++] = strtod(sot, &endptr);
        if (eot != endptr) {
            return -1;
        }
        if (count > max) {
            return -1;
        }
    }
    return count;
}

// expects lower cased input
static struct char_buffer parseFetch(struct apiCon *con, struct char_buffer *request, struct apiThread *thread) {
    struct char_buffer invalid = { 0 };

    char *req = request->buffer;

    // GET URL HTTPVERSION
    char *query = memchr(req, '?', request->len);
    if (!query) {
        return invalid;
    }
    // skip URL to after ? which signifies start of query options
    query++;

    // find end of query
    char *eoq = memchr(query, ' ', request->len);
    if (!eoq) {
        return invalid;
    }

    // we only want the URL
    *eoq = '\0';

    struct apiOptions optionsBack = { 0 };
    struct apiOptions *options = &optionsBack;

    // set some option defaults:
    options->above_alt_baro = INT32_MIN;
    options->below_alt_baro = INT32_MAX;

    options->request_received = microtime();

    char *sot;
    char *eot = query - 1;
    while ((sot = eot + 1) < eoq) {
        eot = memchr(sot, '&', eoq - sot);
        if (!eot) {
            eot = eoq; // last token memchr returns NULL and eot is set to eoq
        }
        *eot = '\0';

        char *p = sot;
        char *option = strsep(&p, "=");
        char *value = p;
        if (value) {
            //fprintf(stderr, "%s=%s\n", option, value);
            // handle parameters WITH associated value
            if (byteMatch0(option, "box")) {
                options->is_box = 1;

                double *box = options->box;
                int count = parseDoubles(value, eot, box, 4);
                if (count < 4)
                    return invalid;

                for (int i = 0; i < 4; i++) {
                    if (box[i] > 180 || box[i] < -180)
                        return invalid;
                }
                if (box[0] > box[1])
                    return invalid;

            } else if (byteMatch0(option, "closest") || byteMatch0(option, "circle")) {
                options->is_circle = 1;
                if (byteMatch0(option, "closest")) {
                    options->closest = 1;
                }
                struct apiCircle *circle = &options->circle;
                double numbers[3];
                int count = parseDoubles(value, eot, numbers, 3);
                if (count < 3)
                    return invalid;

                circle->onlyClosest = options->closest;

                circle->lat = numbers[0];
                circle->lon = numbers[1];
                // user input in nmi, internally we use meters
                circle->radius = numbers[2] * 1852;

                //fprintf(stderr, "%.1f, %.1f, %.1f\n", circle->lat, circle->lon, circle->radius);

                if (circle->lat > 90 || circle->lat < -90)
                    return invalid;
                if (circle->lon > 180 || circle->lon < -180)
                    return invalid;

            } else if (byteMatch0(option, "find_hex") || byteMatch0(option, "hexlist")) {
                options->is_hexList = 1;

                int hexCount = 0;
                int maxCount = API_REQ_LIST_MAX;
                uint32_t *hexList = options->hexList;

                char *saveptr = NULL;
                char *endptr = NULL;
                char *tok = strtok_r(value, ",", &saveptr);
                while (tok && hexCount < maxCount) {
                    int other = 0;
                    if (tok[0] == '~') {
                        other = 1;
                        tok++; // skip over ~
                    }
                    uint32_t hex = (uint32_t) strtol(tok, &endptr, 16);
                    if (tok != endptr) {
                        hex |= (other ? MODES_NON_ICAO_ADDRESS : 0);
                        hexList[hexCount] = hex;
                        hexCount++;
                        //fprintf(stderr, "%06x\n", hex);
                    }
                    tok = strtok_r(NULL, ",", &saveptr);
                }

                options->hexCount = hexCount;
            } else if (byteMatch0(option, "find_callsign")) {
                options->is_callsignList = 1;

                int callsignCount = 0;
                int maxCount = API_REQ_LIST_MAX;
                char *callsignList = options->callsignList;

                char *saveptr = NULL;
                char *endptr = NULL;
                char *tok = strtok_r(value, ",", &saveptr);
                while (tok && callsignCount < maxCount) {
                    strncpy(callsignList + callsignCount * 8, tok, 8);
                    if (tok != endptr)
                        callsignCount++;
                    tok = strtok_r(NULL, ",", &saveptr);
                }
                if (callsignCount == 0)
                    return invalid;

                options->callsignCount = callsignCount;
            } else if (byteMatch0(option, "find_reg")) {
                options->is_regList = 1;

                int regCount = 0;
                int maxCount = API_REQ_LIST_MAX;
                char *regList = options->regList;

                char *saveptr = NULL;
                char *endptr = NULL;
                char *tok = strtok_r(value, ",", &saveptr);
                while (tok && regCount < maxCount) {
                    strncpy(regList + regCount * 12, tok, 12);
                    if (tok != endptr)
                        regCount++;
                    tok = strtok_r(NULL, ",", &saveptr);
                }
                if (regCount == 0)
                    return invalid;

                options->regCount = regCount;
            } else if (byteMatch0(option, "find_type") || byteMatch0(option, "filter_type")) {
                if (byteMatch0(option, "find_type")) {
                    options->is_typeList = 1;
                } else {
                    options->filter_typeList = 1;
                }

                int typeCount = 0;
                int maxCount = API_REQ_LIST_MAX;
                char *typeList = options->typeList;

                char *saveptr = NULL;
                char *endptr = NULL;
                char *tok = strtok_r(value, ",", &saveptr);
                while (tok && typeCount < maxCount) {
                    strncpy(typeList + typeCount * 4, tok, 4);
                    if (tok != endptr)
                        typeCount++;
                    tok = strtok_r(NULL, ",", &saveptr);
                }
                if (typeCount == 0)
                    return invalid;

                options->typeCount = typeCount;
            } else if (byteMatch0(option, "filter_callsign_exact")) {

                options->filter_callsign_exact = 1;

                memset(options->callsign_exact, 0x0, sizeof(options->callsign_exact));
                strncpy(options->callsign_exact, value, 8);

            } else if (byteMatch0(option, "filter_callsign_prefix")) {

                options->filter_callsign_prefix = 1;

                memset(options->callsign_prefix, 0x0, sizeof(options->callsign_prefix));
                strncpy(options->callsign_prefix, value, 8);

            } else if (byteMatch0(option, "above_alt_baro")) {
                options->filter_alt_baro = 1;
                options->above_alt_baro = strtol(value, NULL, 10);
            } else if (byteMatch0(option, "below_alt_baro")) {
                options->filter_alt_baro = 1;
                options->below_alt_baro = strtol(value, NULL, 10);
            } else if (byteMatch0(option, "filter_squawk")) {
                options->filter_squawk = 1;
                //int dec = strtol(value, NULL, 10);
                //options->squawk = (dec / 1000) * 16*16*16 + (dec / 100 % 10) * 16*16 + (dec / 10 % 10) * 16 + (dec % 10);
                int hex = strtol(value, NULL, 16);
                //fprintf(stderr, "%04d %04x\n", dec, hex);

                options->squawk = hex;
            } else {
                return invalid;
            }
        } else {
            // handle parameters WITHOUT associated value
            if (byteMatch0(option, "json")) {
                // this is the default
            } else if (byteMatch0(option, "jv2")) {
                options->jamesv2 = 1;
            } else if (byteMatch0(option, "zstd")) {
                options->zstd = 1;
            } else if (byteMatch0(option, "bincraft")) {
                options->binCraft = 1;
            } else if (byteMatch0(option, "all")) {
                options->all = 1;
            } else if (byteMatch0(option, "all_with_pos")) {
                options->all_with_pos = 1;
            } else if (byteMatch0(option, "filter_with_pos")) {
                options->filter_with_pos = 1;
            } else if (byteMatch0(option, "filter_mil")) {
                options->filter_dbFlag = 1;
                options->filter_mil = 1;
            } else if (byteMatch0(option, "filter_interesting")) {
                options->filter_dbFlag = 1;
                options->filter_interesting = 1;
            } else if (byteMatch0(option, "filter_pia")) {
                options->filter_dbFlag = 1;
                options->filter_pia = 1;
            } else if (byteMatch0(option, "filter_ladd")) {
                options->filter_dbFlag = 1;
                options->filter_ladd = 1;
            } else {
                return invalid;
            }
        }
    }
    int mainOptionCount = options->is_box
        + options->is_circle
        + options->is_hexList
        + options->is_callsignList
        + options->is_regList
        + options->is_typeList
        + options->all
        + options->all_with_pos;

    if (mainOptionCount != 1) {
        if (mainOptionCount == 2 && options->is_hexList && options->is_box) {
            // this is ok
        } else {
            return invalid;
        }
    }

    if (options->is_typeList && options->filter_typeList) {
        return invalid;
    }

    //fprintf(stderr, "parseFetch calling apiReq\n");


    if (options->zstd) {
         con->content_type = "application/zstd";
    } else if (options->binCraft) {
         con->content_type = "application/octet-stream";
    } else {
         con->content_type = "application/json";
    }

    return apiReq(thread, options);
}

static void apiSendData(struct apiCon *con, struct apiThread *thread) {
    struct char_buffer *reply = &con->reply;
    int toSend = reply->len - con->bytesSent;

    if (toSend <= 0) {
        if ((con->events & EPOLLOUT)) {
            con->events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLHUP;
            struct epoll_event epollEvent = { .events = con->events };
            epollEvent.data.ptr = con;

            if (epoll_ctl(thread->epfd, EPOLL_CTL_MOD, con->fd, &epollEvent)) {
                perror("apiResetCon() epoll_ctl fail:");
            }
        }
        if (toSend < 0) {
            fprintf(stderr, "wat?! toSend < 0\n");
        }
        return;
    }

    char *dataStart = reply->buffer + con->bytesSent;

    int nwritten = send(con->fd, dataStart, toSend, 0);

    if (nwritten > 0) {
        con->bytesSent += nwritten;
    }

    // all data has been sent, reset the connection
    if (nwritten == toSend) {
        apiResetCon(con, thread);
        return;
    }

    if (nwritten < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            // no progress, make sure EPOLLOUT is set.
        } else {
            // non recoverable error, close connection
            if (antiSpam(&thread->antiSpam[0], 5 * SECONDS)) {
                fprintf(stderr, "apiSendData fail: %s (was trying to send %d bytes)\n", strerror(errno), toSend);
            }
            apiCloseCon(con, thread);
            return;
        }
    }

    //fprintf(stderr, "wrote only %d of %d\n", nwritten, toSend);

    // couldn't write everything, set EPOLLOUT
    if (!(con->events & EPOLLOUT)) {
        con->events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLHUP | EPOLLOUT;
        struct epoll_event epollEvent = { .events = con->events };
        epollEvent.data.ptr = con;

        if (epoll_ctl(thread->epfd, EPOLL_CTL_MOD, con->fd, &epollEvent)) {
            perror("apiSendData() epoll_ctl fail:");
        }
    }

    return;
}

static void apiShutdown(struct apiCon *con, struct apiThread *thread, int line, int err) {
    if (con->bytesSent != con->reply.len) {
        if (antiSpam(&thread->antiSpam[1], 5 * SECONDS)) {
            fprintf(stderr, "Connection shutdown with incomplete or no reply sent."
                    " (reply.len: %d, bytesSent: %d, request.len: %d open: %d line: %d errno: %s)\n",
                    (int) con->reply.len,
                    (int) con->bytesSent,
                    (int) con->request.len,
                    con->open,
                    line,
                    err ? strerror(err) : "-");
        }
    }
    apiCloseCon(con, thread);
}

static void apiReadRequest(struct apiCon *con, struct apiThread *thread) {

    // delay processing requests until we have more memory
    if (thread->responseBytesBuffered > 512 * 1024 * 1024) {
        if (antiSpam(&thread->antiSpam[2], 5 * SECONDS)) {
            fprintf(stderr, "Delaying request processing due to per thread memory limit: 512 MB\n");
        }
        return;
    }

    int nread, toRead;
    int fd = con->fd;

    struct char_buffer *request = &con->request;

    int end_pad = 32;
    size_t requestMax = 1024 + 13 * API_REQ_LIST_MAX + end_pad;
    if (request->len > requestMax) {
        send400(con->fd, con->keepalive);
        apiResetCon(con, thread);
        return;
    }
    if (!request->alloc) {
        request->alloc = 2048;
        request->buffer = realloc(request->buffer, request->alloc);
    } else if (request->len + end_pad + 512 > request->alloc) {
        request->alloc = requestMax;
        request->buffer = realloc(request->buffer, request->alloc);
    }
    if (!request->buffer) {
        fprintf(stderr, "FATAL: apiReadRequest request->buffer malloc fail\n");
        setExit(2);
        send503(con->fd, con->keepalive);
        apiCloseCon(con, thread);
        return;
    }
    toRead = (request->alloc - end_pad) - request->len;
    nread = recv(fd, request->buffer + request->len, toRead, 0);

    if (Modes.debug_api) {
        fprintf(stderr, "%d %d nread %d\n", thread->index, con->fd, nread);
    }

    if (nread < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return;
        }
        apiShutdown(con, thread, __LINE__, errno);
        return;
    }

    if (nread == 0) {
        apiShutdown(con, thread, __LINE__, 0);
        return;
    }

    if (con->reply.buffer) {
        int toSend = con->reply.len - con->bytesSent;
        fprintf(stderr, "wat?! reply buffer but got new request data. toSend: %d\n", toSend);
        apiCloseCon(con, thread);
        return;
    }

    int oldlen = request->len;

    if (nread > 0) {
        request->len += nread;
        // terminate string
        request->buffer[request->len] = '\0';
    }


    int req_len = request->len;
    char *req_start = request->buffer;
    char *newline = memchr(req_start + oldlen, '\n', req_len);
    if (!newline || !strstr(req_start + imax(0, oldlen - 4), "\r\n\r\n")) {
        // request not complete
        return;
    }

    thread->requestCount++;

    char *eol = memchr(req_start, '\n', req_len) + 1; // we already know we have at least one newline
    char *req_end = req_start + req_len;
    char *protocol = eol - litLen("HTTP/1.x\r\n"); // points to H
    if (protocol < req_start) {
        send505(con->fd, con->keepalive);
        apiResetCon(con, thread);
        return;
    }

    // set end padding to zeros for byteMatch (memcmp) use without regrets
    memset(req_end, 0, end_pad);

    int isGET = byteMatch(req_start, "GET");
    char *minor_version = protocol + litLen("HTTP/1.");
    if (!byteMatch(protocol, "HTTP/1.") || !((*minor_version == '0') || (*minor_version == '1'))) {
        send505(con->fd, con->keepalive);
        apiResetCon(con, thread);
        return;
    }
    con->minor_version = (*minor_version == '1') ? 1 : 0;

    // parseFetch expects lower cased input
    // lower case entire request
    // HTTP / GET checks are done above as they are case sensitive
    _unroll_32
    for (int k = 0; k < req_len; k++) {
        req_start[k] = tolower(req_start[k]);
    }

    // header parsing
    char *hl = eol;
    con->keepalive = con->minor_version == 1 ? 1 : 0;
    while (hl < req_end && (eol = memchr(hl, '\n', req_end - hl))) {
        *eol = '\0';

        if (byteMatch(hl, "connection")) {
            if (strstr(hl, "close")) {
                con->keepalive = 0;
            } else if (con->keepalive || strstr(hl, "keep-alive")) {
                con->keepalive = 1;
            }
        }
        hl = eol + 1;
    }

    if (!isGET) {
        send405(con->fd, con->keepalive);
        apiResetCon(con, thread);
        return;
    }
    //fprintf(stderr, "%s\n", request->buffer);
    thread->request_len_sum += req_len;
    thread->request_count++;
    if (thread->request_count % (1000 * 1000) == 0) {
        int64_t avg = thread->request_len_sum / thread->request_count;
        thread->request_len_sum = 0;
        thread->request_count = 0;
        fprintf(stderr, "API average req_len: %d\n", (int) avg);
    }

    char *status = protocol - litLen("?status ");
    if (status > req_start && byteMatch(status, "?status ")) {
        if (Modes.exitSoon) {
            send503(con->fd, con->keepalive);
        } else {
            send200(con->fd, con->keepalive);
        }
        apiResetCon(con, thread);
        return;
    }

    con->content_type = "multipart/mixed";
    struct char_buffer reply = parseFetch(con, request, thread);
    if (reply.len == 0) {
        //fprintf(stderr, "parseFetch returned invalid\n");
        send400(con->fd, con->keepalive);
        apiResetCon(con, thread);
        return;
    }

    thread->responseBytesBuffered += reply.len;

    // at header before payload
    char header[API_REQ_PADSTART];
    char *p = header;
    char *end = header + API_REQ_PADSTART;

    int content_len = reply.len - API_REQ_PADSTART;

    p = safe_snprintf(p, end,
            "HTTP/1.1 200 OK\r\n"
            "server: readsb/3.1442\r\n"
            "content-type: %s\r\n"
            "connection: %s\r\n"
            "cache-control: no-store\r\n"
            "content-length: %d\r\n\r\n",
            con->content_type,
            con->keepalive ? "keep-alive" : "close",
            content_len);

    int hlen = p - header;
    //fprintf(stderr, "hlen %d\n", hlen);
    if (hlen >= API_REQ_PADSTART) {
        fprintf(stderr, "API error: API_REQ_PADSTART insufficient\n");
        send500(con->fd, con->keepalive);
        apiResetCon(con, thread);
        return;
    }

    // increase bytesSent counter so we don't transmit the empty buffer before the header
    con->bytesSent = API_REQ_PADSTART - hlen;
    // copy the header into the correct position immediately before the payload (which we already have)
    memcpy(reply.buffer + con->bytesSent, header, hlen);

    con->reply = reply;
    apiSendData(con, thread);
}
static void acceptCon(struct apiCon *con, struct apiThread *thread) {
    int listen_fd = con->fd;
    struct sockaddr_storage storage;
    struct sockaddr *saddr = (struct sockaddr *) &storage;
    socklen_t slen = sizeof(storage);


    // accept at most 16 connections per epoll_wait wakeup and thread
    for (int j = 0; j < 16; j++) {

        int fd = accept4(listen_fd, saddr, &slen, SOCK_NONBLOCK);
        if (fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                break;
            } else if (errno == EMFILE) {
                if (antiSpam(&thread->antiSpam[5], 5 * SECONDS)) {
                    fprintf(stderr, "<3>Out of file descriptors accepting api clients, "
                            "exiting to make sure we don't remain in a broken state!\n");
                }
                setExit(2);
                break;
            } else {
                if (antiSpam(&thread->antiSpam[6], 5 * SECONDS)) {
                    fprintf(stderr, "api acceptCon(): Error accepting new connection: errno: %d %s\n", errno, strerror(errno));
                }
                break;
            }
        }

        // when starving for connections, close old connections
        if (!thread->stackCount) {
            int64_t now = mstime();
            int64_t bounce_delay = 5 * SECONDS;
            if (now > thread->next_bounce) {
                thread->next_bounce = now + bounce_delay / 20;
                if (antiSpam(&thread->antiSpam[3], 5 * SECONDS)) {
                    fprintf(stderr, "starving for connections, closing all connections idle for 5 or more seconds\n");
                }
                for (int j = 0; j < Modes.api_fds_per_thread; j++) {
                    struct apiCon *con = &thread->cons[j];
                    if (now - con->lastReset > bounce_delay) {
                        apiCloseCon(con, thread);
                    }
                }
            }
        }


        // reject new connection if we still don't have a free connection
        if (!thread->stackCount) {
            if (antiSpam(&thread->antiSpam[4], 5 * SECONDS)) {
                fprintf(stderr, "too many concurrent connections, rejecting new connections, sendng 503s :/\n");
            }
            send503(fd, 0);
            if (shutClose(fd) != 0) {
                if (antiSpam(&thread->antiSpam[4], 5 * SECONDS)) {
                    perror("accept: shutClose failed when rejecting a new connection:");
                }
            }
            return;
        }

        // take a free connection from the stack
        struct apiCon *con = thread->stack[--thread->stackCount];
        memset(con, 0, sizeof(struct apiCon));

        thread->conCount++;
        con->open = 1;
        con->fd = fd;

        con->lastReset = mstime();

        if (Modes.debug_api) {
            fprintf(stderr, "%d %d acceptCon()\n", thread->index, fd);
        }

        int op = EPOLL_CTL_ADD;
        con->events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLHUP;
        struct epoll_event epollEvent = { .events = con->events };
        epollEvent.data.ptr = con;

        if (epoll_ctl(thread->epfd, op, fd, &epollEvent)) {
            perror("acceptCon() epoll_ctl fail:");
        }
    }
}

static void *apiThreadEntryPoint(void *arg) {
    struct apiThread *thread = (struct apiThread *) arg;
    srandom(get_seed());

    int core = imax(0, Modes.num_procs - Modes.apiThreadCount + thread->index);
    //fprintf(stderr, "%d\n", core);
    threadAffinity(core);


    thread->cons = cmalloc(Modes.api_fds_per_thread * sizeof(struct apiCon));
    memset(thread->cons, 0x0, Modes.api_fds_per_thread * sizeof(struct apiCon));

    thread->stack = cmalloc(Modes.api_fds_per_thread * sizeof(struct apiCon *));
    for (int k = 0; k < Modes.api_fds_per_thread; k++) {
        thread->stack[k] = &thread->cons[k];
        thread->stackCount++;
    }

    thread->cctx = ZSTD_createCCtx();

    thread->epfd = my_epoll_create(&Modes.exitNowEventfd);

    for (int i = 0; i < Modes.apiService.listener_count; ++i) {
        struct apiCon *con = Modes.apiListeners[i];
        struct epoll_event epollEvent = { .events = con->events };
        epollEvent.data.ptr = con;

        if (epoll_ctl(thread->epfd, EPOLL_CTL_ADD, con->fd, &epollEvent)) {
            perror("apiThreadEntryPoint() epoll_ctl fail:");
        }
    }

    int count = 0;
    struct epoll_event *events = NULL;
    int maxEvents = 0;

    struct timespec cpu_timer;
    start_cpu_timing(&cpu_timer);
    int64_t next_stats_sync = 0;
    while (!Modes.exit) {
        if (count == maxEvents) {
            epollAllocEvents(&events, &maxEvents);
        }
        count = epoll_wait(thread->epfd, events, maxEvents, 5 * SECONDS);

        int64_t now = mstime();
        if (now > next_stats_sync) {
            next_stats_sync = now + 1 * SECONDS;

            struct timespec used = { 0 };
            end_cpu_timing(&cpu_timer, &used);
            int micro = (int) (used.tv_sec * 1000LL * 1000LL + used.tv_nsec / 1000LL);
            atomic_fetch_add(&Modes.apiWorkerCpuMicro, micro);
            start_cpu_timing(&cpu_timer);
            //fprintf(stderr, "%2d %5d\n", thread->index, thread->conCount);

            unsigned int requestCount = thread->requestCount;
            atomic_fetch_add(&Modes.apiRequestCounter, requestCount);
            thread->requestCount = 0;
        }

        for (int i = 0; i < count; i++) {
            struct epoll_event event = events[i];
            if (event.data.ptr == &Modes.exitNowEventfd)
                continue;

            struct apiCon *con = event.data.ptr;
            if (con->accept && (event.events & EPOLLIN)) {
                acceptCon(con, thread);
                continue;
            }

            if (event.events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                if (con->open) {
                    apiReadRequest(con, thread);
                } else {
                    apiShutdown(con, thread, __LINE__, 0);
                }
                continue;
            }

            if (con->open) {
                if (event.events & EPOLLIN) {
                    apiReadRequest(con, thread);
                }
                if (event.events & EPOLLOUT) {
                    apiSendData(con, thread);
                }
            }

            if (con->wakeups++ > 512 * 1024) {
                if (antiSpam(&thread->antiSpam[7], 5 * SECONDS)) {
                    fprintf(stderr, "connection triggered too many events (bad webserver logic), send 500 :/ (EPOLLIN: %d, EPOLLOUT: %d) "
                            "(reply.len: %d, bytesSent: %d, request.len: %d open: %d)\n",
                            (event.events & EPOLLIN), (event.events & EPOLLOUT),
                            (int) con->reply.len,
                            (int) con->bytesSent,
                            (int) con->request.len,
                            con->open);
                }

                send500(con->fd, con->keepalive);
                apiCloseCon(con, thread);
                continue;
            }

        }
    }

    for (int j = 0; j < Modes.api_fds_per_thread; j++) {
        struct apiCon *con = &thread->cons[j];
        if (con->open) {
            apiCloseCon(con, thread);
        }
    }


    sfree(events);

    ZSTD_freeCCtx(thread->cctx);
    close(thread->epfd);

    sfree(thread->stack);
    sfree(thread->cons);

    return NULL;
}

static void *apiUpdateEntryPoint(void *arg) {
    MODES_NOTUSED(arg);
    srandom(get_seed());
    pthread_mutex_lock(&Threads.apiUpdate.mutex);

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct timespec cpu_timer;
    while (!Modes.exit) {

        //struct timespec watch;
        //startWatch(&watch);

        start_cpu_timing(&cpu_timer);

        apiUpdate();

        end_cpu_timing(&cpu_timer, &Modes.stats_current.api_update_cpu);

        //int64_t elapsed = stopWatch(&watch);
        //fprintf(stderr, "api req took: %.5f s, got %d aircraft!\n", elapsed / 1000.0, n);

        threadTimedWait(&Threads.apiUpdate, &ts, Modes.json_interval);
    }
    pthread_mutex_unlock(&Threads.apiUpdate.mutex);
    return NULL;
}

void apiBufferInit() {
    // 1 api thread per 2 cores as we assume nginx running on the same box, better chances not swamping the CPU under high API load scenarios
    if (Modes.apiThreadCount <= 0) {
        Modes.apiThreadCount = imax(1, Modes.num_procs - (Modes.num_procs > 6 ? 2 : 1));
    }

    size_t size = sizeof(struct apiThread) * Modes.apiThreadCount;
    Modes.apiThread = cmalloc(size);
    memset(Modes.apiThread, 0x0, size);

    size = sizeof(atomic_int) * Modes.apiThreadCount;
    Modes.apiFlip = cmalloc(size);
    memset(Modes.apiFlip, 0x0, size);

    for (int i = 0; i < 2; i++) {
        struct apiBuffer *buffer = &Modes.apiBuffer[i];
        buffer->hexHash = cmalloc(API_BUCKETS * sizeof(struct apiEntry*));
        buffer->regHash = cmalloc(API_BUCKETS * sizeof(struct apiEntry*));
        buffer->callsignHash = cmalloc(API_BUCKETS * sizeof(struct apiEntry*));
    }
    apiUpdate(); // run an initial apiUpdate
    threadCreate(&Threads.apiUpdate, NULL, apiUpdateEntryPoint, NULL);
}

void apiBufferCleanup() {

    threadSignalJoin(&Threads.apiUpdate);

    for (int i = 0; i < 2; i++) {
        sfree(Modes.apiBuffer[i].list);
        sfree(Modes.apiBuffer[i].list_flag);
        sfree(Modes.apiBuffer[i].json);
        sfree(Modes.apiBuffer[i].hexHash);
        sfree(Modes.apiBuffer[i].regHash);
        sfree(Modes.apiBuffer[i].callsignHash);
    }

    sfree(Modes.apiThread);
    sfree(Modes.apiFlip);
}

void apiInit() {
    Modes.apiService.descr = "API output";
    serviceListen(&Modes.apiService, Modes.net_bind_address, Modes.net_output_api_ports, -1);
    fprintf(stderr, "\n");
    if (Modes.apiService.listener_count <= 0) {
        Modes.api = 0;
        return;
    }
    Modes.apiListeners = cmalloc(sizeof(struct apiCon*) * Modes.apiService.listener_count);
    memset(Modes.apiListeners, 0, sizeof(struct apiCon*) * Modes.apiService.listener_count);
    for (int i = 0; i < Modes.apiService.listener_count; ++i) {
        struct apiCon *con = cmalloc(sizeof(struct apiCon));
        memset(con, 0, sizeof(struct apiCon));
        if (!con) fprintf(stderr, "EMEM, how much is the fish?\n"), exit(1);

        Modes.apiListeners[i] = con;
        con->fd = Modes.apiService.listener_fds[i];
        con->accept = 1;

#ifndef EPOLLEXCLUSIVE
#define EPOLLEXCLUSIVE (0)
#endif
        con->events = EPOLLIN | EPOLLEXCLUSIVE;
    }

    Modes.api_fds_per_thread = Modes.max_fds * 7 / 8 / Modes.apiThreadCount;
    //fprintf(stderr, "Modes.api_fds_per_thread: %d\n", Modes.api_fds_per_thread);
    for (int i = 0; i < Modes.apiThreadCount; i++) {
        Modes.apiThread[i].index = i;
        pthread_create(&Modes.apiThread[i].thread, NULL, apiThreadEntryPoint, &Modes.apiThread[i]);
    }
}
void apiCleanup() {
    for (int i = 0; i < Modes.apiThreadCount; i++) {
        pthread_join(Modes.apiThread[i].thread, NULL);
    }
    struct net_service *service = &Modes.apiService;

    for (int i = 0; i < service->listener_count; ++i) {
        sfree(Modes.apiListeners[i]);
    }
    sfree(Modes.apiListeners);

    sfree(service->listener_fds);

    if (service->unixSocket) {
        unlink(service->unixSocket);
        sfree(service->unixSocket);
    }
}

struct char_buffer apiGenerateAircraftJson(threadpool_buffer_t *pbuffer) {
    struct char_buffer cb = { 0 };

    int flip = atomic_load(&Modes.apiFlip[0]);

    struct apiBuffer *buffer = &Modes.apiBuffer[flip];

    ssize_t alloc = buffer->jsonLen + 2048;

    char *buf = check_grow_threadpool_buffer_t(pbuffer, alloc);
    char *p = buf;
    char *end = buf + alloc;

    if (!buf) {
        return cb;
    }

    p = safe_snprintf(p, end,
            "{ \"now\" : %.3f,\n"
            "  \"messages\" : %u,\n",
            buffer->timestamp / 1000.0,
            Modes.stats_current.messages_total + Modes.stats_alltime.messages_total);

    //fprintf(stderr, "%.3f\n", ((double) mstime() - (double) buffer->timestamp) / 1000.0);

    p = safe_snprintf(p, end, "  \"aircraft\" : [");

    memcpy(p, buffer->json, buffer->jsonLen);
    p += buffer->jsonLen;

    // json objects in cache are terminated by a comma: \n{ .... },
    if (*(p-1) == ',')
        p--;

    p = safe_snprintf(p, end, "\n  ]\n}\n");

    cb.len = p - buf;
    cb.buffer = buf;
    return cb;
}

struct char_buffer apiGenerateGlobeJson(int globe_index, threadpool_buffer_t *pbuffer) {
    assert (globe_index <= GLOBE_MAX_INDEX);

    struct char_buffer cb = { 0 };

    int flip = atomic_load(&Modes.apiFlip[0]);

    struct apiBuffer *buffer = &Modes.apiBuffer[flip];

    ssize_t alloc = 16 * 1024 + buffer->jsonLen;

    char *buf = check_grow_threadpool_buffer_t(pbuffer, alloc);
    char *p = buf;
    char *end = buf + alloc;

    p = safe_snprintf(p, end,
            "{ \"now\" : %.3f,\n"
            "  \"messages\" : %u,\n",
            buffer->timestamp / 1000.0,
            Modes.stats_current.messages_total + Modes.stats_alltime.messages_total);

    p = safe_snprintf(p, end,
            "  \"global_ac_count_withpos\" : %d,\n",
            Modes.globalStatsCount.readsb_aircraft_with_position
            );

    p = safe_snprintf(p, end, "  \"globeIndex\" : %d, ", globe_index);
    if (globe_index >= GLOBE_MIN_INDEX) {
        int grid = GLOBE_INDEX_GRID;
        int lat = ((globe_index - GLOBE_MIN_INDEX) / GLOBE_LAT_MULT) * grid - 90;
        int lon = ((globe_index - GLOBE_MIN_INDEX) % GLOBE_LAT_MULT) * grid - 180;
        p = safe_snprintf(p, end,
                "\"south\" : %d, "
                "\"west\" : %d, "
                "\"north\" : %d, "
                "\"east\" : %d,\n",
                lat,
                lon,
                lat + grid,
                lon + grid);
    } else {
        struct tile *tiles = Modes.json_globe_special_tiles;
        struct tile tile = tiles[globe_index];
        p = safe_snprintf(p, end,
                "\"south\" : %d, "
                "\"west\" : %d, "
                "\"north\" : %d, "
                "\"east\" : %d,\n",
                tile.south,
                tile.west,
                tile.north,
                tile.east);
    }

    p = safe_snprintf(p, end, "  \"aircraft\" : [");

    for (int j = 0; j < buffer->len; j++) {

        struct apiEntry *entry = &buffer->list[j];
        if (entry->globe_index != globe_index)
            continue;

        // check if we have enough space
        if (p + entry->jsonOffset.len >= end) {
            fprintf(stderr, "apiGenerateGlobeJson buffer overrun\n");
            break;
        }

        memcpy(p, buffer->json + entry->jsonOffset.offset, entry->jsonOffset.len);
        p += entry->jsonOffset.len;

    }

    // json objects in cache are terminated by a comma: \n{ .... },
    if (*(p - 1) == ',')
        p--;

    p = safe_snprintf(p, end, "\n  ]\n}\n");

    cb.len = p - buf;
    cb.buffer = buf;
    return cb;
}
