#ifndef STEREO_MATCH_H
#define STEREO_MATCH_H
#define SW 320
#define SH 180
#define MAX_DISPARITY 64
/* Prototype only: approximately parallel cameras, no metric calibration. */
int stereo_match(const unsigned char *left,const unsigned char *right,
                 unsigned char *disparity,int *vertical_offset,int *direction);
void depth_color(unsigned char d,unsigned char rgb[3]);
void stereo_reset(void);
/* Current diagnostic state: this is not a calibrated quality metric. */
void stereo_diagnostics(unsigned int out[4]);
#endif
