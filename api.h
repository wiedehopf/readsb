#ifndef API_H
#define API_H

#define API_REQ_PADSTART (192)

struct apiCon {
    int fd;
    int accept;
    struct char_buffer cb;
    int cbOffset;
    uint32_t events;
};

struct offset {
    int offset;
    int len;
};

struct apiEntry {
    uint32_t addr;
    int32_t lat;
    int32_t lon;

    struct offset jsonOffset;

    int32_t alt;
    char typeCode[4];
    uint16_t dbFlags;
};

struct apiBuffer {
    int len;
    int alloc;
    struct apiEntry *list;
    uint64_t timestamp;
    char *json;
};

struct apiThread {
    pthread_t thread;
    pthread_mutex_t mutex;
    int index;
    int epfd;
    int eventfd;
};

struct range {
    int from; // inclusive
    int to; // exclusive
};

void apiInit();
void apiCleanup();

int apiUpdate(struct craftArray *ca);
struct char_buffer apiReq(struct apiBuffer *buffer, double latMin, double latMax, double lonMin, double lonMax);

#endif
