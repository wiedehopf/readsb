#include "readsb.h"

#define LEG_FOCUS 0xfffffff
//#define LEG_FOCUS 0xae290d

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


void write_trace(struct aircraft *a, uint64_t now) {
    struct char_buffer recent;
    struct char_buffer full;
    struct char_buffer hist;
    size_t shadow_size = 0;
    char *shadow = NULL;
    char filename[PATH_MAX];
    static uint32_t count2, count3, count4;

    time_t hour_ago = now/1000 - GLOBE_OVERLAP;

    recent.len = 0;
    full.len = 0;
    hist.len = 0;

    if (Modes.debug_traceCount && ++count2 % 1000 == 0)
        fprintf(stderr, "recent trace write: %u\n", count2);

    //pthread_mutex_lock(&a->trace_mutex);

    a->trace_write = 0;

    mark_legs(a);

    int start24 = 0;
    for (int i = 0; i < a->trace_len; i++) {
        if (a->trace[i].timestamp > now - (24 * 3600 + 900) * 1000) {
            start24 = i;
            break;
        }
    }

    int start_recent = start24;
    if (a->trace_len > 142 && a->trace_len - 142 > start24)
       start_recent = (a->trace_len - 142);

    recent = generateTraceJson(a, start_recent, -1);
    // write recent trace to /run

    if (now > a->trace_next_mw || a->trace_full_write > 35 || now > a->trace_next_fw) {
        // write full trace to /run
        int write_perm = 0;

        if (Modes.debug_traceCount && ++count3 % 1000 == 0)
            fprintf(stderr, "memory trace writes: %u\n", count3);

        full = generateTraceJson(a, start24, -1);

        if (a->trace_full_write == 0xc0ffee) {
            a->trace_next_mw = now + 1 * 60 * 1000 + rand() % (10 * 60 * 1000);
        } else {
            a->trace_next_mw = now + 10 * 60 * 1000 + rand() % (1 * 60 * 1000);
        }

        if (now > a->trace_next_fw || a->trace_full_write == 0xc0ffee) {
            if (Modes.debug_traceCount && ++count4 % 1000 == 0)
                fprintf(stderr, "perm trace writes: %u\n", count4);

            write_perm = 1;

            if (a->trace_full_write == 0xc0ffee) {
                a->trace_next_fw = now + 1000 * (rand() % (GLOBE_OVERLAP - 60 - GLOBE_OVERLAP / 16));
            } else if (!Modes.json_globe_index)  {
                a->trace_next_fw = now + 6 * (GLOBE_OVERLAP - 60 - rand() % GLOBE_OVERLAP / 16) * 1000;
            } else {
                a->trace_next_fw = now + (GLOBE_OVERLAP - 60 - rand() % GLOBE_OVERLAP / 16) * 1000;
            }
        }
        a->trace_full_write = 0;

        //fprintf(stderr, "%06x\n", a->addr);

        // prepare stuff to be written to disk
        // written to memory, then to disk outside the trace_mutex
        if (write_perm) {
            // if the trace length is zero, the trace is written once more
            // and not written again until a new position is received
            if (a->trace_len == 0) {
                a->trace_full_write = 0xdead;
            }

            // no longer used
            if (0 && a->pos_set && Modes.globe_history_dir && !(a->addr & MODES_NON_ICAO_ADDRESS)) {
                int size_state = a->trace_len * sizeof(struct state);
                int size_all = (a->trace_len + 3) / 4 * sizeof(struct state_all);
                shadow_size = sizeof(struct aircraft) + size_state + size_all;

                shadow = malloc(shadow_size);
                char *start = shadow;

                memcpy(start, a, sizeof(struct aircraft));
                start += sizeof(struct aircraft);
                if (a->trace_len > 0) {
                    memcpy(start, a->trace, size_state);
                    start += size_state;
                    memcpy(start, a->trace_all, size_all);
                    start += size_all;
                }
            }

            // prepare writing the permanent history
            if (a->trace_len > 0 &&
                    Modes.globe_history_dir && !(a->addr & MODES_NON_ICAO_ADDRESS)) {

                struct tm utc;
                gmtime_r(&hour_ago, &utc);
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
                if (start >= 0 && end >= 0)
                    hist = generateTraceJson(a, start, end);
            }
        }
    }


    //pthread_mutex_unlock(&a->trace_mutex);


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

        static int day; // STATIC !!!!!!!!!!

        char tstring[100];
        struct tm utc;
        gmtime_r(&hour_ago, &utc);
        strftime (tstring, 100, "%Y-%m-%d", &utc);

        if (utc.tm_mday != day) {

            day = utc.tm_mday;

            snprintf(filename, PATH_MAX - 200, "%s/%s", Modes.globe_history_dir, tstring);
            filename[PATH_MAX - 201] = 0;

            mkdir(filename, 0755);

            char pathbuf[PATH_MAX+20];
            snprintf(pathbuf, sizeof(pathbuf), "%s/traces", filename);
            pathbuf[PATH_MAX - 1] = 0;
            mkdir(pathbuf, 0755);
            for (int i = 0; i < 256; i++) {
                snprintf(pathbuf, sizeof(pathbuf), "%s/traces/%02x", filename, i);
                mkdir(pathbuf, 0755);
            }
        }

        snprintf(filename, PATH_MAX, "%s/traces/%02x/trace_full_%s%06x.json", tstring, a->addr % 256, (a->addr & MODES_NON_ICAO_ADDRESS) ? "~" : "", a->addr & 0xFFFFFF);
        filename[PATH_MAX - 101] = 0;
        writeJsonToGzip(Modes.globe_history_dir, filename, hist, 9);

        free(hist.buffer);
    }

    // no longer used
    if (0 && shadow && shadow_size > 0) {
        snprintf(filename, 1024, "%s/internal_state/%02x/%06x", Modes.globe_history_dir, a->addr % 256, a->addr);

        int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            perror(filename);
        } else {
            check_write(fd, shadow, shadow_size, filename);
            close(fd);
        }
    }
    free(shadow);
}

void *save_state(void *arg) {
    int thread_number = *((int *) arg);
    for (int j = 0; j < 256; j++) {
        if (j % 8 != thread_number)
            continue;
        save_blob(j);
    }
    return NULL;
    // old internal state, no longer needed
    for (int j = 0; j < AIRCRAFT_BUCKETS; j++) {
        if (j % 8 != thread_number)
            continue;
        for (struct aircraft *a = Modes.aircraft[j]; a; a = a->next) {
            if (!a->pos_set)
                continue;
            if (a->addr & MODES_NON_ICAO_ADDRESS)
                continue;
            if (a->messages < 2)
                continue;

            char filename[1024];
            snprintf(filename, 1024, "%s/internal_state/%02x/%06x", Modes.globe_history_dir, a->addr % 256, a->addr);

            int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) {
                perror(filename);
            } else {
                int res;
                res = check_write(fd, a, sizeof(struct aircraft), filename);
                if (res > 0 && a->trace_len > 0) {
                    int size_state = a->trace_len * sizeof(struct state);
                    int size_all = (a->trace_len + 3) / 4 * sizeof(struct state_all);
                    check_write(fd, a->trace, size_state, filename);
                    check_write(fd, a->trace_all, size_all, filename);
                }

                close(fd);
            }
        }
    }
    return NULL;
}


static int load_aircraft(int fd, uint64_t now) {

    int ret = 0;
    struct aircraft *a = (struct aircraft *) aligned_alloc(64, sizeof(struct aircraft));
    int res = read(fd, a, sizeof(struct aircraft));
    if (res != sizeof(struct aircraft) ||
            a->size_struct_aircraft != sizeof(struct aircraft)
       ) {
        if (res == sizeof(struct aircraft)) {
            fprintf(stderr, "sizeof(struct aircraft) has changed, unable to read state!\n");
        } else {
            fprintf(stderr, "read fail\n");
        }
        free(a);
        return -1;
    }

    a->trace = NULL;
    a->trace_all = NULL;

    if (!Modes.json_globe_index) {
        a->trace_alloc = 0;
        a->trace_len = 0;
    }

    // read trace
    if (a->trace_len > 0) {

        int size_state = a->trace_len * sizeof(struct state);
        int size_all = (a->trace_len + 3) / 4 * sizeof(struct state_all);

        a->trace = malloc(a->trace_alloc * sizeof(struct state));
        a->trace_all = malloc(a->trace_alloc / 4 * sizeof(struct state_all));

        if (read(fd, a->trace, size_state) != size_state
                || read(fd, a->trace_all, size_all) != size_all) {
            // TRACE FAIL
            fprintf(stderr, "read trace fail\n");
            free(a->trace);
            free(a->trace_all);
            a->trace = NULL;
            a->trace_all = NULL;
            a->trace_alloc = 0;
            a->trace_len = 0;
            ret = -1;
        } else {
            // TRACE SUCCESS
            if (a->trace_full_write != 0xdead) {
                if (a->addr == LEG_FOCUS)
                    a->trace_next_fw = now;
                else
                    a->trace_next_fw = now + 1000 * (rand() % 120); // spread over 2 mins

                a->trace_full_write = 0xc0ffee; // rewrite full history file
            }
        }
    }

    if (pthread_mutex_init(&a->trace_mutex, NULL)) {
        fprintf(stderr, "Unable to initialize trace mutex!\n");
        exit(1);
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
        Modes.stats_current.unique_aircraft++;

        a->next = Modes.aircraft[hash];
        Modes.aircraft[hash] = a;
    }

    return ret;
}

void *load_state(void *arg) {
    uint64_t now = mstime();
    char pathbuf[PATH_MAX];
    //struct stat fileinfo = {0};
    //fstat(fd, &fileinfo);
    //off_t len = fileinfo.st_size;
    int thread_number = *((int *) arg);
    srand(get_seed());
    for (int i = 0; i < 256; i++) {
        if (i % 8 != thread_number)
            continue;
        snprintf(pathbuf, PATH_MAX, "%s/internal_state/%02x", Modes.globe_history_dir, i);

        DIR *dp;
        struct dirent *ep;

        dp = opendir (pathbuf);
        if (dp == NULL)
            continue;

        while ((ep = readdir (dp))) {
            if (strlen(ep->d_name) < 6)
                continue;
            snprintf(pathbuf, PATH_MAX, "%s/internal_state/%02x/%s", Modes.globe_history_dir, i, ep->d_name);

            int fd = open(pathbuf, O_RDONLY);

            load_aircraft(fd, now);

            close(fd);
            // old internal state format, no longer needed
            unlink(pathbuf);
        }

        closedir (dp);
    }
    return NULL;
}

void *jsonTraceThreadEntryPoint(void *arg) {

    int thread = * (int *) arg;

    srand(get_seed());

    int part = 0;
    int n_parts = 64; // power of 2

    int thread_section_len = (AIRCRAFT_BUCKETS / TRACE_THREADS);
    int thread_start = thread * thread_section_len;
    //int thread_end = thread_start + thread_section_len;
    //fprintf(stderr, "%d %d\n", thread_start, thread_end);
    int section_len = thread_section_len / n_parts;

    struct timespec slp = {0, 0};
    // write each part every 10 seconds
    uint64_t sleep = 10 * 1000 / n_parts;

    slp.tv_sec =  (sleep / 1000);
    slp.tv_nsec = (sleep % 1000) * 1000 * 1000;

    pthread_mutex_lock(&Modes.jsonTraceThreadMutex[thread]);

    while (!Modes.exit) {
        struct aircraft *a;

        pthread_mutex_unlock(&Modes.jsonTraceThreadMutex[thread]);

        nanosleep(&slp, NULL);

        pthread_mutex_lock(&Modes.jsonTraceThreadMutex[thread]);

        int start = thread_start + part * section_len;
        int end = start + section_len;

        //fprintf(stderr, "%d %d %d\n", part, start, end);

        uint64_t now = mstime();

        for (int j = start; j < end; j++) {
            for (a = Modes.aircraft[j]; a; a = a->next) {
                if (a->trace_write)
                    write_trace(a, now);
            }
        }

        part++;
        part %= n_parts;
    }

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
    int was_ground = 0;

    for (int i = 0; i < 5; i++)
        last_five[i] = 0;
    five_pos = 0;

    for (int i = 1; i < a->trace_len; i++) {
        struct state *state = &a->trace[i];
        struct state *prev = &a->trace[i-1];

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

        if (on_ground) {
            int avg = 0;
            for (int i = 0; i < 5; i++)
                avg += last_five[i];
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

        if (!on_ground)
            last_airborne = state->timestamp;

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

        if (abs(low - altitude) < threshold * 1 / 3) {
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
                fprintf(stderr, "leg, elapsed: %0.fmin, distance: %0.f\n", elapsed / (60 * 1000.0), distance / 1000.0);
        }

        int leg_float = 0;
        if (major_climb && major_descent && major_climb > major_descent + 12 * 60 * 1000) {
            for (int i = major_descent_index + 1; i < major_climb_index; i++) {
                if (a->trace[i].timestamp > a->trace[i - 1].timestamp + 6 * 60 * 1000) {
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
                new_leg = &a->trace[i];
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
                time_t nowish = leg_ts/1000;
                struct tm utc;
                gmtime_r(&nowish, &utc);
                char tstring[100];
                strftime (tstring, 100, "%H:%M:%S", &utc);
                fprintf(stderr, "leg: %s\n", tstring);
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


// blobs 00 to ff (0 to 255)
void save_blob(int blob) {
    if (!Modes.globe_history_dir)
        return;
    if (blob < 0 || blob > 255)
        fprintf(stderr, "save_blob: invalid argument: %d", blob);
    //fprintf(stderr, "Save blob: %02x\n", blob);

    char filename[1024];
    snprintf(filename, 1024, "%s/internal_state/blob_%02x", Modes.globe_history_dir, blob);

    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror(filename);
        return;
    }

    int stride = AIRCRAFT_BUCKETS / 256;
    int start = stride * blob;
    int end = start + stride;

    uint64_t magic = 0x7ba09e63757913eeULL;

    for (int j = start; j < end; j++) {
        for (struct aircraft *a = Modes.aircraft[j]; a; a = a->next) {
            if (!a->pos_set)
                continue;
            if (a->addr & MODES_NON_ICAO_ADDRESS)
                continue;
            if (a->messages < 2)
                continue;

            check_write(fd, &magic, sizeof(magic), filename);

            check_write(fd, a, sizeof(struct aircraft), filename);
            if (a->trace_len > 0) {
                int size_state = a->trace_len * sizeof(struct state);
                int size_all = (a->trace_len + 3) / 4 * sizeof(struct state_all);
                check_write(fd, a->trace, size_state, filename);
                check_write(fd, a->trace_all, size_all, filename);
            }
        }
    }
    magic--;
    check_write(fd, &magic, sizeof(magic), filename);
    close(fd);
}
void *load_blobs(void *arg) {
    int thread_number = *((int *) arg);
    srand(get_seed());
    for (int j = 0; j < 256; j++) {
        if (j % 8 != thread_number)
           continue;
        load_blob(j);
    }
    return NULL;
}

// blobs 00 to ff (0 to 255)
static void load_blob(int blob) {
    if (blob < 0 || blob > 255)
        fprintf(stderr, "load_blob: invalid argument: %d", blob);
    uint64_t magic = 0x7ba09e63757913eeULL;
    char filename[1024];
    uint64_t now = mstime();
    snprintf(filename, 1024, "%s/internal_state/blob_%02x", Modes.globe_history_dir, blob);
    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
        perror(filename);
        close(fd);
        return;
    }
    int res = 0;
    while (res == 0) {
        uint64_t value = 0;
        res = read(fd, &value, sizeof(value));
        if (res != sizeof(value) || value != magic) {
            if (value != magic - 1)
                fprintf(stderr, "Incomplete state file: %s\n", filename);
            break;
        }
        res = load_aircraft(fd, now);
    }
    close(fd);
}

void handleHeatmap() {
    time_t nowish = (mstime() - 30 * MINUTES)/1000;
    struct tm utc;
    gmtime_r(&nowish, &utc);
    int half_hour = utc.tm_hour * 2 + utc.tm_min / 30;

    // only do this every 30 minutes.
    if (half_hour == Modes.heatmap_current_interval)
        return;

    utc.tm_hour = half_hour / 2;
    utc.tm_min = 30 * (half_hour % 2);
    utc.tm_sec = 0;
    uint64_t start = 1000 * (uint64_t) (timegm(&utc));
    uint64_t end = start + 30 * MINUTES;
    int num_slices = (30 * MINUTES) / Modes.globe_history_heatmap;

    Modes.heatmap_current_interval = half_hour;

    char pathbuf[PATH_MAX];
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

            //pthread_mutex_lock(&a->trace_mutex);

            for (int i = 0; i < a->trace_len; i++) {
                if (len >= alloc)
                    break;
                if (trace[i].timestamp > end)
                    break;
                if (trace[i].timestamp < next)
                    continue;
                if (!trace[i].flags.altitude_valid)
                    continue;

                while (trace[i].timestamp > next + Modes.globe_history_heatmap) {
                    next += Modes.globe_history_heatmap;
                    slice++;
                }

                buffer[len].hex = a->addr;
                buffer[len].lat = trace[i].lat;
                buffer[len].lon = trace[i].lon;
                buffer[len].alt = trace[i].altitude;

                slices[len] = slice;

                len++;

                next += Modes.globe_history_heatmap;
                slice++;
            }

            //pthread_mutex_unlock(&a->trace_mutex);
        }
    }

    for (int i = 0; i < num_slices; i++) {
        struct heatEntry specialSauce = (struct heatEntry) {0};
        uint64_t slice_stamp = start + i * Modes.globe_history_heatmap;
        specialSauce.hex = 0xe7f7c9d;
        specialSauce.lat = slice_stamp >> 32;
        specialSauce.lon = slice_stamp & ((1ULL << 32) - 1);
        specialSauce.alt = Modes.globe_history_heatmap;

        index[i] = (struct heatEntry) {0};
        index[i].hex = len2 + num_slices;
        buffer2[len2++] = specialSauce;

        for (int k = 0; k < len; k++) {
            if (slices[k] == i)
                buffer2[len2++] = buffer[k];
        }
    }

    char tstring[100];
    strftime (tstring, 100, "%Y-%m-%d", &utc);

    snprintf(pathbuf, PATH_MAX, "%s/%s/heatmap", Modes.globe_history_dir, tstring);
    if (mkdir(pathbuf, 0755) && errno != EEXIST)
        perror(pathbuf);

    snprintf(pathbuf, PATH_MAX, "%s/%s/heatmap/%02d.bin.ttf", Modes.globe_history_dir, tstring, half_hour);

    fprintf(stderr, "%s using %d positions\n", pathbuf, len);

    int fd = open(pathbuf, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror(pathbuf);
    } else {
        gzFile gzfp = gzdopen(fd, "wb");
        gzbuffer(gzfp, 256 * 1024);
        gzsetparams(gzfp, 9, Z_DEFAULT_STRATEGY);
        ssize_t toWrite = sizeof(index);
        if (gzwrite(gzfp, index, toWrite) != toWrite)
            perror("Error writing heatmap:");
        toWrite = len2 * sizeof(struct heatEntry);
        if (gzwrite(gzfp, buffer2, toWrite) != toWrite)
            perror("Error writing heatmap:");
        gzclose(gzfp);
    }

    free(buffer);
    free(buffer2);
    free(slices);
}
