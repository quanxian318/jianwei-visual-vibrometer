#ifndef SHARED_DEPTH_H
#define SHARED_DEPTH_H
#include "stereo_match.h"
typedef struct {
    volatile unsigned int ready,sequence,valid,vertical,direction,fps10,drops,buffer;
    volatile unsigned char disparity[2][SW*SH];
    volatile unsigned int quality[4];
} DepthShared;
#define DEPTH_SHARED ((volatile DepthShared *)0x03004000u)
#endif
