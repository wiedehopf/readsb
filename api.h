#ifndef API_H
#define API_H

#define API_REQ_PADSTART (192)

#define API_HEXLIST_MAX 4096

struct apiCon {
    int fd;
    int accept;
    struct char_buffer cb;
    int cbOffset;
    uint32_t events;
    struct char_buffer request;
    int open;
    int wakeups;
};

struct apiCircle {
    double lat;
    double lon;
    double radius; // in meters
    bool onlyClosest;
};

struct apiOptions {
    int64_t request_received;
    int64_t request_processed;
    double box[4];
    struct apiCircle circle;
    int is_box;
    int is_circle;
    int is_hexList;
    int closest;
    int all;
    int all_with_pos;
    int jamesv2;
    char callsign[9];
    int find_callsign;
    int filter;
    int filter_squawk;
    unsigned squawk;
    int filter_dbFlag;
    int filter_mil;
    int filter_interesting;
    int filter_pia;
    int filter_ladd;
    int hexCount;
    uint32_t hexList[API_HEXLIST_MAX];
};

struct offset {
    int32_t offset;
    int32_t len;
};

struct apiEntry {
    struct apiEntry *next;

    struct binCraft bin;

    struct offset jsonOffset;

    float distance;
    float direction;
    int32_t globe_index;
    int aircraftJson;
};


struct apiBuffer {
    int len;
    int len_flag;
    int alloc;
    struct apiEntry *list;
    struct apiEntry *list_flag;
    uint64_t timestamp;
    char *json;
    struct apiEntry **hashList;
    uint32_t focus;
    int aircraftJsonCount;
};

struct apiThread {
    pthread_t thread;
    pthread_mutex_t mutex;
    int index;
    int epfd;
    int eventfd;
    int openFDs;
    int responseBytesBuffered;
    struct apiCon *cons;
    int nextCon;
};

struct range {
    int from; // inclusive
    int to; // exclusive
};

void apiLockMutex();
void apiUnlockMutex();

void apiBufferInit();
void apiBufferCleanup();

void apiInit();
void apiCleanup();

int apiUpdate(struct craftArray *ca);

struct char_buffer apiGenerateAircraftJson();
struct char_buffer apiGenerateGlobeJson(int globe_index);

#endif
