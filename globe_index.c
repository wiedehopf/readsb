#include "readsb.h"

#define LEG_FOCUS (0xc0ffeeba)

static void mark_legs(struct aircraft *a);
static void load_blob(int blob);

ssize_t check_write(int fd, const void *buf, size_t count, const char *error_context) {
    ssize_t res = write(fd, buf, count);
    if (res < 0)
        perror(error_context);
    else if (res != (ssize_t) count)
        fprintf(stderr, "%s: Only %zd of %zd bytes written!\n", error_context, res, count);
    return res;
}

void init_globe_index(struct tile *s_tiles) {
    int count = 0;

    // Arctic
    s_tiles[count++] = (struct tile) {
        60, -130,
        90, 150
    };

    // North Pacific
    s_tiles[count++] = (struct tile) {
        10, 150,
        90, -130
    };

    // Northern Canada
    s_tiles[count++] = (struct tile) {
        50, -130,
        60, -70
    };

    // Northwest USA
    s_tiles[count++] = (struct tile) {
        40, -130,
        50, -100
    };

    // West Russia
    s_tiles[count++] = (struct tile) {
        40, 20,
        60, 50
    };

    // Central Russia
    s_tiles[count++] = (struct tile) {
        30, 50,
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
        60, 150
    };

    // Persian Gulf / Arabian Sea
    s_tiles[count++] = (struct tile) {
        10, 50,
        30, 70
    };

    // India
    s_tiles[count++] = (struct tile) {
        10, 70,
        30, 90
    };

    // South China and ICAO special use
    s_tiles[count++] = (struct tile) {
        10, 90,
        30, 110
    };
    s_tiles[count++] = (struct tile) {
        10, 110,
        30, 150
    };


    // South Atlantic and Indian Ocean
    s_tiles[count++] = (struct tile) {
        -90, -40,
        10, 110
    };

    // Australia
    s_tiles[count++] = (struct tile) {
        -90, 110,
        10, 160
    };

    // South Pacific and NZ
    s_tiles[count++] = (struct tile) {
        -90, 160,
        10, -90
    };

    // North South America
    s_tiles[count++] = (struct tile) {
        -10, -90,
        10, -40
    };

    // South South America
    s_tiles[count++] = (struct tile) {
        -90, -90,
        -10, -40
    };

    // Guatemala / Mexico
    s_tiles[count++] = (struct tile) {
        10, -130,
        30, -90
    };

    // Cuba / Haiti / Honduras
    s_tiles[count++] = (struct tile) {
        10, -90,
        20, -70
    };



    // North Africa
    s_tiles[count++] = (struct tile) {
        10, -10,
        40, 30
    };

    // Middle East
    s_tiles[count++] = (struct tile) {
        10, 30,
        40, 50
    };

    // North Atlantic
    s_tiles[count++] = (struct tile) {
        10, -70,
        60, -10
    };

    if (count + 1 > GLOBE_SPECIAL_INDEX)
        fprintf(stderr, "increase GLOBE_SPECIAL_INDEX please!\n");
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

    return (i * GLOBE_LAT_MULT + j + GLOBE_MIN_INDEX);
    // highest number returned: globe_index(90, 180)
    // first 1000 are reserved for special use
}

int globe_index_index(int index) {
    double lat = ((index - GLOBE_MIN_INDEX) /  GLOBE_LAT_MULT) * GLOBE_INDEX_GRID - 90;
    double lon = ((index - GLOBE_MIN_INDEX) % GLOBE_LAT_MULT) * GLOBE_INDEX_GRID - 180;
    return globe_index(lat, lon);
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
    char tstring[100];
    strftime (tstring, 100, TDATE_FORMAT, utc);
    snprintf(dateDir, PATH_MAX * 3/4, "%s/%s", base_dir, tstring);
    if (mkdir(dateDir, 0755) && errno != EEXIST)
        perror(dateDir);
}

static void traceWrite(struct aircraft *a, uint64_t now, int init) {
    struct char_buffer recent;
    struct char_buffer full;
    struct char_buffer hist;
    char filename[PATH_MAX];
    static uint32_t count2, count3, count4;

    // nineteen_ago changes day 19 min after midnight: stop writing the previous days traces
    // twenty_ago changes day 20 min after midnight: allow webserver to read the previous days traces
    // this is in seconds, not milliseconds
    time_t nineteen_ago = now / 1000 - 19 * 60;

    recent.len = 0;
    full.len = 0;
    hist.len = 0;

    if (Modes.debug_traceCount && ++count2 % 1000 == 0)
        fprintf(stderr, "recent trace write: %u\n", count2);

    a->trace_write = 0;

    if (!a->trace_alloc)
        return;

    if (!init) {
        mark_legs(a);
    }

    int start24 = 0;
    for (int i = 0; i < a->trace_len; i++) {
        if (a->trace[i].timestamp > now - (24 * HOURS + 15 * MINUTES)) {
            start24 = i;
            break;
        }
    }

    int recent_points = init ? 42 : 142;
    int start_recent = a->trace_len - recent_points;
    if (start_recent < start24)
        start_recent = start24;

    // write recent trace to /run
    recent = generateTraceJson(a, start_recent, -1);

    if (now > a->trace_next_mw || a->trace_full_write > 35 || now > a->trace_next_fw) {
        int write_perm = 0;

        if (Modes.debug_traceCount && ++count3 % 1000 == 0)
            fprintf(stderr, "memory trace writes: %u\n", count3);

        // write full trace to /run
        full = generateTraceJson(a, start24, -1);

        if (a->trace_full_write == 0xc0ffee)
            a->trace_next_mw = now + random() % (20 * MINUTES);
        else
            a->trace_next_mw = now + 20 * MINUTES + random() % (2 * MINUTES);

        if (now > a->trace_next_fw || a->trace_full_write == 0xc0ffee) {
            write_perm = 1;

            if (a->trace_full_write == 0xc0ffee) {
                a->trace_next_fw = now + random() % (2 * HOURS);
            } else {
                a->trace_next_fw = now + 2 * HOURS + random() % (30 * MINUTES);
            }
        }
        a->trace_full_write = 0;

        //fprintf(stderr, "%06x\n", a->addr);

        // prepare writing the permanent history
        if (write_perm && a->trace_len > 0 &&
                Modes.globe_history_dir && !(a->addr & MODES_NON_ICAO_ADDRESS)) {

            struct tm utc;
            gmtime_r(&nineteen_ago, &utc);
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
                }
                if (a->trace[i].timestamp < end_of_day) {
                    end = i;
                }
            }
            if (start >= 0 && end >= 0 && end >= start)
                hist = generateTraceJson(a, start, end);
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

    if (hist.len > 0) {
        char tstring[100];
        struct tm utc;
        gmtime_r(&nineteen_ago, &utc);
        strftime (tstring, 100, TDATE_FORMAT, &utc);

        snprintf(filename, PATH_MAX, "%s/traces/%02x/trace_full_%s%06x.json", tstring, a->addr % 256, (a->addr & MODES_NON_ICAO_ADDRESS) ? "~" : "", a->addr & 0xFFFFFF);
        filename[PATH_MAX - 101] = 0;

        writeJsonToGzip(Modes.globe_history_dir, filename, hist, 9);
        free(hist.buffer);

        if (Modes.debug_traceCount && ++count4 % 100 == 0)
            fprintf(stderr, "perm trace writes: %u\n", count4);
    }
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

    if (end - *p < (long) sizeof(struct aircraft))
        return -1;

    struct aircraft *a = malloc(sizeof(struct aircraft));
    memcpy(a, *p, sizeof(struct aircraft));
    *p += sizeof(struct aircraft);

    if (a->size_struct_aircraft != sizeof(struct aircraft)) {
            fprintf(stderr, "sizeof(struct aircraft) has changed, unable to read state!\n");
        free(a);
        return -1;
    }

    a->trace = NULL;
    a->trace_all = NULL;

    if (!Modes.keep_traces) {
        a->trace_alloc = 0;
        a->trace_len = 0;
    }

    // just in case we have bogus values saved, make sure they time out
    if (a->seen_pos > now + 26 * HOURS)
        a->seen_pos = 0;
    if (a->seen > now + 26 * HOURS)
        a->seen = now;

    // read trace
    int size_state = stateBytes(a->trace_len);
    int size_all = stateAllBytes(a->trace_len);
    if (a->trace_len > 0
            && a->trace_len <= 10 * TRACE_SIZE
            && a->trace_alloc >= a->trace_len
            && a->trace_alloc <= 10 * TRACE_SIZE
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

            if (a->addr == LEG_FOCUS) {
                a->trace_next_fw = now;
                fprintf(stderr, "%06x trace len: %d\n", a->addr, a->trace_len);
            }
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

    a->first_message = NULL;
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

    if (a->trace_alloc && Modes.json_dir && Modes.json_globe_index && now < a->seen_pos + 2 * MINUTES) {
        // the value below is again overwritten in track.c when a fullWrite is done on startup
        a->trace_next_mw = a->trace_next_fw = now + 1 * MINUTES + random() % (2 * MINUTES);
        // setting mw and fw ensures only the recent trace is written, full trace would take too long
        traceWrite(a, now, 1);
    }

    return 0;
}

void *jsonTraceThreadEntryPoint(void *arg) {

    int thread = * (int *) arg;

    srandom(get_seed());

    int part = 0;
    int n_parts = 64; // power of 2

    int thread_section_len = (AIRCRAFT_BUCKETS / TRACE_THREADS);
    int thread_start = thread * thread_section_len;
    int thread_end = thread_start + thread_section_len;
    //fprintf(stderr, "%d %d\n", thread_start, thread_end);
    int section_len = thread_section_len / n_parts;

    // write each part every 10 seconds
    uint64_t sleep_ms = 10 * SECONDS / n_parts;

    pthread_mutex_lock(&Modes.jsonTraceThreadMutex[thread]);

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    while (!Modes.exit) {
        struct aircraft *a;
        int err;


        if (Modes.json_dir && Modes.json_globe_index) {
            incTimedwait(&ts, sleep_ms);
            err = pthread_cond_timedwait(&Modes.jsonTraceThreadCond[thread], &Modes.jsonTraceThreadMutex[thread], &ts);
        } else {
            err = pthread_cond_wait(&Modes.jsonTraceThreadCond[thread], &Modes.jsonTraceThreadMutex[thread]);
        }
        if (err && err != ETIMEDOUT)
            fprintf(stderr, "jsonTraceThread: pthread_cond_timedwait unexpected error: %s\n", strerror(err));

        //fprintf(stderr, "%d %d %d\n", part, start, end);
        if (Modes.exit)
            break;

        uint64_t now = mstime();

        if (Modes.removeStale) {
            if (Modes.removeStaleThread[thread]) {
                trackRemoveStaleThread(thread_start, thread_end, now);
                Modes.removeStaleThread[thread] = 0;
                pthread_mutex_unlock(&Modes.jsonTraceThreadMutexFin[thread]);
                //fprintf(stderr, "%d %d %d\n", thread, thread_start, thread_end);
            }
        }
        if (!Modes.removeStale && Modes.json_dir && Modes.json_globe_index) {
            int start = thread_start + part * section_len;
            int end = start + section_len;

            struct timespec start_time;
            start_cpu_timing(&start_time);

            for (int j = start; j < end; j++) {
                for (a = Modes.aircraft[j]; a; a = a->next) {
                    if (a->trace_write)
                        traceWrite(a, now, 0);
                }
            }

            part++;
            part %= n_parts;

            end_cpu_timing(&start_time, &Modes.stats_current.trace_json_cpu[thread]);
        }
    }

    pthread_mutex_unlock(&Modes.jsonTraceThreadMutexFin[thread]);
    pthread_mutex_unlock(&Modes.jsonTraceThreadMutex[thread]);

#ifndef _WIN32
    pthread_exit(NULL);
#else
    return NULL;
#endif
}

static void mark_legs(struct aircraft *a) {
    if (a->trace_len < 20)
        return;

    int high = 0;
    int low = 100000;

    int last_five[5] = { 0 };
    uint32_t five_pos = 0;

    double sum = 0;

    struct state *last_leg = NULL;
    struct state *new_leg = NULL;

    for (int i = 0; i < a->trace_len; i++) {
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

    if (a->addr == LEG_FOCUS) {
        fprintf(stderr, "threshold: %d\n", threshold);
        fprintf(stderr, "trace_len: %d\n", a->trace_len);
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

    int was_ground = 0;

    for (int i = 0; i < 5; i++)
        last_five[i] = 0;
    five_pos = 0;

    int prev_tmp = 0;
    for (int i = 1; i < a->trace_len; i++) {
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
            altitude = avg - threshold / 2;
        } else {
            if (five_pos == 0) {
                for (int i = 0; i < 5; i++)
                    last_five[i] = altitude;
            } else {
                last_five[five_pos % 5] = altitude;
            }
            five_pos++;
        }

        if (!on_ground)
            last_airborne = state->timestamp;
        else
            last_ground = state->timestamp;

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
                if (a->addr == LEG_FOCUS) {
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
                if (a->addr == LEG_FOCUS) {
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
            if (a->addr == LEG_FOCUS)
                fprintf(stderr, "ground leg\n");
            leg_now = 1;
        }
        double distance = greatcircle(
                (double) a->trace[i].lat / 1E6,
                (double) a->trace[i].lon / 1E6,
                (double) a->trace[i-1].lat / 1E6,
                (double) a->trace[i-1].lon / 1E6
                );

        if ( elapsed > 30 * 60 * 1000 && distance < 10E3 * (elapsed / (30 * 60 * 1000.0)) && distance > 1) {
            leg_now = 1;
            if (a->addr == LEG_FOCUS)
                fprintf(stderr, "time/distance leg, elapsed: %0.fmin, distance: %0.f\n", elapsed / (60 * 1000.0), distance / 1000.0);
        }

        int leg_float = 0;
        if (major_climb && major_descent &&
                (major_climb > major_descent + 8 * MINUTES || last_ground > major_descent - 2 * MINUTES)
           ) {
            for (int i = major_descent_index + 1; i < major_climb_index; i++) {
                if (a->trace[i].timestamp > a->trace[i - 1].timestamp + 5 * MINUTES) {
                    leg_float = 1;
                    if (a->addr == LEG_FOCUS)
                        fprintf(stderr, "float leg\n");
                }
            }
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

            if (a->addr == LEG_FOCUS) {
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
        a->trace_full_write = 9999;
        //fprintf(stderr, "%06x\n", a->addr);
    }
}

void ca_init (struct craftArray *ca) {
    *ca = (struct craftArray) {0};
}

void ca_destroy (struct craftArray *ca) {
    if (ca->list)
        free(ca->list);
    *ca = (struct craftArray) {0};
}

void ca_add (struct craftArray *ca, struct aircraft *a) {
    if (ca->alloc == 0) {
        ca->alloc = 64;
        ca->list = realloc(ca->list, ca->alloc * sizeof(struct aircraft *));
    }
    if (ca->len == ca->alloc) {
        ca->alloc *= 2;
        ca->list = realloc(ca->list, ca->alloc * sizeof(struct aircraft *));
    }
    if (!ca->list) {
        fprintf(stderr, "ca_add(): out of memory!\n");
        exit(1);
    }
    for (int i = 0; i < ca->len; i++) {
        if (a == ca->list[i]) {
            fprintf(stderr, "ca_add(): double add!\n");
            return;
        }
    }
    for (int i = 0; i < ca->len; i++) {
        if (ca->list[i] == NULL) {
            ca->list[i] = a;
            return; // added, len stays the same
        }
    }
    ca->list[ca->len] = a;  // added, len is incremented
    ca->len++;
    return;
}

void ca_remove (struct craftArray *ca, struct aircraft *a) {
    if (!ca->list)
        return;
    for (int i = 0; i < ca->len; i++) {
        if (ca->list[i] == a) {
            ca->list[i] = NULL;

            if (i == ca->len - 1)
                ca->len--;

            return;
        }
    }
    //fprintf(stderr, "hex: %06x, ca_remove(): pointer not in array!\n", a->addr);
    return;
}

void set_globe_index (struct aircraft *a, int new_index) {

    int old_index = a->globe_index;
    a->globe_index = new_index;

    if (old_index == new_index)
        return;
    if (new_index > GLOBE_MAX_INDEX || old_index > GLOBE_MAX_INDEX) {
        fprintf(stderr, "hex: %06x, old_index: %d, new_index: %d, GLOBE_MAX_INDEX: %d\n", a->addr, Modes.globeLists[new_index].len, new_index, GLOBE_MAX_INDEX );
        return;
    }
    if (old_index >= 0)
        ca_remove(&Modes.globeLists[old_index], a);
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

void traceCleanup(struct aircraft *a) {
    free(a->trace);
    free(a->trace_all);

    a->trace_alloc = 0;
    a->trace = NULL;
    a->trace_all = NULL;

    traceUnlink(a);
}

int traceAdd(struct aircraft *a, uint64_t now) {

    if (!Modes.keep_traces)
        return 0 ;

    for (int i = max(0, a->trace_len - 6); i < a->trace_len; i++) {
        if ( (int32_t) (a->lat * 1E6) == a->trace[i].lat
                && (int32_t) (a->lon * 1E6) == a->trace[i].lon ) {
            return 0 ;
        }
    }

    int on_ground = 0;
    float turn_density = 5;
    float track = a->track;
    int track_valid = trackVState(now, &a->track_valid, &a->position_valid);
    struct state *last = NULL;
    int stale = 0;

    if (now > a->seenPosReliable + 15 * SECONDS) {
        stale = 1;
        // save a point if reception is spotty so we can mark track as spotty on display
        goto save_state;
        //if (a->addr == Modes.cpr_focus)
        //    fprintf(stderr, "stale, elapsed: %0.1f\n", (now - a->seenPosReliable) / 1000.0);
    }

    if (trackDataValid(&a->airground_valid) && a->airground_valid.source >= SOURCE_MODE_S_CHECKED && a->airground == AG_GROUND) {
        on_ground = 1;

        if (trackVState(now, &a->true_heading_valid, &a->position_valid)) {
            track = a->true_heading;
            track_valid = 1;
        } else {
            track_valid = 0;
        }
    }

    if (a->trace_len == 0 )
        goto save_state;

    last = &(a->trace[a->trace_len-1]);
    float track_diff = fabs(track - last->track / 10.0);
    uint64_t elapsed = now - last->timestamp;
    if (now < last->timestamp)
        elapsed = 0;

    int32_t last_alt = last->altitude * 25;

    if (on_ground != last->flags.on_ground) {
        goto save_state;
    }

    double distance = greatcircle(a->trace_llat, a->trace_llon, a->lat, a->lon);

    // record non moving targets every 10 minutes
    if (elapsed > 20 * Modes.json_trace_interval)
        goto save_state;
    if (distance < 40)
        goto no_save_state;

    if (elapsed > Modes.json_trace_interval) // default 30000 ms
        goto save_state;

    if (elapsed < 2000)
        goto no_save_state;


    if (on_ground) {
        if (distance * track_diff > 200)
            goto save_state;

        if (distance > 400)
            goto save_state;
    }

    if (trackVState(now, &a->altitude_baro_valid, &a->position_valid)
            && a->alt_reliable >= ALTITUDE_BARO_RELIABLE_MAX / 5) {
        if (!last->flags.altitude_valid) {
            goto save_state;
        }
        if (last->flags.altitude_valid) {

            if (a->altitude_baro > 8000 && abs((a->altitude_baro + 250)/500 - (last_alt + 250)/500) >= 1) {
                //fprintf(stderr, "1");
                goto save_state;
            }

            {
                int offset = 125;
                int div = 250;
                int alt_add = (a->altitude_baro >= 0) ? offset : (-1 * offset);
                int last_alt_add = (last_alt >= 0) ? offset : (-1 * offset);
                if (a->altitude_baro <= 8000 && a->altitude_baro > 4000
                        && abs((a->altitude_baro + alt_add)/div - (last_alt + last_alt_add)/div) >= 1) {
                    //fprintf(stderr, "2");
                    goto save_state;
                }
            }

            {
                int offset = 62;
                int div = 125;
                int alt_add = (a->altitude_baro >= 0) ? offset : (-1 * offset);
                int last_alt_add = (last_alt >= 0) ? offset : (-1 * offset);
                if (a->altitude_baro <= 4000
                        && abs((a->altitude_baro + alt_add)/div - (last_alt + last_alt_add)/div) >= 1) {
                    //fprintf(stderr, "3");
                    goto save_state;
                }
            }

            if (abs(a->altitude_baro - last_alt) >= 100 && now > last->timestamp + ((1000 * 12000)  / abs(a->altitude_baro - last_alt))) {
                //fprintf(stderr, "4");
                //fprintf(stderr, "%06x %d %d\n", a->addr, abs(a->altitude_baro - last_alt), ((1000 * 12000)  / abs(a->altitude_baro - last_alt)));
                goto save_state;
            }
        }
    }

    if (last->flags.track_valid && track_valid) {
        if (track_diff > 0.5
                && (elapsed > (uint64_t) (100.0 * 1000.0 / turn_density / track_diff))
           ) {
            //fprintf(stderr, "t");
            goto save_state;
        }
    }

    if (trackDataValid(&a->gs_valid) && last->flags.gs_valid && fabs(last->gs / 10.0 - a->gs) > 8) {
        //fprintf(stderr, "s\n");
        //fprintf(stderr, "%06x %0.1f %0.1f\n", a->addr, fabs(last->gs / 10.0 - a->gs), a->gs);
        goto save_state;
    }

    goto no_save_state;
save_state:

    if (!a->trace) {
        // allocate trace memory
        traceRealloc(a, 3 * TRACE_MARGIN);
        a->trace->timestamp = now;
        a->trace_full_write = 9999; // rewrite full history file

        //fprintf(stderr, "%06x: new trace\n", a->addr);
    }
    if (a->trace_len + 1 >= a->trace_alloc) {
        static uint64_t antiSpam;
        if (now > antiSpam + 30 * SECONDS) {
            fprintf(stderr, "CHECK CPU LOAD (maybe loop?) %06x: trace_len + 1 >= a->trace_alloc (%d).\n", a->addr, a->trace_len);
            antiSpam = now;
        }
        goto no_save_state;
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

    // bookkeeping:
    a->trace_llat = a->lat;
    a->trace_llon = a->lon;

    (a->trace_len)++;
    a->trace_write = 1;
    a->trace_full_write++;

    //fprintf(stderr, "Added to trace for %06x (%d).\n", a->addr, a->trace_len);

    return 1;

no_save_state:
    return 0;
}


void save_blob(int blob) {
    if (!Modes.state_dir)
        return;
    //static int count;
    //fprintf(stderr, "Save blob: %02x, count: %d\n", blob, ++count);
    if (blob < 0 || blob > STATE_BLOBS)
        fprintf(stderr, "save_blob: invalid argument: %04d", blob);

    int gzip = 1;

    char filename[PATH_MAX];
    char tmppath[PATH_MAX];
    if (gzip) {
        snprintf(filename, 1024, "%s/blob_%04d", Modes.state_dir, blob);
        unlink(filename);
        snprintf(filename, 1024, "%s/blob_%04d.gz", Modes.state_dir, blob);
    } else {
        snprintf(filename, 1024, "%s/blob_%04d.gz", Modes.state_dir, blob);
        unlink(filename);
        snprintf(filename, 1024, "%s/blob_%04d", Modes.state_dir, blob);
    }
    snprintf(tmppath, PATH_MAX, "%s/tmp.%lx_%lx", Modes.state_dir, random(), random());

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

    int alloc = 32 * 1024 * 1024;
    unsigned char *buf = malloc(alloc);
    unsigned char *p = buf;

    //uint64_t now = mstime();

    for (int j = start; j < end; j++) {
        for (struct aircraft *a = Modes.aircraft[j]; a; a = a->next) {
            if (!a->seen_pos && a->trace_len == 0)
                continue;
            if (a->addr & MODES_NON_ICAO_ADDRESS)
                continue;
            if (a->messages < 2)
                continue;

            memcpy(p, &magic, sizeof(magic));
            p += sizeof(magic);

            //if (Modes.exit && now < a->seenPosReliable + 8 * SECONDS && posReliable(a))
            //    traceAdd(a, a->seen_pos);

            int size_state = stateBytes(a->trace_len);
            int size_all = stateAllBytes(a->trace_len);

            if (p + size_state + size_all + sizeof(struct aircraft) < buf + alloc) {

                memcpy(p, a, sizeof(struct aircraft));
                p += sizeof(struct aircraft);
                if (a->trace_len > 0) {
                    memcpy(p, a->trace, size_state);
                    p += size_state;
                    memcpy(p, a->trace_all, size_all);
                    p += size_all;
                }
            } else {
                fprintf(stderr, "%06x: too big for save_blob!\n", a->addr);
            }

            if (p - buf > alloc - 4 * 1024 * 1024) {
                fprintf(stderr, "buffer almost full: loop_write %d KB\n", (int) ((p - buf) / 1024));
                if (gzip) {
                    writeGz(gzfp, buf, p - buf, tmppath);
                } else {
                    check_write(fd, buf, p - buf, tmppath);
                }

                p = buf;
            }
        }
    }
    magic--;
    memcpy(p, &magic, sizeof(magic));
    p += sizeof(magic);

    //fprintf(stderr, "end_write %d KB\n", (int) ((p - buf) / 1024));
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

    snprintf(filename, 1024, "%s/blob_%04d.gz", Modes.state_dir, blob);
    gzFile gzfp = gzopen(filename, "r");
    if (gzfp) {
        cb = readWholeGz(gzfp, filename);
        gzclose(gzfp);
    } else {
        snprintf(filename, 1024, "%s/blob_%04d", Modes.state_dir, blob);
        fd = open(filename, O_RDONLY);
        if (fd == -1) {
            fprintf(stderr, "missing state blob:");
            snprintf(filename, 1024, "%s/blob_%04d[.gz]", Modes.state_dir, blob);
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
            memcpy(&value, p, sizeof(value));
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

int checkHeatmap(uint64_t now) {
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
    return 1;
}

void *handleHeatmap(void *arg) {
    MODES_NOTUSED(arg);

    struct timespec start_time;
    start_cpu_timing(&start_time);

    time_t nowish = (mstime() - 30 * MINUTES)/1000;
    struct tm utc;
    gmtime_r(&nowish, &utc);
    int half_hour = utc.tm_hour * 2 + utc.tm_min / 30;

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
            uint32_t squawk = 8888; // impossible squawk
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

                        if (a->addr == LEG_FOCUS) {
                            fprintf(stderr, "squawk: %d %04x\n", d, s);
                        }

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

    end_cpu_timing(&start_time, &Modes.stats_current.heatmap_and_state_cpu);

    //fprintf(stderr, "heatmapMutex unlock\n");
    pthread_mutex_unlock(&Modes.heatmapMutex);
    return NULL;
}


void checkNewDay() {
    char filename[PATH_MAX];
    char dateDir[PATH_MAX * 3/4];
    uint64_t now = mstime();
    struct tm utc;

    if (!Modes.globe_history_dir || !Modes.json_globe_index)
        return;

    // nineteen_ago changes day 19 min after midnight: stop writing the previous days traces
    // twenty_ago changes day 20 min after midnight: allow webserver to read the previous days traces
    // this is in seconds, not milliseconds
    time_t twenty_ago = now / 1000 - 20 * 60;
    gmtime_r(&twenty_ago, &utc);

    if (utc.tm_mday != Modes.traceDay) {
        Modes.traceDay = utc.tm_mday;

        time_t yesterday = now / 1000 - 24 * 3600;
        gmtime_r(&yesterday, &utc);

        createDateDir(Modes.globe_history_dir, &utc, dateDir); // doesn't usually create a directory ... but use the function anyhow worst that can happen is an empty directory for yesterday

        snprintf(filename, PATH_MAX, "%s/traces", dateDir);
        chmod(filename, 0755);
    }

    // 2 seconds after midnight, start a permanent write of all traces
    // create the new directory for writing traces
    // prevent the webserver from reading it until they are in a finished state
    time_t nowish = (now - 2000)/1000;
    gmtime_r(&nowish, &utc);

    if (utc.tm_mday != Modes.mday) {
        Modes.mday = utc.tm_mday;

        Modes.doFullTraceWrite = 1;

        createDateDir(Modes.globe_history_dir, &utc, dateDir);

        snprintf(filename, PATH_MAX, "%s/traces", dateDir);
        if (mkdir(filename, 0700) && errno != EEXIST)
            perror(filename);

        for (int i = 0; i < 256; i++) {
            snprintf(filename, PATH_MAX, "%s/traces/%02x", dateDir, i);
            if (mkdir(filename, 0755) && errno != EEXIST)
                perror(filename);
        }
    }
    return;
}

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

