#ifndef RECEIVER_H
#define RECEIVER_H

#define RECEIVER_TABLE_HASH_BITS 18
#define RECEIVER_TABLE_SIZE (1 << RECEIVER_TABLE_HASH_BITS)

typedef struct receiver {
    uint64_t id;
    struct receiver *next;
    uint64_t lastSeen;
    uint64_t positionCounter;
    double lonMin;
    double latMin;
    double lonMax;
    double latMax;
} receiver;


uint32_t receiverHash(uint64_t id);
struct receiver *receiverGet(uint64_t id);
struct receiver *receiverCreate(uint64_t id);


void receiverPositionReceived(struct aircraft *a, uint64_t id, double lat, double lon, uint64_t now);
void receiverTimeout(int part, int nParts);
void receiverCleanup();
void receiverTest();
struct receiver *receiverGetReference(uint64_t id, double *lat, double *lon, struct aircraft *a);



#endif
