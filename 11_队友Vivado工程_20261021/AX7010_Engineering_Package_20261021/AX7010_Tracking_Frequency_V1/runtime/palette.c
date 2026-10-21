/* Display only; this build runs matching in FPGA, not ARM BM/SGM. */
#include "src/stereo_match.h"
void depth_color(unsigned char d,unsigned char rgb[3]){
 if(!d){rgb[0]=rgb[1]=rgb[2]=48;return;}
 unsigned int inferred=d&128;d&=127;unsigned int t=(d-1u)*255u/(MAX_DISPARITY-1u);
 rgb[0]=t<128?0:(t-128)*2;rgb[1]=t<128?t*2:(255-t)*2;rgb[2]=t<128?(127-t)*2:0;
 if(inferred)for(int c=0;c<3;c++)rgb[c]=(rgb[c]+96)/2;
}
