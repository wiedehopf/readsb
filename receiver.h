#ifndef RECEIVER_H
#define RECEIVER_H

#define RECEIVER_BAD_AIRCRAFT (2)

#define RECEIVER_RANGE_GOOD (7)
#define RECEIVER_RANGE_BAD (-7)
#define RECEIVER_RANGE_UNCLEAR (-1)

#define RECEIVER_MAINTENANCE_INTERVAL (5 * MINUTES)

struct bad_ac {
  uint32_t addr;
  int64_t ts;
};
typedef struct receiver {
    uint64_t id;
    struct receiver *next;
    int64_t firstSeen;
    int64_t lastSeen;
    uint64_t positionCounter;
    double latMin;
    double latMax;
    double lonMin;
    double lonMax;
    int64_t badExtent; // timestamp of first lat/lon (max-min) > MAX_DIFF (receiver.c)
    struct bad_ac badAircraft[RECEIVER_BAD_AIRCRAFT];
    float badCounter; // plus one for a bad position, -0.5 for a good position
    int32_t goodCounter; // plus one for a good position
    // reset both counters on timing out a receiver.
    int64_t timedOutUntil;
    uint32_t timedOutCounter; // how many times a receiver has been timed out
} receiver;


uint32_t receiverHash(uint64_t id);
struct receiver *receiverGet(uint64_t id);
struct receiver *receiverCreate(uint64_t id);

struct char_buffer generateReceiversJson();

int receiverPositionReceived(struct aircraft *a, struct modesMessage *mm, double lat, double lon, int64_t now);
void receiverTimeout(int part, int nParts, int64_t now);
void receiverInit();
void receiverCleanup();
void receiverTest();
struct receiver *receiverGetReference(uint64_t id, double *lat, double *lon, struct aircraft *a, int noDebug);
int receiverCheckBad(uint64_t id, int64_t now);
struct receiver *receiverBad(uint64_t id, uint32_t addr, int64_t now);



#endif
