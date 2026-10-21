/* Minimal polled IPv4/UDP snapshot endpoint for a directly connected host.
 * Only finite start/clear baseline controls; no flash writes, generic memory
 * writes, routing or dependency on an external server.
 * GEM DMA descriptors/buffers use a dedicated noncached 1-MiB region.
 */
#include "xemacps.h"
#include "xemacps_hw.h"
#include "xil_cache.h"
#include "xil_mmu.h"
#include "xparameters.h"
#include <string.h>
#include <stdint.h>
#include "tracking_shared.h"
#include "frequency_shared.h"
#define GEM 0xe000b000u
#define RXN 32
#define TXN 64
#define CHUNK 1400
#define DBG ((volatile uint32_t *)0x03070000u)
typedef struct {volatile uint32_t addr,status;} Descriptor;
static volatile Descriptor *rx=(void *)0x0f000000u,*tx=(void *)0x0f001000u;
static XEmacPs emac;
static unsigned char image[640*360*2] __attribute__((aligned(64)));
static const unsigned char mac[6]={0x02,0x41,0x58,0x70,0x10,0x18};
static const unsigned char ip[4]={192,168,1,10};
static unsigned char peer_mac[6],peer_ip[4];
static unsigned int ri,ti,width,height,format,total,offset,frame_id,stamp;
static unsigned short peer_port;
static int initialized,busy;
static uint32_t getreg(unsigned a){return *(volatile uint32_t *)a;}
static void putreg(unsigned a,unsigned v){*(volatile uint32_t *)a=v;__asm__ volatile("dsb sy":::"memory");}
static unsigned short be16(const unsigned char *p){return (p[0]<<8)|p[1];}
static void set16(unsigned char *p,unsigned v){p[0]=v>>8;p[1]=v;}
static void le32(unsigned char *p,unsigned v){p[0]=v;p[1]=v>>8;p[2]=v>>16;p[3]=v>>24;}
static unsigned checksum(const unsigned char *p,unsigned n){unsigned s=0;for(unsigned i=0;i<n;i+=2)s+=be16(p+i);while(s>>16)s=(s&65535)+(s>>16);return (~s)&65535;}
static unsigned char *tx_packet(void){
 if(!(tx[ti].status&XEMACPS_TXBUF_USED_MASK))return 0;
 return (void *)(0x0f020000u+ti*2048);
}
static void send_packet(unsigned n){
 if(n<60)n=60;
 __asm__ volatile("dsb sy":::"memory");
 tx[ti].status=n|XEMACPS_TXBUF_LAST_MASK|(ti==TXN-1?XEMACPS_TXBUF_WRAP_MASK:0);
 __asm__ volatile("dsb sy":::"memory");
 putreg(GEM+XEMACPS_NWCTRL_OFFSET,getreg(GEM+XEMACPS_NWCTRL_OFFSET)|XEMACPS_NWCTRL_STARTTX_MASK);
 ti=(ti+1)%TXN;DBG[3]++;
}
static void arp_reply(const unsigned char *r){
 unsigned char *p=tx_packet();if(!p)return;
 memset(p,0,60);memcpy(p,r+6,6);memcpy(p+6,mac,6);set16(p+12,0x806);
 set16(p+14,1);set16(p+16,0x800);p[18]=6;p[19]=4;set16(p+20,2);
 memcpy(p+22,mac,6);memcpy(p+28,ip,4);memcpy(p+32,r+22,6);memcpy(p+38,r+28,4);
 send_packet(60);DBG[4]++;
}
static void snapshot(const unsigned char *r,unsigned type,const unsigned char *screen){
 if(busy){DBG[8]++;return;}
 memcpy(peer_mac,r+6,6);memcpy(peer_ip,r+26,4);peer_port=be16(r+34);
 width=640;height=360;format=1;
 const unsigned char *source=screen;unsigned stride=3840,step=2;
 unsigned roi_dma=0,roi_slot=0;
 if(type==2||type==3){
  width=320;height=180;source=screen+440*3840+(type==3?960:0);step=1;
 }
 if(type==6||type==7){
  unsigned ch=type-6;roi_dma=ch?0x43020000:0x43010000;
  unsigned active=(getreg(roi_dma+0x28)>>24)&31;if(active>2)return;roi_slot=(active+2)%3;
  static const unsigned bases[2][3]={{0x04000000,0x05000000,0x06000000},{0x07000000,0x08000000,0x09000000}};
  unsigned sx=ch?TRACK_SHARED->seed_x1:TRACK_SHARED->seed_x0,sy=ch?TRACK_SHARED->seed_y1:TRACK_SHARED->seed_y0;
  if(sx<48||sx>1232||sy<48||sy>672)return;
  source=(void *)(bases[ch][roi_slot]+((sy-48)*1280+sx-48)*3);width=height=96;step=1;
 }
 // A complete cached snapshot is immutable while chunks are transmitted.
 if(type==9){
  volatile FrequencyShared *s=FREQ_SHARED;unsigned before=s->words[3];
  if(before&1)return;
  __asm__ volatile("dsb sy":::"memory");
  volatile unsigned *words=(void *)s;
  for(unsigned i=0;i<FREQ_BYTES/4;i++)le32(image+i*4,words[i]);
  __asm__ volatile("dsb sy":::"memory");if(s->words[3]!=before){DBG[8]++;return;}
  width=FREQ_BYTES;height=1;format=4;total=FREQ_BYTES;
 }else if(type==5){
  volatile TrackingShared *s=TRACK_SHARED;unsigned seq=s->sequence;
  volatile unsigned *words=(void *)s;
  for(unsigned i=0;i<64;i++)le32(image+i*4,words[i]);
  for(unsigned record=0;record<TRACK_RING;record++){
   unsigned start=64+record*24,before=words[start];
   __asm__ volatile("dsb sy":::"memory");
   for(unsigned i=0;i<24;i++)le32(image+(start+i)*4,words[start+i]);
   __asm__ volatile("dsb sy":::"memory");
   if(!before||words[start]!=before)le32(image+start*4,0);
  }
  __asm__ volatile("dsb sy":::"memory");if(s->sequence!=seq){DBG[8]++;return;}
  width=TRACK_BYTES;height=1;format=3;total=TRACK_BYTES;
 }else if(type==4){
  volatile unsigned *shared=(void *)0x03004000;unsigned seq=shared[1],slot=shared[7];
  if(slot>1)return;
  volatile unsigned *s=(void *)(0x03004020+slot*57600);
  for(unsigned i=0;i<14400;i++){unsigned v=s[i];le32(image+i*4,v);}
  __asm__ volatile("dsb sy":::"memory");if(shared[1]!=seq){DBG[8]++;return;}
  width=320;height=180;format=2;total=57600;
 }else{
  for(unsigned y=0;y<height;y++){
   if(roi_dma){if(((getreg(roi_dma+0x28)>>24)&31)==roi_slot)return;Xil_DCacheInvalidateRange((UINTPTR)(source+y*stride),width*3);}
   for(unsigned x=0;x<width;x++){
   const unsigned char *q=source+y*step*stride+x*step*3;
   unsigned v=((q[2]&248)<<8)|((q[1]&252)<<3)|(q[0]>>3);
   unsigned i=(y*width+x)*2;image[i]=v;image[i+1]=v>>8;
  }
  }
  if(roi_dma&&((getreg(roi_dma+0x28)>>24)&31)==roi_slot)return;
  total=width*height*2;
 }
 offset=0;frame_id++;stamp=getreg(0xf8f00200);busy=1;DBG[5]++;
}
void network_view_init(void){
 for(unsigned i=0;i<16;i++)DBG[i]=0;DBG[0]=0x4e455431;
 Xil_SetTlbAttributes(0x0f000000,NORM_NONCACHE);
 XEmacPs_Config *cfg=XEmacPs_LookupConfig(XPAR_XEMACPS_0_DEVICE_ID);
 if(!cfg||XEmacPs_CfgInitialize(&emac,cfg,cfg->BaseAddress)!=0){DBG[1]=1;return;}
 XEmacPs_SetMdioDivisor(&emac,MDC_DIV_224);
 for(unsigned a=0;a<32;a++){
  unsigned short id=0,status=0;
  if(XEmacPs_PhyRead(&emac,a,2,&id)==0&&id&&id!=65535){
   XEmacPs_PhyRead(&emac,a,1,&status);DBG[9]=a;DBG[10]=id;DBG[11]=status;break;
  }
 }
 XEmacPs_SetMacAddress(&emac,(void *)mac,1);
 XEmacPs_ClearOptions(&emac,XEMACPS_TX_CHKSUM_ENABLE_OPTION|XEMACPS_RX_CHKSUM_ENABLE_OPTION);
 XEmacPs_SetOperatingSpeed(&emac,1000);
 // PS GEM0 reference clock, matching the existing platform's 1-Gbps divisor.
 putreg(0xf8000008,0xdf0d);unsigned c=getreg(0xf8000140);
 putreg(0xf8000140,(c&~0x03f03f00u)|(8u<<8)|(1u<<20));putreg(0xf8000004,0x767b);
 for(unsigned i=0;i<RXN;i++){rx[i].addr=(0x0f004000+i*2048)|(i==RXN-1?2:0);rx[i].status=0;}
 for(unsigned i=0;i<TXN;i++){tx[i].addr=0x0f020000+i*2048;tx[i].status=0x80000000u|(i==TXN-1?0x40000000u:0);}
 unsigned d=getreg(GEM+XEMACPS_DMACR_OFFSET);
 d=(d&~XEMACPS_DMACR_RXBUF_MASK)|(24u<<16);
 putreg(GEM+XEMACPS_DMACR_OFFSET,d);
 // Start directly in polling mode; no unused interrupt handlers installed.
 putreg(GEM+XEMACPS_IDR_OFFSET,0xffffffffu);
 putreg(GEM+XEMACPS_RXQBASE_OFFSET,0x0f000000);
 putreg(GEM+XEMACPS_TXQBASE_OFFSET,0x0f001000);
 putreg(GEM+XEMACPS_NWCTRL_OFFSET,getreg(GEM+XEMACPS_NWCTRL_OFFSET)|0x1c);
 initialized=1;DBG[1]=2;
}
void network_view_poll(const unsigned char *screen){
 if(!initialized||!screen)return;
 for(unsigned k=0;k<8;k++){
  if(!(rx[ri].addr&1))break;
  __asm__ volatile("dsb sy":::"memory");
  unsigned n=rx[ri].status&0x1fff;
  unsigned char *r=(void *)(0x0f004000+ri*2048);DBG[2]++;
  if(n>=42&&be16(r+12)==0x806&&be16(r+20)==1&&!memcmp(r+38,ip,4))arp_reply(r);
  if(n>=50&&be16(r+12)==0x800&&r[14]==0x45&&r[23]==17&&
     !(be16(r+20)&0x3fff)&&!memcmp(r+30,ip,4)&&be16(r+36)==5005&&
     be16(r+38)>=16&&be16(r+38)<=n-34&&checksum(r+14,20)==0&&
     !memcmp(r+42,"AXS1",4)&&!memcmp(r+26,"\xc0\xa8\x01\x64",4)){
   unsigned type=r[46];
   /* Finite diagnostic controls only: explicitly start/clear stationary baseline.
    * Same token retries are idempotent; no generic memory or hardware writes. */
   if(type==8&&(r[47]==1||r[47]==2)){
    unsigned token=r[48]|((unsigned)r[49]<<8);
    if(token&&token!=FREQ_SHARED->words[30]){
     FREQ_SHARED->words[31]=r[47];__asm__ volatile("dsb sy":::"memory");FREQ_SHARED->words[30]=token;
    }
    type=9;
   }
   if((type>=1&&type<=7)||type==9)snapshot(r,type,screen);
  }
  rx[ri].status=0;__asm__ volatile("dsb sy":::"memory");
  rx[ri].addr&=~1u;ri=(ri+1)%RXN;
 }
 // Bounded transmit budget per foreground poll; no waiting on DMA or host.
 for(unsigned k=0;k<128&&busy;k++){
  unsigned char *p=tx_packet();if(!p)break;
  unsigned n=total-offset;if(n>CHUNK)n=CHUNK;
  memset(p,0,74);memcpy(p,peer_mac,6);memcpy(p+6,mac,6);set16(p+12,0x800);
  p[14]=0x45;set16(p+16,60+n);set16(p+18,frame_id);p[22]=64;p[23]=17;
  memcpy(p+26,ip,4);memcpy(p+30,peer_ip,4);set16(p+24,checksum(p+14,20));
  set16(p+34,5005);set16(p+36,peer_port);set16(p+38,40+n);
  le32(p+42,0x31565841);le32(p+46,frame_id);le32(p+50,width);le32(p+54,height);
  le32(p+58,format);le32(p+62,total);le32(p+66,offset);le32(p+70,stamp);
  memcpy(p+74,image+offset,n);send_packet(74+n);offset+=n;
  if(offset==total){busy=0;DBG[6]++;}
 }
 DBG[7]=getreg(GEM+XEMACPS_TXSR_OFFSET);DBG[12]=getreg(GEM+XEMACPS_RXSR_OFFSET);
}
