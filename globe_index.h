#ifndef GLOBE_INDEX_H
#define GLOBE_INDEX_H

#define TRACE_FOCUS BADDR

#define WRECENT (1<<10)
#define WMEM (1<<11)
#define WPERM (1<<12)

#ifndef GLOBE_PERM_IVAL
#define GLOBE_PERM_IVAL (4 * HOURS)
#endif
#define GLOBE_MEM_IVAL (60 * MINUTES)

#define GLOBE_INDEX_GRID 3
#define GLOBE_SPECIAL_INDEX 70
#define GLOBE_LAT_MULT (360 / GLOBE_INDEX_GRID + 1)
#define GLOBE_MIN_INDEX (1000)
#define GLOBE_MAX_INDEX (180 / GLOBE_INDEX_GRID * GLOBE_LAT_MULT + GLOBE_MIN_INDEX)

#define TDATE_FORMAT "%Y/%m/%d"

#define TRACE_STALE (15 * SECONDS)

#ifndef TRACE_RECENT_POINTS
#define TRACE_RECENT_POINTS (92)
#endif


#define TRACE_CACHE_LIFETIME (1 * MINUTES)
#define TRACE_CACHE_EXTRA (8)

struct tile {
    int south;
    int west;
    int north;
    int east;
};

void checkNewDay(int64_t now);
void checkNewDayAcas(int64_t now);
int globe_index(double lat_in, double lon_in);
int globe_index_index(int index);
void init_globe_index();
void cleanup_globe_index();
void save_blob(int blob, threadpool_buffer_t *pbuffer1, threadpool_buffer_t *pbuffer2, char *stateDir);
void load_blob(char *blob, threadpool_threadbuffers_t * buffer_group);
void writeRangeDirs();
void writeInternalState();
void readInternalState();
void traceWrite(struct aircraft *a, threadpool_threadbuffers_t *buffer_group);
void traceCleanup(struct aircraft *a);
int traceAdd(struct aircraft *a, struct modesMessage *mm, int64_t now, int stale);
int traceUsePosBuffered(struct aircraft *a);
void traceMaintenance(struct aircraft *a, int64_t now, threadpool_buffer_t *passbuffer);

int handleHeatmap(int64_t now);

struct craftArray {
    struct aircraft **list;
    int len;
    int alloc; // memory allocated for aircraft pointers

    // unclean changing of arrays, we always check for NULL when iterating
    pthread_mutex_t change_mutex;

    // be strict with reallocation operations
    pthread_mutex_t read_mutex;
    int reader_count;
    pthread_mutex_t write_mutex;
};

void ca_lock_read(struct craftArray *ca);
void ca_unlock_read(struct craftArray *ca);
void ca_init (struct craftArray *ca);
void ca_destroy (struct craftArray *ca);
void ca_remove (struct craftArray *ca, struct aircraft *a);
void ca_add (struct craftArray *ca, struct aircraft *a);
void set_globe_index (struct aircraft *a, int new_index);

// this format is fixed, don't change.
// if the latitude has bit 30 set (lat & (1<<30)), it's an info entry:
// the lowest 12 bits of the lat contain squawk digits as a decimal number
// lon and alt together contain the 8 byte callsign
struct heatEntry {
    int32_t hex;
    int32_t lat;
    int32_t lon;
    int16_t alt;
    int16_t gs;
} __attribute__ ((__packed__));

void traceDelete();
struct hexInterval {
    struct hexInterval* next;
    uint32_t hex;
    int64_t from;
    int64_t to;
};

#endif
