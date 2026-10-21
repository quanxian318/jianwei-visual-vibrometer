#ifndef TRACKING_SHARED_H
#define TRACKING_SHARED_H
#include <stdint.h>
#define TRACK_RING 512
typedef struct {
 uint32_t sequence,ticks_low,ticks_high,buffers;
 float x0,y0,score0,x1,y1,score1;
 uint32_t flags,copy_ticks,track_ticks,lost0,lost1,reserved;
 float refx0,refy0,refscore0,refx1,refy1,refscore1;
 uint32_t reference_flags,reference_reserved;
} TrackRecord;
typedef struct {
 uint32_t magic,error,selftest,sequence,fps10,valid0,valid1,copy_drops;
 uint32_t timer_hz,seed_x0,seed_y0,seed_x1,seed_y1,width,height,ring_size;
 TrackRecord latest;
 uint32_t reference_seed[4],reserved[20];
 TrackRecord ring[TRACK_RING];
} TrackingShared;
#define TRACK_SHARED ((volatile TrackingShared *)0x03080000u)
#define TRACK_BYTES 49408
#endif
