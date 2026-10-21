// Multi-point vibration wrapper around the team's core (third_party/axvibration).
#include <unistd.h>

#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "measure/frame_source.h"
#include "measure/multipoint.h"
#include "test_util.h"
#include "vb_runtime.h"

using namespace vm;

namespace {

SyntheticConfig scene(double amplitude) {
  SyntheticConfig c;
  c.width = 640;
  c.height = 480;
  c.fps = 60;
  c.freqHz = 7;
  c.amplitudePx = amplitude;
  c.axis = 'y';
  c.noise = 1;
  c.realtime = false;
  return c;
}

// The moving centre third of the synthetic scene; references bottom left.
MultiPointLayout layout(const MultiPointConfig& mc = MultiPointConfig()) {
  return multiPointLayout(640, 480, Roi{272, 208, 96, 64}, 0.1, 0.9, mc);
}

void testLayout() {
  const MultiPointLayout l = layout();
  CHECK(l.error.empty());
  CHECK(l.points.size() == 6);  // 3 x 2 at 32 px pitch
  CHECK(l.refs.size() == 4);
  CHECK_NEAR(l.points[0][0], 272 + 16, 1e-4);
  CHECK_NEAR(l.points[5][1], 208 + 48, 1e-4);
  MultiPointConfig few;
  few.maxPoints = 4;
  CHECK(layout(few).points.size() == 4);
  CHECK(multiPointLayout(640, 480, Roi{0, 200, 20, 64}, 0.1, 0.9, MultiPointConfig()).error == "roi_outside_frame");
  CHECK(multiPointLayout(640, 480, Roi{40, 400, 64, 64}, 0.1, 0.9, MultiPointConfig()).error == "reference_in_box");
  CHECK(multiPointLayout(640, 480, Roi{272, 208, 96, 64}, 0.01, 0.9, MultiPointConfig()).error ==
        "reference_outside_frame");
  CHECK(multiPointHash(l) != multiPointHash(multiPointLayout(640, 480, Roi{276, 208, 96, 64}, 0.1, 0.9,
                                                             MultiPointConfig())));
}

// Region-limited blur against the team's bridge, which blurs every whole frame.
void testSameAsWholeFrameBlur() {
  const MultiPointLayout l = layout();
  MultiPointTracker t;
  std::string err;
  CHECK(t.start(l, true, "", err));
  VRConfig c;
  memset(&c, 0, sizeof(c));
  c.width = 640;
  c.height = 480;
  c.nroi = static_cast<int>(l.points.size());
  c.nref = static_cast<int>(l.refs.size());
  for (size_t i = 0; i < l.points.size(); ++i) {
    c.xy[i][0] = l.points[i][0];
    c.xy[i][1] = l.points[i][1];
  }
  for (size_t i = 0; i < l.refs.size(); ++i) {
    c.xy[l.points.size() + i][0] = l.refs[i][0];
    c.xy[l.points.size() + i][1] = l.refs[i][1];
  }
  std::unique_ptr<VRSession> s(new VRSession());
  std::unique_ptr<VRBaseline> b(new VRBaseline());
  memset(b.get(), 0, sizeof(*b));
  vr_init(s.get(), &c);
  std::vector<float> gray(640 * 480), blur(640 * 480);
  auto src = makeSyntheticSource(scene(3));  // several pixels: the patches really move
  src->start();
  GrayFrame f;
  int mismatches = 0, windows = 0;
  for (int k = 0; k < 200; ++k) {
    src->next(f, 0);
    const bool analysed = t.push(f);
    for (size_t i = 0; i < gray.size(); ++i) gray[i] = f.pixels[i];
    vb_blur3(gray.data(), 640, 480, blur.data());
    vr_frame(s.get(), blur.data(), f.sequence, f.timestampNs * 1e-9);
    for (int i = 0; i < c.nroi; ++i) {
      if (t.pointU(i) != s->last[i][0] || t.pointV(i) != s->last[i][1] || t.pointValid(i) != (s->last_valid[i] != 0))
        ++mismatches;
    }
    if (s->frames >= VR_N && s->frames % 8 == 0) {
      VRResult r;
      vr_analyze(s.get(), b.get(), 1, &r);
      CHECK(analysed && t.lastWindow().status == r.status);
      ++windows;
    } else {
      CHECK(!analysed);
    }
  }
  CHECK(mismatches == 0);
  CHECK(windows == (200 - 128) / 8 + 1);
  CHECK(t.baselineWindows() == static_cast<int>(b->windows));
}

void testBaselineThenSignal() {
  char dir[] = "/tmp/vm_mp_XXXXXX";
  CHECK(mkdtemp(dir) != nullptr);
  const MultiPointLayout l = layout();
  const std::string path = multiPointBaselinePath(dir, multiPointHash(l), "test");
  std::string err;
  GrayFrame f;
  {
    MultiPointTracker t;
    CHECK(t.start(l, true, "", err));
    auto src = makeSyntheticSource(scene(0));  // motor off: noise only
    src->start();
    for (int k = 0; k < 6 * 60; ++k) {
      src->next(f, 0);
      t.push(f);
    }
    CHECK(t.lastWindow().status == 2);
    CHECK(t.baselineWindows() >= 5);
    CHECK(t.saveBaseline(path));
    CHECK(!t.saveBaseline(path));  // never overwritten
  }
  int windows = 0;
  CHECK(findMultiPointBaseline(dir, l, &windows) == path);
  CHECK(windows >= 5);
  CHECK(findMultiPointBaseline(dir, multiPointLayout(640, 480, Roi{276, 208, 96, 64}, 0.1, 0.9,
                                                     MultiPointConfig())).empty());
  MultiPointTracker none;
  CHECK(!none.start(l, false, std::string(dir) + "/missing.vbb", err) && err == "baseline_required");

  // Motor on: 7 Hz, 1 px on the plate.
  MultiPointTracker t;
  CHECK(t.start(l, false, path, err));
  auto src = makeSyntheticSource(scene(1));
  src->start();
  int signal = 0, analysed = 0;
  for (int k = 0; k < 6 * 60; ++k) {
    src->next(f, 0);
    if (!t.push(f)) continue;
    ++analysed;
    const MultiPointWindow& w = t.lastWindow();
    if (w.status != 1) continue;
    ++signal;
    CHECK_NEAR(w.frequencyHz, 7, 0.25);
    CHECK(w.passed == static_cast<int>(l.points.size()));
    CHECK_NEAR(w.rmsPx[0], 1 / std::sqrt(2.0), 0.15);
  }
  CHECK(analysed == (6 * 60 - 128) / 32 + 1);
  CHECK(signal == analysed);

  // Motor off again: nothing above the baseline.
  MultiPointTracker quiet;
  CHECK(quiet.start(l, false, path, err));
  auto still = makeSyntheticSource(scene(0));
  still->start();
  int passedWindows = 0;
  for (int k = 0; k < 6 * 60; ++k) {
    still->next(f, 0);
    if (quiet.push(f) && quiet.lastWindow().status == 1) ++passedWindows;
  }
  CHECK(passedWindows == 0);
  unlink(path.c_str());
  rmdir(dir);
}

}  // namespace

int main() {
  testLayout();
  testSameAsWholeFrameBlur();
  testBaselineThenSignal();
  return testSummary("multipoint");
}
