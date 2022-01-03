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

struct offset {
    int32_t offset;
    int32_t len;
};

struct apiEntry {
    struct apiEntry *next;

    uint32_t addr;
    int32_t lat;

    int32_t lon;
    int32_t alt;

    struct offset jsonOffset;

    float distance;
    char typeCode[4];
    uint16_t dbFlags;
    unsigned aircraftJson:1;
    unsigned padding:15;
    int32_t globe_index;

    uint64_t pad3;
} __attribute__ ((__packed__));

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
