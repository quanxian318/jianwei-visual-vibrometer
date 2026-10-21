#include "src/tracking_shared.h"
typedef uint32_t u32;typedef unsigned char u8;
#define HZ 333333343u
#define TICKS (*(volatile u32 *)0xf8f00200u)
#include "translation_tracker.h"
static TranslationTracker trackers[4];
static float raw[ROI*ROI],images[4][ROI*ROI],synthetic[ROI*ROI];
// Located from native 96x96 ROI captures, not display-thumbnail estimates.
// Include BOTH cross arms: a vertical stroke alone is poorly constrained in Y.
static const int seedx[2]={596,712},seedy[2]={514,486};
static const int reference_x[2]={192,256},reference_y[2]={528,512};
#include "frequency_worker.h"
void *memset(void *p,int v,unsigned n){u8 *d=p;while(n--)*d++=v;return p;}
void *memcpy(void *d,const void *s,unsigned n){u8 *a=d;const u8 *b=s;while(n--)*a++=*b++;return d;}
static u32 rd(u32 a){return *(volatile u32 *)a;}
static unsigned long long ticks(void){u32 hi,lo;do{hi=rd(0xf8f00204);lo=TICKS;}while(hi!=rd(0xf8f00204));return ((unsigned long long)hi<<32)|lo;}
static void boot_mmu(void){
 volatile u32 *table=(void *)0x02f00000;
 for(u32 i=0;i<4096;i++)table[i]=(i<<20)|(i<512?0x15de6:0xc06);
 table[0x30]=0x03000000|0x11de2;
 for(u32 i=0x40;i<0xa0;i++)table[i]=(i<<20)|0x11de2;
 __asm__ volatile("dsb sy":::"memory");
 u32 zero=0,dacr=0xffffffff,ttbr=0x02f00000,control,actlr;
 __asm__ volatile("mcr p15,0,%0,c2,c0,2"::"r"(zero):"memory");
 __asm__ volatile("mcr p15,0,%0,c2,c0,0"::"r"(ttbr):"memory");
 __asm__ volatile("mcr p15,0,%0,c3,c0,0"::"r"(dacr):"memory");
 __asm__ volatile("mrc p15,0,%0,c1,c0,1":"=r"(actlr));actlr|=0x40;
 __asm__ volatile("mcr p15,0,%0,c1,c0,1"::"r"(actlr):"memory");
 __asm__ volatile("mrc p15,0,%0,c1,c0,0":"=r"(control));control|=0x1005;
 __asm__ volatile("mcr p15,0,%0,c1,c0,0\n dsb sy\n isb"::"r"(control):"memory");
}
static void blur(const float *a,float *b){
 for(int y=0;y<ROI;y++)for(int x=0;x<ROI;x++){
  int p=y*ROI+x;if(!x||!y||x==ROI-1||y==ROI-1){b[p]=a[p];continue;}
  b[p]=(a[p-ROI-1]+2*a[p-ROI]+a[p-ROI+1]+2*a[p-1]+4*a[p]+2*a[p+1]+a[p+ROI-1]+2*a[p+ROI]+a[p+ROI+1])/16;
 }
}
static int selftest(void){
 for(int y=0;y<ROI;y++)for(int x=0;x<ROI;x++)raw[y*ROI+x]=(x>=46&&x<=49)||(y>=46&&y<=49)?40:180;
 blur(raw,images[0]);if(!tracker_reference(&trackers[0],images[0]))return 1;
 const float shifts[3][2]={{.5f,-.25f},{1.25f,2.5f},{-1.5f,-1.25f}};
 for(int k=0;k<3;k++){
  memset(synthetic,0,sizeof(synthetic));
  for(int y=4;y<ROI-4;y++)for(int x=4;x<ROI-4;x++)synthetic[y*ROI+x]=sample(images[0],x-shifts[k][0],y-shifts[k][1]);
  if(!tracker_update(&trackers[0],synthetic)||absf(trackers[0].u-shifts[k][0])>.15f||absf(trackers[0].v-shifts[k][1])>.15f)return 2+k;
 }
 for(int i=0;i<ROI*ROI;i++)synthetic[i]=100;
 if(tracker_update(&trackers[0],synthetic))return 5;
 if(tracker_reference(&trackers[1],synthetic))return 6;
 if(!tracker_reference_at(&trackers[0],images[0],6,-6)||!tracker_update(&trackers[0],images[0])||absf(trackers[0].u-6)>.05f||absf(trackers[0].v+6)>.05f)return 7;
 memset(trackers,0,sizeof(trackers));return 0;
}
static int initialize_background(int ch,const float *im){
 static TranslationTracker candidate;float best=-1;
 for(int oy=-12;oy<=12;oy+=6)for(int ox=-12;ox<=12;ox+=6){
  if(!tracker_reference_at(&candidate,im,ox,oy))continue;
  float trace=candidate.hxx+candidate.hyy;
  float quality=(candidate.hxx*candidate.hyy-candidate.hxy*candidate.hxy)/(trace+1);
  if(quality>best){best=quality;memcpy(&trackers[ch],&candidate,sizeof(candidate));}
 }
 if(best<0)return 0;
 TRACK_SHARED->reference_seed[(ch-2)*2]=(u32)(reference_x[ch-2]+trackers[ch].u);
 TRACK_SHARED->reference_seed[(ch-2)*2+1]=(u32)(reference_y[ch-2]+trackers[ch].v);
 return 1;
}
static int capture_roi(int ch,u32 slot,int ref){
 const u32 bases[2][3]={{0x04000000,0x05000000,0x06000000},{0x07000000,0x08000000,0x09000000}};
 u32 dma=ch?0x43020000:0x43010000;
 int sx=(ref?reference_x[ch]:seedx[ch])-CENTER,sy=(ref?reference_y[ch]:seedy[ch])-CENTER;
 for(int y=0;y<ROI;y++){
  if(((rd(dma+0x28)>>24)&31)==slot)return 0;
  for(int x=0;x<ROI;x++){
   u32 address=bases[ch][slot]+((sy+y)*1280+sx+x)*3;
   u32 shift=(address&3)*8,pixel=rd(address&~3u)>>shift;
   if(shift>8)pixel|=rd((address&~3u)+4)<<(32-shift);
   raw[y*ROI+x]=(float)((29*(pixel&255)+150*((pixel>>8)&255)+77*((pixel>>16)&255))>>8);
  }
 }
 if(((rd(dma+0x28)>>24)&31)==slot)return 0;
 blur(raw,images[ch+ref*2]);return 1;
}
void worker_boot(void){
 boot_mmu();volatile TrackingShared *s=TRACK_SHARED;
 memset((void *)s,0,sizeof(*s));s->magic=0x54524b32;s->timer_hz=HZ;s->width=1280;s->height=720;s->ring_size=TRACK_RING;
 s->seed_x0=seedx[0];s->seed_y0=seedy[0];s->seed_x1=seedx[1];s->seed_y1=seedy[1];
 s->reference_seed[0]=reference_x[0];s->reference_seed[1]=reference_y[0];s->reference_seed[2]=reference_x[1];s->reference_seed[3]=reference_y[1];
 s->error=selftest();if(s->error)for(;;)__asm__ volatile("wfe");s->selftest=0x50415353;
 memset((void *)FREQ_SHARED,0,FREQ_BYTES);FREQ_SHARED->words[0]=0x46524631;FREQ_SHARED->words[12]=HZ;FREQ_SHARED->words[13]=FREQ_N;
 FREQ_SHARED->words[1]=freq_test();if(FREQ_SHARED->words[1])for(;;)__asm__ volatile("wfe");FREQ_SHARED->words[2]=0x50415353;
 u32 last=99,count=0,start=TICKS,sequence=0;
 for(;;){
  u32 a0=(rd(0x43010028)>>24)&31,a1=(rd(0x43020028)>>24)&31;
  if(a0>2||a1>2||a0==last)continue;last=a0;
  unsigned long long time=ticks();u32 begin=TICKS,slot0=(a0+2)%3,slot1=(a1+2)%3;
  TrackRecord r;memset(&r,0,sizeof(r));r.ticks_low=time;r.ticks_high=time>>32;r.buffers=slot0|(slot1<<8);
  int good=capture_roi(0,slot0,0)&&capture_roi(0,slot0,1)&&capture_roi(1,slot1,0)&&capture_roi(1,slot1,1);r.copy_ticks=TICKS-begin;u32 tr=TICKS;
  if(good){
   for(int ch=0;ch<4;ch++){
    int valid;
    if(!trackers[ch].ready)valid=ch<2?tracker_reference(&trackers[ch],images[ch]):initialize_background(ch,images[ch]);
    else valid=tracker_update(&trackers[ch],images[ch]);
    if(valid){if(ch<2)r.flags|=1u<<ch;else r.reference_flags|=1u<<(ch-2);}
   }
  }else{s->copy_drops++;r.flags=4;}
  r.track_ticks=TICKS-tr;
  r.x0=seedx[0]+trackers[0].u;r.y0=seedy[0]+trackers[0].v;r.score0=trackers[0].score;
  r.x1=seedx[1]+trackers[1].u;r.y1=seedy[1]+trackers[1].v;r.score1=trackers[1].score;
  r.lost0=trackers[0].lost;r.lost1=trackers[1].lost;
  r.refx0=reference_x[0]+trackers[2].u;r.refy0=reference_y[0]+trackers[2].v;r.refscore0=trackers[2].score;
  r.refx1=reference_x[1]+trackers[3].u;r.refy1=reference_y[1]+trackers[3].v;r.refscore1=trackers[3].score;
  volatile TrackRecord *dst=&s->ring[(sequence+1)%TRACK_RING];dst->sequence=0;
  __asm__ volatile("dsb sy":::"memory");
  r.sequence=0;memcpy((void *)dst,&r,sizeof(r));__asm__ volatile("dsb sy":::"memory");
  r.sequence=++sequence;dst->sequence=sequence;s->latest.sequence=0;
  __asm__ volatile("dsb sy":::"memory");memcpy((void *)&s->latest,&r,sizeof(r));
  if(r.flags&1)s->valid0++;if(r.flags&2)s->valid1++;count++;
  __asm__ volatile("dsb sy":::"memory");s->sequence=sequence;
  freq_ingest(&r);
  if(TICKS-start>=HZ){u32 elapsed=TICKS-start;s->fps10=(u32)((unsigned long long)count*HZ*10/elapsed);count=0;start=TICKS;}
 }
}
