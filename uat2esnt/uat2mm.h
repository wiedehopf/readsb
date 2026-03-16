#ifndef UAT2MM_H
#define UAT2MM_H

#include <stdint.h>

enum frame_type_t { UAT_DOWNLINK, UAT_SHORT, UAT_LONG, UAT_UPLINK };
typedef enum frame_type_t frame_type_t;

int uat2mm(frame_type_t type, uint8_t *frame, float ss, int64_t now, struct modesMessage *mm);
int process_dump978(char *p, char *end, frame_type_t *frametype, uint8_t *frame, float *signal_strength);

#endif
