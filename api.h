#ifndef API_H
#define API_H

#define API_REQ_PADSTART (192)

struct apiCon {
    int fd;
    int accept;
    struct char_buffer cb;
    int cbOffset;
    uint32_t events;
    struct char_buffer request;
};

struct apiOptions {
    int64_t request_received;
    int64_t request_processed;
    double *box;
    uint32_t *hexList;
    int hexCount;
    struct apiCircle *circle;
    int is_box;
    int is_circle;
    int is_hexList;
    int closest;
    int all;
    int all_with_pos;
    int jamesv2;
    char callsign[9];
    int find_callsign;
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
    int32_t globe_index;

    unsigned aircraftJson:1;
};

struct apiCircle {
    double lat;
    double lon;
    double radius; // in meters
    bool onlyClosest;
};

struct apiBuffer {
    int len;
    int alloc;
    struct apiEntry *list;
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
