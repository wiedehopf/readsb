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
uint32_t dbHash(uint32_t addr) {
    uint64_t h = 0x30732349f7810465ULL ^ (4 * 0x2127599bf4325c37ULL);
    uint64_t in = addr;
    uint64_t v = in << 48;
    v ^= in << 24;
    v ^= in;
    h ^= mix_fasthash(v);

    h -= (h >> 32);
    h &= (1ULL << 32) - 1;
    h -= (h >> DB_HASH_BITS);

    return h & (DB_BUCKETS - 1);
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
        a->signalLevel[i] = fmax(0, mm->signalLevel);
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

    updateTypeReg(a);

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

void toBinCraft(struct aircraft *a, struct binCraft *new, uint64_t now) {

    memset(new, 0, sizeof(struct binCraft));
    new->hex = a->addr;
    new->seen = (now - a->seen) / 100.0;

    new->callsign_valid = trackDataValid(&a->callsign_valid);
    for (unsigned i = 0; i < sizeof(new->callsign); i++)
        new->callsign[i] = a->callsign[i] * new->callsign_valid;

    if (Modes.db) {
        memcpy(new->registration, a->registration, sizeof(new->registration));
        memcpy(new->typeCode, a->typeCode, sizeof(new->typeCode));
        new->dbFlags = a->dbFlags;
    }

    new->messages = a->messages;

    new->position_valid = trackDataValid(&a->position_valid)
        && ( (a->pos_reliable_odd >= Modes.json_reliable && a->pos_reliable_even >= Modes.json_reliable)
                || a->position_valid.source <= SOURCE_JAERO );

    new->seen_pos = (now - a->seen_pos) / 100.0 * new->position_valid;

    new->pos_nic = a->pos_nic * new->position_valid;
    new->pos_rc = a->pos_rc * new->position_valid;

    new->lat = (int32_t) nearbyint(a->lat * 1E6) * new->position_valid;
    new->lon = (int32_t) nearbyint(a->lon * 1E6) * new->position_valid;

    new->altitude_baro_valid = trackDataValid(&a->altitude_baro_valid)
        && (a->alt_reliable >= Modes.json_reliable + 1 || a->position_valid.source <= SOURCE_JAERO);

    new->altitude_baro = (int16_t) nearbyint(a->altitude_baro / 25.0) * new->altitude_baro_valid;

    new->altitude_geom = (int16_t) nearbyint(a->altitude_geom / 25.0);
    new->baro_rate = (int16_t) nearbyint(a->baro_rate / 8.0);
    new->geom_rate = (int16_t) nearbyint(a->geom_rate / 8.0);
    new->ias = a->ias;
    new->tas = a->tas;

    new->squawk = a->squawk;
    new->category = a->category * (now < a->category_updated + TRACK_EXPIRE_JAERO);
    // Aircraft category A0 - D7 encoded as a single hex byte. 00 = unset
    new->nav_altitude_mcp = (uint16_t) nearbyint(a->nav_altitude_mcp / 4.0);
    new->nav_altitude_fms = (uint16_t) nearbyint(a->nav_altitude_fms / 4.0);

    new->nav_qnh = (int16_t) nearbyint(a->nav_qnh * 10.0);
    new->gs = (int16_t) nearbyint(a->gs * 10.0);
    new->mach = (int16_t) nearbyint(a->mach * 1000.0);

    new->track_rate = (int16_t) nearbyint(a->track_rate * 100.0);
    new->roll = (int16_t) nearbyint(a->roll * 100.0);

    new->track = (int16_t) nearbyint(a->track * 90.0);
    new->mag_heading = (int16_t) nearbyint(a->mag_heading * 90.0);
    new->true_heading = (int16_t) nearbyint(a->true_heading * 90.0);
    new->nav_heading = (int16_t) nearbyint(a->nav_heading * 90.0);

    new->emergency = a->emergency;
    new->airground = a->airground * trackDataValid(&a->airground_valid);

    new->addrtype = a->addrtype;
    new->nav_modes = a->nav_modes;
    new->nav_altitude_src = a->nav_altitude_src;
    new->sil_type = a->sil_type;

    new->wind_valid = (now < a->wind_updated + TRACK_EXPIRE && abs(a->wind_altitude - a->altitude_baro) < 500);
    new->wind_direction = (int) nearbyint(a->wind_direction) * new->wind_valid;
    new->wind_speed = (int) nearbyint(a->wind_speed) * new->wind_valid;

    new->temp_valid = (now < a->oat_updated + TRACK_EXPIRE);
    new->oat = (int) nearbyint(a->oat) * new->temp_valid;
    new->tat = (int) nearbyint(a->tat) * new->temp_valid;

    if (a->adsb_version < 0)
        new->adsb_version = 15;
    else
        new->adsb_version = a->adsb_version;

    if (a->adsr_version < 0)
        new->adsr_version = 15;
    else
        new->adsr_version = a->adsr_version;

    if (a->tisb_version < 0)
        new->tisb_version = 15;
    else
        new->tisb_version = a->tisb_version;

    new->nic_a = a->nic_a;
    new->nic_c = a->nic_c;
    new->nic_baro = a->nic_baro;
    new->nac_p = a->nac_p;
    new->nac_v = a->nac_v;
    new->sil = a->sil;
    new->gva = a->gva;
    new->sda = a->sda;
    new->alert = a->alert;
    new->spi = a->spi;

    new->signal = get8bitSignal(a);

    if (a->position_valid.source == SOURCE_MLAT) {
        new->receiverCount = a->receiverCountMlat;
    } else {
        uint16_t *set1 = a->receiverIds;
        uint16_t set2[16] = { 0 };
        int div = 0;
        for (int k = 0; k < RECEIVERIDBUFFER; k++) {
            int unequal = 0;
            for (int j = 0; j < div; j++) {
                unequal += (set1[k] != set2[j]);
            }
            if (unequal == div && set1[k])
                set2[div++] = set1[k];
        }
        new->receiverCount = div;
    }
#define F(f) do { new->f##_valid = trackDataValid(&a->f##_valid); new->f *= new->f##_valid; } while (0)
    F(altitude_geom);
    F(gs);
    F(ias);
    F(tas);
    F(mach);
    F(track);
    F(track_rate);
    F(roll);
    F(mag_heading);
    F(true_heading);
    F(baro_rate);
    F(geom_rate);
    F(nic_a);
    F(nic_c);
    F(nic_baro);
    F(nac_p);
    F(nac_v);
    F(sil);
    F(gva);
    F(sda);
    F(squawk);
    F(emergency);
    F(nav_qnh);
    F(nav_altitude_mcp);
    F(nav_altitude_fms);
    F(nav_altitude_src);
    F(nav_heading);
    F(nav_modes);
    F(alert);
    F(spi);
#undef F
}

// rudimentary sanitization so the json output hopefully won't be invalid
static inline void sanitize(char *str, unsigned len) {
    char b2 = (1<<7) + (1<<6); // 2 byte code or more
    char b3 = (1<<7) + (1<<6) + (1<<5); // 3 byte code or more
    char b4 = (1<<7) + (1<<6) + (1<<5) + (1<<4); // 4 byte code

    if (len >= 3 && (str[len - 3] & b4) == b4) {
        //fprintf(stderr, "%c\n", str[len - 3]);
        str[len - 3] = '\0';
    }
    if (len >= 2 && (str[len - 2] & b3) == b3) {
        //fprintf(stderr, "%c\n", str[len - 2]);
        str[len - 2] = '\0';
    }
    if (len >= 1 && (str[len - 1] & b2) == b2) {
        //fprintf(stderr, "%c\n", str[len - 1]);
        str[len - 1] = '\0';
    }
    char *p = str;
    while(p < str + len && *p) {
        if (*p == '"')
            *p = '\'';
        if (*p > 0 && *p < 0x1f)
            *p = ' ';
        p++;
    }
    if (p - 1 >= str && *(p - 1) == '\\') {
        *(p - 1) = '\0';
    }
}
static char *sprintDB(char *p, char *end, dbEntry *d) {
    p = safe_snprintf(p, end, "\n\"%s%06x\":{", (d->addr & MODES_NON_ICAO_ADDRESS) ? "~" : "", d->addr & 0xFFFFFF);
    char *regInfo = p;
    if (d->registration[0])
        p = safe_snprintf(p, end, "\"r\":\"%.*s\",", (int) sizeof(d->registration), d->registration);
    if (d->typeCode[0])
        p = safe_snprintf(p, end, "\"t\":\"%.*s\",", (int) sizeof(d->typeCode), d->typeCode);
    if (d->typeLong[0])
        p = safe_snprintf(p, end, "\"desc\":\"%.*s\",", (int) sizeof(d->typeLong), d->typeLong);
    if (d->dbFlags)
        p = safe_snprintf(p, end, "\"dbFlags\":%u,", d->dbFlags);
    if (p == regInfo)
        p = safe_snprintf(p, end, "\"noRegData\":true");
    if (*(p-1) == ',')
        p--;
    p = safe_snprintf(p, end, "},");
    return p;
}
static void dbToJson() {
    size_t buflen = 32 * 1024 * 1024;
    char *buf = (char *) malloc(buflen), *p = buf, *end = buf + buflen;
    p = safe_snprintf(p, end, "{");

    for (int j = 0; j < DB_BUCKETS; j++) {
        for (dbEntry *d = Modes.db2Index[j]; d; d = d->next) {
            p = sprintDB(p, end, d);
            if ((p + 1000) >= end) {
                int used = p - buf;
                buflen *= 2;
                buf = (char *) realloc(buf, buflen);
                p = buf + used;
                end = buf + buflen;
            }
        }
    }

    if (*(p-1) == ',')
        p--;
    p = safe_snprintf(p, end, "\n}");
    struct char_buffer cb2;
    cb2.len = p - buf;
    cb2.buffer = buf;
    writeJsonToFile(Modes.json_dir, "db.json", cb2); // location changed
}

// get next CSV token based on the assumption eot points to the previous delimiter
static inline int nextToken(char delim, char **sot, char **eot, char **eol) {
    *sot = *eot + 1;
    if (*sot >= *eol)
        return 0;
    *eot = memchr(*sot, delim, *eol - *sot);

    if (!*eot)
        return 0;

    **eot = '\0';
    return 1;
}

int dbUpdate() {
    struct char_buffer cb = {0};
    char *filename = Modes.db_file;
    if (!strlen(filename) || !strcmp(filename, "none"))
        return 0;
    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
        filename = "/opt/html/aircraft.csv.gz";
        fd = open(filename, O_RDONLY);
        Modes.dbExchange = 1;
    }
    if (fd == -1)
        return 0;

    struct stat fileinfo = {0};
    if (fstat(fd, &fileinfo)) {
        fprintf(stderr, "%s: dbUpdate: fstat failed, wat?!\n", filename);
        goto DBU0;
    }
    uint64_t modTime = fileinfo.st_mtim.tv_sec;


    if (Modes.dbModificationTime == modTime)
        goto DBU0;

    int alloc = (1<<20);
    Modes.db2 = malloc(alloc * sizeof(dbEntry));
    Modes.db2Index = calloc(DB_BUCKETS, sizeof(void*));

    if (!Modes.db2 || !Modes.db2Index) {
        fprintf(stderr, "db update error: malloc failure!\n");
        goto DBU0;
    }

    gzFile gzfp = gzdopen(fd, "r");
    if (!gzfp) {
        fprintf(stderr, "db update error: gzdopen failed.\n");
        goto DBU0;
    }


    cb = readWholeGz(gzfp, filename);
    if (!cb.buffer) {
        fprintf(stderr, "readWholeGz failed.\n");
        gzclose(gzfp);
        goto DBU0;
    }
    if (cb.len < 1000) {
        fprintf(stderr, "database file very small, bailing out of dbUpdate.\n");
        gzclose(gzfp);
        goto DBU0;
    }

    char *eob = cb.buffer + cb.len;
    char *sol = cb.buffer;
    char *eol;
    for (int i = 0; eob > sol && (eol = memchr(sol, '\n', eob - sol)); sol = eol + 1) {

        char *sot;
        char *eot = sol - 1; // this pointer must not be dereferenced, nextToken will increment it.

        dbEntry *curr = &Modes.db2[i];
        memset(curr, 0, sizeof(dbEntry));

        if (!nextToken(';', &sot, &eot, &eol)) continue;
        curr->addr = strtol(sot, NULL, 16);
        if (curr->addr == 0)
            continue;

        if (!nextToken(';', &sot, &eot, &eol)) continue;
        memcpy(curr->registration, sot, min(sizeof(curr->registration), eot - sot));
        sanitize(curr->registration, sizeof(curr->registration));

        if (!nextToken(';', &sot, &eot, &eol)) continue;
        memcpy(curr->typeCode, sot, min(sizeof(curr->typeCode), eot - sot));
        sanitize(curr->typeCode, sizeof(curr->typeCode));

        if (!nextToken(';', &sot, &eot, &eol)) continue;
        for (int j = 0; j < 16 && sot < eot; j++, sot++)
            curr->dbFlags |= ((*sot == '1') << j);


        // nextToken wouldn't work as there is no trailing ;, set sot / eot by hand
        sot = eot + 1;
        eot = eol;
        memcpy(curr->typeLong, sot, min(sizeof(curr->typeLong), eot - sot));
        sanitize(curr->typeLong, sizeof(curr->typeLong));

        if (false) // debugging output
            fprintf(stdout, "%06X;%.12s;%.4s;%c%c;%.54s\n",
                    curr->addr,
                    curr->registration,
                    curr->typeCode,
                    curr->dbFlags & 1 ? '1' : '0',
                    curr->dbFlags & 2 ? '1' : '0',
                    curr->typeLong);

        i++; // increment db array index
        // add to hashtable
        dbPut(curr->addr, Modes.db2Index, curr);
    }
    //fflush(stdout);

    gzclose(gzfp);
    free(cb.buffer);
    Modes.dbModificationTime = modTime;
    fprintf(stderr, "db update done!\n");
    writeJsonToFile(Modes.json_dir, "receiver.json", generateReceiverJson());
    return 1;
DBU0:
    free(cb.buffer);
    free(Modes.db2);
    free(Modes.db2Index);
    Modes.db2 = NULL;
    Modes.db2Index = NULL;
    close(fd);
    return 0;
}

void dbFinishUpdate() {
    // finish db update
    if (Modes.db2 && Modes.db2Index) {
        if (Modes.debug_dbJson)
            dbToJson();
        free(Modes.dbIndex);
        free(Modes.db);
        Modes.dbIndex = Modes.db2Index;
        Modes.db = Modes.db2;
        Modes.db2Index = NULL;
        Modes.db2 = NULL;

        for (int j = 0; j < AIRCRAFT_BUCKETS; j++) {
            for (struct aircraft *a = Modes.aircraft[j]; a; a = a->next) {
                updateTypeReg(a);
            }
        }
    }
}


dbEntry *dbGet(uint32_t addr, dbEntry **index) {
    if (!index)
        return NULL;
    dbEntry *d = index[dbHash(addr)];

    while (d && d->addr != addr) {
        d = d->next;
    }
    return d;
}

void dbPut(uint32_t addr, dbEntry **index, dbEntry *d) {
    uint32_t hash = dbHash(addr);
    d->next = index[hash];
    index[hash] = d;
}

void updateTypeReg(struct aircraft *a) {
    dbEntry *d = dbGet(a->addr, Modes.dbIndex);
    if (d) {
        memcpy(a->registration, d->registration, sizeof(a->registration));
        memcpy(a->typeCode, d->typeCode, sizeof(a->typeCode));
        memcpy(a->typeLong, d->typeLong, sizeof(a->typeLong));
        a->dbFlags = d->dbFlags;
    } else {
        memset(a->registration, 0, sizeof(a->registration));
        memset(a->typeCode, 0, sizeof(a->typeCode));
        memset(a->typeLong, 0, sizeof(a->typeLong));
        a->dbFlags = 0;
    }
    uint32_t i = a->addr;
    if (
            false
            // us military
            //adf7c8-adf7cf = united states mil_5(uf)
            //adf7d0-adf7df = united states mil_4(uf)
            //adf7e0-adf7ff = united states mil_3(uf)
            //adf800-adffff = united states mil_2(uf)
            //ae0000-afffff = united states mil_1(uf)
            || (i >= 0xadf7c8 && i <= 0xafffff)

            //010070-01008f = egypt_mil
            || (i >= 0x010070 && i <= 0x01008f)

            //0a4000-0a4fff = algeria mil(ap)
            || (i >= 0x0a4000 && i <= 0x0a4fff)

            //33ff00-33ffff = italy mil(iy)
            || (i >= 0x33ff00 && i <= 0x33ffff)

            //350000-37ffff = spain mil(sp)
            || (i >= 0x350000 && i <= 0x37ffff)

            //3a8000-3affff = france mil_1(fs)
            || (i >= 0x3a8000 && i <= 0x3affff)
            //3b0000-3bffff = france mil_2(fs)
            || (i >= 0x3b0000 && i <= 0x3bffff)

            //3ea000-3ebfff = germany mil_1(df)
            || (i >= 0x3ea000 && i <= 0x3ebfff)
            //3f4000-3f7fff = germany mil_2(df)
            //3f8000-3fbfff = germany mil_3(df)
            || (i >= 0x3f4000 && i <= 0x3fbfff)

            //400000-40003f = united kingdom mil_1(ra)
            || (i >= 0x400000 && i <= 0x40003f)
            //43c000-43cfff = united kingdom mil(ra)
            || (i >= 0x43c000 && i <= 0x43cfff)

            //444000-446fff = austria mil(aq)
            || (i >= 0x444000 && i <= 0x446fff)

            //44f000-44ffff = belgium mil(bc)
            || (i >= 0x44f000 && i <= 0x44ffff)

            //457000-457fff = bulgaria mil(bu)
            || (i >= 0x457000 && i <= 0x457fff)

            //45f400-45f4ff = denmark mil(dg)
            || (i >= 0x45f400 && i <= 0x45f4ff)

            //468000-4683ff = greece mil(gc)
            || (i >= 0x468000 && i <= 0x4683ff)

            //473c00-473c0f = hungary mil(hm)
            || (i >= 0x473c00 && i <= 0x473c0f)

            //478100-4781ff = norway mil(nn)
            || (i >= 0x478100 && i <= 0x4781ff)
            //480000-480fff = netherlands mil(nm)
            || (i >= 0x480000 && i <= 0x480fff)
            //48d800-48d87f = poland mil(po)
            || (i >= 0x48d800 && i <= 0x48d87f)
            //497c00-497cff = portugal mil(pu)
            || (i >= 0x497c00 && i <= 0x497cff)
            //498420-49842f = czech republic mil(ct)
            || (i >= 0x498420 && i <= 0x49842f)

            //4b7000-4b7fff = switzerland mil(su)
            || (i >= 0x4b7000 && i <= 0x4b7fff)
            //4b8200-4b82ff = turkey mil(tq)
            || (i >= 0x4b8200 && i <= 0x4b82ff)

            //506f00-506fff = slovenia mil(sj)
            || (i >= 0x506f00 && i <= 0x506fff)

            //70c070-70c07f = oman mil(on)
            || (i >= 0x70c070 && i <= 0x70c07f)

            //710258-71025f = saudi arabia mil_1(sx)
            //710260-71027f = saudi arabia mil_2(sx)
            //710280-71028f = saudi arabia mil_3(sx)
            || (i >= 0x710258 && i <= 0x71028f)
            //710380-71039f = saudi arabia mil_4(sx)
            || (i >= 0x710380 && i <= 0x71039f)

            //738a00-738aff = israel mil(iz)
            || (i >= 0x738a00 && i <= 0x738aff)

            //7c822e-7c84ff = australia mil_1(av)
            || (i >= 0x7c822e && i <= 0x7c84ff)
            //7c8800-7c8fff = australia mil_7(av)
            || (i >= 0x7c8800 && i <= 0x7c88ff)
            //7c9000-7c9fff = australia mil_8(av)
            //7ca000-7cbfff = australia mil_9(av)
            || (i >= 0x7c9000 && i <= 0x7cbfff)
            //7d0000-7dffff = australia mil_11(av)
            //7e0000-7fffff = australia mil_12(av)
            || (i >= 0x7d0000 && i <= 0x7fffff)

            //800200-8002ff = india mil(im)
            || (i >= 0x800200 && i <= 0x8002ff)

            //c20000-c3ffff = canada mil(cb)
            || (i >= 0xc20000 && i <= 0xc3ffff)

            //e40000-e41fff = brazil mil(bq)
            || (i >= 0xe40000 && i <= 0xe41fff)

            //e80600-e806ff = chile mil(cq)
            || (i >= 0xe80600 && i <= 0xe806ff)
    ) {
        a->dbFlags |= 1;
    }
}
