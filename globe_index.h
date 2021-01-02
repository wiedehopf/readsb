#define GLOBE_INDEX_GRID 10
#define GLOBE_SPECIAL_INDEX 30
#define GLOBE_LAT_MULT (360 / GLOBE_INDEX_GRID  + 1)
#define GLOBE_MIN_INDEX (1000)
#define GLOBE_MAX_INDEX (180 / GLOBE_INDEX_GRID * GLOBE_LAT_MULT + GLOBE_MIN_INDEX)

struct tile {
    int south;
    int west;
    int north;
    int east;
};

int globe_index(double lat_in, double lon_in);
int globe_index_index(int index);
void init_globe_index(struct tile *s_tiles);
//void write_trace(struct aircraft *a, uint64_t now);
void *load_state(void *arg);
void *save_state(void *arg);
void *jsonTraceThreadEntryPoint(void *arg);
