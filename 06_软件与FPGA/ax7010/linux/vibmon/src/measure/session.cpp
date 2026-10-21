#include "measure/session.h"

#include <algorithm>
#include <cmath>

#include "util/clock.h"
#include "util/log.h"

namespace vm {

namespace {
const char* kTag = "measure";
}

MeasureResult analyzeCapture(const std::vector<CaptureSample>& samples, const MeasureConfig& cfg, double nominalFps) {
  MeasureResult r;
  r.axis = cfg.axis;
  r.mmPerPx = cfg.mmPerPx;
  r.calibrationId = cfg.calibrationId;
  r.fpsNominal = nominalFps;
  r.frames = static_cast<uint32_t>(samples.size());
  if (samples.size() < 32) {
    r.reason = "too_few_frames";
    return r;
  }
  r.startMonoNs = samples.front().tNs;
  std::vector<double> t, x, xo, dts;
  bool badTiming = false;
  for (size_t i = 0; i < samples.size(); ++i) {
    const CaptureSample& s = samples[i];
    if (i > 0) {
      const uint32_t step = s.seq - samples[i - 1].seq;
      if (step == 0 || step >= 100000 || s.tNs <= samples[i - 1].tNs) badTiming = true;
      if (step > 1 && step < 100000) r.dropped += step - 1;
      dts.push_back((s.tNs - samples[i - 1].tNs) * 1e-9);
    }
    if (s.edge) r.edgeHits++;
    if (s.saturated) r.saturatedFrames++;
    if (!s.timestampValid) badTiming = true;
    if (!s.ok || !std::isfinite(s.score) || !std::isfinite(s.dx) || !std::isfinite(s.dy) || s.score < cfg.minScore) {
      r.badFrames++;
      continue;
    }
    r.minScore = std::min(r.minScore, s.score);
    t.push_back((s.tNs - r.startMonoNs) * 1e-9);
    x.push_back(cfg.axis == 'x' ? s.dx : s.dy);
    xo.push_back(cfg.axis == 'x' ? s.dy : s.dx);
  }
  r.durationS = (samples.back().tNs - samples.front().tNs) * 1e-9;
  if (r.durationS <= 0) {
    r.reason = "bad_timestamps";
    return r;
  }
  r.fpsMeasured = (samples.size() - 1 + r.dropped) / r.durationS;
  double mean = 0, sq = 0;
  for (double d : dts) mean += d;
  mean /= std::max<size_t>(1, dts.size());
  for (double d : dts) sq += (d - mean) * (d - mean);
  r.jitterMs = std::sqrt(sq / std::max<size_t>(1, dts.size())) * 1000;
  r.t = t;
  r.disp = x;
  r.dispOther = xo;
  if (badTiming || r.dropped || r.badFrames || r.edgeHits || r.saturatedFrames) {
    r.reason = badTiming ? "bad_timestamps" : (r.dropped ? "dropped_frames" :
               (r.badFrames ? "tracking_lost" : (r.edgeHits ? "search_edge" : "image_saturated")));
    return r;
  }
  // Bound timestamp jitter before correcting the small nonuniform sample intervals.
  if (std::any_of(dts.begin(), dts.end(), [mean](double dt) { return std::abs(dt - mean) > mean * 0.1; })) {
    r.reason = "irregular_sampling";
    return r;
  }
  if (t.size() < 32) {
    r.reason = "too_few_valid_frames";
    return r;
  }
  // Only complete, quality-approved captures reach uniform resampling.
  // Use measured timestamps rather than nominal UI/camera FPS.
  std::vector<double> u = resampleUniform(t, x, r.fpsMeasured);
  std::vector<double> d = u;
  removeLinearTrend(d);
  SeriesStats st = seriesStats(d);
  r.rmsPx = st.rms;
  r.ppPx = st.peakToPeak;
  r.spectrum = amplitudeSpectrum(u, r.fpsMeasured, cfg.fMinHz, 0);
  if (!r.spectrum.valid) {
    r.reason = "no_spectrum";
    return r;
  }
  r.dominantHz = r.spectrum.peakHz;
  r.peakAmpPx = r.spectrum.peakAmp;
  r.valid = true;
  return r;
}

MeasurementEngine::MeasurementEngine(std::unique_ptr<FrameSource> source, int previewWidth)
    : src_(std::move(source)), previewWidth_(previewWidth) {}

MeasurementEngine::~MeasurementEngine() { stopCamera(); }

bool MeasurementEngine::startCamera() {
  if (running_) return true;
  if (!src_ || !src_->start()) {
    std::lock_guard<std::mutex> lock(mutex_);
    live_.cameraOk = false;
    live_.error = "camera_start_failed";
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    live_.cameraOk = true;
    live_.error.clear();
    live_.source = src_->describe();
  }
  running_ = true;
  state_ = State::kPreview;
  thread_ = std::thread(&MeasurementEngine::run, this);
  vision_.start();
  return true;
}

void MeasurementEngine::stopCamera() {
  if (!running_) return;
  running_ = false;
  if (thread_.joinable()) thread_.join();
  vision_.stop();
  src_->stop();
  state_ = State::kStopped;
}

void MeasurementEngine::setVision(const VisionConfig& cfg) { vision_.configure(cfg); }

bool MeasurementEngine::beginCapture(const MeasureConfig& cfg, double cx, double cy, DoneFn done) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!running_ || state_ != State::kPreview || startRequested_ || !live_.cameraOk) return false;
  cfg_ = cfg;
  cx_ = std::max(0.0, std::min(1.0, cx));
  cy_ = std::max(0.0, std::min(1.0, cy));
  done_ = std::move(done);
  startRequested_ = true;
  state_ = State::kCapturing;  // reserve the capture before the camera thread consumes the request
  endRequested_ = cancelRequested_ = false;
  return true;
}

void MeasurementEngine::endCapture() {
  std::lock_guard<std::mutex> lock(mutex_);
  endRequested_ = true;
}

void MeasurementEngine::cancel() {
  std::lock_guard<std::mutex> lock(mutex_);
  cancelRequested_ = true;
}

MeasurementEngine::Live MeasurementEngine::live() {
  std::lock_guard<std::mutex> lock(mutex_);
  Live l = live_;
  l.state = state_;
  l.vision = vision_.live();
  return l;
}

void MeasurementEngine::updatePreview(const GrayFrame& f) {
  const int pw = std::min(previewWidth_, f.width);
  const int ph = std::max(1, f.height * pw / std::max(1, f.width));
  std::vector<uint8_t> p(static_cast<size_t>(pw) * ph);
  for (int y = 0; y < ph; ++y) {
    const int sy = y * f.height / ph;
    for (int x = 0; x < pw; ++x) p[static_cast<size_t>(y) * pw + x] = f.at(x * f.width / pw, sy);
  }
  std::lock_guard<std::mutex> lock(mutex_);
  live_.preview.swap(p);
  live_.previewW = pw;
  live_.previewH = ph;
  live_.frameW = f.width;
  live_.frameH = f.height;
}

void MeasurementEngine::run() {
  GrayFrame frame;
  NccTracker tracker;
  int64_t captureStartNs = 0;
  int64_t fpsWindowStart = monoNs();
  int fpsFrames = 0;
  int failures = 0;
  while (running_) {
    // Cancellation must work even when the camera has stopped delivering frames.
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (cancelRequested_) {
        cancelRequested_ = startRequested_ = endRequested_ = false;
        samples_.clear();
        done_ = nullptr;
        state_ = State::kPreview;
      }
    }
    if (!src_->next(frame, 500)) {
      if (++failures == 10) {
        DoneFn done;
        {
          std::lock_guard<std::mutex> lock(mutex_);
          live_.cameraOk = false;
          live_.error = "no_frames";
          if (state_ == State::kCapturing || startRequested_) {
            done = done_;
            done_ = nullptr;
            startRequested_ = endRequested_ = false;
            samples_.clear();
            state_ = State::kPreview;
          }
        }
        LOGW(kTag, "no frames from %s", src_->describe().c_str());
        MeasureResult result;
        result.reason = "camera_timeout";
        if (done) done(result);
      }
      continue;
    }
    if (failures >= 10) {
      std::lock_guard<std::mutex> lock(mutex_);
      live_.cameraOk = true;
      live_.error.clear();
    }
    failures = 0;
    const int64_t now = monoNs();
    fpsFrames++;
    if (now - fpsWindowStart >= 1000000000LL) {
      std::lock_guard<std::mutex> lock(mutex_);
      live_.fps = fpsFrames * 1e9 / static_cast<double>(now - fpsWindowStart);
      fpsWindowStart = now;
      fpsFrames = 0;
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      live_.frameCount++;
    }
    if (now - lastPreviewNs_ >= 100000000LL) {  // 10 Hz preview
      lastPreviewNs_ = now;
      updatePreview(frame);
    }
    // The software vision chain shares the CPU: it pauses while a capture runs.
    const State st = state_;
    vision_.setPaused(st == State::kCapturing || st == State::kAnalyzing);
    vision_.offer(frame.pixels.data(), frame.width, frame.height, frame.timestampNs);

    bool start = false, end = false, cancel = false;
    MeasureConfig cfg;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      start = startRequested_;
      end = endRequested_;
      cancel = cancelRequested_;
      startRequested_ = endRequested_ = cancelRequested_ = false;
      cfg = cfg_;
    }
    if (cancel) {
      samples_.clear();
      state_ = State::kPreview;
      continue;
    }
    if (start) {
      Roi roi;
      roi.w = roi.h = cfg.roiSize;
      roi.x = static_cast<int>(cx_ * frame.width) - roi.w / 2;
      roi.y = static_cast<int>(cy_ * frame.height) - roi.h / 2;
      samples_.clear();
      if (!tracker.init(frame, roi, cfg.searchRadius)) {
        MeasureResult r;
        r.reason = "roi_not_trackable";
        state_ = State::kPreview;
        LOGW(kTag, "ROI has too little texture or lies outside the image");
        DoneFn done;
        {
          std::lock_guard<std::mutex> lock(mutex_);
          done = done_;
        }
        if (done) done(r);
        continue;
      }
      captureStartNs = frame.timestampNs;
      CaptureSample s;
      s.tNs = frame.timestampNs;
      s.seq = frame.sequence;
      s.timestampValid = frame.timestampValid;
      s.ok = true;
      s.score = 1;
      samples_.push_back(s);
      state_ = State::kCapturing;
      std::lock_guard<std::mutex> lock(mutex_);
      live_.roi = tracker.roi();
      live_.durationS = cfg.durationS;
      live_.recent.clear();
      continue;
    }
    if (state_ != State::kCapturing) continue;
    NccTracker::Result tr = tracker.track(frame);
    CaptureSample s;
    s.tNs = frame.timestampNs;
    s.seq = frame.sequence;
    s.timestampValid = frame.timestampValid;
    s.ok = tr.ok;
    s.dx = tr.dx;
    s.dy = tr.dy;
    s.score = tr.score;
    s.edge = tr.atEdge;
    s.saturated = tr.saturated;
    samples_.push_back(s);
    const double elapsed = (frame.timestampNs - captureStartNs) * 1e-9;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      live_.elapsedS = elapsed;
      live_.lastDisp = cfg.axis == 'x' ? tr.dx : tr.dy;
      live_.lastScore = tr.score;
      live_.recent.push_back(static_cast<float>(live_.lastDisp));
      if (live_.recent.size() > 600) live_.recent.erase(live_.recent.begin(), live_.recent.begin() + 100);
    }
    if (end || elapsed >= cfg.durationS) {
      state_ = State::kAnalyzing;
      MeasureResult r = analyzeCapture(samples_, cfg, src_->nominalFps());
      r.roi = tracker.roi();
      r.frameWidth = frame.width;
      r.frameHeight = frame.height;
      samples_.clear();
      DoneFn done;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        done = done_;
      }
      state_ = State::kPreview;
      if (done) done(r);
    }
  }
}

}  // namespace vm
