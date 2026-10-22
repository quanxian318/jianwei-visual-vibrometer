#include <cmath>
#include <cstdio>
#include <random>
#include "measure/session.h"
#include "stereo/stereo_service.h"
using namespace vm;
int main(int argc,char**argv){
  MultiPointCapture c;c.layout.width=640;c.layout.height=480;c.layout.points.push_back({320,240});
  c.u.resize(1);c.v.resize(1);c.ok.resize(1);
  for(int i=0;i<160;i++){
    c.tNs.push_back(1000000000LL+static_cast<int64_t>(i+(i>=150?3:0))*1000000000LL/60);
    c.seq.push_back(i+(i>=150?3:0));c.timestampValid.push_back(1);
    c.u[0].push_back(0);c.v[0].push_back(std::sin(2*3.141592653589793*7*i/60));c.ok[0].push_back(1);
  }
  MultiPointWindow w;w.status=1;w.frequencyHz=7;w.fs=60;w.rmsPx={0.707f};w.gateRatio={5};w.valid={1};w.gate={1};w.passed=1;c.windows.push_back(w);
  auto m=analyzeMultiPoint(c,MeasureConfig(),60);
  std::printf("multipoint_gap: valid=%d dropped=%u spectrum_valid=%d rms=%g reason=%s\n",m.valid,m.dropped,m.spectrum.valid,m.rmsPx,m.reason.c_str());
  std::vector<CaptureSample> ss;std::mt19937 rng(42);std::normal_distribution<double> normal(0,1);
  for(int i=0;i<600;i++){CaptureSample q;q.tNs=1000000000LL+static_cast<int64_t>(i)*1000000000LL/60;q.seq=i;q.ok=true;q.score=1;q.dy=normal(rng);ss.push_back(q);}
  auto n=analyzeCapture(ss,MeasureConfig(),60);
  std::printf("noise_frequency: valid=%d peak_clear=%d peak_snr=%g frequency=%g\n",n.valid,n.peakClear,n.peakSnr,n.dominantHz);
  StereoConfig sc;sc.calibrationFile=argv[1];StereoService svc(sc,nullptr,nullptr);std::string err;
  double before=svc.calibration().right.K[2];bool a=svc.correctOffset(20,"1000",err);double once=svc.calibration().right.K[2];
  bool b=svc.correctOffset(20,"1000",err);double twice=svc.calibration().right.K[2];
  std::printf("repeat_depth_correction: ok=%d/%d cx=%g -> %g -> %g error=%s\n",a,b,before,once,twice,err.c_str());
  return 0;
}
