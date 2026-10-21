#include "stereo_match.h"
#include <string.h>
#include <arm_neon.h>
#define N (SW*SH)
#define BAD 60000u
static uint32_t cl[N],cr[N];
static unsigned char gradient_l[N],gradient_r[N];
static unsigned short right_cost[N];
static unsigned char raw[N],clean[N],right_d[N];
static unsigned short component_queue[N];
static unsigned char visited[N],previous_gray[N],previous_depth[N];
static unsigned short previous_q[N];
static unsigned int diag[4];
static int ab(int x);
/* Full disparity state, not the V6 four-candidate approximation. The supplied
 * FP-Stereo CPU reference uses the same SGM recurrence; this is a separately
 * implemented four axial path, uint16/NEON implementation for Cortex-A9. */
static unsigned char cost_volume[N][MAX_DISPARITY];
static unsigned short sum_volume[N][MAX_DISPARITY];
static void aggregate_full_paths(const unsigned char *image){
 memset(sum_volume,0,sizeof(sum_volume));
 for(int axis=0;axis<2;axis++)for(int reverse=0;reverse<2;reverse++){
  int lines=axis?SW:SH,steps=axis?SH:SW;
  for(int line=0;line<lines;line++){
   unsigned short a[MAX_DISPARITY+2],b[MAX_DISPARITY+2];
   unsigned short *prev=a,*cur=b;unsigned int minimum=0;
   for(int d=0;d<MAX_DISPARITY+2;d++)prev[d]=0;
   prev[0]=prev[MAX_DISPARITY+1]=4096;
   for(int step=0;step<steps;step++){
    int t=reverse?steps-1-step:step;
    int p=axis?t*SW+line:line*SW+t;
    int prev_p=p+(reverse?1:-1)*(axis?SW:1);
    unsigned int p2=step&&ab(image[p]-image[prev_p])>18?12:40;
    uint16x8_t mn=vdupq_n_u16(minimum),large=vdupq_n_u16(minimum+p2),small=vdupq_n_u16(4);
    uint16x8_t next_min=vdupq_n_u16(65535);
    for(int d=0;d<MAX_DISPARITY;d+=8){
     uint16x8_t same=vld1q_u16(prev+d+1);
     uint16x8_t lower=vaddq_u16(vld1q_u16(prev+d),small);
     uint16x8_t upper=vaddq_u16(vld1q_u16(prev+d+2),small);
     uint16x8_t value=vminq_u16(vminq_u16(same,lower),vminq_u16(upper,large));
     value=vaddq_u16(vmovl_u8(vld1_u8(cost_volume[p]+d)),vsubq_u16(value,mn));
     vst1q_u16(cur+d+1,value);
     vst1q_u16(sum_volume[p]+d,vaddq_u16(vld1q_u16(sum_volume[p]+d),value));
     next_min=vminq_u16(next_min,value);
    }
    uint16x4_t m4=vmin_u16(vget_low_u16(next_min),vget_high_u16(next_min));
    m4=vpmin_u16(m4,m4);m4=vpmin_u16(m4,m4);minimum=vget_lane_u16(m4,0);
    cur[0]=cur[MAX_DISPARITY+1]=4096;
    unsigned short *swap=prev;prev=cur;cur=swap;
   }
  }
 }
}
static int geometry_ready,locked_y,locked_dir,pending_y,pending_dir,pending_count;
void stereo_reset(void){geometry_ready=0;pending_count=0;memset(previous_depth,0,N);memset(diag,0,sizeof(diag));}
void stereo_diagnostics(unsigned int out[4]){for(int i=0;i<4;i++)out[i]=diag[i];}
static unsigned int bits(unsigned int x){
 x-=((x>>1)&0x55555555u);x=(x&0x33333333u)+((x>>2)&0x33333333u);
 x=(x+(x>>4))&0x0f0f0f0fu;return (x*0x01010101u)>>24;
}
static int ab(int x){return x<0?-x:x;}
/* Independent software adaptation of X-Sobel prefilter + window SAD from
 * the supplied sdoira RTL design. No foreign rectification coefficients. */
static void xsobel(const unsigned char *image,unsigned char *out){
 memset(out,32,N);
 for(int y=1;y<SH-1;y++)for(int x=1;x<SW-1;x++){
  int p=y*SW+x;
  int g=image[p-SW+1]-image[p-SW-1]+2*(image[p+1]-image[p-1])+image[p+SW+1]-image[p+SW-1];
  g/=4;if(g<-31)g=-31;if(g>31)g=31;out[p]=g+32;
 }
}
static void census(const unsigned char *g,uint32_t *c){
 memset(c,0,N*sizeof(*c));
 for(int y=2;y<SH-2;y++)for(int x=2;x<SW-2;x++){
  unsigned int v=0;int p=y*SW+x;
  for(int dy=-2;dy<=2;dy++)for(int dx=-2;dx<=2;dx++)if(dx||dy)v=(v<<1)|(g[p+dy*SW+dx]<g[p]);
  c[p]=v;
 }
}
/* This global offset estimate does not replace calibration. */
static int geometry(int *oy_out,int *dir_out){
 unsigned int best=~0u,old_score=~0u;int by=0,bd=1;
 for(int dir=-1;dir<=1;dir+=2)for(int oy=-SW/40;oy<=SW/40;oy++){
  unsigned int total=0,used=0;
  for(int y=SH/10;y<SH-SH/10;y+=SH/12)for(int x=SW/4;x<SW-SW/4;x+=SW/20){
   int p=y*SW+x;unsigned int pop=bits(cl[p]);if(pop<3||pop>21)continue;
   unsigned int b=121;
   for(int d=1;d<=MAX_DISPARITY;d++){
    int xr=x-dir*d;if(xr<3||xr>=SW-3)continue;
    int q=(y+oy)*SW+xr;
    unsigned int c=bits(cl[p]^cr[q])+bits(cl[p-1]^cr[q-1])+bits(cl[p+1]^cr[q+1]);if(c<b)b=c;
   }
   total+=b;used++;
  }
  if(used>=12){
   unsigned int score=total*1024/used;
   if(geometry_ready&&oy==locked_y&&dir==locked_dir)old_score=score;
   if(score<best){best=score;by=oy;bd=dir;}
  }
 }
 if(best==~0u){*oy_out=locked_y;*dir_out=geometry_ready?locked_dir:1;return 0;}
 if(!geometry_ready){locked_y=by;locked_dir=bd;geometry_ready=1;}
 else if(by!=locked_y||bd!=locked_dir){
  /* Require persistent and substantially better evidence, not frame-wise
   * flips caused by a moving foreground object or repeated background. */
  unsigned int margin=bd!=locked_dir?6*1024u:2*1024u;
  if(old_score!=~0u&&best+margin<old_score){
   if(by==pending_y&&bd==pending_dir)pending_count++;else{pending_y=by;pending_dir=bd;pending_count=1;}
   if(pending_count>=5){locked_y=by;locked_dir=bd;pending_count=0;memset(previous_depth,0,N);}
  }else pending_count=0;
 }else pending_count=0;
 *oy_out=locked_y;*dir_out=locked_dir;diag[0]=best/1024;return 1;
}
static void suppress_speckles(void){
 memset(visited,0,N);diag[1]=0;
 for(int start=0;start<N;start++){
  if(visited[start]||!clean[start])continue;
  int head=0,tail=1;component_queue[0]=start;visited[start]=1;
  while(head<tail){
   int p=component_queue[head++],x=p%SW,y=p/SW;
   int neighbours[4]={x?p-1:-1,x<SW-1?p+1:-1,y?p-SW:-1,y<SH-1?p+SW:-1};
   for(int k=0;k<4;k++){int q=neighbours[k];if(q<0||visited[q]||!clean[q]||ab(clean[p]-clean[q])>1)continue;
    visited[q]=1;component_queue[tail++]=q;
   }
  }
  if(tail<20){diag[1]+=tail;for(int i=0;i<tail;i++)clean[component_queue[i]]=0;}
 }
}
int stereo_match(const unsigned char *left,const unsigned char *right,unsigned char *out,int *vertical_offset,int *direction){
 census(left,cl);census(right,cr);xsobel(left,gradient_l);xsobel(right,gradient_r);memset(out,0,N);
 if(!geometry(vertical_offset,direction)){memset(previous_depth,0,N);return 0;}
 int oy=*vertical_offset,dir=*direction;
 memset(right_d,0,N);memset(cost_volume,128,sizeof(cost_volume));
 for(int p=0;p<N;p++)right_cost[p]=BAD;
 /* Census window preserves local order under exposure differences. Store all
  * disparities contiguously, with a clipped gradient term rather than a large
  * per-disparity SAD window. This avoids strided writes thrashing shared L2. */
 for(int y=SH/15;y<SH-SH/15;y++)for(int x=5;x<SW-5;x++){
  int p=y*SW+x,yr=y+oy;if(yr<3||yr>=SH-3)continue;
  for(int d=1;d<=MAX_DISPARITY;d++){
   int xr=x-dir*d;if(xr<3||xr>=SW-3)continue;
   int q=yr*SW+xr,g=ab(gradient_l[p]-gradient_r[q]);if(g>24)g=24;
   cost_volume[p][d-1]=2*bits(cl[p]^cr[q])+g;
  }
 }
 aggregate_full_paths(left);
 for(int y=SH/15;y<SH-SH/15;y++)for(int x=5;x<SW-5;x++){
  int p=y*SW+x;
  for(int k=0;k<MAX_DISPARITY;k++)if(cost_volume[p][k]<128){
   int xr=x-dir*(k+1);unsigned int c=sum_volume[p][k];
   if(xr>=4&&xr<SW-4&&c<right_cost[y*SW+xr]){right_cost[y*SW+xr]=c;right_d[y*SW+xr]=k+1;}
  }
 }
 memset(raw,0,N);memset(clean,0,N);
 for(int y=SH/15;y<SH-SH/15;y++)for(int x=5;x<SW-5;x++){
  int p=y*SW+x,bk=-1,bd=0;unsigned int best=BAD,second=BAD;
  for(int k=0;k<MAX_DISPARITY;k++)if(cost_volume[p][k]<128&&sum_volume[p][k]<best){best=sum_volume[p][k];bk=k;bd=k+1;}
  for(int k=0;k<MAX_DISPARITY;k++)if(cost_volume[p][k]<128&&ab(k+1-bd)>1&&sum_volume[p][k]<second)second=sum_volume[p][k];
  int lo=255,hi=0;
  for(int dy=-2;dy<=2;dy++)for(int dx=-2;dx<=2;dx++){int v=left[p+dy*SW+dx];if(v<lo)lo=v;if(v>hi)hi=v;}
  if(bk>=0&&hi-lo>=8&&cost_volume[p][bk]<=64&&second>=best+12+best/10)raw[p]=bd;
 }
 /* Symmetric aggregated-cost reverse check and local support. */
 for(int y=SH/15+1;y<SH-SH/15-1;y++)for(int x=6;x<SW-6;x++){
  int p=y*SW+x,d=raw[p];if(!d)continue;
  int xr=x-dir*d;if(ab(right_d[y*SW+xr]-d)>1)continue;
  int agree=0;for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){int n=raw[p+dy*SW+dx];if(n&&ab(n-d)<=2)agree++;}
  if(agree>=3)clean[p]=d;
 }
 suppress_speckles();
 /* Smooth only CURRENT valid, edge-consistent matches. No persistence into
  * invalid/occluded pixels, and no old-mask hole fill or silhouette ghosts. */
 diag[2]=0;diag[3]=0;
 int valid=0;for(int p=0;p<N;p++){
  if(clean[p]){
   int d=clean[p],old=previous_depth[p],q=d*256;
   if(old&&ab(left[p]-previous_gray[p])<=5&&ab(old-d)<=1){q=(3*previous_q[p]+q+2)/4;diag[2]++;}
   previous_q[p]=q;out[p]=(q+128)/256;valid++;
  }
  previous_depth[p]=out[p];previous_gray[p]=left[p];
 }
 /* Small enclosed holes only, original samples only. High bit = inferred. */
 for(int y=SH/15+2;y<SH-SH/15-2;y++)for(int x=10;x<SW-10;x++){
  int p=y*SW+x;if(clean[p])continue;
  int a=0,b=0,c=0,e=0,la=0,lb=0,lc=0,le=0;
  for(int k=1;k<=6;k++){
   if(!a&&clean[p-k]){a=clean[p-k];la=k;}if(!b&&clean[p+k]){b=clean[p+k];lb=k;}
   if(!c&&clean[p-k*SW]){c=clean[p-k*SW];lc=k;}if(!e&&clean[p+k*SW]){e=clean[p+k*SW];le=k;}
  }
  if(a&&b&&c&&e&&ab(a-b)<=2&&ab(c-e)<=2&&ab(a-c)<=3){
   int lo=255,hi=0;
   for(int k=-la;k<=lb;k++){int v=left[p+k];if(v<lo)lo=v;if(v>hi)hi=v;}
   for(int k=-lc;k<=le;k++){int v=left[p+k*SW];if(v<lo)lo=v;if(v>hi)hi=v;}
   if(hi-lo<=36){out[p]=128|((a+b+c+e+2)/4);diag[3]++;}
  }
 }
 return valid;
}
void depth_color(unsigned char d,unsigned char rgb[3]){
 if(!d){rgb[0]=rgb[1]=rgb[2]=48;return;}
 unsigned int inferred=d&128;d&=127;unsigned int t=(d-1u)*255u/(MAX_DISPARITY-1u);
 rgb[0]=t<128?0:(t-128)*2;rgb[1]=t<128?t*2:(255-t)*2;rgb[2]=t<128?(127-t)*2:0;
 if(inferred)for(int c=0;c<3;c++)rgb[c]=(rgb[c]+96)/2;
}
