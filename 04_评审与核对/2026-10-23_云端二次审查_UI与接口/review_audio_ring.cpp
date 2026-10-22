
#include <cstdio>
#include "app/acoustic_service.h"
#include "util/clock.h"
using namespace vm;
class FixedSource:public PcmSource {
 bool sent=false;
 public:
 bool start(StartError&) override {return true;}
 void stop() override {}
 bool read(std::vector<Block>& out,std::string&) override {
  if(!sent){Block b;b.pcm.assign(1878125,200); // 60.1 seconds
   b.firstIndex=0;b.anchorIndex=0;b.anchorNs=1000000000LL;b.anchored=true;
   out.push_back(std::move(b));sent=true;}return true;}
 double sampleRate()const override{return 31250;}
 double pdmClockHz()const override{return 2000000;}
 std::string describe()const override{return "test";}
 std::string lr()const override{return "GND";}
 uint32_t clipped()override{return 0;}
};
int main(){AcousticOptions o;AcousticService s(o,std::unique_ptr<PcmSource>(new FixedSource));s.start();
 for(int n=0;n<100&&s.status().samples<1878125;n++)sleepMs(10);
 auto a=s.endCapture(1000000000LL,61000000000LL,0);
 auto b=s.endCapture(2000000000LL,61000000000LL,0);
 std::printf("60s window + 100ms reader lead: available=%d reason=%s\n",a.available,a.reason.c_str());
 std::printf("59s window same reader position: available=%d samples=%zu reason=%s\n",b.available,b.pcm.size(),b.reason.c_str());s.stop();
 return (!a.available && a.reason=="not_buffered" && b.available)?0:1;}
