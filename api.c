#include "readsb.h"

#define API_HASH_BITS (16)
#define API_BUCKETS (1 << API_HASH_BITS)

static inline uint32_t apiHash(uint32_t addr) {
    return addrHash(addr, API_HASH_BITS);
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

static int findCall(struct apiEntry *haystack, int haylen, struct apiEntry *matches, size_t *alloc, char *callsign) {
    int count = 0;
    for (int j = 0; j < haylen; j++) {
        struct apiEntry *e = &haystack[j];
        if (e->bin.callsign_valid && strncasecmp(e->bin.callsign, callsign, 8) == 0) {
            matches[count++] = *e;
            *alloc += e->jsonOffset.len;
        }
    }
    return count;
}

static int findAll(struct apiEntry *haystack, int haylen, struct apiEntry *matches, size_t *alloc) {
    struct range r[2];
    memset(r, 0, sizeof(r));
    int count = 0;

    int32_t lon1 = INT32_MIN;
    int32_t lon2 = INT32_MAX;

    r[0] = findLonRange(lon1, lon2, haystack, haylen);

    for (int k = 0; k < 2; k++) {
        for (int j = r[k].from; j < r[k].to; j++) {
            struct apiEntry *e = &haystack[j];
            if (e->aircraftJson) {
                matches[count++] = *e;
                *alloc += e->jsonOffset.len;
            }
        }
    }
    //fprintf(stderr, "findAllPos count: %d\n", count);
    return count;
}

static int findAllPos(struct apiEntry *haystack, int haylen, struct apiEntry *matches, size_t *alloc) {
    struct range r[2];
    memset(r, 0, sizeof(r));
    int count = 0;

    int32_t lon1 = (int32_t) (-180 * 1E6);
    int32_t lon2 = (int32_t) (180 * 1E6);

    r[0] = findLonRange(lon1, lon2, haystack, haylen);

    for (int k = 0; k < 2; k++) {
        for (int j = r[k].from; j < r[k].to; j++) {
            struct apiEntry *e = &haystack[j];
            matches[count++] = *e;
            *alloc += e->jsonOffset.len;
        }
    }
    //fprintf(stderr, "findAllPos count: %d\n", count);
    return count;
}

static int findInBox(struct apiEntry *haystack, int haylen, double *box, struct apiEntry *matches, size_t *alloc) {
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
            if (e->bin.lat >= lat1 && e->bin.lat <= lat2) {
                matches[count++] = *e;
                *alloc += e->jsonOffset.len;
            }
        }
    }
    //fprintf(stderr, "box: lat %.1f to %.1f, lon %.1f to %.1f, count: %d\n", box[0], box[1], box[2], box[3], count);
    return count;
}
static int findHexList(struct apiEntry **hashList, uint32_t *hexList, int hexCount, struct apiEntry *matches, size_t *alloc) {
    int count = 0;
    for (int k = 0; k < hexCount; k++) {
        uint32_t addr = hexList[k];
        uint32_t hash = apiHash(addr);
        struct apiEntry *e = hashList[hash];
        while (e) {
            if (e->bin.hex == addr) {
                matches[count++] = *e;
                *alloc += e->jsonOffset.len;
                break;
            }
            e = e->next;
        }
    }
    return count;
}
static int findInCircle(struct apiEntry *haystack, int haylen, struct apiCircle *circle, struct apiEntry *matches, size_t *alloc) {
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
                if (e->bin.lat >= lat1 && e->bin.lat <= lat2) {
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
                if (e->bin.lat >= lat1 && e->bin.lat <= lat2) {
                    double dist = greatcircle(lat, lon, e->bin.lat / 1E6, e->bin.lon / 1E6, 0);
                    if (dist < radius) {
                        matches[count] = *e;
                        matches[count].distance = (float) dist;
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

static struct char_buffer apiReq(struct apiThread *thread, struct apiOptions options) {
    pthread_mutex_lock(&thread->mutex);
    int flip = Modes.apiFlip;
    pthread_mutex_unlock(&thread->mutex);
    struct apiBuffer *buffer = &Modes.apiBuffer[flip];
    struct apiEntry *haystack;
    int haylen;
    if (options.filter_dbFlag) {
        haystack = buffer->list_flag;
        haylen = buffer->len_flag;
    } else {
        haystack = buffer->list;
        haylen = buffer->len;
    }

    struct char_buffer cb = { 0 };
    struct apiEntry *matches = aligned_malloc(haylen * sizeof(struct apiEntry));

    size_t alloc_base = API_REQ_PADSTART + 1024;
    size_t alloc = alloc_base;
    int count = 0;

    if (options.is_box) {
        count = findInBox(haystack, haylen, options.box, matches, &alloc);
    } else if (options.is_hexList) {
        count = findHexList(buffer->hashList, options.hexList, options.hexCount, matches, &alloc);
    } else if (options.is_circle) {
        count = findInCircle(haystack, haylen, &options.circle, matches, &alloc);
        alloc += count * 20; // adding 15 characters per entry: ,"dst":1000.000
    } else if (options.find_callsign) {
        count = findCall(haystack, haylen, matches, &alloc, options.callsign);
    } else if (options.all) {
        count = findAll(haystack, haylen, matches, &alloc);
    } else if (options.all_with_pos) {
        count = findAllPos(haystack, haylen, matches, &alloc);
    }

    if (options.filter_squawk) {
        struct apiEntry *filtered = aligned_malloc(haylen * sizeof(struct apiEntry));
        if (!filtered) {
            fprintf(stderr, "FATAL: apiReq malloc fail\n");
            setExit(2);
            return cb;
        }
        int filtered_count = 0;
        size_t alloc = alloc_base;

        for (int i = 0; i < count; i++) {
            struct apiEntry *e = &matches[i];
            //fprintf(stderr, "%04x %04x\n", options.squawk, e->bin.squawk);
            if (e->bin.squawk == options.squawk && e->bin.squawk_valid) {
                filtered[filtered_count++] = *e;
                alloc += e->jsonOffset.len;
            }
        }

        sfree(matches);
        matches = filtered;
        count = filtered_count;
    }
    if (options.filter_dbFlag) {
        struct apiEntry *filtered = aligned_malloc(haylen * sizeof(struct apiEntry));
        if (!filtered) {
            fprintf(stderr, "FATAL: apiReq malloc fail\n");
            setExit(2);
            return cb;
        }
        int filtered_count = 0;
        size_t alloc = alloc_base;

        for (int i = 0; i < count; i++) {
            struct apiEntry *e = &matches[i];
            //fprintf(stderr, "%04x %04x %01x\n", options.squawk, e->bin.squawk, e->bin.dbFlags);
            if (
                    (options.filter_mil && (e->bin.dbFlags & 1))
                    || (options.filter_interesting && (e->bin.dbFlags & 2))
                    || (options.filter_pia && (e->bin.dbFlags & 4))
                    || (options.filter_ladd && (e->bin.dbFlags & 8))
               ) {
                filtered[filtered_count++] = *e;
                alloc += e->jsonOffset.len;
            }
        }

        sfree(matches);
        matches = filtered;
        count = filtered_count;
    }

    // add for comma and new line for each entry
    alloc += count * 2;

    cb.buffer = aligned_malloc(alloc);
    if (!cb.buffer)
        return cb;

    char *p = cb.buffer + API_REQ_PADSTART;
    char *end = cb.buffer + alloc;

    if (options.jamesv2) {
        p = safe_snprintf(p, end, "{\"ac\":[");
    } else {
        p = safe_snprintf(p, end, "{\"now\": %.1f,\n", buffer->timestamp / 1000.0);
        p = safe_snprintf(p, end, "\"resultCount\": %d,\n", count);
        p = safe_snprintf(p, end, "\"aircraft\":[");
    }

    char *json = buffer->json;

    for (int i = 0; i < count; i++) {
        *p++ = '\n';
        struct apiEntry *e = &matches[i];
        struct offset off = e->jsonOffset; // READ-ONLY here
        if (p + off.len + 100 >= end) {
            fprintf(stderr, "search code ieva2aeV: need: %d alloc: %d\n", (int) ((p + off.len + 100) - cb.buffer), (int) alloc);
            break;
        }
        memcpy(p, json + off.offset, off.len);
        p += off.len;
        if (options.is_circle) {
            p--;
            p = safe_snprintf(p, end, ",\"dst\":%.3f}", e->distance / 1852.0);
        }
        *p++ = ',';
    }

    sfree(matches);

    if (*(p - 1) == ',')
        p--; // remove trailing comma if necessary

    options.request_processed = mstime();
    p = safe_snprintf(p, end, "\n]");

    if (options.jamesv2) {
        p = safe_snprintf(p, end, "\n,\"msg\": \"No error\"");
        p = safe_snprintf(p, end, "\n,\"now\": %lld", (long long) buffer->timestamp);
        p = safe_snprintf(p, end, "\n,\"total\": %d", count);
        p = safe_snprintf(p, end, "\n,\"ctime\": %lld", (long long) buffer->timestamp);
        p = safe_snprintf(p, end, "\n,\"ptime\": %lld", (long long) (options.request_processed - options.request_received));
    }
    p = safe_snprintf(p, end, "\n}\n");

    cb.len = p - cb.buffer;

    if (cb.len >= alloc)
        fprintf(stderr, "apiReq buffer insufficient\n");

    return cb;
}

static inline void apiAdd(struct apiBuffer *buffer, struct aircraft *a, int64_t now) {
    if (!(now < a->seen + 5 * MINUTES || includeAircraftJson(now, a)))
        return;

    struct apiEntry *entry = &(buffer->list[buffer->len]);
    memset(entry, 0, sizeof(struct apiEntry));

    toBinCraft(a, &entry->bin, now);

    if (!trackDataValid(&a->position_valid)) {
        entry->bin.lat = INT32_MAX;
        entry->bin.lon = INT32_MAX;
    }

    if (includeAircraftJson(now, a)) {
        buffer->aircraftJsonCount++;
        entry->aircraftJson = 1;
    }

    if (includeGlobeJson(now, a)) {
        entry->globe_index = a->globe_index;
    } else {
        entry->globe_index = -2;
    }

    buffer->len++;
}

static inline void apiGenerateJson(struct apiBuffer *buffer, int64_t now) {
    sfree(buffer->json);
    buffer->json = NULL;

    size_t alloc = buffer->len * 1024 + 4096; // The initial buffer is resized as needed
    buffer->json = (char *) aligned_malloc(alloc);
    char *p = buffer->json;
    char *end = buffer->json + alloc;

    for (int i = 0; i < buffer->len; i++) {
        if ((p + 2000) >= end) {
            int used = p - buffer->json;
            alloc *= 2;
            buffer->json = (char *) realloc(buffer->json, alloc);
            p = buffer->json + used;
            end = buffer->json + alloc;
        }

        struct apiEntry *entry = &buffer->list[i];
        struct aircraft *a = aircraftGet(entry->bin.hex);
        if (!a) {
            fprintf(stderr, "apiGenerateJson: aircraft missing, this shouldn't happen.");
            entry->jsonOffset.offset = 0;
            entry->jsonOffset.len = 0;
            continue;
        }

        uint32_t hash = apiHash(entry->bin.hex);
        entry->next = buffer->hashList[hash];
        buffer->hashList[hash] = entry;

        char *start = p;
        p = sprintAircraftObject(p, end, a, now, 0, NULL);

        entry->jsonOffset.offset = start - buffer->json;
        entry->jsonOffset.len = p - start;
    }

    if (p >= end) {
        fprintf(stderr, "buffer full apiAdd\n");
    }
}


int apiUpdate(struct craftArray *ca) {

    // always clear and update the inactive apiBuffer
    int flip = (Modes.apiFlip + 1) % 2;
    struct apiBuffer *buffer = &Modes.apiBuffer[flip];

    // reset buffer lengths
    buffer->len = 0;
    buffer->len_flag = 0;

    int acCount = ca->len;
    if (buffer->alloc < acCount) {
        if (acCount > 50000) {
            fprintf(stderr, "api bailing, too many aircraft!\n");
            return buffer->len;
        }
        buffer->alloc = acCount + 128;
        sfree(buffer->list);
        sfree(buffer->list_flag);
        buffer->list = aligned_malloc(buffer->alloc * sizeof(struct apiEntry));
        buffer->list_flag = aligned_malloc(buffer->alloc * sizeof(struct apiEntry));
        if (!buffer->list || !buffer->list_flag) {
            fprintf(stderr, "apiList alloc: out of memory!\n");
            exit(1);
        }
    }

    // reset hashList to NULL
    memset(buffer->hashList, 0x0, API_BUCKETS * sizeof(struct apiEntry*));

    // reset api list, just in case we don't set the entries completely due to oversight
    memset(buffer->list, 0x0, buffer->alloc * sizeof(struct apiEntry));
    memset(buffer->list_flag, 0x0, buffer->alloc * sizeof(struct apiEntry));

    buffer->aircraftJsonCount = 0;

    int64_t now = mstime();
    for (int i = 0; i < ca->len; i++) {
        struct aircraft *a = ca->list[i];

        if (a == NULL)
            continue;

        apiAdd(buffer, a, now);
    }

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

    buffer->timestamp = now;

    // doesn't matter which of the 2 buffers the api req will use they are both pretty current
    apiLockMutex();
    pthread_mutex_lock(&Modes.apiFlipMutex);

    Modes.apiFlip = flip;

    pthread_mutex_unlock(&Modes.apiFlipMutex);
    apiUnlockMutex();

    pthread_cond_signal(&Threads.json.cond);
    pthread_cond_signal(&Threads.globeJson.cond);

    return buffer->len;
}

// lock for flipping apiFlip
void apiLockMutex() {
    for (int i = 0; i < API_THREADS; i++) {
        pthread_mutex_lock(&Modes.apiThread[i].mutex);
    }
}
void apiUnlockMutex() {
    for (int i = 0; i < API_THREADS; i++) {
        pthread_mutex_unlock(&Modes.apiThread[i].mutex);
    }
}

static void shutClose(int fd) {
    if (shutdown(fd, SHUT_RDWR) < 0) { // Secondly, terminate the reliable delivery
        if (errno != ENOTCONN && errno != EINVAL) { // SGI causes EINVAL
            fprintf(stderr, "API: Shutdown client socket failed.\n");
        }
    }
    close(fd);
}

static void apiCloseConn(struct apiCon *con, struct apiThread *thread) {
    if (!con->open)
        return;

    int fd = con->fd;
    epoll_ctl(thread->epfd, EPOLL_CTL_DEL, fd, NULL);

    shutClose(fd);

    if (Modes.debug_api)
        fprintf(stderr, "%d: clo c: %d\n", thread->index, fd);

    sfree(con->request.buffer);

    struct char_buffer *cb = &con->cb;

    sfree(cb->buffer);
    cb->len = 0;
    cb->buffer = NULL;

    con->open = 0;
    thread->openFDs--;
    //fprintf(stderr, "%2d %5d\n", thread->index, thread->openFDs);
}

static void send500(int fd) {
    char buf[256];
    char *p = buf;
    char *end = buf + sizeof(buf);

    p = safe_snprintf(p, end,
    "HTTP/1.1 500 Internal Server Error\r\n"
    "Server: readsb/3.1442\r\n"
    "Connection: close\r\n"
    "Content-Length: 0\r\n\r\n");

    int res = send(fd, buf, strlen(buf), 0);
    MODES_NOTUSED(res);
}

static void send400(int fd) {
    char buf[256];
    char *p = buf;
    char *end = buf + sizeof(buf);

    p = safe_snprintf(p, end,
    "HTTP/1.1 400 Bad Request\r\n"
    "Server: readsb/3.1442\r\n"
    "Connection: close\r\n"
    "Content-Length: 0\r\n\r\n");

    int res = send(fd, buf, strlen(buf), 0);
    MODES_NOTUSED(res);
}

static int parseDoubles(char *p, double *results, int max) {
    char *saveptr = NULL;
    char *endptr = NULL;
    int count = 0;
    char *tok = strtok_r(p, ",", &saveptr);
    while (tok && count < max) {
        results[count] = strtod(tok, &endptr);
        if (tok != endptr)
            count++;
        tok = strtok_r(NULL, ",", &saveptr);
    }
    return count;
}

static struct char_buffer parseFetch(struct char_buffer *request, struct apiThread *thread) {
    struct char_buffer invalid = { 0 };
    char *p;
    char *saveptr;

    char *req = request->buffer;

    // GET URL HTTPVERSION
    // skip URL to after ? which signifies start of query options
    char *query = memchr(req, '?', request->len) + 1;
    if (!query)
        return invalid;

    // find end of query
    char *eoq = memchr(query, ' ', request->len);
    if (!eoq)
        return invalid;

    // we only want the URL
    *eoq = '\0';

    struct apiOptions options = { 0 };

    options.request_received = mstime();

    while ((p = strsep(&query, "&"))) {
        //fprintf(stderr, "%s\n", p);
        char *option = strsep(&p, "=");
        char *value = strsep(&p, "=");
        if (value) {
            // handle parameters WITH associated value
            if (strcasecmp(option, "box") == 0) {
                options.is_box = 1;

                double *box = options.box;
                int count = parseDoubles(value, box, 4);
                if (count < 4)
                    return invalid;

                for (int i = 0; i < 4; i++) {
                    if (box[i] > 180 || box[i] < -180)
                        return invalid;
                }
                if (box[0] > box[1])
                    return invalid;

            } else if (strcasecmp(option, "closest") == 0 || strcasecmp(option, "circle") == 0) {
                options.is_circle = 1;
                if (strcasecmp(option, "closest") == 0) {
                    options.closest = 1;
                }
                struct apiCircle *circle = &options.circle;
                double numbers[3];
                int count = parseDoubles(value, numbers, 3);
                if (count < 3)
                    return invalid;

                circle->lat = numbers[0];
                circle->lon = numbers[1];
                // user input in nmi, internally we use meters
                circle->radius = numbers[2] * 1852;

                if (circle->lat > 90 || circle->lat < -90)
                    return invalid;
                if (circle->lon > 180 || circle->lon < -180)
                    return invalid;

                circle->onlyClosest = options.closest;
            } else if (strcasecmp(option, "hexList") == 0) {
                options.is_hexList = 1;

                int hexCount = 0;
                int maxLen = API_HEXLIST_MAX;
                uint32_t *hexList = options.hexList;

                saveptr = NULL;
                char *endptr = NULL;
                char *tok = strtok_r(value, ",", &saveptr);
                while (tok && hexCount < maxLen) {
                    hexList[hexCount] = (uint32_t) strtol(tok, &endptr, 16);
                    if (tok != endptr)
                        hexCount++;
                    tok = strtok_r(NULL, ",", &saveptr);
                }
                if (hexCount == 0)
                    return invalid;

                options.hexCount = hexCount;
            } else if (strcasecmp(option, "find_callsign") == 0) {
                options.find_callsign = 1;
                strncpy(options.callsign, value, 8);
                // strncpy pads with null bytes, replace with space padding
                for (int i = 0; i < 8; i++) {
                    if (options.callsign[i] == '\0')
                        options.callsign[i] = ' ';
                }
            } else if (strcasecmp(option, "filter_squawk") == 0) {
                options.filter_squawk = 1;
                options.filter = 1;
                //int dec = strtol(value, NULL, 10);
                //options.squawk = (dec / 1000) * 16*16*16 + (dec / 100 % 10) * 16*16 + (dec / 10 % 10) * 16 + (dec % 10);
                int hex = strtol(value, NULL, 16);
                //fprintf(stderr, "%04d %04x\n", dec, hex);

                options.squawk = hex;
            } else {
                return invalid;
            }
        } else {
            // handle parameters WITHOUT associated value
            if (strcasecmp(option, "jv2") == 0) {
                options.jamesv2 = 1;
            } else if (strcasecmp(option, "all") == 0) {
                options.all = 1;
            } else if (strcasecmp(option, "all_with_pos") == 0) {
                options.all_with_pos = 1;
            } else if (strcasecmp(option, "filter_mil") == 0) {
                options.filter_dbFlag = 1;
                options.filter_mil = 1;
            } else if (strcasecmp(option, "filter_interesting") == 0) {
                options.filter_dbFlag = 1;
                options.filter_interesting = 1;
            } else if (strcasecmp(option, "filter_pia") == 0) {
                options.filter_dbFlag = 1;
                options.filter_pia = 1;
            } else if (strcasecmp(option, "filter_ladd") == 0) {
                options.filter_dbFlag = 1;
                options.filter_ladd = 1;
            } else {
                return invalid;
            }
        }
    }
    if ((
                options.is_box
                + options.is_circle
                + options.is_hexList
                + options.all
                + options.all_with_pos
                + options.closest
                + options.find_callsign
        ) != 1) {
        return invalid;
    }


    return apiReq(thread, options);
}

static void apiSendData(struct apiCon *con, struct apiThread *thread) {
    struct char_buffer *cb = &con->cb;
    int len = cb->len - con->cbOffset;
    char *dataStart = cb->buffer + con->cbOffset;

    int nwritten = send(con->fd, dataStart, len, 0);
    int err = errno;

    if ((nwritten >= 0 && nwritten < len) || (nwritten < 0 && (err == EAGAIN || err == EWOULDBLOCK))) {
        //fprintf(stderr, "wrote only %d of %d\n", nwritten, len);

        con->cbOffset += nwritten;

        if (!(con->events & EPOLLOUT)) {
            // notify if fd is available for writing
            con->events ^= EPOLLOUT; // toggle xor
            struct epoll_event epollEvent = { .events = con->events };
            epollEvent.data.ptr = con;

            if (epoll_ctl(thread->epfd, EPOLL_CTL_MOD, con->fd, &epollEvent))
                perror("epoll_ctl MOD fail:");
        }

        return;
        // free stuff some other time
    }

    sfree(cb->buffer);
    cb->len = 0;
    cb->buffer = NULL;

    if (nwritten < 0) {
        fprintf(stderr, "apiSendData fail: %s (was trying to send %d bytes)\n", strerror(err), len);
        apiCloseConn(con, thread);
        return;
    }

    apiCloseConn(con, thread);
    return;

    // doing one request per connection for the moment, keep-alive maybe later
    if (con->events & EPOLLOUT) {
        // no more writing necessary for the moment, no longer get notified for EPOLLOUT
        con->events ^= EPOLLOUT; // toggle xor
        struct epoll_event epollEvent = { .events = con->events };
        epollEvent.data.ptr = con;

        if (epoll_ctl(thread->epfd, EPOLL_CTL_MOD, con->fd, &epollEvent))
            perror("epoll_ctl MOD fail:");
    }

    return;
}

static void apiReadRequest(struct apiCon *con, struct apiThread *thread) {
    int nread, err, toRead;
    int fd = con->fd;

    struct char_buffer *request = &con->request;
    do {
        if (request->len > 1024 + 7 * API_HEXLIST_MAX) {
            send400(fd);
            apiCloseConn(con, thread);
            return;
        }
        if (request->len + 2048 > request->alloc) {
            request->alloc += 16 * 1024;
            request->buffer = realloc(request->buffer, request->alloc);
        }
        toRead = request->alloc - request->len - 1; // leave an extra byte we can set \0
        nread = recv(fd, request->buffer + request->len, toRead, 0);
        err = errno;

        if (nread > 0) {
            request->len += nread;
            // terminate string
            request->buffer[request->len] = '\0';
        }
    } while (nread == toRead);

    if (nread == 0 || (nread < 0 && (err != EAGAIN && err != EWOULDBLOCK))) {
        apiCloseConn(con, thread);
        return;
    }

    // we already have a response that hasn't been sent yet, discard request.
    if (con->cb.buffer)
        return;

    if (!memchr(request->buffer, '\n', request->len)) {
        // no newline, wait for more data
        return;
    }

    //fprintf(stderr, "%s\n", req);

    struct char_buffer cb = parseFetch(request, thread);
    if (cb.len == 0) {
        send400(fd);
        apiCloseConn(con, thread);
        return;
    }

    // at header before payload
    char header[API_REQ_PADSTART];
    char *p = header;
    char *end = header + API_REQ_PADSTART;

    int plen = cb.len - API_REQ_PADSTART;

    p = safe_snprintf(p, end,
            "HTTP/1.1 200 OK\r\n"
            "Server: readsb/3.1442\r\n"
            "Connection: close\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %d\r\n\r\n",
            plen);

    int hlen = p - header;
    if (hlen == API_REQ_PADSTART)
        fprintf(stderr, "API_REQ_PADSTART insufficient\n");

    con->cbOffset = API_REQ_PADSTART - hlen;
    memcpy(cb.buffer + con->cbOffset, header, hlen);

    con->cb = cb;
    apiSendData(con, thread);
}
static void acceptConn(struct apiCon *con, struct apiThread *thread) {
    int listen_fd = con->fd;
    struct sockaddr_storage storage;
    struct sockaddr *saddr = (struct sockaddr *) &storage;
    socklen_t slen = sizeof(storage);

    char aneterr[ANET_ERR_LEN];
    int this_cycle = 0;
    while (this_cycle++ < Modes.api_fds_per_thread / 4) {
        errno = 0;
        int fd = anetGenericAccept(aneterr, listen_fd, saddr, &slen, SOCK_NONBLOCK);
        if (fd < 0) {
            break;
        }
        thread->openFDs++;

        struct apiCon *con;
        int tryCounter = 0;
        do {
            con = &thread->cons[thread->nextCon];
            thread->nextCon = (thread->nextCon + 1) % Modes.api_fds_per_thread;
        } while (con->open && tryCounter++ < Modes.api_fds_per_thread / 8);

        if (con->open) {
            fprintf(stderr, "too many concurrent connections / connections unresponsive, send 500 :/\n");
            send500(con->fd);
            apiCloseConn(con, thread);
        }

        memset(con, 0, sizeof(struct apiCon));

        con->open = 1;
        con->fd = fd;
        con->events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLHUP;
        struct epoll_event epollEvent = { .events = con->events };
        epollEvent.data.ptr = con;


        if (Modes.debug_api)
            fprintf(stderr, "%d: new c: %d\n", thread->index, fd);

        if (epoll_ctl(thread->epfd, EPOLL_CTL_ADD, fd, &epollEvent))
            perror("epoll_ctl fail:");
    }
    if (errno) {
        if (!(errno & (EMFILE | EINTR | EAGAIN | EWOULDBLOCK))) {
            fprintf(stderr, "<3>API: Error accepting new connection: %s\n", aneterr);
        }
        if (errno == EMFILE) {
            fprintf(stderr, "<3>Out of file descriptors accepting api clients, "
                    "exiting to make sure we don't remain in a broken state!\n");
            Modes.exit = 2;
        }
    }
}

static void *apiThreadEntryPoint(void *arg) {
    struct apiThread *thread = (struct apiThread *) arg;
    srandom(get_seed());

    thread->epfd = my_epoll_create();

    for (int i = 0; i < Modes.apiService.listener_count; ++i) {
        struct apiCon *con = Modes.apiListeners[i];
        struct epoll_event epollEvent = { .events = con->events };
        epollEvent.data.ptr = con;

        if (epoll_ctl(thread->epfd, EPOLL_CTL_ADD, con->fd, &epollEvent))
            perror("epoll_ctl fail:");
    }

    int count = 0;
    struct epoll_event *events = NULL;
    int maxEvents = 0;

    struct timespec cpu_timer;
    start_cpu_timing(&cpu_timer);
    uint32_t loop = 0;
    while (!Modes.exit) {
        if (count == maxEvents) {
            epollAllocEvents(&events, &maxEvents);
        }
        count = epoll_wait(thread->epfd, events, maxEvents, 1000);

        if (loop++ % 128) {
            pthread_mutex_lock(&thread->mutex);
            end_cpu_timing(&cpu_timer, &Modes.stats_current.api_worker_cpu);
            pthread_mutex_unlock(&thread->mutex);
            start_cpu_timing(&cpu_timer);
        }

        for (int i = 0; i < count; i++) {
            struct epoll_event event = events[i];
            if (event.data.ptr == &Modes.exitEventfd)
                break;

            struct apiCon *con = event.data.ptr;
            if (con->accept) {
                acceptConn(con, thread);
            } else {
                if (event.events & (EPOLLIN | EPOLLERR | EPOLLHUP)) {
                    apiReadRequest(con, thread);
                } else if (event.events & EPOLLOUT) {
                    apiSendData(con, thread);
                } else {
                    apiReadRequest(con, thread);
                }
            }
        }
    }

    pthread_mutex_lock(&thread->mutex);
    end_cpu_timing(&cpu_timer, &Modes.stats_current.api_worker_cpu);
    pthread_mutex_unlock(&thread->mutex);

    close(thread->epfd);
    sfree(events);
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

        apiUpdate(&Modes.aircraftActive);

        end_cpu_timing(&cpu_timer, &Modes.stats_current.api_update_cpu);

        //int64_t elapsed = stopWatch(&watch);
        //fprintf(stderr, "api req took: %.5f s, got %d aircraft!\n", elapsed / 1000.0, n);

        threadTimedWait(&Threads.apiUpdate, &ts, Modes.json_interval);
    }
    pthread_mutex_unlock(&Threads.apiUpdate.mutex);
    return NULL;
}

void apiBufferInit() {
    for (int i = 0; i < 2; i++) {
        struct apiBuffer *buffer = &Modes.apiBuffer[i];
        buffer->hashList = aligned_malloc(API_BUCKETS * sizeof(struct apiEntry*));
    }
    for (int i = 0; i < API_THREADS; i++) {
        pthread_mutex_init(&Modes.apiThread[i].mutex, NULL);
    }
    apiUpdate(&Modes.aircraftActive); // run an initial apiUpdate
    threadCreate(&Threads.apiUpdate, NULL, apiUpdateEntryPoint, NULL);
}

void apiBufferCleanup() {

    threadSignalJoin(&Threads.apiUpdate);

    for (int i = 0; i < 2; i++) {
        sfree(Modes.apiBuffer[i].list);
        sfree(Modes.apiBuffer[i].list_flag);
        sfree(Modes.apiBuffer[i].json);
        sfree(Modes.apiBuffer[i].hashList);
    }

    for (int i = 0; i < API_THREADS; i++) {
        pthread_mutex_init(&Modes.apiThread[i].mutex, NULL);
    }
}

void apiInit() {
    Modes.apiService.descr = "API output";
    serviceListen(&Modes.apiService, Modes.net_bind_address, Modes.net_output_api_ports, -1);
    if (strncmp(Modes.net_output_api_ports, "unix:", 5) == 0) {
        chmod(Modes.net_output_api_ports + 5, 0666);
    }
    if (Modes.apiService.listener_count <= 0) {
        Modes.api = 0;
        return;
    }
    Modes.apiListeners = aligned_malloc(sizeof(struct apiCon*) * Modes.apiService.listener_count);
    memset(Modes.apiListeners, 0, sizeof(struct apiCon*) * Modes.apiService.listener_count);
    for (int i = 0; i < Modes.apiService.listener_count; ++i) {
        struct apiCon *con = aligned_malloc(sizeof(struct apiCon));
        memset(con, 0, sizeof(struct apiCon));
        if (!con) fprintf(stderr, "EMEM, how much is the fish?\n"), exit(1);

        Modes.apiListeners[i] = con;
        con->fd = Modes.apiService.listener_fds[i];
        con->accept = 1;
        con->events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLHUP;
    }

    Modes.api_fds_per_thread = Modes.max_fds * 7 / 8 / API_THREADS;
    for (int i = 0; i < API_THREADS; i++) {
        Modes.apiThread[i].cons = aligned_malloc(Modes.api_fds_per_thread * sizeof(struct apiCon));
        memset(Modes.apiThread[i].cons, 0x0, Modes.api_fds_per_thread * sizeof(struct apiCon));
        Modes.apiThread[i].index = i;
        pthread_create(&Modes.apiThread[i].thread, NULL, apiThreadEntryPoint, &Modes.apiThread[i]);
    }
}
void apiCleanup() {
    for (int i = 0; i < API_THREADS; i++) {
        pthread_join(Modes.apiThread[i].thread, NULL);
        sfree(Modes.apiThread[i].cons);
    }

    for (int i = 0; i < Modes.apiService.listener_count; ++i) {
        sfree(Modes.apiListeners[i]);
    }
    sfree(Modes.apiListeners);

    sfree(Modes.apiService.listener_fds);
}

struct char_buffer apiGenerateAircraftJson() {
    struct char_buffer cb;

    pthread_mutex_lock(&Modes.apiFlipMutex);
    int flip = Modes.apiFlip;
    pthread_mutex_unlock(&Modes.apiFlipMutex);

    struct apiBuffer *buffer = &Modes.apiBuffer[flip];
    int acCount = buffer->aircraftJsonCount;

    size_t alloc = acCount * 1024 + 2048;
    char *buf = (char *) aligned_malloc(alloc), *p = buf, *end = buf + alloc;

    p = safe_snprintf(p, end,
            "{ \"now\" : %.1f,\n"
            "  \"messages\" : %u,\n",
            buffer->timestamp / 1000.0,
            Modes.stats_current.messages_total + Modes.stats_alltime.messages_total);

    //fprintf(stderr, "%.3f\n", ((double) mstime() - (double) buffer->timestamp) / 1000.0);

    p = safe_snprintf(p, end, "  \"aircraft\" : [");
    for (int j = 0; j < buffer->len; j++) {
        struct apiEntry *entry = &buffer->list[j];
        if (!entry->aircraftJson)
            continue;

        // check if we have enough space
        if ((p + 2000) >= end) {
            int used = p - buf;
            alloc *= 2;
            buf = (char *) realloc(buf, alloc);
            p = buf + used;
            end = buf + alloc;
        }

        *p++ = '\n';

        memcpy(p, buffer->json + entry->jsonOffset.offset, entry->jsonOffset.len);
        p += entry->jsonOffset.len;

        *p++ = ',';

        if (p >= end)
            fprintf(stderr, "apiGenerateAircraftJson buffer overrun\n");
    }

    if (*(p-1) == ',')
        p--;

    p = safe_snprintf(p, end, "\n  ]\n}\n");

    cb.len = p - buf;
    cb.buffer = buf;
    return cb;
}

struct char_buffer apiGenerateGlobeJson(int globe_index) {
    assert (globe_index <= GLOBE_MAX_INDEX);

    struct char_buffer cb;

    pthread_mutex_lock(&Modes.apiFlipMutex);
    int flip = Modes.apiFlip;
    pthread_mutex_unlock(&Modes.apiFlipMutex);

    struct apiBuffer *buffer = &Modes.apiBuffer[flip];


    size_t alloc = 4096;
    // only used to estimate allocation size
    struct craftArray *ca = &Modes.globeLists[globe_index];
    if (ca)
        alloc += ca->len * 1024;

    char *buf = (char *) aligned_malloc(alloc), *p = buf, *end = buf + alloc;

    p = safe_snprintf(p, end,
            "{ \"now\" : %.1f,\n"
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
        if ((p + 2000) >= end) {
            int used = p - buf;
            alloc *= 2;
            buf = (char *) realloc(buf, alloc);
            p = buf + used;
            end = buf + alloc;
        }

        *p++ = '\n';

        memcpy(p, buffer->json + entry->jsonOffset.offset, entry->jsonOffset.len);
        p += entry->jsonOffset.len;

        *p++ = ',';

        if (p >= end)
            fprintf(stderr, "apiGenerateGlobeJson buffer overrun\n");
    }

    if (*(p - 1) == ',')
        p--;

    p = safe_snprintf(p, end, "\n  ]\n}\n");

    cb.len = p - buf;
    cb.buffer = buf;
    return cb;
}
