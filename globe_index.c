#include "readsb.h"

static void mark_legs(struct aircraft *a, int start);
static void load_blob(int blob);

void init_globe_index() {
    struct tile *s_tiles = Modes.json_globe_special_tiles = calloc(GLOBE_SPECIAL_INDEX, sizeof(struct tile));
    int count = 0;

    // Arctic
    s_tiles[count++] = (struct tile) {
        60, -126,
        90, 0
    };
    s_tiles[count++] = (struct tile) {
        60, 0,
        90, 150
    };

    // Alaska and Chukotka
    s_tiles[count++] = (struct tile) {
        51, 150,
        90, -126
    };
    // North Pacific
    s_tiles[count++] = (struct tile) {
        9, 150,
        51, -126
    };

    // Northern Canada
    s_tiles[count++] = (struct tile) {
        51, -126,
        60, -69
    };

    // Northwest USA
    s_tiles[count++] = (struct tile) {
        45, -120,
        51, -114
    };
    s_tiles[count++] = (struct tile) {
        45, -114,
        51, -102
    };
    s_tiles[count++] = (struct tile) {
        45, -102,
        51, -90
    };
    // Eastern Canada
    s_tiles[count++] = (struct tile) {
        45, -90,
        51, -75
    };

    s_tiles[count++] = (struct tile) {
        45, -75,
        51, -69
    };
    // Balkan
    s_tiles[count++] = (struct tile) {
        42, 12,
        48, 18
    };
    s_tiles[count++] = (struct tile) {
        42, 18,
        48, 24
    };
    // Poland
    s_tiles[count++] = (struct tile) {
        48, 18,
        54, 24
    };
    // Sweden
    s_tiles[count++] = (struct tile) {
        54, 12,
        60, 24
    };
    // Denmark
    s_tiles[count++] = (struct tile) {
        54, 3,
        60, 12
    };
    // Northern UK
    s_tiles[count++] = (struct tile) {
        54, -9,
        60, 3
    };

    // Golfo de Vizcaya / Bay of Biscay
    s_tiles[count++] = (struct tile) {
        42, -9,
        48, 0
    };

    // West Russia
    s_tiles[count++] = (struct tile) {
        42, 24,
        51, 51
    };
    s_tiles[count++] = (struct tile) {
        51, 24,
        60, 51
    };

    // Central Russia
    s_tiles[count++] = (struct tile) {
        30, 51,
        60, 90
    };

    // East Russia
    s_tiles[count++] = (struct tile) {
        30, 90,
        60, 120
    };
    // Koreas and Japan and some Russia
    s_tiles[count++] = (struct tile) {
        30, 120,
        39, 129
    };
    s_tiles[count++] = (struct tile) {
        30, 129,
        39, 138
    };
    s_tiles[count++] = (struct tile) {
        30, 138,
        39, 150
    };
    s_tiles[count++] = (struct tile) {
        39, 120,
        60, 150
    };
    // Vietnam
    s_tiles[count++] = (struct tile) {
        9, 90,
        21, 111
    };

    // South China
    s_tiles[count++] = (struct tile) {
        21, 90,
        30, 111
    };

    // South China and ICAO special use
    s_tiles[count++] = (struct tile) {
        9, 111,
        24, 129
    };
    s_tiles[count++] = (struct tile) {
        24, 111,
        30, 120
    };
    s_tiles[count++] = (struct tile) {
        24, 120,
        30, 129
    };

    // mostly pacific south of Japan
    s_tiles[count++] = (struct tile) {
        9, 129,
        30, 150
    };


    // Persian Gulf / Arabian Sea
    s_tiles[count++] = (struct tile) {
        9, 51,
        30, 69
    };

    // India
    s_tiles[count++] = (struct tile) {
        9, 69,
        30, 90
    };

    // South Atlantic / South Africa
    s_tiles[count++] = (struct tile) {
        -90, -30,
        9, 51
    };
    //Indian Ocean
    s_tiles[count++] = (struct tile) {
        -90, 51,
        9, 111
    };

    // Australia
    s_tiles[count++] = (struct tile) {
        -90, 111,
        -18, 160
    };
    s_tiles[count++] = (struct tile) {
        -18, 111,
        9, 160
    };

    // South Pacific and NZ
    s_tiles[count++] = (struct tile) {
        -90, 160,
        -42, -90
    };
    s_tiles[count++] = (struct tile) {
        -42, 160,
        9, -90
    };

    // North South America
    s_tiles[count++] = (struct tile) {
        -9, -90,
        9, -42
    };

    // South South America
    // west
    s_tiles[count++] = (struct tile) {
        -90, -90,
        -9, -63
    };
    // east
    s_tiles[count++] = (struct tile) {
        -21, -63,
        -9, -42
    };
    s_tiles[count++] = (struct tile) {
        -90, -63,
        -21, -42
    };

    s_tiles[count++] = (struct tile) {
        -90, -42,
        9, -30
    };

    // Guatemala / Mexico
    s_tiles[count++] = (struct tile) {
        9, -126,
        33, -117
    };
    s_tiles[count++] = (struct tile) {
        9, -117,
        30, -102
    };
    // western gulf + east mexico
    s_tiles[count++] = (struct tile) {
        9, -102,
        27, -90
    };
    // Eastern Gulf of Mexico
    s_tiles[count++] = (struct tile) {
        24, -90,
        30, -84
    };

    // south of jamaica
    s_tiles[count++] = (struct tile) {
        9, -90,
        18, -69
    };

    // Cuba / Haiti
    s_tiles[count++] = (struct tile) {
        18, -90,
        24, -69
    };

    // Mediterranean
    s_tiles[count++] = (struct tile) {
        36, 6,
        42, 18
    };
    s_tiles[count++] = (struct tile) {
        36, 18,
        42, 30
    };

    // North Africa
    s_tiles[count++] = (struct tile) {
        9, -9,
        39, 6
    };
    s_tiles[count++] = (struct tile) {
        9, 6,
        36, 30
    };

    // Middle East
    s_tiles[count++] = (struct tile) {
        9, 30,
        42, 51
    };

    // west of Bermuda
    s_tiles[count++] = (struct tile) {
        24, -75,
        39, -69
    };
    // North Atlantic
    s_tiles[count++] = (struct tile) {
        9, -69,
        30, -33
    };
    s_tiles[count++] = (struct tile) {
        30, -69,
        60, -33
    };
    s_tiles[count++] = (struct tile) {
        9, -33,
        30, -9
    };
    s_tiles[count++] = (struct tile) {
        30, -33,
        60, -9
    };

    Modes.specialTileCount = count;

    if (count + 1 >= GLOBE_SPECIAL_INDEX)
        fprintf(stderr, "increase GLOBE_SPECIAL_INDEX please!\n");

    Modes.json_globe_indexes = calloc(GLOBE_MAX_INDEX, sizeof(int32_t));
    Modes.json_globe_indexes_len = 0;
    for (int i = 0; i <= GLOBE_MAX_INDEX; i++) {
        if (i == Modes.specialTileCount)
            i = GLOBE_MIN_INDEX;

        if (i >= GLOBE_MIN_INDEX) {
            int index_index = globe_index_index(i);
            if (index_index != i) {

                if (index_index >= GLOBE_MIN_INDEX) {
                    fprintf(stderr, "weird globe index: %d\n", i);
                }
                continue;
            }
        }
        Modes.json_globe_indexes[Modes.json_globe_indexes_len++] = i;
    }
}
void cleanup_globe_index() {
    free(Modes.json_globe_indexes);
    Modes.json_globe_indexes = NULL;

    free(Modes.json_globe_special_tiles);
    Modes.json_globe_special_tiles = NULL;
}

int globe_index(double lat_in, double lon_in) {
    int grid = GLOBE_INDEX_GRID;
    int lat = grid * ((int) ((lat_in + 90) / grid)) - 90;
    int lon = grid * ((int) ((lon_in + 180) / grid)) - 180;

    struct tile *tiles = Modes.json_globe_special_tiles;

    for (int i = 0; tiles[i].south != 0 || tiles[i].north != 0; i++) {
        struct tile tile = tiles[i];
        if (lat >= tile.south && lat < tile.north) {
            if (tile.west < tile.east && lon >= tile.west && lon < tile.east) {
                return i;
            }
            if (tile.west > tile.east && (lon >= tile.west || lon < tile.east)) {
                return i;
            }
        }
    }


    int i = (lat + 90) / grid;
    int j = (lon + 180) / grid;

    int res = (i * GLOBE_LAT_MULT + j + GLOBE_MIN_INDEX);
    if (res > GLOBE_MAX_INDEX) {
        fprintf(stderr, "globe_index out of bounds: %d %d %d\n", res, lat, lon);
        return 0;
    }
    return res;
    // highest number returned: globe_index(90, 180)
    // first 1000 are reserved for special use
}

int globe_index_index(int index) {
    double lat = ((index - GLOBE_MIN_INDEX) /  GLOBE_LAT_MULT) * GLOBE_INDEX_GRID - 90;
    double lon = ((index - GLOBE_MIN_INDEX) % GLOBE_LAT_MULT) * GLOBE_INDEX_GRID - 180;
    return globe_index(lat, lon);
}

static void sprintDateDir(char *base_dir, struct tm *utc, char *dateDir) {
    char tstring[100];
    strftime (tstring, 100, TDATE_FORMAT, utc);
    snprintf(dateDir, PATH_MAX * 3/4, "%s/%s", base_dir, tstring);
}
static void createDateDir(char *base_dir, struct tm *utc, char *dateDir) {
    if (!strcmp(TDATE_FORMAT, "%Y/%m/%d")) {
        char yy[100];
        char mm[100];
        strftime (yy, 100, "%Y", utc);
        strftime (mm, 100, "%m", utc);
        char pathbuf[PATH_MAX];

        snprintf(pathbuf, PATH_MAX, "%s/%s", base_dir, yy);
        if (mkdir(pathbuf, 0755) && errno != EEXIST)
            perror(pathbuf);
        snprintf(pathbuf, PATH_MAX, "%s/%s/%s", base_dir, yy, mm);
        if (mkdir(pathbuf, 0755) && errno != EEXIST)
            perror(pathbuf);
    }
    sprintDateDir(base_dir, utc, dateDir);
    if (mkdir(dateDir, 0755) && errno != EEXIST)
        perror(dateDir);
}

static void scheduleMemBothWrite(struct aircraft *a, uint64_t schedTime) {
    a->trace_next_mw = schedTime;
    a->trace_writeCounter = 0xc0ffee;
}

static void traceWrite(struct aircraft *a, uint64_t now, int init) {
    struct char_buffer recent;
    struct char_buffer full;
    struct char_buffer hist;
    char filename[PATH_MAX];
    //static uint32_t count2, count3, count4;

    recent.len = 0;
    full.len = 0;
    hist.len = 0;

    int trace_write = a->trace_write;
    a->trace_write = 0;

    if (!a->trace_alloc) {
        return;
    }

    int startFull = 0;
    for (int i = 0; i < a->trace_len; i++) {
        if (a->trace[i].timestamp > now - Modes.keep_traces) {
            startFull = i;
            break;
        }
    }

    int recent_points = TRACE_RECENT_POINTS;
    if (a->trace_writeCounter >= recent_points - 2) {
        trace_write |= WMEM;
        trace_write |= WRECENT;
    }

    if ((trace_write & WRECENT)) {
        int start_recent = a->trace_len - recent_points;
        if (start_recent < startFull)
            start_recent = startFull;

        if (!init && a->trace_len % 4 == 0) {
            mark_legs(a, max(0, start_recent - 256 - recent_points));
        }

        // prepare the data for the trace_recent file in /run
        recent = generateTraceJson(a, start_recent, -2);

        //if (Modes.debug_traceCount && ++count2 % 1000 == 0)
        //    fprintf(stderr, "recent trace write: %u\n", count2);
    }

    if (a->addr == TRACE_FOCUS)
        fprintf(stderr, "mw: %.0f, perm: %.0f, count: %d %x\n",
                ((int64_t) a->trace_next_mw - (int64_t) now) / 1000.0,
                ((int64_t) a->trace_next_perm - (int64_t) now) / 1000.0,
                a->trace_writeCounter, a->trace_writeCounter);

    int memWritten = 0;
    // prepare the data for the trace_full file in /run
    if ((trace_write & WMEM)) {
        if (a->trace_writeCounter > 0) {
            memWritten = a->trace_writeCounter;
            //if (Modes.debug_traceCount && ++count3 % 1000 == 0)
            //    fprintf(stderr, "memory trace writes: %u\n", count3);
            if (a->addr == TRACE_FOCUS)
                fprintf(stderr, "full\n");

            mark_legs(a, 0);

            full = generateTraceJson(a, startFull, -1);
        }

        if (a->trace_writeCounter >= 0xc0ffee) {
            a->trace_next_mw = now + GLOBE_MEM_IVAL / 8 + random() % (GLOBE_MEM_IVAL / 1);
        } else {
            a->trace_next_mw = now + GLOBE_MEM_IVAL / 1 + random() % (GLOBE_MEM_IVAL / 8);
        }

        a->trace_writeCounter = 0;
    }

    int permWritten = 0;
    // prepare writing the permanent history
    // until 20 min after midnight we only write permanent traces for the previous day
    if ((trace_write & WPERM)) {
        if (!Modes.globe_history_dir || (a->addr & MODES_NON_ICAO_ADDRESS)) {
            // be sure to push the timer back if we don't write permanent history
            a->trace_next_perm = now + GLOBE_PERM_IVAL;
        } else {
            if (a->addr == TRACE_FOCUS)
                fprintf(stderr, "perm\n");

            // fiftyfive_ago changes day 55 min after midnight: stop writing the previous days traces
            // fiftysix_ago changes day 56 min after midnight: allow webserver to read the previous days traces (see checkNewDay function)
            // this is in seconds, not milliseconds
            time_t fiftyfive = now / 1000 - 55 * 60;

            struct tm utc;
            gmtime_r(&fiftyfive, &utc);

            // this is in reference to the fiftyfive clock ....
            if (utc.tm_hour == 23 && utc.tm_min > 30) {
                a->trace_next_perm = now + GLOBE_PERM_IVAL / 8 + random() % (GLOBE_PERM_IVAL / 1);
            } else {
                a->trace_next_perm = now + GLOBE_PERM_IVAL / 1 + random() % (GLOBE_PERM_IVAL / 8);
            }

            // we just use the day of the struct tm in the next lines
            utc.tm_sec = 0;
            utc.tm_min = 0;
            utc.tm_hour = 0;
            uint64_t start_of_day = 1000 * (uint64_t) (timegm(&utc));
            uint64_t end_of_day = 1000 * (uint64_t) (timegm(&utc) + 86400);

            int start = -1;
            int end = -1;
            for (int i = 0; i < a->trace_len; i++) {
                if (start == -1 && a->trace[i].timestamp > start_of_day) {
                    start = i;
                    break;
                }
            }
            for (int i = a->trace_len - 1; i >= 0; i--) {
                if (a->trace[i].timestamp < end_of_day) {
                    end = i;
                    break;
                }
            }
            uint64_t endStamp = a->trace[end].timestamp;
            if (start >= 0 && end >= 0 && end >= start
                    // only write permanent trace if we haven't already written it
                    && a->trace_perm_last_timestamp != endStamp
               ) {
                mark_legs(a, 0);
                hist = generateTraceJson(a, start, end);
                if (hist.len > 0) {
                    permWritten = 1;
                    char tstring[100];
                    strftime (tstring, 100, TDATE_FORMAT, &utc);

                    snprintf(filename, PATH_MAX, "%s/traces/%02x/trace_full_%s%06x.json", tstring, a->addr % 256, (a->addr & MODES_NON_ICAO_ADDRESS) ? "~" : "", a->addr & 0xFFFFFF);
                    filename[PATH_MAX - 101] = 0;

                    if (writeJsonToGzip(Modes.globe_history_dir, filename, hist, 9) == 0) {
                        // no errors, note what we have written to disk
                        a->trace_perm_last_timestamp = endStamp;
                    } else {
                        // some error, schedule another try in 10 minutes
                        a->trace_next_perm = now + 10 * MINUTES;
                    }

                    free(hist.buffer);

                    //if (Modes.debug_traceCount && ++count4 % 100 == 0)
                    //    fprintf(stderr, "perm trace writes: %u\n", count4);
                }
            }
        }
    }

    if (recent.len > 0) {
        snprintf(filename, 256, "traces/%02x/trace_recent_%s%06x.json", a->addr % 256, (a->addr & MODES_NON_ICAO_ADDRESS) ? "~" : "", a->addr & 0xFFFFFF);

        writeJsonToGzip(Modes.json_dir, filename, recent, 1);
        free(recent.buffer);
    }

    if (full.len > 0) {
        snprintf(filename, 256, "traces/%02x/trace_full_%s%06x.json", a->addr % 256, (a->addr & MODES_NON_ICAO_ADDRESS) ? "~" : "", a->addr & 0xFFFFFF);

        writeJsonToGzip(Modes.json_dir, filename, full, 7);
        free(full.buffer);
    }

    if (Modes.debug_traceCount) {
        static uint32_t timedCount, pointsCount, permCount;
        int timed = 0;
        int byCounter = 0;
        if (permWritten || (memWritten && memWritten < 0xc0ffee)) {
            pthread_mutex_lock(&Modes.traceDebugMutex);
            {
                if (permWritten) {
                    permCount++;
                }
                if (memWritten && memWritten < 0xc0ffee) {
                    if (memWritten >= recent_points - 2) {
                        byCounter = 1;
                        pointsCount++;
                    } else {
                        timed = 1;
                        timedCount++;
                    }
                }
            }
            pthread_mutex_unlock(&Modes.traceDebugMutex);

            int print = 0;
            if (timed && timedCount % 500 == 0) {
                fprintf(stderr, "full_time  :%6d", timedCount);
                print = 1;
            }
            if (byCounter && pointsCount % 500 == 0) {
                fprintf(stderr, "full_points:%6d", pointsCount);
                print = 1;
            }
            if (permWritten && permCount % 500 == 0) {
                fprintf(stderr, "perm       :%6d", permCount);
                print = 1;
            }

            if (print) {
                fprintf(stderr, " hex: %06x mw: %6.0f, perm: %6.0f, count: %4d / %4d (%4x) \n",
                        a->addr,
                        ((int64_t) a->trace_next_mw - (int64_t) now) / 1000.0,
                        ((int64_t) a->trace_next_perm - (int64_t) now) / 1000.0,
                        a->trace_writeCounter,
                        recent_points,
                        a->trace_writeCounter);
            }
        }
    }


    if (0 && a->addr == TRACE_FOCUS)
        fprintf(stderr, "mw: %.0f, perm: %.0f, count: %d\n",
                ((int64_t) a->trace_next_mw - (int64_t) now) / 1000.0,
                ((int64_t) a->trace_next_perm - (int64_t) now) / 1000.0,
                a->trace_writeCounter);
}

void *save_blobs(void *arg) {
    int thread_number = *((int *) arg);
    for (int j = 0; j < STATE_BLOBS; j++) {
        if (j % IO_THREADS != thread_number)
            continue;

        //fprintf(stderr, "save_blob(%d)\n", j);
        save_blob(j);
    }
    return NULL;
}

static int load_aircraft(char **p, char *end, uint64_t now) {
    static int size_changed;

    if (end - *p < 1000)
        return -1;

    struct aircraft *source = (struct aircraft *) *p;
    struct aircraft *a;

    if (end - *p < (int) source->size_struct_aircraft)
        return -1;

    if (source->size_struct_aircraft < sizeof(struct aircraft)) {
        a = calloc(1, sizeof(struct aircraft)); // null the new fields that were added
        memcpy(a, *p, source->size_struct_aircraft);
    } else {
        a = malloc(sizeof(struct aircraft));
        memcpy(a, *p, sizeof(struct aircraft));
    }
    *p += source->size_struct_aircraft;

    if (!size_changed && source->size_struct_aircraft != sizeof(struct aircraft)) {
        size_changed = 1;
        fprintf(stderr, "sizeof(struct aircraft) has changed from %ld to %ld bytes, this means the code changed and if the coder didn't think properly might result in bad aircraft data. If your map doesn't have weird stuff ... probably all good and just an upgrade.\n",
                (long) source->size_struct_aircraft, (long) sizeof(struct aircraft));
    }
    a->size_struct_aircraft = sizeof(struct aircraft);

    a->trace = NULL;
    a->trace_all = NULL;
    a->traceCache = NULL;

    if (!Modes.keep_traces) {
        a->trace_alloc = 0;
        a->trace_len = 0;
    }

    // just in case we have bogus values saved, make sure they time out
    if (a->seen_pos > now + 1 * MINUTES)
        a->seen_pos = now - 26 * HOURS;
    if (a->seen > now + 1 * MINUTES)
        a->seen = now - 26 * HOURS;

    // make sure we don't think an extra position is still buffered in the trace memory
    a->tracePosBuffered = 0;

    // read trace
    int size_state = stateBytes(a->trace_len);
    int size_all = stateAllBytes(a->trace_len);
    // check that the trace meta data make sense before loading it
    if (a->trace_len > 0
            && a->trace_alloc >= a->trace_len
            // let's allow for loading traces larger than we normally allow by a factor of 32
            && a->trace_len <= 32 * TRACE_SIZE
            && a->trace_alloc <= 32 * TRACE_SIZE
       ) {


        if (end - *p < (long) (size_state + size_all)) {
            // TRACE FAIL
            fprintf(stderr, "read trace fail 1\n");
            a->trace = NULL;
            a->trace_all = NULL;
            a->trace_alloc = 0;
            a->trace_len = 0;
        } else {
            // TRACE SUCCESS
            traceRealloc(a, a->trace_alloc);

            memcpy(a->trace, *p, size_state);
            *p += size_state;
            memcpy(a->trace_all, *p, size_all);
            *p += size_all;
        }
    } else {
        // no or bad trace
        if (a->trace_len > 0) {
            fprintf(stderr, "read trace fail 2\n");
            *p += a->trace_len * (size_state + size_all); // increment pointer not to invalidate state file
        }
        a->trace = NULL;
        a->trace_all = NULL;
        a->trace_len = 0;
        a->trace_alloc = 0;
    }

    if (a->globe_index > GLOBE_MAX_INDEX)
        a->globe_index = -5;

    if (a->addrtype_updated > now)
        a->addrtype_updated = now;

    if (a->seen > now)
        a->seen = 0;

    struct aircraft *old = aircraftGet(a->addr);
    uint32_t hash = aircraftHash(a->addr);
    if (old) {
        struct aircraft **c = (struct aircraft **) &Modes.aircraft[hash];
        while (*c && *c != old) {
            c = &((*c)->next);
        }
        if (*c == old) {
            a->next = old->next;
            *c = a;
            freeAircraft(old);
        } else {
            freeAircraft(a);
            fprintf(stderr, "%06x aircraft replacement failed!\n", old->addr);
        }
    } else {
        a->next = Modes.aircraft[hash];
        Modes.aircraft[hash] = a;
    }

    if (a->trace_next_perm < now) {
        a->trace_next_perm = now + 1 * MINUTES + random() % (5 * MINUTES);
    } else if (a->trace_next_perm > now + GLOBE_PERM_IVAL * 3 / 2) {
        a->trace_next_perm = now + 10 * MINUTES + random() % GLOBE_PERM_IVAL;
    }

    if (a->trace) {
        // let's clean up old points in the trace if necessary
        traceMaintenance(a, now);

        // write the recent trace to /run so it's available in the webinterface
        if (a->position_valid.source != SOURCE_INVALID) {
            a->trace_writeCounter = 0; // avoid full writes here
            a->trace_write |= WRECENT;
            traceWrite(a, now, 1);
        }

        if (a->addr == Modes.leg_focus) {
            scheduleMemBothWrite(a, now);
            fprintf(stderr, "leg_focus: %06x trace len: %d\n", a->addr, a->trace_len);
            traceWrite(a, now, 0);
        }

        // schedule writing all the traces into run so they are present for the webinterface
        scheduleMemBothWrite(a, now + random() % (90 * SECONDS) + (now - a->seen_pos) / (24 * 60 / 4)); // condense 24h into 4 minutes
    }

    int new_index = a->globe_index;
    a->globe_index = -5;
    set_globe_index(a, new_index);
    updateValidities(a, now);

    if (a->onActiveList) {
        a->onActiveList = 1;
        ca_add(&Modes.aircraftActive, a);
    }

    return 0;
}

void *jsonTraceThreadEntryPoint(void *arg) {

    int thread = * (int *) arg;

    srandom(get_seed());

    int part = 0;
    int n_parts = 250;

    // adding 1 means we handle divisions with remainder gracefully
    // just need to check that we don't go out of bounds
    int thread_section_len = AIRCRAFT_BUCKETS / TRACE_THREADS + 1;
    int thread_start = thread * thread_section_len;
    int thread_end = thread_start + thread_section_len;
    if (thread_end > AIRCRAFT_BUCKETS)
        thread_end = AIRCRAFT_BUCKETS;
    //fprintf(stderr, "%d %d\n", thread_start, thread_end);

    // write each part every 5 seconds
    uint64_t sleep_ms = 5 * SECONDS / n_parts;

    pthread_mutex_lock(&Modes.jsonTraceMutex[thread]);

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    while (!Modes.exit) {
        //fprintf(stderr, "%d %d %d\n", part, start, end);
        uint64_t now = mstime();

        // adding 1 means we handle divisions with remainder gracefully
        // just need to check that we don't go out of bounds
        int section_len = thread_section_len / n_parts + 1;
        int start = thread_start + part * section_len;
        int end = start + section_len;
        if (end > thread_end)
            end = thread_end;

        struct timespec start_time;
        start_cpu_timing(&start_time);

        struct aircraft *a;
        for (int j = start; j < end; j++) {
            for (a = Modes.aircraft[j]; a; a = a->next) {
                if (a->trace_write)
                    traceWrite(a, now, 0);
            }
        }

        part++;
        part %= n_parts;

        end_cpu_timing(&start_time, &Modes.stats_current.trace_json_cpu[thread]);

        incTimedwait(&ts, sleep_ms);
        int err = pthread_cond_timedwait(&Modes.jsonTraceCond[thread], &Modes.jsonTraceMutex[thread], &ts);
        if (err && err != ETIMEDOUT)
            fprintf(stderr, "jsonTraceThread: pthread_cond_timedwait unexpected error: %s\n", strerror(err));
    }

    pthread_mutex_unlock(&Modes.jsonTraceMutex[thread]);

    return NULL;
}

static void mark_legs(struct aircraft *a, int start) {
    if (a->trace_len < 20)
        return;
    if (start < 1)
        start = 1;

    int high = 0;
    int low = 100000;

    int last_five[5];
    for (int i = 0; i < 5; i++) { last_five[i] = -1000; }
    uint32_t five_pos = 0;

    double sum = 0;

    struct state *last_leg = NULL;
    struct state *new_leg = NULL;

    for (int i = start; i < a->trace_len; i++) {
        int32_t altitude = a->trace[i].altitude * 25;
        int on_ground = a->trace[i].flags.on_ground;
        int altitude_valid = a->trace[i].flags.altitude_valid;

        if (a->trace[i].flags.leg_marker) {
            a->trace[i].flags.leg_marker = 0;
            // reset leg marker
            last_leg = &a->trace[i];
        }

        if (!altitude_valid)
            continue;

        if (on_ground) {
            int avg = 0;
            for (int i = 0; i < 5; i++) avg += last_five[i];
            avg /= 5;
            altitude = avg;
        } else {
            if (five_pos == 0) {
                for (int i = 0; i < 5; i++)
                    last_five[i] = altitude;
            } else {
                last_five[five_pos % 5] = altitude;
            }
            five_pos++;
        }

        sum += altitude;
    }

    int threshold = (int) (sum / (double) (a->trace_len * 3));

    if (a->addr == Modes.leg_focus) {
        fprintf(stderr, "--------------------------\n");
        fprintf(stderr, "start: %d\n", start);
        fprintf(stderr, "trace_len: %d\n", a->trace_len);
        fprintf(stderr, "threshold: %d\n", threshold);
    }


    if (threshold > 10000)
        threshold = 10000;
    if (threshold < 200)
        threshold = 200;

    high = 0;
    low = 100000;

    uint64_t major_climb = 0;
    uint64_t major_descent = 0;
    int major_climb_index = 0;
    int major_descent_index = 0;
    uint64_t last_high = 0;
    uint64_t last_low = 0;

    int last_low_index = 0;

    uint64_t last_airborne = 0;
    uint64_t last_ground = 0;
    uint64_t first_ground = 0;

    int was_ground = 0;

    for (int i = 0; i < 5; i++)
        last_five[i] = 0;
    five_pos = 0;

    int prev_tmp = start - 1;
    for (int i = start; i < a->trace_len; i++) {
        struct state *state = &a->trace[i];
        int prev_index = prev_tmp;
        struct state *prev = &a->trace[prev_index];

        uint64_t elapsed = state->timestamp - prev->timestamp;

        int32_t altitude = state->altitude * 25;
        //int32_t geom_rate = state->geom_rate * 32;
        //int geom_rate_valid = state->flags.geom_rate_valid;
        //int stale = state->flags.stale;
        int on_ground = state->flags.on_ground;
        int altitude_valid = state->flags.altitude_valid;
        //int gs_valid = state->flags.gs_valid;
        //int track_valid = state->flags.track_valid;
        //int leg_marker = state->flags.leg_marker;
        //
        if (!on_ground && !altitude_valid)
            continue;

        prev_tmp = i;

        if (on_ground) {
            int avg = 0;
            for (int i = 0; i < 5; i++)
                avg += last_five[i];
            avg /= 5;
            altitude = avg - threshold / 8;
            //if (a->addr == Modes.leg_focus) {
            //    fprintf(stderr, "%d\n", altitude);
            //}
        } else {
            if (five_pos == 0) {
                for (int i = 0; i < 5; i++)
                    last_five[i] = altitude;
            } else {
                last_five[five_pos % 5] = altitude;
            }
            five_pos++;
        }

        if (on_ground || was_ground) {
            // count the last point in time on ground to be when the aircraft is received airborn after being on ground
            if (state->timestamp > last_ground + 5 * MINUTES) {
                first_ground = state->timestamp;
            }
            last_ground = state->timestamp;
        } else {
            last_airborne = state->timestamp;
        }

        if (altitude >= high) {
            high = altitude;
        }
        if (altitude <= low) {
            low = altitude;
        }

        /*
        if (state->timestamp > a->trace[i-1].timestamp + 45 * 60 * 1000) {
            high = low = altitude;
        }
        */

        if (abs(low - altitude) < threshold * 1 / 3 && elapsed < 30 * MINUTES) {
            last_low = state->timestamp;
            last_low_index = i;
        }
        if (abs(high - altitude) < threshold * 1 / 3)
            last_high = state->timestamp;

        if (high - low > threshold) {
            if (last_high > last_low) {
                // only set new major climb time if this is after a major descent.
                // then keep that time associated with the climb
                // still report continuation of thta climb
                if (major_climb <= major_descent) {
                    int bla = min(a->trace_len - 1, last_low_index + 3);
                    major_climb = a->trace[bla].timestamp;
                    major_climb_index = bla;
                }
                if (a->addr == Modes.leg_focus) {
                    time_t nowish = major_climb/1000;
                    struct tm utc;
                    gmtime_r(&nowish, &utc);
                    char tstring[100];
                    strftime (tstring, 100, "%H:%M:%S", &utc);
                    fprintf(stderr, "climb: %d %s\n", altitude, tstring);
                }
                low = high - threshold * 9/10;
            } else if (last_high < last_low) {
                int bla = max(0, last_low_index - 3);
                major_descent = a->trace[bla].timestamp;
                major_descent_index = bla;
                if (a->addr == Modes.leg_focus) {
                    time_t nowish = major_descent/1000;
                    struct tm utc;
                    gmtime_r(&nowish, &utc);
                    char tstring[100];
                    strftime (tstring, 100, "%H:%M:%S", &utc);
                    fprintf(stderr, "desc: %d %s\n", altitude, tstring);
                }
                high = low + threshold * 9/10;
            }
        }
        int leg_now = 0;
        if ( (major_descent && (on_ground || was_ground) && elapsed > 25 * 60 * 1000) ||
                (major_descent && (on_ground || was_ground) && state->timestamp > last_airborne + 45 * 60 * 1000)
           )
        {
            if (a->addr == Modes.leg_focus)
                fprintf(stderr, "ground leg (on ground and time between reception > 25 min)\n");
            leg_now = 1;
        }
        double distance = greatcircle(
                (double) a->trace[i].lat / 1E6,
                (double) a->trace[i].lon / 1E6,
                (double) a->trace[i-1].lat / 1E6,
                (double) a->trace[i-1].lon / 1E6
                );

        if (elapsed > 30 * 60 * 1000 && distance < 10E3 * (elapsed / (30 * 60 * 1000.0)) && distance > 1) {
            leg_now = 1;
            if (a->addr == Modes.leg_focus)
                fprintf(stderr, "time/distance leg, elapsed: %0.fmin, distance: %0.f\n", elapsed / (60 * 1000.0), distance / 1000.0);
        }

        int leg_float = 0;
        if (major_climb && major_descent && major_climb > major_descent + 8 * MINUTES) {
            for (int i = major_descent_index + 1; i < major_climb_index; i++) {
                if (a->trace[i].timestamp > a->trace[i - 1].timestamp + 5 * MINUTES) {
                    leg_float = 1;
                    if (a->addr == Modes.leg_focus)
                        fprintf(stderr, "float leg: 8 minutes between descent / climb, 5 minute reception gap in between somewhere\n");
                }
            }
        }
        if (major_climb && major_descent
                && major_climb > major_descent + 1 * MINUTES
                && last_ground >= major_descent
                && last_ground > first_ground + 1 * MINUTES
           ) {
            leg_float = 1;
            if (a->addr == Modes.leg_focus)
                fprintf(stderr, "float leg: 1 minutes between descent / climb, 1 minute on ground\n");
        }


        if (leg_float || leg_now)
        {
            uint64_t leg_ts = 0;

            if (leg_now) {
                new_leg = &a->trace[prev_index + 1];
                for (int k = prev_index + 1; k < i; k++) {
                    struct state *state = &a->trace[i];
                    struct state *last = &a->trace[i - 1];

                    if (state->timestamp > last->timestamp + 5 * 60 * 1000) {
                        new_leg = state;
                        break;
                    }
                }
            } else if (major_descent_index + 1 == major_climb_index) {
                new_leg = &a->trace[major_climb_index];
            } else {
                for (int i = major_climb_index; i > major_descent_index; i--) {
                    struct state *state = &a->trace[i];
                    struct state *last = &a->trace[i - 1];

                    if (state->timestamp > last->timestamp + 5 * 60 * 1000) {
                        new_leg = state;
                        break;
                    }
                }
                uint64_t half = major_descent + (major_climb - major_descent) / 2;
                for (int i = major_descent_index + 1; i < major_climb_index; i++) {
                    struct state *state = &a->trace[i];

                    if (state->timestamp > half) {
                        new_leg = state;
                        break;
                    }
                }
            }

            if (new_leg) {
                leg_ts = new_leg->timestamp;
                new_leg->flags.leg_marker = 1;
                // set leg marker
            }

            major_climb = 0;
            major_climb_index = 0;
            major_descent = 0;
            major_descent_index = 0;
            low += threshold;
            high -= threshold;

            if (a->addr == Modes.leg_focus) {
                if (new_leg) {
                    time_t nowish = leg_ts/1000;
                    struct tm utc;
                    gmtime_r(&nowish, &utc);
                    char tstring[100];
                    strftime (tstring, 100, "%H:%M:%S", &utc);
                    fprintf(stderr, "leg: %s\n", tstring);
                } else {
                    time_t nowish = state->timestamp/1000;
                    struct tm utc;
                    gmtime_r(&nowish, &utc);
                    char tstring[100];
                    strftime (tstring, 100, "%H:%M:%S", &utc);
                    fprintf(stderr, "resetting major_c/d without leg: %s\n", tstring);
                }
            }
        }

        was_ground = on_ground;
    }
    if (last_leg != new_leg) {
        //a->trace_next_mw = 0;
        //fprintf(stderr, "%06x\n", a->addr);
    }
}

void ca_init (struct craftArray *ca) {
    //*ca = (struct craftArray) {0};
    pthread_mutex_init(&ca->mutex , NULL);
}

void ca_destroy (struct craftArray *ca) {
    if (ca->list)
        free(ca->list);
    pthread_mutex_destroy(&ca->mutex);
    *ca = (struct craftArray) {0};
}

void ca_add (struct craftArray *ca, struct aircraft *a) {
    pthread_mutex_lock(&ca->mutex);
    if (ca->alloc == 0) {
        ca->alloc = 64;
        ca->list = realloc(ca->list, ca->alloc * sizeof(struct aircraft *));
    }
    // + 32 ... some arbitrary buffer for concurrent stuff with limited locking
    if (ca->len + 32 >= ca->alloc) {
        ca->alloc = ca->alloc * 3 / 2;
        ca->list = realloc(ca->list, ca->alloc * sizeof(struct aircraft *));
    }
    if (!ca->list) {
        fprintf(stderr, "ca_add(): out of memory!\n");
        exit(1);
    }
    /*
    for (int i = 0; i < ca->len; i++) {
        if (a == ca->list[i]) {
            pthread_mutex_lock(&ca->mutex);
            // re-check under mutex
            if (a == ca->list[i]) {
                fprintf(stderr, "ca_add(): double add!\n");
                pthread_mutex_unlock(&ca->mutex);
                return;
            }
            pthread_mutex_unlock(&ca->mutex);
        }
    }
    */

    ca->list[ca->len] = a;  // add at the end
    ca->len++;
    pthread_mutex_unlock(&ca->mutex);
}

void ca_remove (struct craftArray *ca, struct aircraft *a) {
    pthread_mutex_lock(&ca->mutex);
    if (!ca->list) {
        pthread_mutex_unlock(&ca->mutex);
        return;
    }
    int found = 0;
    pthread_mutex_unlock(&ca->mutex);
    for (int i = 0; i < ca->len; i++) {
        if (ca->list[i] == a) {
            pthread_mutex_lock(&ca->mutex);
            // re-check under mutex
            if (ca->list[i] == a) {
                // replace with last element in array
                ca->list[i] = ca->list[ca->len - 1];
                ca->list[ca->len - 1] = NULL;
                ca->len--;
                i--;
                if (found)
                    fprintf(stderr, "<3>hex: %06x, ca_remove(): pointer found twice in array!\n", a->addr);
                found = 1;
            }
            pthread_mutex_unlock(&ca->mutex);
        }
    }
    if (!found)
        fprintf(stderr, "<3>hex: %06x, ca_remove(): pointer not in array!\n", a->addr);
}

void set_globe_index (struct aircraft *a, int new_index) {

    if (!Modes.json_globe_index)
        return;

    int old_index = a->globe_index;
    a->globe_index = new_index;

    if (old_index == new_index)
        return;
    if (new_index > GLOBE_MAX_INDEX || old_index > GLOBE_MAX_INDEX) {
        fprintf(stderr, "hex: %06x, old_index: %d, new_index: %d, GLOBE_MAX_INDEX: %d\n", a->addr, Modes.globeLists[new_index].len, new_index, GLOBE_MAX_INDEX );
        return;
    }
    if (old_index >= 0) {
        ca_remove(&Modes.globeLists[old_index], a);
    }
    if (new_index >= 0) {
        ca_add(&Modes.globeLists[new_index], a);
    }
}

static void traceUnlink(struct aircraft *a) {
    char filename[PATH_MAX];

    if (!Modes.json_globe_index || !Modes.json_dir)
        return;

    snprintf(filename, 1024, "%s/traces/%02x/trace_recent_%s%06x.json", Modes.json_dir, a->addr % 256, (a->addr & MODES_NON_ICAO_ADDRESS) ? "~" : "", a->addr & 0xFFFFFF);
    unlink(filename);

    snprintf(filename, 1024, "%s/traces/%02x/trace_full_%s%06x.json", Modes.json_dir, a->addr % 256, (a->addr & MODES_NON_ICAO_ADDRESS) ? "~" : "", a->addr & 0xFFFFFF);
    unlink(filename);

    //fprintf(stderr, "unlink %06x: %s\n", a->addr, filename);
}

ssize_t stateBytes(int len) {
    return len * sizeof(struct state);
}
ssize_t stateAllBytes(int len) {
    return (len + 3) / 4 * sizeof(struct state_all);
}

void traceRealloc(struct aircraft *a, int len) {
    if (len == 0) {
        traceCleanup(a);
        return;
    }
    a->trace_alloc = len;
    if (a->trace_alloc > TRACE_SIZE)
        a->trace_alloc = TRACE_SIZE;
    a->trace = realloc(a->trace, stateBytes(a->trace_alloc));
    a->trace_all = realloc(a->trace_all, stateAllBytes(a->trace_alloc));

    if (a->trace_len >= TRACE_SIZE / 2)
        fprintf(stderr, "Quite a long trace: %06x (%d).\n", a->addr, a->trace_len);
}

static void tracePrune(struct aircraft *a, uint64_t now) {
    if (a->trace_alloc == 0) {
        return;
    }
    if (a->trace_len == 0) { // this shouldn't ever trigger
        traceCleanup(a);
        return;
    }

    uint64_t keep_after = now - Modes.keep_traces;

    int new_start = -1;
    // throw out oldest values if approaching max trace size
    if (a->trace_len + TRACE_MARGIN >= TRACE_SIZE) {
        new_start = TRACE_SIZE / 64 + 2 * TRACE_MARGIN;
    } else if (a->trace->timestamp < keep_after - 30 * MINUTES)  {
        new_start = a->trace_len;
        for (int i = 0; i < a->trace_len; i++) {
            struct state *state = &a->trace[i];
            if (state->timestamp > keep_after) {
                new_start = i;
                break;
            }
        }
    }

    if (new_start != -1) {

        if (new_start >= a->trace_len) {
            traceCleanup(a);
            // if the trace length was reduced to zero, the trace is deleted from run
            // it is not written again until a new position is received
            return;
        }

        // make sure we keep state and state_all together
        new_start -= (new_start % 4);

        if (new_start % 4 != 0)
            fprintf(stderr, "not divisible by 4: %d %d\n", new_start, a->trace_len);


        a->trace_len -= new_start;

        // carry over buffered position as well if present
        int len = a->trace_len + (a->tracePosBuffered ? 1 : 0);

        memmove(a->trace, a->trace + new_start, stateBytes(len));
        memmove(a->trace_all, a->trace_all + stateAllBytes(new_start) / sizeof(struct state_all), stateAllBytes(len));

        // invalidate traceCache
        free(a->traceCache);
        a->traceCache = NULL;
    }
}

int traceUsePosBuffered(struct aircraft *a) {
    if (a->tracePosBuffered) {
        a->tracePosBuffered = 0;
        // bookkeeping:
        a->trace_len++;
        a->trace_write |= WRECENT;
        a->trace_writeCounter++;
        return 1;
    } else {
        return 0;
    }
}

void traceCleanup(struct aircraft *a) {
    free(a->trace);
    free(a->trace_all);

    a->tracePosBuffered = 0;
    a->trace_len = 0;
    a->trace_alloc = 0;
    a->trace = NULL;
    a->trace_all = NULL;

    free(a->traceCache);
    a->traceCache = NULL;

    traceUnlink(a);
}


void traceMaintenance(struct aircraft *a, uint64_t now) {
    if (!a->trace_alloc)
        return;

    //fprintf(stderr, "%06x\n", a->addr);

    // throw out old data if older than keep_trace or trace is getting full
    tracePrune(a, now);

    if (Modes.json_globe_index) {
        if (now > a->trace_next_perm)
            a->trace_write |= WPERM;
        if (now > a->trace_next_mw)
            a->trace_write |= WMEM;
    }

    // free trace cache for inactive aircraft
    if (a->traceCache && now > a->seen_pos + TRACE_CACHE_LIFETIME) {
        //fprintf(stderr, "%06x free traceCache\n", a->addr);
        free(a->traceCache);
        a->traceCache = NULL;
    }

    // on day change write out the traces for yesterday
    // for which day and which time span is written is determined by traceday
    if (a->traceWrittenForYesterday != Modes.triggerPermWriteDay) {
        a->traceWrittenForYesterday = Modes.triggerPermWriteDay;
        if (a->addr == TRACE_FOCUS)
            fprintf(stderr, "schedule_perm\n");

        a->trace_next_perm = now + random() % (5 * MINUTES);
    }

    int oldAlloc = a->trace_alloc;

    // shrink allocation if necessary
    int shrink = 0;
    int shrinkTo = (a->trace_alloc - TRACE_MARGIN) * 3 / 4;
    if (a->trace_len && a->trace_len + 2 * TRACE_MARGIN <= shrinkTo) {
        traceRealloc(a, shrinkTo);
        shrink = 1;
    }

    int midAlloc = a->trace_alloc;
    // grow allocation if necessary
    if (a->trace_alloc && a->trace_len + TRACE_MARGIN >= a->trace_alloc) {
        traceRealloc(a, a->trace_alloc * 4 / 3 + TRACE_MARGIN);
    }

    if (shrink && a->trace_alloc != midAlloc) {
        fprintf(stderr, "%06x: shrink - grow: trace_len: %d traceRealloc: %d -> %d -> %d\n", a->addr, a->trace_len, oldAlloc, midAlloc, a->trace_alloc);
    }

    if (Modes.debug_traceAlloc && a->trace_alloc != oldAlloc) {
        fprintf(stderr, "%06x: grow: trace_len: %d traceRealloc: %d -> %d\n", a->addr, a->trace_len, oldAlloc, a->trace_alloc);
    }
}


int traceAdd(struct aircraft *a, uint64_t now) {
    if (!Modes.keep_traces)
        return 0;

    int traceDebug = (a->addr == Modes.trace_focus);


    int posUsed = 0;
    int bufferedPosUsed = 0;
    double distance = 0;
    int64_t elapsed = 0;
    int64_t elapsed_buffered = 0;

    int64_t min_elapsed = TRACE_MIN_ELAPSED;
    int64_t max_elapsed = Modes.json_trace_interval;
    float turn_density = 4.5;

    float max_speed_diff = 5.0;
    int max_alt_diff = 200;

    if (trackVState(now, &a->altitude_baro_valid, &a->position_valid) && a->altitude_baro > 10000) {
        max_speed_diff = 10.0;
    }

    if (a->position_valid.source == SOURCE_MLAT) {
        min_elapsed *= 2;
        turn_density /= 2;
        max_elapsed *= 0.75;
        max_speed_diff = 30;
    }
    // some towers on MLAT .... create unnecessary data
    if (a->squawk_valid.source != SOURCE_INVALID && a->squawk == 0x7777) {
        min_elapsed += 60 * SECONDS;
    }

    for (int i = max(0, a->trace_len - 6); i < a->trace_len; i++) {
        if ( (int32_t) (a->lat * 1E6) == a->trace[i].lat
                && (int32_t) (a->lon * 1E6) == a->trace[i].lon ) {
            return 0;
        }
    }

    int on_ground = 0;
    float track = a->track;
    int track_valid = trackVState(now, &a->track_valid, &a->position_valid);
    int stale = 0;

    if (now > a->seenPosReliable + TRACE_STALE + 2 * SECONDS) {
        stale = 1;
    }

    int agValid = 0;
    if (trackDataValid(&a->airground_valid) && a->airground_valid.source >= SOURCE_MODE_S_CHECKED) {
        agValid = 1;
        if (a->airground == AG_GROUND) {
            on_ground = 1;
            if (trackVState(now, &a->true_heading_valid, &a->position_valid)) {
                track = a->true_heading;
                track_valid = 1;
            } else {
                track_valid = 0;
            }
        }
    }

    if (a->trace_len == 0 )
        goto save_state;

    struct state *last = &(a->trace[a->trace_len-1]);

    if (a->tracePosBuffered) {
        elapsed_buffered = (int64_t) a->trace[a->trace_len].timestamp - (int64_t) last->timestamp;
    }

    int alt = a->altitude_baro;
    int32_t last_alt = last->altitude * 25;

    int alt_diff = 0;
    if (trackVState(now, &a->altitude_baro_valid, &a->position_valid) && a->alt_reliable >= ALTITUDE_BARO_RELIABLE_MAX / 5) {
        alt_diff = abs(a->altitude_baro - last_alt);
    }

    if (now >= last->timestamp)
        elapsed = now - last->timestamp;

    float speed_diff = 0;
    if (trackDataValid(&a->gs_valid) && last->flags.gs_valid)
        speed_diff = fabs(last->gs / 10.0 - a->gs);

    // keep the last air ground state if the current isn't valid
    if (!agValid) {
        on_ground = last->flags.on_ground;
    }
    if (on_ground) {
        // just do this twice so we cover the first point in a trace as well as using the last airground state
        if (trackVState(now, &a->true_heading_valid, &a->position_valid)) {
            track = a->true_heading;
            track_valid = 1;
        } else {
            track_valid = 0;
        }
    }

    float track_diff = 0;
    if (last->flags.track_valid && track_valid) {
        track_diff = fabs(track - last->track / 10.0);
        if (track_diff > 180)
            track_diff = 360 - track_diff;
    }


    distance = greatcircle(last->lat / 1E6, last->lon / 1E6, a->lat, a->lon);

    if (distance < 5)
        traceDebug = 0;

    if (traceDebug) {
        fprintf(stderr, "%5.1fs d:%5.0f a:%6d D%4d s:%4.0f D%3.0f t: %5.1f D%5.1f ",
                elapsed / 1000.0,
                distance, alt, alt_diff, a->gs, speed_diff, a->track, track_diff);
    }

    // record more points when the altitude changes very quickly
    if (alt_diff >= max_alt_diff && elapsed <= min_elapsed) {
        goto save_state;
    }

    if (speed_diff > max_speed_diff) {
        if (traceDebug) fprintf(stderr, "speed_change: %0.1f %0.1f -> %0.1f", fabs(last->gs / 10.0 - a->gs), last->gs / 10.0, a->gs);
        goto save_state;
    }

    // record ground air state changes precisely
    if (on_ground != last->flags.on_ground) {
        goto save_state;
    }

    // record non moving targets every 5 minutes
    if (elapsed > 10 * max_elapsed)
        goto save_state;

    // don't record non moving targets
    if (distance < 35)
        goto no_save_state;

    // record trace precisely if we have a TCAS advisory
    if (trackDataValid(&a->acas_ra_valid) && trackDataAge(now, &a->acas_ra_valid) < 15 * SECONDS) {
        goto save_state;
    }

    // don't record unnecessary many points
    if (elapsed < min_elapsed)
        goto no_save_state;

    // even if the squawk gets invalid we continue to record more points
    if (a->squawk == 0x7700 && elapsed > 2 * min_elapsed) {
        goto save_state;
    }

    if (!on_ground && elapsed > max_elapsed) // default 30000 ms
        goto save_state;

    if (on_ground && elapsed > 4 * max_elapsed)
        goto save_state;

    if (stale) {
        // save a point if reception is spotty so we can mark track as spotty on display
        goto save_state;
    }

    if (on_ground) {
        if (distance * track_diff > 250) {
            if (traceDebug) fprintf(stderr, "track_change: %0.1f %0.1f -> %0.1f", track_diff, last->track / 10.0, a->track);
            goto save_state;
        }

        if (distance > 400)
            goto save_state;
    }

    if (track_diff > 0.5
            && (elapsed / 1000.0 * track_diff * turn_density > 100.0)
       ) {
        if (traceDebug) fprintf(stderr, "track_change: %0.1f %0.1f -> %0.1f", track_diff, last->track / 10.0, a->track);
        goto save_state;
    }

    if (trackVState(now, &a->altitude_baro_valid, &a->position_valid)
            && a->alt_reliable >= ALTITUDE_BARO_RELIABLE_MAX / 5) {
        if (!last->flags.altitude_valid) {
            goto save_state;
        }
        if (last->flags.altitude_valid) {

            int div = 500;

            if (alt > 8000) {
                div = 500;
            }

            if (alt > 4000 && alt <= 8000) {
                div = 250;
            }
            if (alt <= 4000) {
                div = 125;
            }

            int offset = div / 2;
            int alt_add = (alt >= 0) ? offset : (-1 * offset);
            int last_alt_add = (last_alt >= 0) ? offset : (-1 * offset);

            // think of this simpler equation for altitudes that are > 0, div 500 and offset 250
            // abs((alt + 250)/500 - (last_alt + 250)/500) >= 1
            // we are basically detecting changes between altitude slices or divs / divisions
            int divDelta = abs((alt + alt_add)/div - (last_alt + last_alt_add)/div);
            if (divDelta >= 1 && alt_diff >= div / 2) {
                if (traceDebug) fprintf(stderr, "alt_change1: %d -> %d", last_alt, alt);

                if (divDelta >= 2)
                    goto save_state;
                else
                    goto save_state_no_buf;
            }

            if (alt_diff >= 25 && elapsed > (1000 * 22 * div / alt_diff)) {
                if (traceDebug) fprintf(stderr, "alt_change2: %d -> %d, %d", last_alt, alt, (1000 * 24 * div / alt_diff));
                goto save_state_no_buf;
            }
        }
    }

    goto no_save_state;

save_state:
    // always try using the buffered position instead of the current one
    // this should provide a better picture of changing track / speed / altitude

    if (elapsed_buffered > min_elapsed * 3 / 2 && traceUsePosBuffered(a)) {
        posUsed = 0;
        bufferedPosUsed = 1;
    } else {
        posUsed = 1;
    }
    //fprintf(stderr, "traceAdd: %06x elapsed: %8.1f s distance: %8.3f km\n", a->addr, elapsed / 1000.0, distance / 1000.0);
    goto no_save_state;

save_state_no_buf:
    posUsed = 1;

no_save_state:

    if (!a->trace || !a->trace_len) {
        // allocate trace memory
        traceRealloc(a, 2 * TRACE_MARGIN);
        a->trace->timestamp = now;
        scheduleMemBothWrite(a, now); // rewrite full history file
        a->trace_next_perm = now + GLOBE_PERM_IVAL / 2; // schedule perm write

        //fprintf(stderr, "%06x: new trace\n", a->addr);
    }
    if (a->trace_len + 1 >= a->trace_alloc) {
        static uint64_t antiSpam;
        if (Modes.debug_traceAlloc || now > antiSpam + 5 * SECONDS) {
            fprintf(stderr, "%06x: trace_alloc insufficient: trace_len %d trace_alloc %d\n",
                    a->addr, a->trace_len, a->trace_alloc);
            antiSpam = now;
        }
        return 0;
    }

    struct state *new = &(a->trace[a->trace_len]);
    memset(new, 0, sizeof(struct state));

    new->lat = (int32_t) nearbyint(a->lat * 1E6);
    new->lon = (int32_t) nearbyint(a->lon * 1E6);
    new->timestamp = now;


    /*
       unsigned on_ground:1;
       unsigned stale:1;
       unsigned leg_marker:1;
       unsigned altitude_valid:1;
       unsigned gs_valid:1;
       unsigned track_valid:1;
       unsigned rate_valid:1;
       unsigned rate_geom:1;
       */


    if (stale)
        new->flags.stale = 1;

    if (on_ground)
        new->flags.on_ground = 1;

    if (trackVState(now, &a->altitude_baro_valid, &a->position_valid)
            && a->alt_reliable >= ALTITUDE_BARO_RELIABLE_MAX / 5) {
        new->flags.altitude_valid = 1;
        new->altitude = (int16_t) nearbyint(a->altitude_baro / 25.0);
    } else if (trackVState(now, &a->altitude_geom_valid, &a->position_valid)) {
        new->flags.altitude_valid = 1;
        new->flags.altitude_geom = 1;
        new->altitude = (int16_t) nearbyint(a->altitude_geom / 25.0);
    }
    if (trackVState(now, &a->gs_valid, &a->position_valid)) {
        new->flags.gs_valid = 1;
        new->gs = (int16_t) nearbyint(10 * a->gs);
    }

    if (trackVState(now, &a->geom_rate_valid, &a->position_valid)) {
        new->flags.rate_valid = 1;
        new->flags.rate_geom = 1;
        new->rate = (int16_t) nearbyint(a->geom_rate / 32.0);
    } else if (trackVState(now, &a->baro_rate_valid, &a->position_valid)) {
        new->flags.rate_valid = 1;
        new->flags.rate_geom = 0;
        new->rate = (int16_t) nearbyint(a->baro_rate / 32.0);
    } else {
        new->rate = 0;
        new->flags.rate_valid = 0;
    }

    if (track_valid) {
        new->track = (int16_t) nearbyint(10 * track);
        new->flags.track_valid = 1;
    }
    // trace_all stuff:

    if (a->trace_len % 4 == 0) {
        struct state_all *new_all = &(a->trace_all[a->trace_len/4]);
        memset(new_all, 0, sizeof(struct state_all));

        to_state_all(a, new_all, now);
    }

    if (posUsed) {
        if (traceDebug) fprintf(stderr, " normal\n");
        a->tracePosBuffered = 0;
        // bookkeeping:
        a->trace_len++;
        a->trace_write |= WRECENT;
        a->trace_writeCounter++;
    } else {
        a->tracePosBuffered = 1;
    }
    if (bufferedPosUsed) {
        if (traceDebug) fprintf(stderr, " buffer\n");
        // in some cases we want to add a 2nd point right now.
        traceAdd(a, now);
    }
    if (traceDebug && !posUsed && !bufferedPosUsed) fprintf(stderr, "none\n");

    return posUsed || bufferedPosUsed;
}


void save_blob(int blob) {
    if (!Modes.state_dir)
        return;
    //static int count;
    //fprintf(stderr, "Save blob: %02x, count: %d\n", blob, ++count);
    if (blob < 0 || blob > STATE_BLOBS)
        fprintf(stderr, "save_blob: invalid argument: %02x", blob);

    int gzip = 1;

    char filename[PATH_MAX];
    char tmppath[PATH_MAX];
    if (gzip) {
        snprintf(filename, 1024, "%s/blob_%02x", Modes.state_dir, blob);
        unlink(filename);
        snprintf(filename, 1024, "%s/blob_%02x.gz", Modes.state_dir, blob);
    } else {
        snprintf(filename, 1024, "%s/blob_%02x.gz", Modes.state_dir, blob);
        unlink(filename);
        snprintf(filename, 1024, "%s/blob_%02x", Modes.state_dir, blob);
    }
    snprintf(tmppath, PATH_MAX, "%s.tmp", filename);

    int fd = open(tmppath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        fprintf(stderr, "open failed:");
        perror(tmppath);
        return;
    }
    gzFile gzfp = NULL;
    if (gzip) {
        int res;
        gzfp = gzdopen(fd, "wb");
        if (!gzfp) {
            fprintf(stderr, "gzdopen failed:");
            perror(tmppath);
            close(fd);
            return;
        }
        if (gzbuffer(gzfp, GZBUFFER_BIG) < 0)
            fprintf(stderr, "gzbuffer fail");
        res = gzsetparams(gzfp, 1, Z_DEFAULT_STRATEGY);
        if (res < 0)
            fprintf(stderr, "gzsetparams fail: %d", res);
    }

    int stride = AIRCRAFT_BUCKETS / STATE_BLOBS;
    int start = stride * blob;
    int end = start + stride;

    uint64_t magic = 0x7ba09e63757913eeULL;

    int alloc = max(16 * 1024 * 1024, (stateBytes(TRACE_SIZE) + stateAllBytes(TRACE_SIZE)));
    unsigned char *buf = malloc(alloc);
    unsigned char *p = buf;

    for (int j = start; j < end; j++) {
        for (struct aircraft *a = Modes.aircraft[j]; a; a = a->next) {
            int trace_len = a->trace_len;
            if (a->tracePosBuffered)
                trace_len++; // use buffered position for saving state

            int size_state = stateBytes(trace_len);
            int size_all = stateAllBytes(trace_len);


            if (p + sizeof(magic) + size_state + size_all + sizeof(struct aircraft) >= buf + alloc) {
                //fprintf(stderr, "save_blob writing %d KB (buffer)\n", (int) ((p - buf) / 1024));
                if (gzip) {
                    writeGz(gzfp, buf, p - buf, tmppath);
                } else {
                    check_write(fd, buf, p - buf, tmppath);
                }

                p = buf;
            }

            memcpy(p, &magic, sizeof(magic));
            p += sizeof(magic);

            if (p + size_state + size_all + sizeof(struct aircraft) >= buf + alloc) {
                fprintf(stderr, "%06x: Couldn't write internal state, check save_blob code!\n", a->addr);
            } else {
                memcpy(p, a, sizeof(struct aircraft));
                struct aircraft *b = (struct aircraft *) p;
                b->trace_len = trace_len; // correct trace_len for buffered position
                b->tracePosBuffered = 0;
                p += sizeof(struct aircraft);
                if (trace_len > 0) {
                    memcpy(p, a->trace, size_state);
                    p += size_state;
                    memcpy(p, a->trace_all, size_all);
                    p += size_all;
                }
            }
        }
    }
    magic--;
    memcpy(p, &magic, sizeof(magic));
    p += sizeof(magic);

    //fprintf(stderr, "save_blob writing %d KB (end)\n", (int) ((p - buf) / 1024));
    if (gzip) {
        writeGz(gzfp, buf, p - buf, tmppath);
    } else {
        check_write(fd, buf, p - buf, tmppath);
    }
    p = buf;

    if (gzfp)
        gzclose(gzfp);
    else if (fd != -1)
        close(fd);

    if (rename(tmppath, filename) == -1) {
        fprintf(stderr, "save_blob rename(): %s -> %s", tmppath, filename);
        perror("");
        unlink(tmppath);
    }

    free(buf);
}
void *load_blobs(void *arg) {
    int thread_number = *((int *) arg);
    srandom(get_seed());
    for (int j = 0; j < STATE_BLOBS; j++) {
        if (j % IO_THREADS != thread_number)
           continue;
        load_blob(j);
    }
    return NULL;
}

static void load_blob(int blob) {
    //fprintf(stderr, "load blob %d\n", blob);
    if (blob < 0 || blob >= STATE_BLOBS)
        fprintf(stderr, "load_blob: invalid argument: %d", blob);
    uint64_t magic = 0x7ba09e63757913eeULL;
    char filename[1024];
    uint64_t now = mstime();
    int fd = -1;
    struct char_buffer cb;
    char *p;
    char *end;

    snprintf(filename, 1024, "%s/blob_%02x.gz", Modes.state_dir, blob);
    gzFile gzfp = gzopen(filename, "r");
    if (gzfp) {
        cb = readWholeGz(gzfp, filename);
        gzclose(gzfp);
    } else {
        snprintf(filename, 1024, "%s/blob_%02x", Modes.state_dir, blob);
        fd = open(filename, O_RDONLY);
        if (fd == -1) {
            fprintf(stderr, "missing state blob:");
            snprintf(filename, 1024, "%s/blob_%02x[.gz]", Modes.state_dir, blob);
            perror(filename);
            return;
        }
        cb = readWholeFile(fd, filename);
        close(fd);
    }
    if (!cb.buffer)
        return;
    p = cb.buffer;
    end = p + cb.len;

    while (end - p > 0) {
        uint64_t value = 0;
        if (end - p >= (long) sizeof(value)) {
            value = *((uint64_t *) p);
            p += sizeof(value);
        }

        if (value != magic) {
            if (value != magic - 1)
                fprintf(stderr, "Incomplete state file: %s\n", filename);
            break;
        }
        load_aircraft(&p, end, now);
    }
    free(cb.buffer);
}


int handleHeatmap(uint64_t now) {
    if (!Modes.heatmap)
        return 0;

    time_t nowish = (now - 30 * MINUTES)/1000;
    struct tm utc;
    gmtime_r(&nowish, &utc);
    int half_hour = utc.tm_hour * 2 + utc.tm_min / 30;

    if (Modes.heatmap_current_interval < -1) {
        Modes.heatmap_current_interval++;
        return 0;
        // startup delay before first time heatmap is written
    }

    // don't write on startup when persistent state isn't enabled
    if (!Modes.state_dir && Modes.heatmap_current_interval < 0) {
        Modes.heatmap_current_interval = half_hour;
        return 0;
    }
    // only do this every 30 minutes.
    if (half_hour == Modes.heatmap_current_interval)
        return 0;

    Modes.heatmap_current_interval = half_hour;

    utc.tm_hour = half_hour / 2;
    utc.tm_min = 30 * (half_hour % 2);
    utc.tm_sec = 0;
    uint64_t start = 1000 * (uint64_t) (timegm(&utc));
    uint64_t end = start + 30 * MINUTES;
    int num_slices = (30 * MINUTES) / Modes.heatmap_interval;


    char pathbuf[PATH_MAX];
    char tmppath[PATH_MAX];
    int len = 0;
    int len2 = 0;
    int alloc = 1 * 1024 * 1024;
    struct heatEntry *buffer = malloc(alloc * sizeof(struct heatEntry));
    struct heatEntry *buffer2 = malloc(alloc * sizeof(struct heatEntry));
    int *slices = malloc(alloc * sizeof(int));
    struct heatEntry index[num_slices];

    for (int j = 0; j < AIRCRAFT_BUCKETS; j++) {
        for (struct aircraft *a = Modes.aircraft[j]; a; a = a->next) {
            if (a->addr & MODES_NON_ICAO_ADDRESS) continue;
            if (a->trace_len == 0) continue;

            struct state *trace = a->trace;
            uint64_t next = start;
            int slice = 0;
            uint32_t squawk = 0x8888; // impossible squawk
            uint64_t callsign = 0; // quackery

            for (int i = 0; i < a->trace_len; i++) {
                if (len >= alloc)
                    break;
                if (trace[i].timestamp > end)
                    break;
                if (trace[i].timestamp > start && i % 4 == 0) {
                    struct state_all *all = &(a->trace_all[i/4]);
                    uint64_t *cs = (uint64_t *) &(all->callsign);
                    if (*cs != callsign || squawk != all->squawk) {

                        callsign = *cs;
                        squawk = all->squawk;

                        uint32_t s = all->squawk;
                        int32_t d = (s & 0xF) + 10 * ((s & 0xF0) >> 4) + 100 * ((s & 0xF00) >> 8) + 1000 * ((s & 0xF000) >> 12);
                        buffer[len].hex = a->addr;
                        buffer[len].lat = (1 << 30) | d;

                        memcpy(&buffer[len].lon, all->callsign, 8);

                        //if (a->addr == Modes.leg_focus) {
                        //    fprintf(stderr, "squawk: %d %04x\n", d, s);
                        //}

                        slices[len] = slice;
                        len++;
                    }
                }
                if (trace[i].timestamp < next)
                    continue;
                if (!trace[i].flags.altitude_valid)
                    continue;

                while (trace[i].timestamp > next + Modes.heatmap_interval) {
                    next += Modes.heatmap_interval;
                    slice++;
                }

                buffer[len].hex = a->addr;
                buffer[len].lat = trace[i].lat;
                buffer[len].lon = trace[i].lon;

                if (!trace[i].flags.on_ground)
                    buffer[len].alt = trace[i].altitude;
                else
                    buffer[len].alt = -123; // on ground

                if (trace[i].flags.gs_valid)
                    buffer[len].gs = trace[i].gs;
                else
                    buffer[len].gs = -1; // invalid

                slices[len] = slice;

                len++;

                next += Modes.heatmap_interval;
                slice++;
            }
        }
    }

    for (int i = 0; i < num_slices; i++) {
        struct heatEntry specialSauce = (struct heatEntry) {0};
        uint64_t slice_stamp = start + i * Modes.heatmap_interval;
        specialSauce.hex = 0xe7f7c9d;
        specialSauce.lat = slice_stamp >> 32;
        specialSauce.lon = slice_stamp & ((1ULL << 32) - 1);
        specialSauce.alt = Modes.heatmap_interval;

        index[i] = (struct heatEntry) {0};
        index[i].hex = len2 + num_slices;
        buffer2[len2++] = specialSauce;

        for (int k = 0; k < len; k++) {
            if (slices[k] == i)
                buffer2[len2++] = buffer[k];
        }
    }
    char *base_dir = Modes.globe_history_dir;
    if (Modes.heatmap_dir) {
        base_dir = Modes.heatmap_dir;
    }

    if (mkdir(base_dir, 0755) && errno != EEXIST)
        perror(base_dir);

    char dateDir[PATH_MAX * 3/4];

    createDateDir(base_dir, &utc, dateDir);

    snprintf(pathbuf, PATH_MAX, "%s/heatmap", dateDir);
    if (mkdir(pathbuf, 0755) && errno != EEXIST)
        perror(pathbuf);

    snprintf(pathbuf, PATH_MAX, "%s/heatmap/%02d.bin.ttf", dateDir, half_hour);
    snprintf(tmppath, PATH_MAX, "%s/heatmap/temp_%lx_%lx", dateDir, random(), random());

    //fprintf(stderr, "%s using %d positions\n", pathbuf, len);

    int fd = open(tmppath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror(tmppath);
    } else {
        int res;
        gzFile gzfp = gzdopen(fd, "wb");
        if (!gzfp)
            fprintf(stderr, "heatmap: gzdopen fail");
        if (gzbuffer(gzfp, GZBUFFER_BIG) < 0)
            fprintf(stderr, "gzbuffer fail");
        res = gzsetparams(gzfp, 9, Z_DEFAULT_STRATEGY);
        if (res < 0)
            fprintf(stderr, "gzsetparams fail: %d", res);
        ssize_t toWrite = sizeof(index);
        writeGz(gzfp, index, toWrite, tmppath);

        toWrite = len2 * sizeof(struct heatEntry);
        writeGz(gzfp, buffer2, toWrite, tmppath);

        gzclose(gzfp);
    }
    if (rename(tmppath, pathbuf) == -1) {
        fprintf(stderr, "heatmap rename(): %s -> %s", tmppath, pathbuf);
        perror("");
    }

    free(buffer);
    free(buffer2);
    free(slices);

    return 1;
}

static void gzip(char *file) {
    int fd;
    char fileGz[PATH_MAX];
    gzFile gzfp;

    // read uncompressed file into buffer
    fd = open(file, O_RDONLY);
    if (fd < 0)
        return;
    struct char_buffer cb = readWholeFile(fd, file);
    close(fd);
    if (!cb.buffer) {
        fprintf(stderr, "compressACAS readWholeFile failed: %s\n", file);
        return;
    }

    snprintf(fileGz, PATH_MAX, "%s.gz", file);
    gzfp = gzopen(fileGz, "wb");
    if (!gzfp) {
        fprintf(stderr, "gzopen failed:");
        perror(fileGz);
        return;
    }
    writeGz(gzfp, cb.buffer, cb.len, fileGz);
    if (gzclose(gzfp) != Z_OK) {
        fprintf(stderr, "compressACAS gzclose failed: %s\n", fileGz);
        unlink(fileGz);
        return;
    }
    // delete uncompressed file
    unlink(file);
}

static void compressACAS(char *dateDir) {
    char filename[PATH_MAX];
    snprintf(filename, PATH_MAX, "%s/acas/acas.csv", dateDir);
    gzip(filename);

    snprintf(filename, PATH_MAX, "%s/acas/acas.json", dateDir);
    gzip(filename);
}

// this doesn't need to run under lock as the there should be no need for synchronisation
void checkNewDay(uint64_t now) {
    if (!Modes.globe_history_dir || !Modes.json_globe_index)
        return;

    char filename[PATH_MAX];
    char dateDir[PATH_MAX * 3/4];
    struct tm utc;

    // at 30 min past midnight, start a permanent write of all traces
    // create the new directory for writing traces
    // prevent the webserver from reading it until they are in a finished state
    time_t thirtyAgo = now / 1000 - 30 * 60; // in seconds
    gmtime_r(&thirtyAgo, &utc);

    if (utc.tm_mday != Modes.triggerPermWriteDay) {
        Modes.triggerPermWriteDay = utc.tm_mday;

        createDateDir(Modes.globe_history_dir, &utc, dateDir);

        snprintf(filename, PATH_MAX, "%s/traces", dateDir);
        int err = mkdir(filename, 0700);
        if (err && errno != EEXIST)
            perror(filename);

        // if the directory exists we assume we already have created the subdirectories
        // if the directory couldn't be created no need to try and create subdirectories it won't work.
        if (!err) {
            for (int i = 0; i < 256; i++) {
                snprintf(filename, PATH_MAX, "%s/traces/%02x", dateDir, i);
                if (mkdir(filename, 0755) && errno != EEXIST)
                    perror(filename);
            }
        }
    }

    // fiftyfive_ago changes day 55 min after midnight: stop writing the previous days traces
    // fiftysix_ago changes day 56 min after midnight: allow webserver to read the previous days traces (see checkNewDay function)
    // this is in seconds, not milliseconds
    time_t fiftysix_ago = now / 1000 - 56 * 60;
    gmtime_r(&fiftysix_ago, &utc);

    if (utc.tm_mday != Modes.traceDay) {
        Modes.traceDay = utc.tm_mday;
        time_t yesterday = now / 1000 - 24 * 3600;
        gmtime_r(&yesterday, &utc);

        createDateDir(Modes.globe_history_dir, &utc, dateDir); // doesn't usually create a directory ... but use the function anyhow worst that can happen is an empty directory for yesterday

        snprintf(filename, PATH_MAX, "%s/traces", dateDir);
        chmod(filename, 0755);

        compressACAS(dateDir);
    }

    return;
}

// do stuff which needs to happen when the other threads locked
void checkNewDayLocked(uint64_t now) {
    if (!Modes.globe_history_dir || !Modes.json_globe_index)
        return;

    struct tm utc;
    time_t time = now / 1000;
    gmtime_r(&time, &utc);

    if (utc.tm_mday != Modes.acasDay) {
        Modes.acasDay = utc.tm_mday;

        char filename[PATH_MAX];
        char dateDir[PATH_MAX * 3/4];

        createDateDir(Modes.globe_history_dir, &utc, dateDir);

        snprintf(filename, PATH_MAX, "%s/acas", dateDir);
        if (mkdir(filename, 0755) && errno != EEXIST)
            perror(filename);


        if (Modes.acasFD1 > -1)
            close(Modes.acasFD1);
        if (Modes.acasFD2 > -1)
            close(Modes.acasFD2);


        snprintf(filename, PATH_MAX, "%s/acas/acas.csv", dateDir);
        Modes.acasFD1 = open(filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (Modes.acasFD1 < 0) {
            fprintf(stderr, "open failed:");
            perror(filename);
        }

        snprintf(filename, PATH_MAX, "%s/acas/acas.json", dateDir);
        Modes.acasFD2 = open(filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (Modes.acasFD2 < 0) {
            fprintf(stderr, "open failed:");
            perror(filename);
        }
    }
}

void writeInternalState() {
    pthread_t threads[IO_THREADS];
    int numbers[IO_THREADS];

    fprintf(stderr, "saving state .....\n");
    struct timespec watch;
    startWatch(&watch);

    for (int i = 0; i < IO_THREADS; i++) {
        numbers[i] = i;
        pthread_create(&threads[i], NULL, save_blobs, &numbers[i]);
    }
    for (int i = 0; i < IO_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    double elapsed = stopWatch(&watch) / 1000.0;
    fprintf(stderr, " .......... done, saved %llu aircraft in %.3f seconds!\n", (unsigned long long) Modes.aircraftCount, elapsed);
}

/*
void *load_state(void *arg) {
    uint64_t now = mstime();
    char pathbuf[PATH_MAX];
    //struct stat fileinfo = {0};
    //fstat(fd, &fileinfo);
    //off_t len = fileinfo.st_size;
    int thread_number = *((int *) arg);
    srandom(get_seed());
    for (int i = 0; i < 256; i++) {
        if (i % IO_THREADS != thread_number)
            continue;
        snprintf(pathbuf, PATH_MAX, "%s/%02x", Modes.state_dir, i);

        DIR *dp;
        struct dirent *ep;

        dp = opendir (pathbuf);
        if (dp == NULL)
            continue;

        while ((ep = readdir (dp))) {
            if (strlen(ep->d_name) < 6)
                continue;
            snprintf(pathbuf, PATH_MAX, "%s/%02x/%s", Modes.state_dir, i, ep->d_name);

            int fd = open(pathbuf, O_RDONLY);
            if (fd == -1)
                continue;

            struct char_buffer cb = readWholeFile(fd, pathbuf);
            if (!cb.buffer)
                continue;
            char *p = cb.buffer;
            char *end = p + cb.len;

            load_aircraft(&p, end, now);

            free(cb.buffer);
            close(fd);
            // old internal state format, no longer needed
            unlink(pathbuf);
        }

        closedir (dp);
    }
    return NULL;
}
*/
