// Spectrum maths and the tracker + analysis chain on synthetic camera frames.
#include <cmath>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "measure/session.h"
#include "measure/spectrum.h"
#include "measure/tracker.h"
#include "test_util.h"

using namespace vm;

namespace {

void testSpectrumSine() {
  const double fs = 100, f = 7.3, a = 1.7;
  std::vector<double> x;
  for (int i = 0; i < 1000; ++i) x.push_back(0.4 + 0.01 * i + a * std::sin(2 * M_PI * f * i / fs + 0.3));
  Spectrum s = amplitudeSpectrum(x, fs, 0.5, 0);
  CHECK(s.valid);
  CHECK_NEAR(s.peakHz, f, 0.02);
  CHECK_NEAR(s.peakAmp, a, 0.03);
  std::vector<double> d = x;
  removeLinearTrend(d);
  SeriesStats st = seriesStats(d);
  CHECK_NEAR(st.rms, a / std::sqrt(2.0), 0.02);
}

void testResample() {
  std::vector<double> t = {0, 0.1, 0.25, 0.3, 0.5};
  std::vector<double> x = {0, 1, 2.5, 3, 5};  // x = 10 t
  std::vector<double> u = resampleUniform(t, x, 10);
  CHECK(u.size() == 6);
  for (size_t i = 0; i < u.size(); ++i) CHECK_NEAR(u[i], static_cast<double>(i), 1e-9);
}

MeasureResult captureSynthetic(const SyntheticConfig& sc, double seconds, double cx, double cy, MeasureConfig mc) {
  auto src = makeSyntheticSource(sc);
  src->start();
  GrayFrame f;
  src->next(f, 0);
  Roi roi;
  roi.w = roi.h = mc.roiSize;
  roi.x = static_cast<int>(cx * f.width) - roi.w / 2;
  roi.y = static_cast<int>(cy * f.height) - roi.h / 2;
  NccTracker tr;
  CHECK(tr.init(f, roi, mc.searchRadius));
  std::vector<CaptureSample> samples;
  CaptureSample s0;
  s0.tNs = f.timestampNs;
  s0.seq = f.sequence;
  s0.ok = true;
  s0.score = 1;
  samples.push_back(s0);
  const int64_t t0 = f.timestampNs;
  while ((f.timestampNs - t0) * 1e-9 < seconds) {
    src->next(f, 0);
    NccTracker::Result r = tr.track(f);
    CaptureSample s;
    s.tNs = f.timestampNs;
    s.seq = f.sequence;
    s.ok = r.ok;
    s.dx = r.dx;
    s.dy = r.dy;
    s.score = r.score;
    s.edge = r.atEdge;
    samples.push_back(s);
  }
  return analyzeCapture(samples, mc, sc.fps);
}

void testTrackingChain() {
  SyntheticConfig sc;
  sc.realtime = false;
  sc.fps = 60;
  sc.freqHz = 5;
  sc.amplitudePx = 2;
  sc.axis = 'y';
  sc.noise = 2;
  MeasureConfig mc;
  mc.roiSize = 48;
  mc.searchRadius = 6;
  mc.axis = 'y';
  MeasureResult r = captureSynthetic(sc, 8, 0.5, 0.5, mc);
  CHECK(r.valid);
  CHECK_NEAR(r.fpsMeasured, 60, 0.01);
  CHECK_NEAR(r.dominantHz, 5, 0.03);
  CHECK_NEAR(r.peakAmpPx, 2, 0.12);
  CHECK_NEAR(r.ppPx, 4, 0.4);
  CHECK_NEAR(r.rmsPx, 2 / std::sqrt(2.0), 0.1);
  CHECK(r.badFrames == 0);
  std::printf("  moving ROI: %.3f Hz, peak %.3f px, p-p %.3f px, rms %.3f px, min NCC %.3f\n", r.dominantHz,
              r.peakAmpPx, r.ppPx, r.rmsPx, r.minScore);

  // Missing frames must invalidate the metrology result rather than be invented by interpolation.
  sc.dropEvery = 7;
  MeasureResult d = captureSynthetic(sc, 8, 0.5, 0.5, mc);
  CHECK(!d.valid);
  CHECK(d.reason == "dropped_frames");
  CHECK(d.dropped > 50);
  CHECK(!d.t.empty());  // raw accepted displacement remains available for diagnosis

  // A ROI on the static background must stay near zero.
  sc.dropEvery = 0;
  MeasureResult z = captureSynthetic(sc, 4, 0.15, 0.15, mc);
  CHECK(z.ppPx < 0.3);
}

void testQualityRejected() {
  std::vector<CaptureSample> good(64);
  for (size_t i = 0; i < good.size(); ++i) {
    good[i].tNs = 1000000000LL + i * 10000000LL;
    good[i].seq = static_cast<uint32_t>(i);
    good[i].ok = true;
    good[i].score = 1;
    good[i].dy = std::sin(2 * M_PI * 5 * i / 100.0);
  }
  MeasureConfig cfg;
  CHECK(analyzeCapture(good, cfg, 100).valid);
  auto checkReason = [&](std::vector<CaptureSample> v, const char* reason) {
    auto r = analyzeCapture(v, cfg, 100);
    CHECK(!r.valid);
    CHECK(r.reason == reason);
  };
  auto v = good; v[30].tNs = v[29].tNs; checkReason(v, "bad_timestamps");
  v = good; v[30].seq = v[29].seq; checkReason(v, "bad_timestamps");
  v = good; v[30].timestampValid = false; checkReason(v, "bad_timestamps");
  v = good; v[30].score = 0.1; checkReason(v, "tracking_lost");
  v = good; v[30].edge = true; checkReason(v, "search_edge");
  v = good; v[30].saturated = true; checkReason(v, "image_saturated");
  v = good; v[30].tNs += 2000000; checkReason(v, "irregular_sampling");
  v = good; v[30].dy = NAN; checkReason(v, "tracking_lost");
}

class StallableSource : public FrameSource {
 public:
  std::atomic<bool> stall{false};
  StallableSource() { SyntheticConfig c; c.realtime = false; src = makeSyntheticSource(c); }
  bool start() override { return src->start(); }
  void stop() override { src->stop(); }
  bool next(GrayFrame& f, int) override {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    return !stall && src->next(f, 0);
  }
  std::string describe() const override { return "stallable-test"; }
  double nominalFps() const override { return 60; }
 private:
  std::unique_ptr<FrameSource> src;
};

template <typename F> bool waitFor(F f) {
  for (int i = 0; i < 500; ++i) {
    if (f()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return false;
}

void testCameraStallAndCancel() {
  auto* source = new StallableSource;
  MeasurementEngine engine{std::unique_ptr<FrameSource>(source)};
  CHECK(engine.startCamera());
  MeasureConfig cfg; cfg.durationS = 60;
  std::atomic<int> callbacks{0};
  MeasureResult result;
  auto done = [&](const MeasureResult& r) { result = r; ++callbacks; };
  CHECK(engine.beginCapture(cfg, .5, .5, done));
  CHECK(!engine.beginCapture(cfg, .5, .5, done));  // pending starts cannot overwrite each other
  CHECK(waitFor([&]() { return engine.state() == MeasurementEngine::State::kCapturing; }));
  source->stall = true;
  CHECK(waitFor([&]() { return callbacks.load() == 1; }));
  CHECK(!result.valid && result.reason == "camera_timeout");
  CHECK(engine.state() == MeasurementEngine::State::kPreview);
  CHECK(!engine.live().cameraOk);
  CHECK(!engine.beginCapture(cfg, .5, .5, done));
  source->stall = false;
  CHECK(waitFor([&]() { return engine.live().cameraOk; }));
  CHECK(engine.beginCapture(cfg, .5, .5, done));
  CHECK(waitFor([&]() { return engine.state() == MeasurementEngine::State::kCapturing; }));
  source->stall = true;
  engine.cancel();
  CHECK(waitFor([&]() { return engine.state() == MeasurementEngine::State::kPreview; }));
  engine.stopCamera();
  CHECK(callbacks.load() == 1);  // cancel has no completion callback
}

void testLowTextureRejected() {
  GrayFrame f;
  f.width = 200;
  f.height = 200;
  f.pixels.assign(200 * 200, 128);
  NccTracker tr;
  Roi roi;
  roi.x = 80;
  roi.y = 80;
  roi.w = roi.h = 40;
  CHECK(!tr.init(f, roi, 6));
}

}  // namespace

int main() {
  testSpectrumSine();
  testResample();
  testTrackingChain();
  testLowTextureRejected();
  testQualityRejected();
  testCameraStallAndCancel();
  return testSummary("measure");
}
