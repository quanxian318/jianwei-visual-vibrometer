/* Translation-only inverse-compositional tracking, fixed reference patch.
 * Fractional-pixel coordinates are estimates, not calibrated accuracy.
 */
#define ROI 96
#define TW 41
#define HALF 20
#define CENTER 48
typedef struct {
 float reference[TW*TW],gx[TW*TW],gy[TW*TW];
 float hxx,hxy,hyy,sx,sy,mean,variance,u,v,score;
 int ready,lost;
} TranslationTracker;
static float absf(float a){return a<0?-a:a;}
static float rootf(float a){float r;__asm__("vsqrt.f32 %0,%1":"=t"(r):"t"(a));return r;}
static float sample(const float *im,float x,float y){
 int ix=(int)x,iy=(int)y;float fx=x-ix,fy=y-iy;int p=iy*ROI+ix;
 return (im[p]*(1-fx)+im[p+1]*fx)*(1-fy)+(im[p+ROI]*(1-fx)+im[p+ROI+1]*fx)*fy;
}
static int tracker_reference_at(TranslationTracker *t,const float *im,int ox,int oy){
 t->hxx=t->hxy=t->hyy=t->sx=t->sy=t->mean=t->variance=0;
 t->u=t->v=0;t->score=1;t->lost=0;
 for(int y=0;y<TW;y++)for(int x=0;x<TW;x++){
  int p=(CENTER-HALF+y+oy)*ROI+CENTER-HALF+x+ox,i=y*TW+x;
  float gx=(im[p+1]-im[p-1])*.5f,gy=(im[p+ROI]-im[p-ROI])*.5f,v=im[p];
  t->reference[i]=v;t->gx[i]=gx;t->gy[i]=gy;
  t->hxx+=gx*gx;t->hxy+=gx*gy;t->hyy+=gy*gy;t->sx+=gx;t->sy+=gy;t->mean+=v;t->variance+=v*v;
 }
 t->mean/=TW*TW;t->variance-=TW*TW*t->mean*t->mean;
 t->ready=t->variance>TW*TW*16&&t->hxx*t->hyy-t->hxy*t->hxy>.01f*(t->hxx+t->hyy)*(t->hxx+t->hyy);
 t->u=ox;t->v=oy;
 return t->ready;
}
static int tracker_reference(TranslationTracker *t,const float *im){return tracker_reference_at(t,im,0,0);}
static float correlation(TranslationTracker *t,const float *im,float u,float v,int step){
 float a=0,b=0,aa=0,bb=0,ab=0;int n=0;
 for(int y=0;y<TW;y+=step)for(int x=0;x<TW;x+=step){
  float av=t->reference[y*TW+x],bv=sample(im,CENTER-HALF+x+u,CENTER-HALF+y+v);
  a+=av;b+=bv;aa+=av*av;bb+=bv*bv;ab+=av*bv;n++;
 }
 float va=aa-a*a/n,vb=bb-b*b/n;
 if(va<=1||vb<=1)return 0;
 return (ab-a*b/n)/rootf(va*vb);
}
static int tracker_update(TranslationTracker *t,const float *im){
 if(!t->ready)return 0;
 float u=t->u,v=t->v;
 if(t->lost&&t->lost%5==0){
  float best=-1,bu=u,bv=v;
  for(int dy=-20;dy<=20;dy+=4)for(int dx=-20;dx<=20;dx+=4){
   float s=correlation(t,im,dx,dy,4);if(s>best){best=s;bu=dx;bv=dy;}
  }
  if(best>.85f){u=bu;v=bv;}
 }
 for(int iteration=0;iteration<12;iteration++){
  if(absf(u)>24||absf(v)>24)break;
  float sum=0,bx=0,by=0;
  for(int y=0;y<TW;y++)for(int x=0;x<TW;x++){
   int i=y*TW+x;float e=sample(im,CENTER-HALF+x+u,CENTER-HALF+y+v)-t->reference[i];
   sum+=e;bx+=t->gx[i]*e;by+=t->gy[i]*e;
  }
  bx-=sum*t->sx/(TW*TW);by-=sum*t->sy/(TW*TW);
  float determinant=t->hxx*t->hyy-t->hxy*t->hxy;
  float du=(t->hyy*bx-t->hxy*by)/determinant,dv=(t->hxx*by-t->hxy*bx)/determinant;
  if(absf(du)>4||absf(dv)>4)break;
  u-=du;v-=dv;if(du*du+dv*dv<.000004f)break;
 }
 if(absf(u)>24||absf(v)>24){t->score=0;t->lost++;return 0;}
 t->score=correlation(t,im,u,v,1);
 if(t->score<.85f){t->lost++;return 0;}
 t->u=u;t->v=v;t->lost=0;return 1;
}
