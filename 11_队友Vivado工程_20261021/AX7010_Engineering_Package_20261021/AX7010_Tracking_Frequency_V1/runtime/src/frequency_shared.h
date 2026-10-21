#ifndef FREQUENCY_SHARED_H
#define FREQUENCY_SHARED_H
#include <stdint.h>
#define FREQ_N 256
#define FREQ_BINS 129
#define FREQ_BYTES 8192
/* words: 0 magic,1 error,2 selftest,3 seqlock,4 track end sequence,
 * 5 valid mask,6..9 reason,10 last ticks,11 maximum ticks,12 timer Hz,
 * 13 FFT length,14 baseline state(0 none,1 collecting,2 ready,3 insufficient),
 * 15 elapsed ms,16..19 baseline windows,20 frequency updates,
 * 21 record timestamp low,22 high,30 command token,31 command op,32 ack.
 * CPU0 owns only words30/31; CPU1 owns all other words and arrays.
 */
typedef struct {
 uint32_t words[64];
 float fs[4],rms[4],peak_hz[4],peak_amp[4],threshold[4],candidate[4];
 float amplitude[4][FREQ_BINS],noise[4][FREQ_BINS];
 uint32_t padding[928];
} FrequencyShared;
typedef char freq_size_check[sizeof(FrequencyShared)==FREQ_BYTES?1:-1];
#define FREQ_SHARED ((volatile FrequencyShared *)0x030e0000u)
#endif
