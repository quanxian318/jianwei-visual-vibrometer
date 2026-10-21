/* CPU1 FFT diagnostic, no dependence on libc/libm, no resampling of lost data.
 * Spectrum is relative translation in native pixels, not calibrated vibration.
 */
#include "src/frequency_shared.h"
static float freq_values[4][FREQ_N],freq_hann[FREQ_N],freq_cos[128],freq_sin[128];
static float freq_re[FREQ_N],freq_im[FREQ_N];
static unsigned long long freq_time[FREQ_N],freq_baseline_start;
static u32 freq_seq[FREQ_N],freq_good[FREQ_N],freq_count,freq_updates;
static float freq_abs(float x){return x<0?-x:x;}
static float freq_sine(float x){
 const float pi=3.14159265358979323846f;
 while(x>pi)x-=2*pi;while(x< -pi)x+=2*pi;
 if(x>pi*.5f)x=pi-x;if(x< -pi*.5f)x=-pi-x;
 float q=x*x;
 return x*(1+q*(-1.0f/6+q*(1.0f/120+q*(-1.0f/5040+q*(1.0f/362880+q*(-1.0f/39916800))))));
}
static void freq_transform(void){
 for(u32 i=1,j=0;i<FREQ_N;i++){
  u32 bit=FREQ_N>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;
  if(i<j){float t=freq_re[i];freq_re[i]=freq_re[j];freq_re[j]=t;t=freq_im[i];freq_im[i]=freq_im[j];freq_im[j]=t;}
 }
 for(u32 length=2;length<=FREQ_N;length<<=1){
  u32 half=length>>1,step=FREQ_N/length;
  for(u32 i=0;i<FREQ_N;i+=length)for(u32 j=0;j<half;j++){
   float c=freq_cos[j*step],s=freq_sin[j*step];u32 a=i+j,b=a+half;
   float tr=c*freq_re[b]-s*freq_im[b],ti=c*freq_im[b]+s*freq_re[b];
   freq_re[b]=freq_re[a]-tr;freq_im[b]=freq_im[a]-ti;freq_re[a]+=tr;freq_im[a]+=ti;
  }
 }
}
static u32 freq_quality(u32 ch,u32 end);
static int freq_test(void){
 const float pi=3.14159265358979323846f;
 for(int i=0;i<128;i++){float a=2*pi*i/FREQ_N;freq_cos[i]=freq_sine(a+pi*.5f);freq_sin[i]=-freq_sine(a);}
 for(int i=0;i<FREQ_N;i++)freq_hann[i]=.5f-.5f*freq_sine(2*pi*i/(FREQ_N-1)+pi*.5f);
 const float f[3]={3,5,9};
 for(int k=0;k<3;k++){
  for(int i=0;i<FREQ_N;i++){freq_re[i]=.5f*freq_sine(2*pi*f[k]*i/45)*freq_hann[i];freq_im[i]=0;}
  freq_transform();u32 best=1;float power=-1;
  for(u32 i=1;i<FREQ_BINS;i++){float p=freq_re[i]*freq_re[i]+freq_im[i]*freq_im[i];if(p>power){power=p;best=i;}}
  if(freq_abs(best*45.0f/FREQ_N-f[k])>45.0f/FREQ_N)return k+1;
 }
 for(int i=0;i<FREQ_N;i++)freq_re[i]=freq_im[i]=0;
 freq_transform();for(int i=0;i<FREQ_N;i++)if(freq_re[i]!=0||freq_im[i]!=0)return 4;
 for(u32 n=1;n<=FREQ_N;n++){freq_seq[n%FREQ_N]=n;freq_good[n%FREQ_N]=15;freq_time[n%FREQ_N]=(unsigned long long)n*7400000;}
 if(freq_quality(0,FREQ_N))return 5;
 freq_good[20]=0;if(freq_quality(0,FREQ_N)!=2)return 6;freq_good[20]=15;
 freq_seq[20]=999;if(freq_quality(0,FREQ_N)!=3)return 7;freq_seq[20]=20;
 freq_time[20]+=3700000;if(freq_quality(0,FREQ_N)!=4)return 8;
 for(int i=0;i<FREQ_N;i++)freq_seq[i]=freq_good[i]=freq_time[i]=0;
 return 0;
}
static u32 freq_quality(u32 ch,u32 end){
 if(end<FREQ_N)return 1;
 unsigned long long first=freq_time[(end-FREQ_N+1)%FREQ_N],last=freq_time[end%FREQ_N];
 if(last<=first)return 3;
 float avg=(float)(last-first)/(FREQ_N-1);
 for(u32 k=0;k<FREQ_N;k++){
  u32 n=end-FREQ_N+1+k,slot=n%FREQ_N;
  if(freq_seq[slot]!=n)return 3;
  if(!(freq_good[slot]&(1u<<ch)))return 2;
  if(k){unsigned long long prev=freq_time[(n-1)%FREQ_N];
   if(freq_time[slot]<=prev||freq_abs((float)(freq_time[slot]-prev)-avg)>avg*.1f)return 4;
  }
 }
 return 0;
}
static void freq_command(unsigned long long now){
 volatile FrequencyShared *s=FREQ_SHARED;
 u32 token=s->words[30];__asm__ volatile("dsb sy":::"memory");u32 op=s->words[31];
 if(!token||token==s->words[32])return;
 s->words[3]++;__asm__ volatile("dsb sy":::"memory");
 if(op==1||op==2){
  s->words[14]=op==1?1:0;s->words[15]=0;freq_baseline_start=now;
  for(int ch=0;ch<4;ch++){s->words[16+ch]=0;s->words[40+ch]=0;s->candidate[ch]=0;for(int i=0;i<FREQ_BINS;i++)s->noise[ch][i]=0;}
 }
 s->words[32]=token;__asm__ volatile("dsb sy":::"memory");s->words[3]++;
}
static void freq_ingest(const TrackRecord *r){
 volatile FrequencyShared *s=FREQ_SHARED;u32 slot=r->sequence%FREQ_N;
 unsigned long long now=((unsigned long long)r->ticks_high<<32)|r->ticks_low;
 freq_command(now);
 freq_time[slot]=now;freq_seq[slot]=r->sequence;
 u32 bits=r->flags&r->reference_flags;freq_good[slot]=((bits&1)?3u:0u)|((bits&2)?12u:0u);
 freq_values[0][slot]=r->x0-seedx[0]-(r->refx0-reference_x[0]);
 freq_values[1][slot]=r->y0-seedy[0]-(r->refy0-reference_y[0]);
 freq_values[2][slot]=r->x1-seedx[1]-(r->refx1-reference_x[1]);
 freq_values[3][slot]=r->y1-seedy[1]-(r->refy1-reference_y[1]);
 freq_count++;
 if(freq_count<FREQ_N||r->sequence%64)return;
 u32 start=TICKS;s->words[3]++;__asm__ volatile("dsb sy":::"memory");
 s->words[4]=r->sequence;s->words[5]=0;s->words[21]=r->ticks_low;s->words[22]=r->ticks_high;
 unsigned long long first=freq_time[(r->sequence-FREQ_N+1)%FREQ_N];
 float fs=(float)((double)(FREQ_N-1)*HZ/(double)(now-first));
 for(u32 ch=0;ch<4;ch++){
  s->candidate[ch]=s->threshold[ch]=s->rms[ch]=s->peak_hz[ch]=s->peak_amp[ch]=0;
  for(int i=0;i<FREQ_BINS;i++)s->amplitude[ch][i]=0;
  s->words[6+ch]=freq_quality(ch,r->sequence);s->fs[ch]=fs;
  if(s->words[6+ch])continue;
  s->words[5]|=1u<<ch;
  float mean=0,slope=0,den=0,energy=0,wsum=0;
  for(u32 k=0;k<FREQ_N;k++)mean+=freq_values[ch][(r->sequence-FREQ_N+1+k)%FREQ_N];mean/=FREQ_N;
  for(u32 k=0;k<FREQ_N;k++){float x=(float)k-127.5f;den+=x*x;slope+=x*(freq_values[ch][(r->sequence-FREQ_N+1+k)%FREQ_N]-mean);}slope/=den;
  for(u32 k=0;k<FREQ_N;k++){
   float v=freq_values[ch][(r->sequence-FREQ_N+1+k)%FREQ_N]-mean-slope*((float)k-127.5f);
   energy+=v*v;wsum+=freq_hann[k];freq_re[k]=v*freq_hann[k];freq_im[k]=0;
  }
  freq_transform();s->rms[ch]=rootf(energy/FREQ_N);u32 best=0;
  for(u32 i=0;i<FREQ_BINS;i++){
   float amp=rootf(freq_re[i]*freq_re[i]+freq_im[i]*freq_im[i])*2/wsum;
   if(!i||i==FREQ_BINS-1)amp*=.5f;s->amplitude[ch][i]=amp;
   float f=i*fs/FREQ_N;
   if(f>=.5f&&f<=18&&f<=fs*.4f&&(!best||amp>s->amplitude[ch][best]))best=i;
  }
  if(best){
   s->peak_hz[ch]=best*fs/FREQ_N;s->peak_amp[ch]=s->amplitude[ch][best];float limit=.00001f;
   for(u32 i=best-1;i<=best+1&&i<FREQ_BINS;i++)if(s->noise[ch][i]*3>limit)limit=s->noise[ch][i]*3;
   s->threshold[ch]=limit;
   if(s->words[14]==2&&s->peak_amp[ch]>limit)s->candidate[ch]=s->peak_hz[ch];
  }
  /* Baseline includes ONLY full windows recorded after the command. */
  if(s->words[14]==1&&first>=freq_baseline_start){
   /* Reject visibly moving baseline windows; do not hide motion by detrending
    * and then call the remainder a stationary reference. Engineering guard,
    * not a calibrated optical detection limit. */
   if(freq_abs(slope)*(FREQ_N-1)>.5f||s->rms[ch]>.5f)s->words[40+ch]++;
   else{
    for(u32 i=0;i<FREQ_BINS;i++)if(s->amplitude[ch][i]>s->noise[ch][i])s->noise[ch][i]=s->amplitude[ch][i];
    s->words[16+ch]++;
   }
  }
 }
 if(s->words[14]==1){
  s->words[15]=(u32)((now-freq_baseline_start)/(HZ/1000));
  if(s->words[15]>=30000){
   int enough=1;for(int ch=0;ch<4;ch++)if(s->words[16+ch]<10)enough=0;
   if(enough)s->words[14]=2;else if(s->words[15]>=60000)s->words[14]=3;
  }
 }
 s->words[10]=TICKS-start;if(s->words[10]>s->words[11])s->words[11]=s->words[10];s->words[20]=++freq_updates;
 __asm__ volatile("dsb sy":::"memory");s->words[3]++;
}
