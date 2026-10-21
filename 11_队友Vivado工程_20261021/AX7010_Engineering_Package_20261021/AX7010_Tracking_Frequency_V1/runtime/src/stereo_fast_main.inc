extern int ov5640_write(XIicPs *,u16,u8);
#include "tracking_shared.h"
#include "frequency_shared.h"
extern void network_view_init(void);
extern void network_view_poll(const u8 *screen);
static const u8 *network_screen;
static u32 depth_seq[3];
static u32 heat_sequence;
static u32 heat_render_time;
static u8 heat_cache[360*1920];
static u8 depth_palette[256][3];
static int palette_ready;
static void configure_camera(int ch){
    XIicPs *b=&buses[ch];
    ov5640_write(b,0x3008,0x42);ov5640_write(b,0x3036,120);
    ov5640_write(b,0x380c,5);ov5640_write(b,0x380d,0xa0);
    ov5640_write(b,0x3a08,0);ov5640_write(b,0x3a09,166);
    ov5640_write(b,0x3a0a,0);ov5640_write(b,0x3a0b,138);
    ov5640_write(b,0x3a0e,4);ov5640_write(b,0x3a0d,5);
    ov5640_write(b,0x5580,2);ov5640_write(b,0x5588,0x41);
    ov5640_write(b,0x5583,0x48);ov5640_write(b,0x5584,0x48);
    const u8 gamma[15]={0x28,0x48,0x68,0x82,0x92,0x9c,0xa6,0xaf,0xb8,0xc1,0xcf,0xd9,0xe6,0xed,0xf4};
    for(int i=0;i<15;i++)ov5640_write(b,0x5481+i,gamma[i]);
    ov5640_write(b,0x3008,2);
}
static void common_gain(u32 gain){
    for(int ch=0;ch<2;ch++){
        XIicPs *b=&buses[ch];
        int s=ov5640_write(b,0x3208,0);
        if(s==0)s=ov5640_write(b,0x350a,(gain>>8)&3);
        if(s==0)s=ov5640_write(b,0x350b,gain&255);
        if(s==0)s=ov5640_write(b,0x3208,0x10);
        if(s==0)s=ov5640_write(b,0x3208,0xa0);
        if(s!=0)DB[27]++;
    }
    DB[21]=gain;
}
static void lock_exposure(void){
    unsigned long long sum=0;
    for(int ch=0;ch<2;ch++){
        u8 e0,e1,e2,g0,g1;XIicPs *b=&buses[ch];
        ov5640_read(b,0x3500,&e0);ov5640_read(b,0x3501,&e1);ov5640_read(b,0x3502,&e2);
        ov5640_read(b,0x350a,&g0);ov5640_read(b,0x350b,&g1);
        u32 exposure=((e0&15u)<<16)|(e1<<8)|e2,gain=((g0&3u)<<8)|g1;
        sum+=(unsigned long long)exposure*gain;
        ov5640_write(b,0x3503,3);ov5640_write(b,0x3500,0);
        ov5640_write(b,0x3501,0x29);ov5640_write(b,0x3502,0xb0); /* 667 lines */
    }
    u32 gain=(u32)(sum/(2u*10672u));if(gain<16)gain=16;if(gain>512)gain=512;
    common_gain(gain);
}
static uint8x8_t even8(uint8x16_t v){return vget_low_u8(vuzpq_u8(v,v).val[0]);}
static void down2row(u8 *dst,const u8 *src,int width){
    for(int x=0;x<width;x+=8){
        uint8x16x3_t p=vld3q_u8(src+x*6);uint8x8x3_t q;
        q.val[0]=even8(p.val[0]);q.val[1]=even8(p.val[1]);q.val[2]=even8(p.val[2]);vst3_u8(dst+x*3,q);
    }
}
static void down4row(u8 *dst,const u8 *src){
    for(int x=0;x<320;x+=8){
        uint8x16x3_t a=vld3q_u8(src+x*12),b=vld3q_u8(src+x*12+48);uint8x8x3_t q;
        for(int c=0;c<3;c++)q.val[c]=vuzp_u8(even8(a.val[c]),even8(b.val[c])).val[0];
        vst3_u8(dst+x*3,q);
    }
}
/* Diagnostic pair export, requested once via a noncacheable mailbox.
 * Independent exposures; used for static-scene analysis, not sync claims. */
static void export_pair(void){
    volatile u32 *control=(volatile u32 *)0x03040000u;
    u32 selected[2];
    for(int ch=0;ch<2;ch++){
        u32 active=(rr((ch?W1:W0)+0x28)>>24)&31u;
        if(active>2){control[0]=0x4641494cu;return;}selected[ch]=(active+2)%3;
    }
    for(int ch=0;ch<2;ch++){
        u32 dma=ch?W1:W0;
        for(int y=0;y<180;y++){
            if(((rr(dma+0x28)>>24)&31u)==selected[ch]){control[0]=0x4641494cu;return;}
            u32 source=capture[ch][selected[ch]]+y*4*3840;
            Xil_DCacheInvalidateRange(source,3840);
            down4row(snap[ch]+y*960,(void *)source);
        }
        if(((rr(dma+0x28)>>24)&31u)==selected[ch]){control[0]=0x4641494cu;return;}
        Xil_DCacheFlushRange((UINTPTR)snap[ch],320*180*3);
    }
    control[1]=320;control[2]=180;__asm__ volatile("dsb sy":::"memory");control[0]=0x4f4b4159u;
}
static int draw_preview(u8 *out,const u32 selected[2]){
    for(int y=0;y<360;y++){
        if(((rr(W0+0x28)>>24)&31u)==selected[0])return 0;
        u32 src=capture[0][selected[0]]+y*2*3840;
        Xil_DCacheInvalidateRange(src,3840);
        down2row(out+(64+y)*3840,(void *)src,640);
    }
    for(int y=0;y<180;y++){
        /* Camera 1 thumbnail reuses the just-rendered cached preview. */
        down2row(out+(440+y)*3840,out+(64+y*2)*3840,320);
        if(((rr(W1+0x28)>>24)&31u)==selected[1])return 0;
        u32 src=capture[1][selected[1]]+y*4*3840;
        Xil_DCacheInvalidateRange(src,3840);
        down4row(out+(440+y)*3840+960,(void *)src);
    }
    return (((rr(W0+0x28)>>24)&31u)!=selected[0] && ((rr(W1+0x28)>>24)&31u)!=selected[1]);
}
static void paint_depth(u8 *out,int slot){
    volatile DepthShared *s=DEPTH_SHARED;u32 seq=s->sequence;
    DB[6]=s->valid;DB[7]=s->vertical;DB[8]=s->direction;DB[13]=s->fps10;
    DB[29]=s->quality[0];DB[30]=s->quality[1];DB[31]=s->quality[3];
    if(!seq)return;
    /* Colour rendering need not stall the camera preview for every result.
     * Matching continues at its measured rate; refresh heat at up to 12 Hz.
     * Each display buffer still receives one complete immutable heat image. */
    u32 now=TIMER;
    if(heat_sequence!=seq&&(!heat_sequence||now-heat_render_time>=HZ/12)){
    u32 b=s->buffer;if(b>1)return;
    /* Shared DDR is noncached: a word transaction carries four disparities. */
    volatile u32 *packed_depth=(volatile u32 *)s->disparity[b];
    for(u32 i=0;i<SW*SH/4;i++){
        u32 values=packed_depth[i];
        disparity[i*4]=values;disparity[i*4+1]=values>>8;
        disparity[i*4+2]=values>>16;disparity[i*4+3]=values>>24;
    }
    __asm__ volatile("dsb sy":::"memory");
    if(s->sequence!=seq)return;
    /* Half-grid colour interpolation only within a coherent valid patch.
     * This smooths display blocks; it does NOT add measured depth pixels. */
    for(int y=0;y<SH;y++){
        u8 row[1920];
        int gy=y,ny=gy<SH-1?gy+1:gy,fy=0;
        for(int x=0;x<SW;x++){
            int gx=x,nx=gx<SW-1?gx+1:gx,fx=0;
            u8 a=disparity[gy*SW+gx],b=disparity[gy*SW+nx],c=disparity[ny*SW+gx],d=disparity[ny*SW+nx];
            int lo=a&127,hi=lo,values[3]={b&127,c&127,d&127};
            for(int k=0;k<3;k++){if(values[k]<lo)lo=values[k];if(values[k]>hi)hi=values[k];}
            u8 rgb[3];
            int smooth=a&&b&&c&&d&&hi-lo<=2&&((a^b)&128)==0&&((a^c)&128)==0&&((a^d)&128)==0;
            for(int k=0;k<3;k++)rgb[k]=smooth?
                (depth_palette[a][k]*(2-fx)*(2-fy)+depth_palette[b][k]*fx*(2-fy)+depth_palette[c][k]*(2-fx)*fy+depth_palette[d][k]*fx*fy+2)/4:depth_palette[a][k];
            for(int dx=0;dx<2;dx++){u8 *p=row+(x*2+dx)*3;p[0]=rgb[0];p[1]=rgb[1];p[2]=rgb[2];}
        }
        for(int dy=0;dy<2;dy++){
            u8 *p=heat_cache+(y*2+dy)*1920;memcpy(p,row,1920);
        }
    }
    heat_sequence=seq;heat_render_time=now;
    }
    if(depth_seq[slot]==heat_sequence)return;
    for(int y=0;y<360;y++){
        u8 *p=out+(64+y)*3840+1920;memcpy(p,heat_cache+y*1920,1920);Xil_DCacheFlushRange((UINTPTR)p,1920);
    }
    depth_seq[slot]=heat_sequence;
}
static void meter_gain(const u32 selected[2]){
    static u32 last,filtered;u32 now=TIMER;if(now-last<HZ/2)return;last=now;
    u32 total=0,count=0;
    for(int ch=0;ch<2;ch++)for(int y=192;y<576;y+=64){
        u32 row=capture[ch][selected[ch]]+y*3840;Xil_DCacheInvalidateRange(row,3840);
        for(int x=256;x<1024;x+=64){u8 *p=(u8 *)(row+x*3);u32 v=(77u*p[0]+150u*p[1]+29u*p[2])>>8;if(v>160)v=160;total+=v;count++;}
    }
    u32 luma=total/count;filtered=filtered?(3*filtered+luma+2)/4:luma;DB[22]=filtered;
    u8 hi=0,lo=0;ov5640_read(&buses[0],0x350a,&hi);ov5640_read(&buses[0],0x350b,&lo);
    u32 gain=((hi&3u)<<8)|lo;if(gain<16){DB[27]++;return;}u32 next=gain,step=(gain+15)/16;
    if(filtered<118)next+=step;else if(filtered>134)next-=step;
    if(next<16)next=16;if(next>512)next=512;if(next!=gain)common_gain(next);
}
static void static_layout(u8 *out){
    if(!palette_ready){
        for(int i=0;i<256;i++)depth_palette[i][0]=depth_palette[i][1]=depth_palette[i][2]=48;
        for(int i=1;i<=MAX_DISPARITY;i++){depth_color(i,depth_palette[i]);depth_color(i|128,depth_palette[i|128]);}
        /* HDMI byte lanes render this framebuffer in B,G,R order. */
        for(int i=0;i<256;i++){u8 t=depth_palette[i][0];depth_palette[i][0]=depth_palette[i][2];depth_palette[i][2]=t;}
        palette_ready=1;
    }
    memset(out,12,BYTES);text(out,10,8,"TRACK + BOARD FFT V1",3);text(out,660,8,"PIXEL MOTION",3);
    text(out,0,36,"CAMERA 1",2);text(out,660,36,"NOT CALIBRATED 3D",2);
    text(out,0,424,"CAMERA 1",2);text(out,320,424,"CAMERA 2",2);
    text(out,670,445,"GRAY MEANS NO RELIABLE MATCH",2);text(out,670,485,"SPATIAL TEMPORAL - NO FILL",2);
    text(out,670,520,"FPGA BM V1.2 ADAPTIVE TEST",2);
    for(int x=0;x<560;x++)for(int y=0;y<20;y++){u8 rgb[3];depth_color(64-x*63/559,rgb);pixel(out,670+x,555+y,rgb[2],rgb[1],rgb[0]);}
    text(out,670,590,"NEAR",2);text(out,1180,590,"FAR",2);
    text(out,10,690,"BOARD ROI TRACKING - NETWORK PREVIEW IS NOT SAMPLE RATE",2);
    for(int y=64;y<424;y++)memset(out+y*3840+1920,48,1920);
}
static void tracking_overlay(u8 *out,const TrackRecord *r){
 for(int ch=0;ch<2;ch++){
  int valid=r->flags&(1u<<ch);float x=ch?r->x1:r->x0,y=ch?r->y1:r->y0;
  int px=(int)(x*.25f)+ch*320,py=440+(int)(y*.25f);
  for(int k=-6;k<=6;k++){pixel(out,px+k,py,0,valid?240:40,valid?0:240);pixel(out,px,py+k,0,valid?240:40,valid?0:240);}
  if(!ch){px=(int)(x*.5f);py=64+(int)(y*.5f);
   for(int k=-8;k<=8;k++){pixel(out,px+k,py,0,valid?240:40,valid?0:240);pixel(out,px,py+k,0,valid?240:40,valid?0:240);}}
  float rx=ch?r->refx1:r->refx0,ry=ch?r->refy1:r->refy0;
  int rv=r->reference_flags&(1u<<ch);int qx=(int)(rx*.25f)+ch*320,qy=440+(int)(ry*.25f);
  for(int k=-5;k<=5;k++){pixel(out,qx+k,qy-5,rv?230:0,0,240);pixel(out,qx+k,qy+5,rv?230:0,0,240);pixel(out,qx-5,qy+k,rv?230:0,0,240);pixel(out,qx+5,qy+k,rv?230:0,0,240);}
  if(!ch){qx=(int)(rx*.5f);qy=64+(int)(ry*.5f);for(int k=-7;k<=7;k++){pixel(out,qx+k,qy-7,rv?230:0,0,240);pixel(out,qx+k,qy+7,rv?230:0,0,240);pixel(out,qx-7,qy+k,rv?230:0,0,240);pixel(out,qx+7,qy+k,rv?230:0,0,240);}}
 }
}
static void paint_tracking(u8 *out){
 volatile TrackingShared *s=TRACK_SHARED;TrackRecord r;
 for(int y=64;y<625;y++)memset(out+y*3840+1920,12,1920);
 if(s->magic!=0x54524b32||!s->sequence){text(out,670,90,"WAITING FOR TRACKER",2);return;}
 u32 seq=s->sequence;memcpy(&r,(const void *)&s->latest,sizeof(r));
 if(r.sequence!=seq||s->sequence!=seq||s->latest.sequence!=seq)return;
 tracking_overlay(out,&r);char line[90];
 snprintf(line,sizeof(line),"LEFT %s Q %u",r.flags&1?"VALID":"LOST",(u32)(r.score0*1000));text(out,670,68,line,2);
 snprintf(line,sizeof(line),"RIGHT %s Q %u",r.flags&2?"VALID":"LOST",(u32)(r.score1*1000));text(out,670,90,line,2);
 snprintf(line,sizeof(line),"L X %u Y %u",(u32)(r.x0*1000),(u32)(r.y0*1000));text(out,670,120,line,2);
 snprintf(line,sizeof(line),"R X %u Y %u",(u32)(r.x1*1000),(u32)(r.y1*1000));text(out,670,142,line,2);
 snprintf(line,sizeof(line),"REF L %s R %s",r.reference_flags&1?"VALID":"LOST",r.reference_flags&2?"VALID":"LOST");text(out,670,165,line,2);
 text(out,670,185,"XY VALUES PIXELS TIMES 1000",2);
 text(out,670,200,"RECENT DX GREEN L ORANGE R",2);text(out,670,390,"RECENT DY GREEN L ORANGE R",2);
 float centrex[2]={0,0},centrey[2]={0,0};
 for(int ch=0;ch<2;ch++){
  centrex[ch]=(ch?r.x1:r.x0)-(float)(ch?s->seed_x1:s->seed_x0)-(ch?r.refx1:r.refx0)+(float)s->reference_seed[ch*2];
  centrey[ch]=(ch?r.y1:r.y0)-(float)(ch?s->seed_y1:s->seed_y0)-(ch?r.refy1:r.refy0)+(float)s->reference_seed[ch*2+1];
 }
 for(int y=240;y<=570;y+=190)for(int x=670;x<1230;x++)pixel(out,x,y+55,65,65,65);
 unsigned count=seq<120?seq:120;
 for(unsigned k=0;k<count;k++){
  unsigned n=seq-count+1+k;volatile TrackRecord *a=&s->ring[n%TRACK_RING];
  if(a->sequence!=n)continue;
  TrackRecord v;memcpy(&v,(const void *)a,sizeof(v));__asm__ volatile("dsb sy":::"memory");if(a->sequence!=n)continue;
  for(int ch=0;ch<2;ch++)if((v.flags&v.reference_flags)&(1u<<ch)){
   float dx=(ch?v.x1:v.x0)-(float)(ch?s->seed_x1:s->seed_x0);
   float dy=(ch?v.y1:v.y0)-(float)(ch?s->seed_y1:s->seed_y0);
   dx-=(ch?v.refx1:v.refx0)-(float)s->reference_seed[ch*2];
   dy-=(ch?v.refy1:v.refy0)-(float)s->reference_seed[ch*2+1];
   int ix=670+(int)(k*559/119),iy=295-(int)((dx-centrex[ch])*50),jy=485-(int)((dy-centrey[ch])*50);
   if(iy>=230&&iy<365)for(int z=0;z<3;z++)pixel(out,ix+z,iy,ch?0:20,ch?130:230,ch?240:20);
   if(jy>=420&&jy<555)for(int z=0;z<3;z++)pixel(out,ix+z,jy,ch?0:20,ch?130:230,ch?240:20);
  }
 }
 volatile FrequencyShared *f=FREQ_SHARED;
 u32 before=f->words[3];float candidate[4];u32 end=f->words[4],base=f->words[14],mask=f->words[5];
 for(int ch=0;ch<4;ch++)candidate[ch]=f->candidate[ch];
 __asm__ volatile("dsb sy":::"memory");
 if(!(before&1)&&f->words[3]==before&&f->words[0]==0x46524631){
  u32 fresh=seq>=end&&seq-end<135;
  if(!fresh||mask!=15)text(out,670,570,"FFT WAITING / INVALID WINDOW",2);
  else if(base==3)text(out,670,570,"BASELINE FAILED - CHECK REF",2);
  else if(base!=2){snprintf(line,sizeof(line),"BASELINE %u SEC %u L %u R %u",base,f->words[15]/1000,f->words[16],f->words[18]);text(out,670,570,line,2);}
  else {snprintf(line,sizeof(line),"CAND HZ X10 L %u/%u R %u/%u",(u32)(candidate[0]*10),(u32)(candidate[1]*10),(u32)(candidate[2]*10),(u32)(candidate[3]*10));text(out,670,570,line,2);}
 }
 text(out,670,600,"0 = NO CANDIDATE - ALIAS RISK",2);
 for(int y=64;y<625;y++)Xil_DCacheFlushRange((UINTPTR)(out+y*3840+1920),1920);
}
int main(void){
    Xil_SetTlbAttributes(0x03000000,NORM_NONCACHE);
    memset((void *)DEPTH_SHARED,0,sizeof(DepthShared));for(int i=0;i<32;i++)DB[i]=0;
    *(volatile u32 *)0x03040000u=0;
    memset((void *)TRACK_SHARED,0,sizeof(TrackingShared));
    DB[0]=0x53544552;DB[1]=1;DB[24]=201;wr(0xf8f00208,rr(0xf8f00208)|1);
    if(i2c_init(&buses[0],XPAR_XIICPS_0_DEVICE_ID,100000)!=0)fail(1);
    XIicPs_Config cfg1={1,0xe0005000,buses[0].Config.InputClockHz};
    if(XIicPs_CfgInitialize(&buses[1],&cfg1,cfg1.BaseAddress)!=0)fail(2);XIicPs_SetSClk(&buses[1],100000);
    XGpio_Initialize(&reset,XPAR_CMOS_RST_DEVICE_ID);XGpio_SetDataDirection(&reset,1,0);
    XGpio_DiscreteWrite(&reset,1,3);usleep(500000);XGpio_DiscreteWrite(&reset,1,0);usleep(500000);XGpio_DiscreteWrite(&reset,1,3);usleep(500000);
    for(int ch=0;ch<2;ch++){u8 h=0,l=0;ov5640_read(&buses[ch],0x300a,&h);ov5640_read(&buses[ch],0x300b,&l);
        DB[4+ch]=(h<<8)|l;if(DB[4+ch]!=0x5640)fail(3+ch);sensor_init(&buses[ch]);configure_camera(ch);}
    usleep(2500000);lock_exposure();DB[1]=2;
    XAxiVdma_Config *vc=XAxiVdma_LookupConfig(XPAR_AXIVDMA_0_DEVICE_ID);
    if(!vc||XAxiVdma_CfgInitialize(&video,vc,vc->BaseAddress)!=0)fail(5);
    for(int i=0;i<3;i++){static_layout(outputs[i]);Xil_DCacheFlushRange((UINTPTR)outputs[i],BYTES);}
    if(DisplayInitialize(&display,&video,XPAR_VTC_0_DEVICE_ID,XPAR_AXI_DYNCLK_0_BASEADDR,outputs,3840)!=0)fail(6);
    if(DisplayStart(&display)!=0)fail(7);wr(RD,rr(RD)&~2u);wr(RD+0x28,rr(RD+0x28)&~31u);
    capture_start(W0,0);capture_start(W1,1);
    for(int i=0;i<20;i++){usleep(100000);wr(W0+0x34,0x7ff0);wr(W1+0x34,0x7ff0);}
    DB[1]=3;DB[2]=2;
    network_screen=outputs[0];network_view_init();
    u32 previous[2]={(rr(W0+0x28)>>24)&31u,(rr(W1+0x28)>>24)&31u};
    u32 cap_count[2]={0,0},shown=0,submitted=0,t0=TIMER,last_render=99,reader=(rr(RD+0x28)>>16)&31u;int pending=0;
    for(;;){
        network_view_poll(network_screen);
        if(*(volatile u32 *)0x03040000u==0x534e4150u)export_pair();
        u32 r=(rr(RD+0x28)>>16)&31u;if(r!=reader){reader=r;shown++;DB[19]++;}
        u32 active[2]={(rr(W0+0x28)>>24)&31u,(rr(W1+0x28)>>24)&31u};
        for(int ch=0;ch<2;ch++)if(active[ch]!=previous[ch]){u32 delta=(active[ch]+3-previous[ch])%3;cap_count[ch]+=delta;DB[25+ch]+=delta;previous[ch]=active[ch];}
        if(active[0]>2||active[1]>2)fail(20);
        DB[20]=DEPTH_SHARED->ready;
        u32 elapsed=TIMER-t0;if(elapsed>=HZ){DB[16]=(u32)((unsigned long long)shown*HZ*10u/elapsed);DB[17]=(u32)((unsigned long long)cap_count[0]*HZ*10u/elapsed);DB[18]=(u32)((unsigned long long)cap_count[1]*HZ*10u/elapsed);DB[23]=(u32)((unsigned long long)submitted*HZ*10u/elapsed);shown=submitted=cap_count[0]=cap_count[1]=0;t0=TIMER;}
        if(active[0]==last_render)continue;last_render=active[0];u32 selected[2]={(active[0]+2)%3,(active[1]+2)%3};
        int slot=0;while(slot==(int)reader||slot==pending)slot++;if(slot>2){DB[12]++;continue;}
        u32 start=TIMER;u8 *out=outputs[slot];if(!draw_preview(out,selected)){DB[12]++;continue;}
        paint_tracking(out);
        memset(out+640*3840,12,25*3840);char stats[100];
        snprintf(stats,sizeof(stats),"VIEW %u CAP1 %u CAP2 %u TRACK %u DROPS %u",DB[16]/10,DB[17]/10,DB[18]/10,TRACK_SHARED->fps10/10,TRACK_SHARED->copy_drops);text(out,10,643,stats,2);
        for(int y=64;y<424;y++)Xil_DCacheFlushRange((UINTPTR)(out+y*3840),1920);
        for(int y=440;y<620;y++)Xil_DCacheFlushRange((UINTPTR)(out+y*3840),1920);
        Xil_DCacheFlushRange((UINTPTR)(out+640*3840),25*3840);
        wr(RD+0x28,(rr(RD+0x28)&~31u)|slot);pending=slot;DB[9]++;submitted++;
        network_screen=out;
        if(TIMER-start>DB[28])DB[28]=TIMER-start;
        for(int ch=0;ch<2;ch++){u32 base=ch?W1:W0;DB[14+ch]=rr(base+0x34);if(DB[14+ch]&0xff0){DB[10+ch]++;wr(base+0x34,0x7ff0);}}
        meter_gain(selected);
    }
}
