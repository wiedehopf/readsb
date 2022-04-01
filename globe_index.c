#include "readsb.h"
#define STATE_SAVE_MAGIC (0x7ba09e63757913ceULL * (uint64_t) SFOUR)
#define STATE_SAVE_MAGIC_END (0x7ba09e63757913cdULL)
#define LZO_MAGIC (0xf7413cc6eaf227dbULL)

static void mark_legs(traceBuffer tb, struct aircraft *a, int start);
static void load_blob(int blob);
static void traceCleanupNoUnlink(struct aircraft *a);
static traceBuffer reassembleTrace(struct aircraft *a, int numPoints, int64_t after_timestamp, fourState *stackBuffer, ssize_t stackBufferSize);

void init_globe_index() {
    struct tile *s_tiles = Modes.json_globe_special_tiles = aligned_malloc(GLOBE_SPECIAL_INDEX * sizeof(struct tile));
    memset(s_tiles, 0, GLOBE_SPECIAL_INDEX * sizeof(struct tile));
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

    Modes.json_globe_indexes = aligned_malloc(GLOBE_MAX_INDEX * sizeof(int32_t));
    memset(Modes.json_globe_indexes, 0, GLOBE_MAX_INDEX * sizeof(int32_t));
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
    // testing out of bounds
    /*
    for (double lat = -90; lat < 90; lat += 0.5) {
        for (double lon = -180; lon < 180; lon += 0.5) {
            globe_index(lat, lon);
        }
    }
    */
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
        fprintf(stderr, "globe_index: %d larger than GLOBE_MAX_INDEX: %d grid: %d,%d input: %.2f,%.2f\n",
                res, GLOBE_MAX_INDEX, lat, lon, lat_in, lon_in);
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

static void scheduleMemBothWrite(struct aircraft *a, int64_t schedTime) {
    a->trace_next_mw = schedTime;
    a->trace_writeCounter = 0xc0ffee;
}

void traceWrite(struct aircraft *a, int64_t now, int init) {
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

    if (a->trace_len == 0) {
        return;
    }

    int recent_points = Modes.traceRecentPoints;
    if (a->trace_writeCounter >= recent_points - 2) {
        trace_write |= WMEM;
        trace_write |= WRECENT;
    }

    if (Modes.trace_hist_only) {
        int hist_only_mask = WPERM;
        if (Modes.trace_hist_only & 1)
            hist_only_mask |= WMEM;
        if (Modes.trace_hist_only & 2)
            hist_only_mask |= WRECENT;

        if (Modes.trace_hist_only & 8) {
            if (a->trace_writeCounter > recent_points) {
                hist_only_mask |= WRECENT;
                a->trace_writeCounter = 0;
            }
            if (now > a->trace_next_mw) {
                hist_only_mask |= WMEM;
            }
        }

        trace_write &= hist_only_mask;
    }
    traceBuffer tb = { 0 };
    fourState stackBuffer[QUARTER_STACK / sizeof(fourState)];

    if ((trace_write & (WPERM | WMEM))) {
        tb = reassembleTrace(a, -1, -1, stackBuffer, sizeof(stackBuffer));
    } else {
        tb = reassembleTrace(a, recent_points, -1, stackBuffer, sizeof(stackBuffer));
    }

    int startFull = 0;
    for (int i = 0; i < tb.len; i++) {
        if (getState(tb.trace, i)->timestamp > now - Modes.keep_traces) {
            startFull = i;
            break;
        }
    }

    if ((Modes.trace_hist_only & 8) && (trace_write & WMEM)) {
        for (int i = 0; i < tb.len; i++) {
            if (getState(tb.trace, i)->timestamp > now - GLOBE_MEM_IVAL) {
                startFull = i;
                break;
            }
        }
    }

    ALIGNED char stackBuffer2[QUARTER_STACK];

    if ((trace_write & WRECENT)) {
        int start_recent = tb.len - recent_points;
        if (start_recent < startFull)
            start_recent = startFull;

        if (!init) {
            mark_legs(tb, a, imax(0, tb.len - 4 * recent_points));
        }


        // prepare the data for the trace_recent file in /run
        recent = generateTraceJson(a, tb, start_recent, -2, stackBuffer2, sizeof(stackBuffer2));

        //if (Modes.debug_traceCount && ++count2 % 1000 == 0)
        //    fprintf(stderr, "recent trace write: %u\n", count2);
    }

    if (trace_write && a->addr == TRACE_FOCUS)
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

            mark_legs(tb, a, 0);

            full = generateTraceJson(a, tb, startFull, -1, NULL, 0);
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
        if (!Modes.globe_history_dir || ((a->addr & MODES_NON_ICAO_ADDRESS) && a->airground == AG_GROUND)) {
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
            int64_t start_of_day = 1000 * (int64_t) (timegm(&utc));
            int64_t end_of_day = 1000 * (int64_t) (timegm(&utc) + 86400);

            int start = -1;
            int end = -1;
            for (int i = 0; i < tb.len; i++) {
                if (start == -1 && getState(tb.trace, i)->timestamp > start_of_day) {
                    start = i;
                    break;
                }
            }
            for (int i = a->trace_len - 1; i >= 0; i--) {
                if (getState(tb.trace, i)->timestamp < end_of_day) {
                    end = i;
                    break;
                }
            }
            int64_t endStamp = getState(tb.trace, end)->timestamp;
            if (start >= 0 && end >= 0 && end >= start
                    // only write permanent trace if we haven't already written it
                    && a->trace_perm_last_timestamp != endStamp
               ) {
                mark_legs(tb, a, 0);
                hist = generateTraceJson(a, tb, start, end, NULL, 0);
                if (hist.len > 0) {
                    permWritten = 1;
                    char tstring[100];
                    strftime (tstring, 100, TDATE_FORMAT, &utc);

                    snprintf(filename, PATH_MAX, "%s/traces/%02x/trace_full_%s%06x.json", tstring, a->addr % 256, (a->addr & MODES_NON_ICAO_ADDRESS) ? "~" : "", a->addr & 0xFFFFFF);
                    filename[PATH_MAX - 101] = 0;

                    writeJsonToGzip(Modes.globe_history_dir, filename, hist, 9);
                    if (hist.free)
                        free(hist.buffer);

                    // note what we have written to disk
                    a->trace_perm_last_timestamp = endStamp;


                    //if (Modes.debug_traceCount && ++count4 % 100 == 0)
                    //    fprintf(stderr, "perm trace writes: %u\n", count4);
                }
            }
        }
    }

    if (recent.len > 0) {
        snprintf(filename, 256, "traces/%02x/trace_recent_%s%06x.json", a->addr % 256, (a->addr & MODES_NON_ICAO_ADDRESS) ? "~" : "", a->addr & 0xFFFFFF);

        writeJsonToGzip(Modes.json_dir, filename, recent, 1);
        if (recent.free)
            free(recent.buffer);
    }

    if (full.len > 0) {
        snprintf(filename, 256, "traces/%02x/trace_full_%s%06x.json", a->addr % 256, (a->addr & MODES_NON_ICAO_ADDRESS) ? "~" : "", a->addr & 0xFFFFFF);

        writeJsonToGzip(Modes.json_dir, filename, full, 7);
        if (full.free)
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

    // free reassembled trace
    if (tb.free)
        sfree(tb.trace);
}

static void free_aircraft_range(int start, int end) {
    for (int j = start; j < end; j++) {
        struct aircraft *a = Modes.aircraft[j], *na;
        /* Go through tracked aircraft chain and free up any used memory */
        while (a) {
            na = a->next;
            if (a) {
                if (a->trace_len) {
                    traceCleanupNoUnlink(a);
                }
                free(a);
            }
            a = na;
        }
    }
}

static void save_blobs(void *arg) {
    struct task_info *info = (struct task_info *) arg;
    for (int j = info->from; j < info->to; j++) {
        //fprintf(stderr, "save_blob(%d)\n", j);

        save_blob(j);

        if (Modes.free_aircraft) {
            int stride = AIRCRAFT_BUCKETS / STATE_BLOBS;
            int start = stride * j;
            int end = start + stride;
            free_aircraft_range(start, end);
        }
    }
}

static size_t memcpySize(void *dest, const void *src, size_t n) {
    memcpy(dest, src, n);
    return n;
}
static int roundUp8(int value) {
    return ((value + 7) / 8) * 8;
}

static int load_aircraft(char **p, char *end, int64_t now) {
    static int size_changed;

    if (end - *p < 1000)
        return -1;

    struct aircraft *source = (struct aircraft *) *p;

    if (end - *p < (int) source->size_struct_aircraft)
        return -1;

    if (aircraftGet(source->addr)) {
        fprintf(stderr, "%06x aircraft already exists, can't be loaded!\n", source->addr);
        return 0;
    }

    struct aircraft *a = aircraftCreate(source->addr);

    struct aircraft *preserveNext = a->next;
    memcpy(a, *p, imin(source->size_struct_aircraft, sizeof(struct aircraft)));
    a->next = preserveNext;

    *p += source->size_struct_aircraft;

    if (!size_changed && source->size_struct_aircraft != sizeof(struct aircraft)) {
        size_changed = 1;
        fprintf(stderr, "sizeof(struct aircraft) has changed from %ld to %ld bytes, this means the code changed and if the coder didn't think properly might result in bad aircraft data. If your map doesn't have weird stuff ... probably all good and just an upgrade.\n",
                (long) source->size_struct_aircraft, (long) sizeof(struct aircraft));
        Modes.writeInternalState = 1; // immediately write in the new format
    }
    a->size_struct_aircraft = sizeof(struct aircraft);

    // make sure we set anything allocated to null pointers
    a->trace_current = NULL;
    a->trace_chunks = NULL;
    memset(&a->traceCache, 0x0, sizeof(struct traceCache));

    a->disc_cache_index = 0;

    if (!Modes.keep_traces) {
        traceCleanupNoUnlink(a);
    }

    // just in case we have bogus values saved, make sure they time out
    if (a->seen_pos > now + 1 * MINUTES)
        a->seen_pos = now - 26 * HOURS;
    if (a->seen > now + 1 * MINUTES)
        a->seen = now - 26 * HOURS;

    // make sure we don't think an extra position is still buffered in the trace memory
    a->tracePosBuffered = 0;

    // check that the trace meta data make sense before loading it
    if (a->trace_len > 0
            // let's allow for loading traces larger than we normally allow by a factor of 32
            && a->trace_len <= 32 * Modes.traceMax
       ) {
        traceAlloc(a);
        a->trace_chunks = calloc(a->trace_chunk_len, sizeof(stateChunk));

        int checkNo = 0;

#define checkSize(size) if (++checkNo && end - *p < (ssize_t) size) { fprintf(stderr, "loadAircraft: checkSize failed for hex %06x checkNo %d\n", a->addr, checkNo); traceCleanupNoUnlink(a); return -1; }

        for (int k = 0; k < a->trace_chunk_len; k++) {
            stateChunk *chunk = &a->trace_chunks[k];
            checkSize(sizeof(stateChunk));
            *p += memcpySize(chunk, *p, sizeof(stateChunk));

            checkSize(chunk->compressed_size);
            chunk->compressed = malloc(chunk->compressed_size);
            *p += memcpySize(chunk->compressed, *p, chunk->compressed_size);

            ssize_t padBytes = roundUp8(chunk->compressed_size) - chunk->compressed_size;
            *p += padBytes;
        }
        checkSize(stateBytes(a->trace_current_len));
        *p += memcpySize(a->trace_current, *p, stateBytes(a->trace_current_len));
#undef checkSize
    } else {
        traceCleanupNoUnlink(a);
    }

    if (a->globe_index > GLOBE_MAX_INDEX)
        a->globe_index = -5;

    if (a->addrtype_updated > now)
        a->addrtype_updated = now;

    if (a->seen > now)
        a->seen = 0;

    if (a->trace_next_perm < now) {
        a->trace_next_perm = now + 1 * MINUTES + random() % (5 * MINUTES);
    } else if (a->trace_next_perm > now + GLOBE_PERM_IVAL * 3 / 2) {
        a->trace_next_perm = now + 10 * MINUTES + random() % GLOBE_PERM_IVAL;
    }

    if (a->trace_len > 0) {
        if (a->addr == Modes.leg_focus) {
            scheduleMemBothWrite(a, now);
            fprintf(stderr, "leg_focus: %06x trace len: %d\n", a->addr, a->trace_len);
            traceWrite(a, now, 0);
        }

        // schedule writing all the traces into run so they are present for the webinterface
        if (a->pos_reliable_valid.source != SOURCE_INVALID) {
            scheduleMemBothWrite(a, now); // write traces for aircraft with valid positions as quickly as possible
            a->trace_write = 1;
        } else {
            scheduleMemBothWrite(a, now + 60 * SECONDS + (now - a->seen_pos) / (24 * 60 / 5)); // condense 24h into 4 minutes
        }
    }

    int new_index = a->globe_index;
    a->globe_index = -5;
    if (a->pos_reliable_valid.source != SOURCE_INVALID) {
        set_globe_index(a, new_index);
    }
    updateValidities(a, now);

    if (a->onActiveList) {
        a->onActiveList = 1;
        ca_add(&Modes.aircraftActive, a);
    }
    if (a->trace_current) {
        traceMaintenance(a, now);
    }

    return 0;
}

static void mark_legs(traceBuffer tb, struct aircraft *a, int start) {
    if (tb.len < 20)
        return;
    if (start < 1)
        start = 1;

    int high = 0;
    int low = 100000;

    int last_five[5];
    for (int i = 0; i < 5; i++) { last_five[i] = -1000; }
    uint32_t five_pos = 0;

    double sum = 0;
    int count = 0;

    struct state *last_leg = NULL;
    struct state *new_leg = NULL;

    for (int i = start; i < tb.len; i++) {
        int on_ground = getState(tb.trace, i)->on_ground;
        int altitude_valid = getState(tb.trace, i)->baro_alt_valid;
        int altitude = getState(tb.trace, i)->baro_alt / _alt_factor;

        if (!altitude_valid && getState(tb.trace, i)->geom_alt_valid) {
            altitude_valid = 1;
            altitude = getState(tb.trace, i)->geom_alt / _alt_factor;
        }

        if (getState(tb.trace, i)->leg_marker) {
            getState(tb.trace, i)->leg_marker = 0;
            // reset leg marker
            last_leg = getState(tb.trace, i);
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
        count++;
    }

    int threshold = (int) (sum / (double) (count * 3));

    if (a->addr == Modes.leg_focus) {
        fprintf(stderr, "--------------------------\n");
        fprintf(stderr, "start: %d\n", start);
        fprintf(stderr, "trace_len: %d\n", tb.len);
        fprintf(stderr, "threshold: %d\n", threshold);
    }


    if (threshold > 2500)
        threshold = 2500;
    if (threshold < 200)
        threshold = 200;

    high = 0;
    low = 100000;

    int64_t major_climb = 0;
    int64_t major_descent = 0;
    int major_climb_index = 0;
    int major_descent_index = 0;
    int64_t last_high = 0;
    int64_t last_low = 0;

    int last_low_index = 0;

    int64_t last_airborne = 0;
    int64_t last_ground = 0;
    int64_t last_ground_index = 0;
    int64_t first_ground = 0;
    int64_t first_ground_index = 0;

    int was_ground = 0;

    for (int i = 0; i < 5; i++)
        last_five[i] = 0;
    five_pos = 0;

    int prev_tmp = start - 1;
    for (int i = start; i < tb.len; i++) {
        struct state *state = getState(tb.trace, i);
        int prev_index = prev_tmp;
        struct state *prev = getState(tb.trace, prev_index);

        int64_t elapsed = state->timestamp - prev->timestamp;

        int on_ground = state->on_ground;
        int altitude_valid = state->baro_alt_valid;
        int altitude = state->baro_alt / _alt_factor;

        if (!altitude_valid && state->geom_alt_valid) {
            altitude_valid = 1;
            altitude = state->geom_alt / _alt_factor;
        }

        if (!on_ground && !altitude_valid)
            continue;
        if (0 && a->addr == Modes.leg_focus) {
            fprintf(stderr, "state: %d %d %d %d\n", i, altitude, altitude_valid, on_ground);
        }

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
                first_ground_index = i;
            }
            last_ground = state->timestamp;
            last_ground_index = i;
        } else {
            last_airborne = state->timestamp;
        }

        if (altitude >= high) {
            high = altitude;
            if (0 && a->addr == Modes.leg_focus) {
                time_t nowish = state->timestamp/1000;
                struct tm utc;
                gmtime_r(&nowish, &utc);
                char tstring[100];
                strftime (tstring, 100, "%H:%M:%S", &utc);
                fprintf(stderr, "high: %d %s\n", altitude, tstring);
            }
        }
        if (!on_ground && major_descent && last_ground >= major_descent
                && last_ground > first_ground + 1 * MINUTES
                && state->timestamp > last_ground + 15 * SECONDS
                && high - low > 200) {
            // fake major_climb after takeoff ... bit hacky
            high = low + threshold + 1;
            last_high = state->timestamp;
            last_low = last_ground;
            last_low_index = last_ground_index;
        }
        if (altitude <= low) {
            low = altitude;
        }

        if (abs(low - altitude) < threshold * 1 / 3) {
            last_low = state->timestamp;
            last_low_index = i;
        }
        if (abs(high - altitude) < threshold * 1 / 3) {
            last_high = state->timestamp;
            if (0 && a->addr == Modes.leg_focus) {
                time_t nowish = state->timestamp/1000;
                struct tm utc;
                gmtime_r(&nowish, &utc);
                char tstring[100];
                strftime (tstring, 100, "%H:%M:%S", &utc);
                fprintf(stderr, "last_high: %d %s\n", altitude, tstring);
            }
        }

        if (high - low > threshold) {
            if (last_high > last_low) {
                // only set new major climb time if this is after a major descent.
                // then keep that time associated with the climb
                // still report continuation of thta climb
                if (major_climb <= major_descent) {
                    int bla = imin(tb.len - 1, last_low_index + 3);
                    major_climb = getState(tb.trace, bla)->timestamp;
                    major_climb_index = bla;
                }
                if (a->addr == Modes.leg_focus) {
                    time_t nowish = major_climb/1000;
                    struct tm utc;
                    gmtime_r(&nowish, &utc);
                    char tstring[100];
                    strftime (tstring, 100, "%H:%M:%S", &utc);
                    fprintf(stderr, "climb: %d %s %d %d\n", altitude, tstring, high, low);
                }
                low = high - threshold * 9/10;
            } else if (last_high < last_low) {
                int bla = imax(0, last_low_index - 3);
                while(bla > 0) {
                    if (0 && a->addr == Modes.leg_focus) {
                        fprintf(stderr, "bla: %d %d %d %d\n", bla, (int) (getState(tb.trace, bla)->baro_alt / _alt_factor),
                                getState(tb.trace, bla)->baro_alt_valid,
                                getState(tb.trace, bla)->on_ground
                               );
                    }
                    if (getState(tb.trace, bla)->baro_alt_valid && !getState(tb.trace, bla)->on_ground) {
                        break;
                    }
                    bla--;
                }
                if (bla < 0)
                    fprintf(stderr, "wat asdf bla? %d\n", bla);
                major_descent = getState(tb.trace, bla)->timestamp;
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
                (double) state->lat / 1E6,
                (double) state->lon / 1E6,
                (double) prev->lat / 1E6,
                (double) prev->lon / 1E6,
                0
                );

        int max_leg_alt = 20000;
        if (elapsed > 30 * 60 * 1000 && distance < 10E3 * (elapsed / (30 * 60 * 1000.0)) && distance > 1
                && (state->on_ground || (state->baro_alt_valid && state->baro_alt / _alt_factor < max_leg_alt))) {
            leg_now = 1;
            if (a->addr == Modes.leg_focus)
                fprintf(stderr, "time/distance leg, elapsed: %0.fmin, distance: %0.f\n", elapsed / (60 * 1000.0), distance / 1000.0);
        }

        int leg_float = 0;
        if (major_climb && major_descent && major_climb > major_descent + 12 * MINUTES) {
            for (int i = major_descent_index + 1; i <= major_climb_index; i++) {
                struct state *st = getState(tb.trace, i);
                if (st->timestamp > getState(tb.trace, i - 1)->timestamp + 5 * MINUTES
                        && (st->on_ground || (st->baro_alt_valid && st->baro_alt / _alt_factor < max_leg_alt))) {
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
            int64_t leg_ts = 0;

            if (leg_now) {
                new_leg = getState(tb.trace, prev_index + 1);
                for (int k = prev_index + 1; k < i; k++) {
                    struct state *state = getState(tb.trace, i);
                    struct state *last = getState(tb.trace, i - 1);

                    if (state->timestamp > last->timestamp + 5 * 60 * 1000) {
                        new_leg = state;
                        break;
                    }
                }
            } else if (major_descent_index + 1 == major_climb_index) {
                new_leg = getState(tb.trace, major_climb_index);
            } else {
                for (int i = major_climb_index; i > major_descent_index; i--) {
                    struct state *state = getState(tb.trace, i);
                    struct state *last = getState(tb.trace, i - 1);

                    if (state->timestamp > last->timestamp + 5 * 60 * 1000) {
                        new_leg = state;
                        break;
                    }
                }
                if (last_ground > major_descent) {
                    int64_t half = first_ground + (last_ground - first_ground) / 2;
                    for (int i = first_ground_index + 1; i <= last_ground_index; i++) {
                        struct state *state = getState(tb.trace, i);

                        if (state->timestamp > half) {
                            new_leg = state;
                            break;
                        }
                    }
                } else {
                    int64_t half = major_descent + (major_climb - major_descent) / 2;
                    for (int i = major_descent_index + 1; i < major_climb_index; i++) {
                        struct state *state = getState(tb.trace, i);

                        if (state->timestamp > half) {
                            new_leg = state;
                            break;
                        }
                    }
                }
            }

            if (new_leg) {
                leg_ts = new_leg->timestamp;
                new_leg->leg_marker = 1;
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
        fprintf(stderr, "<3>hex: %06x, ca_remove(): list does not exist!\n", a->addr);
        pthread_mutex_unlock(&ca->mutex);
        return;
    }
    int found = 0;
    for (int i = 0; i < ca->len; i++) {
        if (ca->list[i] == a) {
            // replace with last element in array
            ca->list[i] = ca->list[ca->len - 1];
            ca->list[ca->len - 1] = NULL;
            ca->len--;
            i--;
            found = 1;
        }
    }
    pthread_mutex_unlock(&ca->mutex);
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
        fprintf(stderr, "hex: %06x, old_index: %d, new_index: %d, GLOBE_MAX_INDEX: %d\n",
                a->addr, old_index, new_index, GLOBE_MAX_INDEX);
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

void traceAlloc(struct aircraft *a) {
    if (a->trace_current) {
        return;
    }
    a->trace_current_max = imax(a->trace_current_max, (Modes.traceChunkPoints + Modes.traceReserve));
    int alloc = stateBytes(a->trace_current_max);
    a->trace_current = aligned_malloc(alloc);
    if (!a->trace_current) {
        fprintf(stderr, "FATAL: malloc fail oceiFa7d\n");
        exit(1);
    }
    memset(a->trace_current, 0x0, alloc);
}

static void resizeTraceChunks(struct aircraft *a, int newLen) {
    if (newLen == 0) {
        sfree(a->trace_chunks);
        a->trace_chunk_len = 0;
        return;
    }

    int oldLen = a->trace_chunk_len;

    int newBytes = newLen* sizeof(stateChunk);
    int oldBytes = oldLen* sizeof(stateChunk);

    stateChunk *new = malloc(newBytes);
    if (!new) {
        fprintf(stderr, "malloc fail: yeiPho0va\n");
        exit(1);
    } else {
        if (newBytes < oldBytes) {
            memcpy(new, a->trace_chunks + (oldBytes - newBytes), newBytes);
        } else {
            memcpy(new, a->trace_chunks, oldBytes);
        }
        sfree(a->trace_chunks);
        a->trace_chunks = new;
        a->trace_chunk_len = newLen;
    }
}

static void tracePrune(struct aircraft *a, int64_t now) {
    if (a->trace_len == 0) {
        traceCleanup(a);
        return;
    }

    int64_t keep_after = now - Modes.keep_traces;

    int deletedChunks = 0;

    for (int k = 0; k < a->trace_chunk_len; k++) {
        stateChunk *chunk = &a->trace_chunks[k];
        if (chunk->lastTimestamp >= keep_after) {
            break;
        }

        deletedChunks++;
        a->trace_len -= chunk->numStates;

        sfree(chunk->compressed);
    }

    if (deletedChunks > 0) {
        fprintf(stderr, "%06x deleting %d chunks\n", a->addr, deletedChunks);
        resizeTraceChunks(a, a->trace_chunk_len - deletedChunks);
    }

    if (getState(a->trace_current, a->trace_current_len - 1)->timestamp < keep_after) {
        traceCleanup(a);
    }
}

int traceUsePosBuffered(struct aircraft *a) {
    if (a->tracePosBuffered) {
        a->tracePosBuffered = 0;
        // bookkeeping:
        a->trace_len++;
        a->trace_current_len++;
        a->trace_write |= WRECENT;
        a->trace_writeCounter++;
        return 1;
    } else {
        return 0;
    }
}

static void destroyTraceCache(struct traceCache *cache) {
    sfree(cache->entries);
    memset(cache, 0x0, sizeof(struct traceCache));
}

static void traceCleanupNoUnlink(struct aircraft *a) {
    if (a->trace_chunks) {
        for (int k = 0; k < a->trace_chunk_len; k++) {
            sfree(a->trace_chunks[k].compressed);
        }
    }
    sfree(a->trace_current);
    sfree(a->trace_chunks);

    a->tracePosBuffered = 0;
    a->trace_len = 0;
    a->trace_current_len = 0;
    a->trace_current_max = 0;

    destroyTraceCache(&a->traceCache);
}

void traceCleanup(struct aircraft *a) {
    if (a->trace_current || a->trace_len) {
        traceUnlink(a);
    }
    traceCleanupNoUnlink(a);
}

// reconstruct at least the last numPoints points from trace chunks / current_trace
// numPoints < 0 => all data / whole trace
static traceBuffer reassembleTrace(struct aircraft *a, int numPoints, int64_t after_timestamp, fourState *stackBuffer, ssize_t stackBufferSize) {
    int firstChunk = 0;

    int currentLen = a->trace_current_len;
    int allocLen = currentLen;

    if (numPoints >= 0) {
        firstChunk = a->trace_chunk_len;
        for (int k = a->trace_chunk_len - 1; k >= 0 && allocLen < numPoints; k--) {
            stateChunk *chunk = &a->trace_chunks[k];
            allocLen += chunk->numStates;
            firstChunk = k;
        }
    } else if (after_timestamp > 0) {
        firstChunk = a->trace_chunk_len;
        for (int k = a->trace_chunk_len - 1; k >= 0; k--) {
            stateChunk *chunk = &a->trace_chunks[k];
            if (after_timestamp > chunk->lastTimestamp) {
                break;
            }
            allocLen += chunk->numStates;
            firstChunk = k;
        }
    } else {
        for (int k = 0; k < a->trace_chunk_len; k++) {
            stateChunk *chunk = &a->trace_chunks[k];
            allocLen += chunk->numStates;
        }
    }

    traceBuffer tb = { 0 };

    ssize_t allocBytes = stateBytes(allocLen);
    if (allocBytes <= stackBufferSize) {
        tb.free = 0;
        tb.trace = stackBuffer;
    } else {
        tb.free = 1;
        tb.trace = aligned_malloc(stateBytes(allocLen));
        if (!tb.trace) { fprintf(stderr, "malloc fail: meehee7I\n"); exit(1); }
    }

    fourState *tp = tb.trace;


    int actual_len = 0;
    for (int k = firstChunk; k < a->trace_chunk_len; k++) {
        stateChunk *chunk = &a->trace_chunks[k];
        actual_len += chunk->numStates;
        if (actual_len > allocLen) { fprintf(stderr, "remakeTrace buffer overflow, bailing eex5ioBu\n"); exit(1); }

        lzo_uint uncompressed_len = stateBytes(chunk->numStates);

        //fprintf(stderr, "reassembleTrace(%06x %d %ld): chunk %d trace_chunk_len %d compressed_size %d uncompressed_size %d outAlloc %d allocLen %d numStates %d trace_current_len %d\n",
        //        a->addr, numPoints, (long) after_timestamp, k, a->trace_chunk_len,
        //        chunk->compressed_size, (int) uncompressed_len, (int) stateBytes(allocLen), allocLen, (int) chunk->numStates, currentLen);

        int res = lzo1x_decompress_safe(chunk->compressed, chunk->compressed_size, (unsigned char*) tp, &uncompressed_len, NULL);

        //fprintf(stderr, "reassembleTrace(%06x %d %ld): chunk %d trace_chunk_len %d compressed_size %d uncompressed_size %d outAlloc %d allocLen %d numStates %d trace_current_len %d\n",
        //        a->addr, numPoints, (long) after_timestamp, k, a->trace_chunk_len,
        //        chunk->compressed_size, (int) uncompressed_len, (int) stateBytes(allocLen), allocLen, (int) chunk->numStates, currentLen);

        if (res != LZO_E_OK) {
            fprintf(stderr, "reassembleTrace(%06x %d %ld): decompress failure chunk %d trace_chunk_len %d compressed_size %d uncompressed_size %d\n",
                    a->addr, numPoints, (long) after_timestamp, k, a->trace_chunk_len,
                    chunk->compressed_size, (int) uncompressed_len);
            if (tb.free)
                sfree(tb.trace);
            tb.len = 0;
            traceCleanup(a);
            return tb;
        }

        tp += getFourStates(chunk->numStates);
    }

    actual_len += currentLen;

    if (actual_len > allocLen) { fprintf(stderr, "remakeTrace buffer overflow, bailing eex5ioBu with actual_len %d allocLen %d\n", actual_len, allocLen); exit(1); }

    memcpy(tp, a->trace_current, stateBytes(currentLen));
    // tp is not incremented here as it's not used anymore after this.
    tb.len = actual_len;
    return tb;
}

static void compressChunk(stateChunk *target, fourState *source, int pointCount) {
    target->numStates = pointCount;
    int chunkBytes = stateBytes(pointCount);
    int lzo_out_alloc = chunkBytes + chunkBytes / 16 + 64 + 3; // from mini lzo example
    unsigned char lzo_work[LZO1X_1_MEM_COMPRESS];
    unsigned char *lzo_out = malloc(lzo_out_alloc);
    if (!lzo_out) { fprintf(stderr, "malloc fail: ieshee7G\n"); exit(1); }

    lzo_uint compressed_len = 0;
    //lzo1x_1_compress        ( const lzo_bytep src, lzo_uint  src_len, lzo_bytep dst, lzo_uintp dst_len, lzo_voidp wrkmem );
    int res = lzo1x_1_compress((unsigned char *) source, chunkBytes, lzo_out, &compressed_len, lzo_work);

    if (res != LZO_E_OK) { fprintf(stderr, "lzo1x_1_compress error Theij8ah\n"); exit(1); }

    target->compressed_size = compressed_len;

    target->compressed = malloc(target->compressed_size);
    if (!target->compressed) { fprintf(stderr, "malloc fail: ieshee7G\n"); exit(1); }

    memcpy(target->compressed, lzo_out, target->compressed_size);
    sfree(lzo_out);
}


static void setTrace(struct aircraft *a, fourState *source, int len) {
    if (len == 0) {
        traceCleanup(a);
        return;
    }

    fprintf(stderr, "%06x setTrace\n", a->addr);

    traceCleanupNoUnlink(a);


    traceAlloc(a);

    fourState *p = source;
    int chunkSize = Modes.traceChunkPoints;
    while (len > chunkSize) {
        resizeTraceChunks(a, a->trace_chunk_len + 1);
        stateChunk *new = &a->trace_chunks[a->trace_chunk_len - 1];

        new->numStates = chunkSize;
        new->firstTimestamp = getState(p, 0)->timestamp;
        new->lastTimestamp = getState(p, chunkSize - 1)->timestamp;

        compressChunk(new, p, chunkSize);

        len -= new->numStates;
    }

    a->trace_current_len = len;
    memcpy(a->trace_current, p, stateBytes(len));

    int64_t now = mstime();
    traceMaintenance(a, now);
    scheduleMemBothWrite(a, now);
}


void traceMaintenance(struct aircraft *a, int64_t now) {
    // free trace cache for inactive aircraft
    if (a->traceCache.entries && now - a->seen_pos > TRACE_CACHE_LIFETIME) {
        //fprintf(stderr, "%06x free traceCache\n", a->addr);
        destroyTraceCache(&a->traceCache);
    }

    //fprintf(stderr, "%06x\n", a->addr);

    if (!a->trace_current) {
        return;
    }
    // throw out old data if older than keep_trace or trace is getting full
    tracePrune(a, now);

    if (!a->trace_current) {
        return;
    }

    if (Modes.json_globe_index) {
        if (now > a->trace_next_perm)
            a->trace_write |= WPERM;
        if (now > a->trace_next_mw)
            a->trace_write |= WMEM;
    }

    // on day change write out the traces for yesterday
    // for which day and which time span is written is determined by traceday
    if (a->traceWrittenForYesterday != Modes.triggerPermWriteDay) {
        a->traceWrittenForYesterday = Modes.triggerPermWriteDay;
        if (a->addr == TRACE_FOCUS)
            fprintf(stderr, "schedule_perm\n");

        a->trace_next_perm = now + random() % (5 * MINUTES);
    }

    int chunkPoints = Modes.traceChunkPoints;

    if (a->trace_current_len >= chunkPoints + Modes.traceReserve / 4) {
        resizeTraceChunks(a, a->trace_chunk_len + 1);

        stateChunk *new = &a->trace_chunks[a->trace_chunk_len - 1];

        new->numStates = chunkPoints;
        new->firstTimestamp = getState(a->trace_current, 0)->timestamp;
        new->lastTimestamp = getState(a->trace_current, chunkPoints - 1)->timestamp;

        compressChunk(new, a->trace_current, chunkPoints);
        if (Modes.verbose) {
            fprintf(stderr, "%06x compressChunk: compressed_size: %d trace_chunk_len %d compression ratio %.2f\n",
                    a->addr, new->compressed_size, a->trace_chunk_len, stateBytes(chunkPoints) / (double) new->compressed_size);
        }

        a->trace_current_len -= chunkPoints;
        memmove(a->trace_current, a->trace_current + getFourStates(chunkPoints), stateBytes(a->trace_current_max) - stateBytes(chunkPoints));

        // shrink trace_current
        int nominal = (Modes.traceChunkPoints + Modes.traceReserve);
        if (a->trace_current_max > nominal && a->trace_current_len < nominal) {
            a->trace_current_max = nominal;
            a->trace_current = realloc(a->trace_current, stateBytes(a->trace_current_max));
        }
    }
}


int traceAdd(struct aircraft *a, int64_t now, int stale) {
    if (!Modes.keep_traces)
        return 0;

    int traceDebug = (a->addr == Modes.trace_focus);

    int posUsed = 0;
    int bufferedPosUsed = 0;
    double distance = 0;
    int64_t elapsed = 0;
    int64_t elapsed_buffered = 0;
    int duplicate = 0;

    int64_t max_elapsed = Modes.json_trace_interval;
    int64_t min_elapsed = imin(TRACE_MIN_ELAPSED, max_elapsed / 5);

    float turn_density = 5.0;
    float max_speed_diff = 5.0;

    int alt = a->baro_alt;
    int alt_valid = altBaroReliableTrace(now, a);

    if (alt_valid && a->baro_alt > 10000) {
        max_speed_diff *= 2;
    }

    if (a->pos_reliable_valid.source == SOURCE_MLAT) {
        min_elapsed = 2500; // 2.5 seconds
        turn_density /= 2;
        max_elapsed *= 0.75;
        max_speed_diff = 30;
    }
    // some towers on MLAT .... create unnecessary data
    if (a->squawk_valid.source != SOURCE_INVALID && a->squawk == 0x7777) {
        min_elapsed += 60 * SECONDS;
    }

    int on_ground = 0;
    float track = a->track;
    if (!trackVState(now, &a->track_valid, &a->pos_reliable_valid)) {
        track = -1;
    }

    int agValid = 0;
    if (trackDataValid(&a->airground_valid)) {
        agValid = 1;
        if (a->airground == AG_GROUND) {
            on_ground = 1;
            if (trackVState(now, &a->true_heading_valid, &a->pos_reliable_valid)) {
                track = a->true_heading;
            } else {
                track = -1;
            }
        }
    }

    if (a->trace_current_len == 0 )
        goto save_state;

    struct state *last = getState(a->trace_current, a->trace_current_len - 1);

    if (now >= last->timestamp)
        elapsed = now - last->timestamp;

    struct state *buffered = NULL;

    if (a->tracePosBuffered) {
        buffered = getState(a->trace_current, a->trace_current_len);
        elapsed_buffered = (int64_t) buffered->timestamp - (int64_t) last->timestamp;
    }

    int32_t new_lat = (int32_t) nearbyint(a->lat * 1E6);
    int32_t new_lon = (int32_t) nearbyint(a->lon * 1E6);
    duplicate = (elapsed < 1 * SECONDS && new_lat == last->lat && new_lon == last->lon);

    int last_alt = last->baro_alt / _alt_factor;
    int last_alt_valid = last->baro_alt_valid;

    int alt_diff = 0;
    if (last_alt_valid && alt_valid) {
        alt_diff = abs(a->baro_alt - last_alt);
    }

    float speed_diff = 0;
    if (trackDataValid(&a->gs_valid) && last->gs_valid)
        speed_diff = fabs(last->gs / _gs_factor - a->gs);

    // keep the last air ground state if the current isn't valid
    if (!agValid && !alt_valid) {
        on_ground = last->on_ground;
    }
    if (on_ground) {
        // just do this twice so we cover the first point in a trace as well as using the last airground state
        if (trackVState(now, &a->true_heading_valid, &a->pos_reliable_valid)) {
            track = a->true_heading;
        } else {
            track = -1;
        }
    }

    float track_diff = 0;
    float last_track = last->track / _track_factor;
    if (last->track_valid && track > -1) {
        track_diff = fabs(norm_diff(track - last_track, 180));
    }


    distance = greatcircle(last->lat / 1E6, last->lon / 1E6, a->lat, a->lon, 0);

    if (distance < 5)
        traceDebug = 0;

    if (traceDebug) {
        fprintf(stderr, "%5.1fs d:%5.0f a:%6d D%4d s:%4.0f D%3.0f t: %5.1f D%5.1f ",
                elapsed / 1000.0,
                distance, alt, alt_diff, a->gs, speed_diff, a->track, track_diff);
    }

    if (speed_diff >= max_speed_diff) {
        if (traceDebug) fprintf(stderr, "speed_change: %0.1f %0.1f -> %0.1f", fabs(last->gs / 10.0 - a->gs), last->gs / 10.0, a->gs);

        if (buffered && last->gs == buffered->gs) {
            goto save_state_no_buf;
        }
        goto save_state;
    }

    // record ground air state changes precisely
    if (on_ground != last->on_ground) {
        goto save_state;
    }

    // record non moving targets every 5 minutes
    if (elapsed > 10 * max_elapsed) {
        goto save_state;
    }

    if (alt_valid && !last_alt_valid) {
        goto save_state;
    }

    // check altitude change before minimum interval
    if (alt_diff > 0) {

        int max_diff = 250;

        if (alt <= 7000) {
            if (buffered && last->baro_alt == buffered->baro_alt) {
                max_diff = 200; // for transponders with 100 ft altitude increments
            } else {
                max_diff = 100;
            }
        } else if (alt <= 12000) {
            max_diff = 200;
        } else {
            max_diff = 400;
        }

        if (alt_diff >= max_diff) {
            if (traceDebug) fprintf(stderr, "alt_change1: %d -> %d", last_alt, alt);
            if (alt_diff == max_diff || (buffered && last->baro_alt == buffered->baro_alt)) {
                goto save_state_no_buf;
            }
            goto save_state;
        }

        int base = 800;
        if (alt <= 7000) {
            base = 125;
        } else if (alt <= 12000) {
            base = 250;
        }
        int64_t too_long = ((max_elapsed / 4) * base / (float) alt_diff);
        if (alt_diff >= 25 && elapsed > too_long) {
            if (traceDebug) fprintf(stderr, "alt_change2: %d -> %d, %5.1f", last_alt, alt, too_long / 1000.0);
            goto save_state_no_buf;
        }
    }

    // don't record non moving targets ... unless json-interval is set to less than 5 seconds
    if (on_ground && distance < 10 && max_elapsed > 5 * SECONDS)
        goto no_save_state;

    // don't record unnecessary many points
    if (elapsed < min_elapsed)
        goto no_save_state;

    // even if the squawk gets invalid we continue to record more points
    if (a->squawk == 0x7700) {
        goto save_state;
    }

    // record trace precisely if we have a TCAS advisory
    if (trackDataValid(&a->acas_ra_valid) && trackDataAge(now, &a->acas_ra_valid) < 15 * SECONDS) {
        goto save_state;
    }

    if (!on_ground && elapsed > max_elapsed) // default 30000 ms
        goto save_state;

    if (on_ground && elapsed > 4 * max_elapsed)
        goto save_state;

    // SS2
    if (a->addr == 0xa19b53 && elapsed > max_elapsed / 4)
        goto save_state;

    if (stale) {
        // save a point if reception is spotty so we can mark track as spotty on display
        goto save_state;
    }

    if (on_ground) {
        if (distance * track_diff > 250) {
            if (traceDebug) fprintf(stderr, "track_change: %0.1f %0.1f -> %0.1f", track_diff, last_track, a->track);
            goto save_state;
        }

        if (distance > 400)
            goto save_state;
    }

    if (track_diff > 0.5
            && (elapsed / 1000.0 * track_diff * turn_density > 100.0)
       ) {
        if (traceDebug) fprintf(stderr, "track_change: %0.1f %0.1f -> %0.1f", track_diff, last_track, a->track);
        goto save_state;
    }

    goto no_save_state;

save_state:
    // always try using the buffered position instead of the current one
    // this should provide a better picture of changing track / speed / altitude

    if (elapsed > max_elapsed || 2 * elapsed_buffered > elapsed || elapsed_buffered > 2700) {
        if (traceUsePosBuffered(a)) {
            if (traceDebug) fprintf(stderr, " buffer\n");
            // in some cases we want to add the current point as well
            // if not, the current point will be put in the buffer
            traceAdd(a, now, stale);
            // return so the point isn't used a second time or put in the buffer
            return 1;
        }
    }

save_state_no_buf:
    posUsed = 1;
    //fprintf(stderr, "traceAdd: %06x elapsed: %8.1f s distance: %8.3f km\n", a->addr, elapsed / 1000.0, distance / 1000.0);

no_save_state:


    if (duplicate) {
        // don't put a duplicate position in the buffer and don't use it for the trace
        return 0;
    }

    if (!a->trace_current) {
        traceAlloc(a);
        scheduleMemBothWrite(a, now); // rewrite full history file
        a->trace_next_perm = now + GLOBE_PERM_IVAL / 2; // schedule perm write

        //fprintf(stderr, "%06x: new trace\n", a->addr);
    }
    if (a->trace_current_len + 2 >= a->trace_current_max) {
        static int64_t antiSpam;
        if (Modes.debug_traceAlloc || now > antiSpam + 5 * SECONDS) {
            fprintf(stderr, "<3>%06x: trace_current insufficient\n", a->addr);
            antiSpam = now;
        }
        return 0;
    }

    struct state *new = getState(a->trace_current, a->trace_current_len);

    to_state(a, new, now, on_ground, track);

    // trace_all stuff:
    struct state_all *new_all = getStateAll(a->trace_current, a->trace_current_len);
    if (new_all) {
        to_state_all(a, new_all, now);
    }

    if (posUsed) {
        if (traceDebug) fprintf(stderr, " normal\n");
        a->tracePosBuffered = 0;
        // bookkeeping:
        a->trace_len++;
        a->trace_current_len++;
        a->trace_write |= WRECENT;
        a->trace_writeCounter++;
    } else {
        a->tracePosBuffered = 1;
    }

    if (traceDebug && !posUsed && !bufferedPosUsed) fprintf(stderr, "none\n");

    return posUsed || bufferedPosUsed;
}

static int state_chunk_size() {
    return 8 * 1024 * 1024;
}

void save_blob(int blob) {
    if (!Modes.state_dir)
        return;
    //static int count;
    //fprintf(stderr, "Save blob: %02x, count: %d\n", blob, ++count);
    if (blob < 0 || blob > STATE_BLOBS)
        fprintf(stderr, "save_blob: invalid argument: %02x", blob);

    int gzip = 0;
    int lzo = 1;

    char filename[PATH_MAX];
    char tmppath[PATH_MAX];
    if (lzo) {
        snprintf(filename, 1024, "%s/blob_%02x.lzol", Modes.state_dir, blob);
    } else if (gzip) {
        snprintf(filename, 1024, "%s/blob_%02x.gz", Modes.state_dir, blob);
    } else {
        snprintf(filename, 1024, "%s/blob_%02x", Modes.state_dir, blob);
    }
    snprintf(tmppath, PATH_MAX, "%s.readsb_tmp", filename);

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
        res = gzsetparams(gzfp, 1, Z_FILTERED);
        if (res < 0)
            fprintf(stderr, "gzsetparams fail: %d", res);
    }

    int stride = AIRCRAFT_BUCKETS / STATE_BLOBS;
    int start = stride * blob;
    int end = start + stride;

    uint64_t magic = STATE_SAVE_MAGIC;

    int alloc = state_chunk_size();
    unsigned char *buf = aligned_malloc(alloc);
    unsigned char *p = buf;

    int lzo_out_alloc = alloc + alloc / 16 + 64 + 3; // from mini lzo example
    lzo_uint compressed_len = 0;
    unsigned char *lzo_out = NULL;
    unsigned char lzo_work[LZO1X_1_MEM_COMPRESS];
    if (lzo) {
        lzo_out = aligned_malloc(lzo_out_alloc);
    }

    for (int j = start; j < end; j++) {
        for (struct aircraft *a = Modes.aircraft[j]; a || (j == end - 1); a = a->next) {
            int size_state = 0;
            if (a) {
                traceUsePosBuffered(a); // use buffered position for saving state
                size_state += sizeof(struct aircraft);
                for (int k = 0; k < a->trace_chunk_len; k++) {
                    stateChunk *chunk = &a->trace_chunks[k];
                    size_state += sizeof(stateChunk);
                    size_state += roundUp8(chunk->compressed_size);
                }
                size_state += stateBytes(a->trace_current_len);
            }

            if (!a || (p + 2 * sizeof(uint64_t) + size_state >= buf + alloc)) {
                //fprintf(stderr, "save_blob writing %d KB (buffer)\n", (int) ((p - buf) / 1024));

                uint64_t magic_end = STATE_SAVE_MAGIC_END;
                memcpy(p, &magic_end, sizeof(magic_end));
                p += sizeof(magic_end);

                if (lzo) {

                    int res = lzo1x_1_compress(buf, p - buf, lzo_out + 2 * sizeof(uint64_t), &compressed_len, lzo_work);

                    //fprintf(stderr, "%d %08lld\n", blob, (long long) compressed_len);

                    if (res != LZO_E_OK) {
                        fprintf(stderr, "lzo1x_1_compress error, couldn't save state blob: %s\n", filename);
                        goto error;
                    }
                    uint64_t lzo_magic = LZO_MAGIC;
                    memcpy(lzo_out, &lzo_magic, sizeof(uint64_t));
                    uint64_t compressed_len_64 = compressed_len;
                    memcpy(lzo_out + sizeof(uint64_t), &compressed_len_64, sizeof(uint64_t));
                    check_write(fd, lzo_out, compressed_len + 2 * sizeof(uint64_t), tmppath);
                } else if (gzip) {
                    writeGz(gzfp, buf, p - buf, tmppath);
                } else {
                    check_write(fd, buf, p - buf, tmppath);
                }

                p = buf;
            }

            if (!a) {
                break;
            }

            p += memcpySize(p, &magic, sizeof(magic));

            if (p + size_state >= buf + alloc) {
                fprintf(stderr, "%06x: Couldn't write internal state, check save_blob code!\n", a->addr);
            } else {
                p += memcpySize(p, a, sizeof(struct aircraft));
                if (a->trace_len > 0) {
                    for (int k = 0; k < a->trace_chunk_len; k++) {
                        stateChunk *chunk = &a->trace_chunks[k];
                        p += memcpySize(p, chunk, sizeof(stateChunk));

                        p += memcpySize(p, chunk->compressed, chunk->compressed_size);
                        ssize_t padBytes = roundUp8(chunk->compressed_size) - chunk->compressed_size;
                        memset(p, 0x0, padBytes);
                        p += padBytes;
                    }
                    p += memcpySize(p, a->trace_current, stateBytes(a->trace_current_len));
                }
            }
        }
    }

    if (gzfp)
        gzclose(gzfp);
    else if (fd != -1)
        close(fd);

    if (rename(tmppath, filename) == -1) {
        fprintf(stderr, "save_blob rename(): %s -> %s", tmppath, filename);
        perror("");
        unlink(tmppath);
    }
    goto out;
error:
    if (gzfp)
        gzclose(gzfp);
    else if (fd != -1)
        close(fd);
    unlink(tmppath);
out:

    if (lzo) {
        free(lzo_out);
    }
    free(buf);
}
static void load_blobs(void *arg) {
    struct task_info *info = (struct task_info *) arg;

    for (int j = info->from; j < info->to; j++) {
        load_blob(j);
    }
}

static int load_aircrafts(char *p, char *end, char *filename, int64_t now) {
    int count = 0;
    while (end - p > 0) {
        uint64_t value = 0;
        if (end - p >= (long) sizeof(value)) {
            value = *((uint64_t *) p);
            p += sizeof(value);
        }

        if (value != STATE_SAVE_MAGIC) {
            if (value != STATE_SAVE_MAGIC_END) {
                fprintf(stderr, "Incomplete state file: %s\n", filename);
                return -1;
            }
            break;
        }
        load_aircraft(&p, end, now);
        count++;
    }
    return count;
}

static void load_blob(int blob) {
    //fprintf(stderr, "load blob %d\n", blob);
    if (blob < 0 || blob >= STATE_BLOBS)
        fprintf(stderr, "load_blob: invalid argument: %d", blob);
    char filename[1024];
    int64_t now = mstime();
    int fd = -1;
    struct char_buffer cb;
    char *p;
    char *end;
    int lzo = 0;

    snprintf(filename, 1024, "%s/blob_%02x.lzol", Modes.state_dir, blob);
    fd = open(filename, O_RDONLY);
    if (fd != -1) {
        lzo = 1;
        cb = readWholeFile(fd, filename);
        close(fd);
    } else {
        Modes.writeInternalState = 1; // not the primary load method, immediately write state
        snprintf(filename, 1024, "%s/blob_%02x.gz", Modes.state_dir, blob);
        gzFile gzfp = gzopen(filename, "r");
        if (gzfp) {
            cb = readWholeGz(gzfp, filename);
            gzclose(gzfp);
            unlink(filename); // moving to lzo
        } else {
            snprintf(filename, 1024, "%s/blob_%02x", Modes.state_dir, blob);
            fd = open(filename, O_RDONLY);
            if (fd == -1) {
                fprintf(stderr, "missing state blob:");
                snprintf(filename, 1024, "%s/blob_%02x[.gz/.lzol]", Modes.state_dir, blob);
                perror(filename);
                return;
            }
            cb = readWholeFile(fd, filename);
            close(fd);
            unlink(filename); // moving to lzo
        }
    }
    if (!cb.buffer)
        return;
    p = cb.buffer;
    end = p + cb.len;

    if (lzo) {
        int lzo_out_alloc = state_chunk_size() * 8 / 7;
        char *lzo_out = aligned_malloc(lzo_out_alloc);
        lzo_uint uncompressed_len = 0;
        int res = 0;
        while (end - p > 0) {
            uint64_t value = 0;
            uint64_t compressed_len = 0;
            if (end - p >= (long) (sizeof(value) + sizeof(compressed_len))) {
                value = *((uint64_t *) p);
                p += sizeof(value);

                compressed_len = *((uint64_t *) p);
                p += sizeof(compressed_len);
            }
            //fprintf(stderr, "%d %08lld\n", blob, (long long) compressed_len);

            if (value != LZO_MAGIC) {
                fprintf(stderr, "Corrupt state file (LZO_MAGIC wrong): %s\n", filename);
                break;
            }

decompress:
            uncompressed_len = lzo_out_alloc;
            res = lzo1x_decompress_safe((unsigned char*) p, compressed_len, (unsigned char*) lzo_out, &uncompressed_len, NULL);
            if (res != LZO_E_OK) {
                lzo_out_alloc *= 2;
                fprintf(stderr, "decompression failed, trying larger buffer: %s\n", filename);
                if (lzo_out_alloc > 256 * 1024 * 1024 || !lzo_out) {
                    fprintf(stderr, "Corrupt state file (decompression failure): %s\n", filename);
                    break;
                }
                sfree(lzo_out);
                lzo_out = aligned_malloc(lzo_out_alloc);
                goto decompress;
            }

            if (load_aircrafts(lzo_out, lzo_out + uncompressed_len, filename, now) < 0) {
                break;
            }
            p += compressed_len;
        }

        sfree(lzo_out);
    } else {
        load_aircrafts(p, end, filename, now);
    }

    free(cb.buffer);
}

static inline void heatmapCheckAlloc(struct heatEntry **buffer, int64_t **slices, int64_t *alloc, int64_t len) {
    if (!*buffer || len + 8 >= *alloc) {
        *alloc += 8;
        *alloc *= 3;
        *buffer = realloc(*buffer, *alloc * sizeof(struct heatEntry));
        *slices = realloc(*slices, *alloc * sizeof(int64_t));
        if (!*buffer || !*slices) {
            fprintf(stderr, "<3> FATAL: handleHeatmap not enough memory, trying to allocate %lld bytes\n",
                    (long long) (*alloc * sizeof(struct heatEntry)));
            exit(1);
        }
    }
}

int handleHeatmap(int64_t now) {
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
    int64_t start = 1000 * (int64_t) (timegm(&utc));
    int64_t end = start + 30 * MINUTES;
    int64_t num_slices = (int)((30 * MINUTES) / Modes.heatmap_interval);


    char pathbuf[PATH_MAX];
    char tmppath[PATH_MAX];
    int64_t len = 0;
    int64_t len2 = 0;
    int64_t alloc = (50 + Modes.globalStatsCount.readsb_aircraft_with_position) * num_slices;
    struct heatEntry *buffer = NULL;
    int64_t *slices = NULL;

    heatmapCheckAlloc(&buffer, &slices, &alloc, len);

    for (int j = 0; j < AIRCRAFT_BUCKETS; j++) {
        for (struct aircraft *a = Modes.aircraft[j]; a; a = a->next) {
            if ((a->addr & MODES_NON_ICAO_ADDRESS) && a->airground == AG_GROUND) continue;
            if (a->trace_len == 0) continue;
            fourState stackBuffer[QUARTER_STACK / sizeof(fourState)];

            traceBuffer tb = reassembleTrace(a, -1, start, stackBuffer, sizeof(stackBuffer));
            int64_t next = start;
            int64_t slice = 0;
            uint32_t squawk = 0x8888; // impossible squawk
            uint64_t callsign = 0; // quackery

            int64_t callsign_interval = imax(Modes.heatmap_interval, 1 * MINUTES);
            int64_t next_callsign = start;

            for (int i = 0; i < tb.len; i++) {
                struct state *state = getState(tb.trace, i);
                if (state->timestamp > end)
                    break;
                struct state_all *all = getStateAll(tb.trace, i);
                if (state->timestamp >= start && all) {
                    uint64_t *cs = (uint64_t *) &(all->callsign);
                    if (state->timestamp >= next_callsign || *cs != callsign || squawk != all->squawk) {

                        next_callsign = state->timestamp + callsign_interval;
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
                        heatmapCheckAlloc(&buffer, &slices, &alloc, len);
                    }
                }
                if (state->timestamp < next)
                    continue;

                if (!state->baro_alt_valid && !state->geom_alt_valid)
                    continue;

                while (state->timestamp > next + Modes.heatmap_interval) {
                    next += Modes.heatmap_interval;
                    slice++;
                }

                uint32_t addrtype_5bits = ((uint32_t) state->addrtype) & 0x1F;

                buffer[len].hex = a->addr | (addrtype_5bits << 27);
                buffer[len].lat = state->lat;
                buffer[len].lon = state->lon;

                // altitude encoded in steps of 25 ft ... file convention
                if (state->on_ground)
                    buffer[len].alt = -123; // on ground
                else if (state->baro_alt_valid)
                    buffer[len].alt = nearbyint(state->baro_alt / (_alt_factor * 25.0f));
                else if (state->geom_alt_valid)
                    buffer[len].alt = nearbyint(state->geom_alt / (_alt_factor * 25.0f));
                else
                    buffer[len].alt = 0;

                if (state->gs_valid)
                    buffer[len].gs = nearbyint(state->gs / _gs_factor * 10.0f);
                else
                    buffer[len].gs = -1; // invalid

                slices[len] = slice;

                len++;
                heatmapCheckAlloc(&buffer, &slices, &alloc, len);

                next += Modes.heatmap_interval;
                slice++;

            }
            // free reassembled trace
            if (tb.free)
                sfree(tb.trace);
        }
    }

    struct heatEntry *buffer2 = malloc(alloc * sizeof(struct heatEntry));
    if (!buffer2) {
        fprintf(stderr, "<3> FATAL: handleHeatmap not enough memory, trying to allocate %lld bytes\n",
                (long long) alloc * sizeof(struct heatEntry));
        exit(1);
    }
    ssize_t indexSize = num_slices * sizeof(struct heatEntry);
    struct heatEntry *index = malloc(indexSize);
    memset(index, 0, indexSize); // avoid having to set zero individually

    for (int i = 0; i < num_slices; i++) {
        struct heatEntry specialSauce = (struct heatEntry) {0};
        int64_t slice_stamp = start + i * Modes.heatmap_interval;
        specialSauce.hex = 0xe7f7c9d;
        specialSauce.lat = slice_stamp >> 32;
        specialSauce.lon = slice_stamp & ((1ULL << 32) - 1);
        specialSauce.alt = Modes.heatmap_interval;

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
    snprintf(tmppath, PATH_MAX, "%s.readsb_tmp", pathbuf);

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
        writeGz(gzfp, index, indexSize, tmppath);

        ssize_t toWrite = len2 * sizeof(struct heatEntry);
        writeGz(gzfp, buffer2, toWrite, tmppath);

        gzclose(gzfp);
    }
    if (rename(tmppath, pathbuf) == -1) {
        fprintf(stderr, "heatmap rename(): %s -> %s", tmppath, pathbuf);
        perror("");
    }

    free(index);
    free(buffer);
    free(buffer2);
    free(slices);

    return 1;
}

static void gzipFile(char *file) {
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
    int res = gzsetparams(gzfp, 9, Z_DEFAULT_STRATEGY);
    if (res < 0) {
        fprintf(stderr, "gzsetparams fail: %d", res);
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
    gzipFile(filename);

    snprintf(filename, PATH_MAX, "%s/acas/acas.json", dateDir);
    gzipFile(filename);
}

// this doesn't need to run under lock as the there should be no need for synchronisation
void checkNewDay(int64_t now) {
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
        if (Modes.trace_hist_only) {
            chmod(filename, 0755);
        }

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
void checkNewDayLocked(int64_t now) {
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
    struct timespec watch;

    if (Modes.state_dir) {
        fprintf(stderr, "saving state .....\n");
        startWatch(&watch);
    }

    int64_t now = mstime();

    threadpool_task_t *tasks = Modes.allPoolTasks;
    struct task_info *ranges = Modes.allPoolRanges;

    //int taskCount = imin(STATE_BLOBS, Modes.allPoolMaxTasks);
    int taskCount = imin(Modes.allPoolSize * 3, Modes.allPoolMaxTasks);
    int stride = STATE_BLOBS / taskCount + 1;

    // assign tasks
    for (int i = 0; i < taskCount; i++) {
        threadpool_task_t *task = &tasks[i];
        struct task_info *range = &ranges[i];

        range->now = now;
        range->from = i * stride;
        range->to = imin(STATE_BLOBS, range->from + stride);

        task->function = save_blobs;
        task->argument = range;
    }
    // run tasks
    threadpool_run(Modes.allPool, tasks, taskCount);

    if (Modes.outline_json) {
        char pathbuf[PATH_MAX];
        snprintf(pathbuf, PATH_MAX, "%s/rangeDirs.gz", Modes.state_dir);
        gzFile gzfp = gzopen(pathbuf, "wb");
        if (gzfp) {
            writeGz(gzfp, &Modes.lastRangeDirHour, sizeof(Modes.lastRangeDirHour), pathbuf);
            writeGz(gzfp, Modes.rangeDirs, sizeof(Modes.rangeDirs), pathbuf);
            gzclose(gzfp);
        }
    }

    if (Modes.state_dir) {
        double elapsed = stopWatch(&watch) / 1000.0;
        fprintf(stderr, " .......... done, saved %llu aircraft in %.3f seconds!\n", (unsigned long long) Modes.total_aircraft_count, elapsed);
    }
}

static void readInternalMiscTask(void *arg) {
    arg = arg; // unused
    char pathbuf[PATH_MAX];

    if (Modes.outline_json) {
        struct char_buffer cb;
        snprintf(pathbuf, PATH_MAX, "%s/rangeDirs.gz", Modes.state_dir);
        gzFile gzfp = gzopen(pathbuf, "r");
        if (gzfp) {
            cb = readWholeGz(gzfp, pathbuf);
            gzclose(gzfp);
            if (cb.len == sizeof(Modes.lastRangeDirHour) + sizeof(Modes.rangeDirs)) {
                fprintf(stderr, "actual range outline, read bytes: %zu\n", cb.len);

                char *p = cb.buffer;
                memcpy(&Modes.lastRangeDirHour, p, sizeof(Modes.lastRangeDirHour));
                p += sizeof(Modes.lastRangeDirHour);
                memcpy(Modes.rangeDirs, p, sizeof(Modes.rangeDirs));
            }
            free(cb.buffer);
        }
    }
}

void readInternalState() {
    int retval = mkdir(Modes.state_dir, 0755);
    if (retval != 0 && errno != EEXIST) {
        fprintf(stderr, "Unable to create state directory (%s): %s\n", Modes.state_dir, strerror(errno));
        return;
    }
    if (retval == 0) {
        fprintf(stderr, "%s: state directory didn't exist, created it, possible reasons: "
                "first start with state enabled / directory not backed by persistent storage\n",
                Modes.state_dir);
        fprintf(stderr, "loading state ..... FAILED!\n");
        return;
    }

    fprintf(stderr, "loading state .....\n");
    struct timespec watch;
    startWatch(&watch);

    int64_t now = mstime();

    threadpool_task_t *tasks = Modes.allPoolTasks;
    struct task_info *ranges = Modes.allPoolRanges;

    //int parts = imin(STATE_BLOBS, Modes.allPoolMaxTasks - 1);
    int parts = imin(Modes.allPoolSize * 3, Modes.allPoolMaxTasks - 1);

    // assign tasks
    int taskCount = 0;
    {
        threadpool_task_t *task = &tasks[taskCount];
        task->function = readInternalMiscTask;
        task->argument = NULL;
        taskCount++;
    }

    int stride = STATE_BLOBS / parts + 1;
    for (int i = 0; i < parts; i++) {
        threadpool_task_t *task = &tasks[taskCount];
        struct task_info *range = &ranges[taskCount];

        //fprintf(stderr, "%d\n", i);

        range->now = now;
        range->from = i * stride;
        range->to = imin(STATE_BLOBS, range->from + stride);

        task->function = load_blobs;
        task->argument = range;

        taskCount++;
    }
    // run tasks
    threadpool_run(Modes.allPool, tasks, taskCount);

    int64_t aircraftCount = 0; // includes quite old aircraft, just for checking hash table fill
    for (int j = 0; j < AIRCRAFT_BUCKETS; j++) {
        for (struct aircraft *a = Modes.aircraft[j]; a; a = a->next) {
            aircraftCount++;
        }
    }
    Modes.total_aircraft_count = aircraftCount;

    double elapsed = stopWatch(&watch) / 1000.0;
    fprintf(stderr, " .......... done, loaded %llu aircraft in %.3f seconds!\n", (unsigned long long) aircraftCount, elapsed);
    fprintf(stderr, "aircraft table fill: %0.1f\n", aircraftCount / (double) AIRCRAFT_BUCKETS );
}

void traceDelete() {

    struct hexInterval* entry = Modes.deleteTrace;
    while (entry) {
        struct hexInterval* curr = entry;
        struct aircraft *a = aircraftGet(curr->hex);
        if (!a || a->trace_len == 0)
            continue;

        traceUsePosBuffered(a);

        fourState stackBuffer[QUARTER_STACK / sizeof(fourState)];

        traceBuffer tb = reassembleTrace(a, -1, -1, stackBuffer, sizeof(stackBuffer));
        fourState *trace = tb.trace;
        int trace_len = tb.len;

        int start = 0;
        int end = trace_len; // exclusive

        int64_t from = curr->from * 1000;
        int64_t to = curr->to * 1000;

        for (int i = 0; i < trace_len; i++) {
            int64_t timestamp = getState(trace, i)->timestamp;
            if (timestamp <= from) {
                start = i;
            }
            if (timestamp > to) {
                end = i;
                break;
            }
        }

        // align to fourState, delete whole fourState tuple if stuff we want to delete is contained
        start = start / SFOUR; // round down
        end = (end + SFOUR - 1) / SFOUR;

        // end points past the last point to be deleted
        if (end >= (int)(stateBytes(trace_len) / sizeof(fourState))) {
            end = stateBytes(trace_len) / sizeof(fourState);
        } else {
            memmove(trace + start, trace + end, (end - start) * sizeof(fourState));
        }

        trace_len -= (end - start) * SFOUR;

        setTrace(a, trace + start, trace_len);
        // free reassembled trace
        if (tb.free)
            sfree(tb.trace);

        entry = entry->next;
        fprintf(stderr, "Deleted %06x from %lld to %lld\n", curr->hex, (long long) curr->from, (long long) curr->to);
        sfree(curr);
    }
    Modes.deleteTrace = NULL;;
}

/*
void *load_state(void *arg) {
    int64_t now = mstime();
    char pathbuf[PATH_MAX];
    //struct stat fileinfo = {0};
    //fstat(fd, &fileinfo);
    //off_t len = fileinfo.st_size;
    int thread_number = *((int *) arg);
    srandom(get_seed());
    for (int i = 0; i < 256; i++) {
        if (i % Modes.io_threads != thread_number)
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
