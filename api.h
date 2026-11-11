#ifndef API_H
#define API_H

#define API_REQ_PADSTART (2048)

#define API_REQ_LIST_MAX 1024

#define API_ZSTD_LVL (2)

struct apiCon {
    int fd;
    int accept;
    struct char_buffer reply;
    size_t bytesSent;
    uint32_t events;
    int open;
    int wakeups;
    int keepalive;
    int http_minor_version;
    int include_version;
    struct char_buffer request;
    int64_t lastReset; // milliseconds
    char *content_type;
};

struct apiCircle {
    double lat;
    double lon;
    double radius; // in meters
    bool onlyClosest;
};

struct apiOptions {
    int64_t request_received; // microseconds
    int64_t request_processed; // microseconds
    double box[4];
    struct apiCircle circle;
    int is_box;
    int is_circle;
    int is_hexList;
    int is_callsignList;
    int is_regList;
    int is_typeList;
    int is_squawkList;
    int include_no_position;
    int filter_typeList;
    int filter_squawkList;
    int closest;
    int all;
    int all_with_pos;
    int jamesv2;
    int binCraft;
    int zstd;
    int zstd_encode;
    int filter_dbFlag;
    int filter_mil;
    int filter_interesting;
    int filter_pia;
    int filter_ladd;
    int filter_with_pos;
    int filter_callsign_exact;
    uint64_t filter_uuid;
    char callsign_exact[9];
    int filter_callsign_prefix;
    char callsign_prefix[9];
    int32_t filter_alt_baro;
    int32_t above_alt_baro;
    int32_t below_alt_baro;
    int hexCount;
    uint32_t hexList[API_REQ_LIST_MAX];
    int callsignCount;
    char callsignList[API_REQ_LIST_MAX * memberSize(struct binCraft, callsign) + 1];
    int regCount;
    char regList[API_REQ_LIST_MAX * memberSize(struct binCraft, registration) + 1];
    int typeCount;
    char typeList[API_REQ_LIST_MAX * memberSize(struct binCraft, typeCode) + 1];
    int squawkCount;
    unsigned squawkList[API_REQ_LIST_MAX];
};

struct offset {
    int32_t offset;
    int32_t len;
};

struct apiEntry {
    struct offset jsonOffset;

    struct binCraft bin;

    struct apiEntry *nextHex;
    struct apiEntry *nextReg;
    struct apiEntry *nextCallsign;

    float distance;
    float direction;
    int32_t globe_index;

#if defined(WITH_UUIDS)
    uint64_t recentReceiverIds[RECENT_RECEIVER_IDS];
#endif
};

struct range {
    int from; // inclusive
    int to; // exclusive
};


struct apiBuffer {
    int len;
    int len_flag;
    int alloc;
    int jsonLen;
    char *json;
    struct apiEntry *list;
    struct apiEntry *list_flag;
    struct range list_pos_range;
    struct range list_flag_pos_range;
    int64_t timestamp;
    int hashBuckets;
    int hashBits;
    struct apiEntry **hexHash;
    struct apiEntry **regHash;
    struct apiEntry **callsignHash;
    uint32_t focus;
    int aircraftJsonCount;
    // readers hold it for reading; apiUpdate() takes it for writing before rebuilding the buffer
    pthread_rwlock_t lock;
};

struct apiThread {
    pthread_t thread;
    int index;
    int epfd;
    int eventfd;
    int responseBytesBuffered;
    uint32_t requestCount;
    int conCount;
    int stackCount;
    struct apiCon *cons;
    struct apiCon **stack;
    ZSTD_CCtx* cctx;
    // for producing average request len numbers
    int64_t request_len_sum;
    int64_t request_count;
    int64_t next_bounce;
    int64_t antiSpam[16];
};

void apiBufferInit();
void apiBufferCleanup();

void apiInit();
void apiCleanup();

struct char_buffer apiGenerateAircraftJson(threadpool_buffer_t *pbuffer);
struct char_buffer apiGenerateGlobeJson(int globe_index, threadpool_buffer_t *pbuffer);

#endif
