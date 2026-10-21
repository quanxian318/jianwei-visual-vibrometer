#include "display_ctrl/display_ctrl.h"
#include "xparameters.h"
#include "xil_cache.h"
#include "xgpio.h"
#include "xiicps.h"
#include "sleep.h"
#include "i2c/PS_i2c.h"
#include "ov5640.h"
#include "stereo_match.h"
#include <string.h>
#include <stdio.h>
#include "xil_mmu.h"
#include "arm_neon.h"
#include "shared_depth.h"
extern int ov5640_read(XIicPs *,u16,u8 *);
#define RD 0x43000000u
#define W0 0x43010000u
#define W1 0x43020000u
#define DB ((volatile u32 *)0x03002000)
#define BYTES (1280u*720u*3u)
#define TIMER (*(volatile u32 *)0xf8f00200)
#define HZ (666666687u/2u)
static DisplayCtrl display;static XAxiVdma video;static XIicPs buses[2];static XGpio reset;
static u8 gl[SW*SH],gr[SW*SH],disparity[SW*SH];
static u8 *outputs[3]={(u8 *)0x0a000000,(u8 *)0x0b000000,(u8 *)0x0c000000};
static const u32 capture[2][3]={{0x04000000,0x05000000,0x06000000},{0x07000000,0x08000000,0x09000000}};
static u8 *snap[2]={(u8 *)0x0d000000,(u8 *)0x0e000000};
static u32 rr(u32 a){return *(volatile u32 *)a;}
static void wr(u32 a,u32 v){*(volatile u32 *)a=v;__asm__ volatile("dsb sy":::"memory");}
static void fail(u32 code){DB[2]=255;DB[3]=code;for(;;){}}
static int waitbit(u32 a,u32 mask,u32 value,u32 seconds){u32 t=TIMER;while(TIMER-t<HZ*seconds)if((rr(a)&mask)==value)return 1;return 0;}
static const u8 font[36][7]={
{14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},{30,17,17,17,17,17,30},
{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},{14,17,16,23,17,17,15},{17,17,17,31,17,17,17},
{14,4,4,4,4,4,14},{7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
{17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},{30,17,17,30,16,16,16},
{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},{15,16,16,14,1,1,30},{31,4,4,4,4,4,4},
{17,17,17,17,17,17,14},{17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
{17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
{14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{30,1,1,14,1,1,30},
{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},{14,16,16,30,17,17,14},{31,1,2,4,8,8,8},
{14,17,17,14,17,17,14},{14,17,17,15,1,1,14}};
static void pixel(u8 *o,int x,int y,u8 r,u8 g,u8 b){if(x<0||x>=1280||y<0||y>=720)return;u8 *p=o+(y*1280+x)*3;p[0]=r;p[1]=g;p[2]=b;}
static void text(u8 *o,int x,int y,const char *s,int scale){
    while(*s){int ch=*s++,i=ch>='A'&&ch<='Z'?ch-'A':ch>='0'&&ch<='9'?ch-'0'+26:-1;
        if(i>=0)for(int dy=0;dy<7;dy++)for(int dx=0;dx<5;dx++)if(font[i][dy]&(16>>dx))
            for(int yy=0;yy<scale;yy++)for(int xx=0;xx<scale;xx++)pixel(o,x+dx*scale+xx,y+dy*scale+yy,210,220,230);
        x+=6*scale;}
}
static void capture_start(u32 base,int ch){
    wr(base+0x30,4);if(!waitbit(base+0x30,4,0,2))fail(10+ch);wr(base+0x48,3);wr(base+0x34,0x7ff0);
    for(int i=0;i<3;i++)wr(base+0xac+i*4,capture[ch][i]);
    wr(base+0xa4,3840);wr(base+0xa8,3840);wr(base+0x30,0x00011003);wr(base+0xa0,720);
}
#if 0 /* Original combined 7 FPS path retained for reference only. */
static int snapshot(void){
    for(int ch=0;ch<2;ch++){u32 base=ch?W1:W0,index=(rr(base+0x28)>>24)&31u;if(index>2)return 0;
        u32 complete=(index+2)%3;Xil_DCacheInvalidateRange(capture[ch][complete],BYTES);
        /* Copy short strips and detect any writer reuse of the selected
         * slot. A single before/after whole-frame index could miss a wrap. */
        for(u32 offset=0;offset<BYTES;offset+=122880u){
            if(((rr(base+0x28)>>24)&31u)==complete)return 0;
            u32 count=BYTES-offset;if(count>122880u)count=122880u;
            memcpy(snap[ch]+offset,(void *)(capture[ch][complete]+offset),count);
            if(((rr(base+0x28)>>24)&31u)==complete)return 0;
        }
        if(rr(base+0x34)&0x0ff0){DB[10+ch]++;wr(base+0x34,0x7ff0);return 0;}}
    for(int y=0;y<SH;y++)for(int x=0;x<SW;x++)for(int ch=0;ch<2;ch++){
        u8 *p=snap[ch]+((y*8+4)*1280+x*8+4)*3;(ch?gr:gl)[y*SW+x]=(77u*p[0]+150u*p[1]+29u*p[2])>>8;}
    return 1;
}
static void preview(u8 *o,int ox,int oy,int w,int h,int ch){for(int y=0;y<h;y++)for(int x=0;x<w;x++){
    const u8 *p=snap[ch]+((y*720/h)*1280+x*1280/w)*3;pixel(o,ox+x,oy+y,p[0],p[1],p[2]);}}
int main(void){
    for(int i=0;i<16;i++)DB[i]=0;DB[0]=0x53544552;DB[1]=1;wr(0xf8f00208,rr(0xf8f00208)|1);
    if(i2c_init(&buses[0],XPAR_XIICPS_0_DEVICE_ID,100000)!=0)fail(1);
    XIicPs_Config cfg1={1,0xe0005000,buses[0].Config.InputClockHz};
    if(XIicPs_CfgInitialize(&buses[1],&cfg1,cfg1.BaseAddress)!=0)fail(2);XIicPs_SetSClk(&buses[1],100000);
    XGpio_Initialize(&reset,XPAR_CMOS_RST_DEVICE_ID);XGpio_SetDataDirection(&reset,1,0);
    XGpio_DiscreteWrite(&reset,1,3);usleep(500000);XGpio_DiscreteWrite(&reset,1,0);usleep(500000);XGpio_DiscreteWrite(&reset,1,3);usleep(500000);
    for(int ch=0;ch<2;ch++){u8 hi=0,lo=0;ov5640_read(&buses[ch],0x300a,&hi);ov5640_read(&buses[ch],0x300b,&lo);
        DB[4+ch]=(hi<<8)|lo;if(DB[4+ch]!=0x5640)fail(3+ch);sensor_init(&buses[ch]);}
    DB[1]=2;XAxiVdma_Config *vc=XAxiVdma_LookupConfig(XPAR_AXIVDMA_0_DEVICE_ID);
    if(!vc||XAxiVdma_CfgInitialize(&video,vc,vc->BaseAddress)!=0)fail(5);
    for(int i=0;i<3;i++){memset(outputs[i],12,BYTES);Xil_DCacheFlushRange((UINTPTR)outputs[i],BYTES);}
    if(DisplayInitialize(&display,&video,XPAR_VTC_0_DEVICE_ID,XPAR_AXI_DYNCLK_0_BASEADDR,outputs,3840)!=0)fail(6);
    if(DisplayStart(&display)!=0)fail(7);wr(RD,rr(RD)&~2u);wr(RD+0x28,rr(RD+0x28)&~31u);
    capture_start(W0,0);capture_start(W1,1);
    for(int i=0;i<30;i++){usleep(100000);wr(W0+0x34,0x7ff0);wr(W1+0x34,0x7ff0);}
    DB[1]=3;DB[2]=2;u32 frames=0,t0=TIMER;int pending=0;
    for(;;){
        if(!snapshot()){DB[12]++;continue;}
        int oy=0,dir=0,n=stereo_match(gl,gr,disparity,&oy,&dir);DB[6]=n;DB[7]=oy;DB[8]=dir;
        int current=(rr(RD+0x28)>>16)&31u,slot=0;while(slot==current||slot==pending)slot++;
        if(slot>2){DB[12]++;continue;}u8 *out=outputs[slot];memset(out,12,BYTES);
        text(out,35,20,"STEREO RELATIVE DEPTH TEST",3);text(out,40,60,"CAMERA VIEW",2);text(out,680,60,"NEAR RED FAR BLUE",2);
        preview(out,40,95,560,315,0);
        for(int y=0;y<315;y++)for(int x=0;x<560;x++){u8 rgb[3];depth_color(disparity[(y*SH/315)*SW+x*SW/560],rgb);pixel(out,680+x,95+y,rgb[0],rgb[1],rgb[2]);}
        text(out,40,435,"CAMERA 1",2);text(out,340,435,"CAMERA 2",2);preview(out,40,465,280,158,0);preview(out,340,465,280,158,1);
        text(out,680,440,"GRAY MEANS NO RELIABLE MATCH",2);text(out,680,485,"UNCALIBRATED STATIC SCENE ONLY",2);text(out,680,530,"BASELINE 40MM LENS 3 6MM",2);
        for(int x=0;x<560;x++)for(int y=0;y<20;y++){u8 rgb[3];depth_color(32-x*31/559,rgb);pixel(out,680+x,580+y,rgb[0],rgb[1],rgb[2]);}
        text(out,680,610,"NEAR",2);text(out,1190,610,"FAR",2);text(out,40,665,"PROTOTYPE DISPARITY NOT METRIC DISTANCE",2);
        char stats[48];snprintf(stats,sizeof(stats),"DEPTH FPS %u VALID %u PERCENT",DB[13]/10u,(unsigned)n*100u/(SW*SH));text(out,680,655,stats,2);
        Xil_DCacheFlushRange((UINTPTR)out,BYTES);wr(RD+0x28,(rr(RD+0x28)&~31u)|slot);pending=slot;DB[9]++;frames++;
        u32 elapsed=TIMER-t0;if(elapsed>=HZ){DB[13]=(u32)((unsigned long long)frames*HZ*10u/elapsed);t0=TIMER;frames=0;}
        DB[14]=rr(W0+0x34);DB[15]=rr(W1+0x34);
    }
}
#endif
#include "stereo_fast_main.inc"
