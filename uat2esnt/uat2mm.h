#ifndef UAT2MM_H
#define UAT2MM_H

#include <stdint.h>

enum frame_type_t { UAT_UPLINK, UAT_DOWNLINK };
typedef enum frame_type_t frame_type_t;

void uat2mm(frame_type_t type, uint8_t *frame, float ss, struct modesMessage *mm);
int process_dump978(char *p, char *end, frame_type_t *frametype, uint8_t *frame, float *signal_strength);

#endif
