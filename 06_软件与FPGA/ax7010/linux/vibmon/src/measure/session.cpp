#include "measure/session.h"

#include <algorithm>
#include <cmath>

#include "util/clock.h"
#include "util/log.h"

namespace vm {

namespace {
const char* kTag = "measure";

// One measurement point with the configured tracker ("ncc" or "lk").
class PointTracker {
 public:
  bool init(const GrayFrame& f, const Roi& roi, const MeasureConfig& cfg) {
    lk_ = cfg.tracker == "lk";
    maxStep_ = cfg.maxFinalStepPx;
    return lk_ ? lkt_.init(f, roi, cfg.searchRadius) : ncc_.init(f, roi, cfg.searchRadius);
  }
  CaptureSample track(const GrayFrame& f) {
    CaptureSample s;
    s.tNs = f.timestampNs;
    s.seq = f.sequence;
    s.timestampValid = f.timestampValid;
    TrackResult tr;
    if (lk_) {
      const LkTracker::Result r = lkt_.track(f);
      tr = r;
      s.converged = r.lastStep <= maxStep_;
    } else {
      tr = ncc_.track(f);
    }
    s.ok = tr.ok;
    s.dx = tr.dx;
    s.dy = tr.dy;
    s.score = tr.score;
    s.edge = tr.atEdge;
    s.saturated = tr.saturated;
    return s;
  }
  const Roi& roi() const { return lk_ ? lkt_.roi() : ncc_.roi(); }

 private:
  bool lk_ = false;
  double maxStep_ = 0.05;
  NccTracker ncc_;
  LkTracker lkt_;
};

CaptureSample firstSample(const GrayFrame& f) {
  CaptureSample s;
  s.tNs = f.timestampNs;
  s.seq = f.sequence;
  s.timestampValid = f.timestampValid;
  s.ok = true;
  s.score = 1;
  return s;
}
}  // namespace

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
    if (!s.converged) {
      r.unconvergedFrames++;
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
  if (badTiming || r.dropped || r.badFrames || r.unconvergedFrames || r.edgeHits || r.saturatedFrames) {
    r.reason = badTiming ? "bad_timestamps" : (r.dropped ? "dropped_frames" :
               (r.badFrames ? "tracking_lost" : (r.unconvergedFrames ? "not_converged" :
               (r.edgeHits ? "search_edge" : "image_saturated"))));
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
  // Confidence of the dominant line: peak over the median of the searched band.
  std::vector<double> band;
  const double fHi = 0.8 * r.fpsMeasured / 2;
  for (size_t k = 0; k < r.spectrum.freq.size(); ++k)
    if (r.spectrum.freq[k] >= cfg.fMinHz && r.spectrum.freq[k] <= fHi) band.push_back(r.spectrum.amp[k]);
  if (!band.empty()) {
    std::nth_element(band.begin(), band.begin() + band.size() / 2, band.end());
    const double med = band[band.size() / 2];
    r.peakSnr = med > 0 ? r.peakAmpPx / med : 0;
  }
  r.peakClear = r.peakSnr >= cfg.minPeakSnr;
  r.valid = true;
  return r;
}

std::vector<CaptureSample> relativeSamples(const std::vector<CaptureSample>& target,
                                           const std::vector<CaptureSample>& reference) {
  std::vector<CaptureSample> out;
  const size_t n = std::min(target.size(), reference.size());
  out.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    CaptureSample s = target[i];
    const CaptureSample& q = reference[i];
    if (q.seq != s.seq || q.tNs != s.tNs) {  // not the same frame: unusable
      s.ok = false;
    } else {
      s.dx -= q.dx;
      s.dy -= q.dy;
      s.score = std::min(s.score, q.score);
      s.ok = s.ok && q.ok;
      s.edge = s.edge || q.edge;
      s.saturated = s.saturated || q.saturated;
      s.converged = s.converged && q.converged;
      s.timestampValid = s.timestampValid && q.timestampValid;
    }
    out.push_back(s);
  }
  return out;
}

MeasureResult analyzeWithReference(const std::vector<CaptureSample>& target, const std::vector<CaptureSample>* reference,
                                   const MeasureConfig& cfg, double nominalFps) {
  MeasureResult raw = analyzeCapture(target, cfg, nominalFps);
  raw.tracker = cfg.tracker;
  raw.rawPpPx = raw.ppPx;
  raw.rawRmsPx = raw.rmsPx;
  raw.rawDominantHz = raw.dominantHz;
  if (!reference) return raw;
  const MeasureResult ref = analyzeCapture(*reference, cfg, nominalFps);
  MeasureResult r = analyzeCapture(relativeSamples(target, *reference), cfg, nominalFps);
  r.tracker = cfg.tracker;
  r.rawPpPx = raw.ppPx;
  r.rawRmsPx = raw.rmsPx;
  r.rawDominantHz = raw.dominantHz;
  r.reference.used = true;
  r.reference.valid = ref.valid;
  r.reference.reason = ref.reason;
  r.reference.ppPx = ref.ppPx;
  r.reference.rmsPx = ref.rmsPx;
  r.reference.dominantHz = ref.dominantHz;
  r.reference.peakAmpPx = ref.peakAmpPx;
  // A failed reference invalidates the compensated result even if the
  // combined series happens to pass (e.g. both lost on different frames).
  if (r.valid && !ref.valid) {
    r.valid = false;
    r.reason = "reference_invalid";
  } else if (!r.valid) {
    // Report the target's own failure first; otherwise the reference caused it.
    r.reason = !raw.valid ? raw.reason : "reference_" + (ref.reason.empty() ? std::string("mismatch") : ref.reason);
  }
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

void MeasurementEngine::setFrameTap(std::function<void(const GrayFrame&)> tap) {
  std::lock_guard<std::mutex> lock(mutex_);
  tap_ = std::move(tap);
}

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
  PointTracker tracker, refTracker;
  bool refActive = false;
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
        refSamples_.clear();
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
    if (st == State::kPreview) {
      std::function<void(const GrayFrame&)> tap;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        tap = tap_;
      }
      if (tap) tap(frame);
    }

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
      auto refuse = [&](const char* reason) {
        MeasureResult r;
        r.reason = reason;
        state_ = State::kPreview;
        DoneFn done;
        {
          std::lock_guard<std::mutex> lock(mutex_);
          done = done_;
        }
        if (done) done(r);
      };
      Roi roi;
      roi.w = cfg.roiW;
      roi.h = cfg.roiH;
      roi.x = static_cast<int>(cx_ * frame.width) - roi.w / 2;
      roi.y = static_cast<int>(cy_ * frame.height) - roi.h / 2;
      samples_.clear();
      refSamples_.clear();
      if (!tracker.init(frame, roi, cfg)) {
        LOGW(kTag, "ROI has too little texture or lies outside the image");
        refuse("roi_not_trackable");
        continue;
      }
      refActive = cfg.referenceEnabled;
      if (refActive) {
        Roi rr = roi;
        rr.x = static_cast<int>(cfg.refCx * frame.width) - rr.w / 2;
        rr.y = static_cast<int>(cfg.refCy * frame.height) - rr.h / 2;
        if (!refTracker.init(frame, rr, cfg)) {
          LOGW(kTag, "reference ROI has too little texture or lies outside the image");
          refuse("reference_not_trackable");
          continue;
        }
        refSamples_.push_back(firstSample(frame));
      }
      if (cfg.fieldSpectrum) {
        field_.reset(frame.width, frame.height);
        field_.add(frame.pixels.data(), frame.width, frame.height);
      }
      captureStartNs = frame.timestampNs;
      samples_.push_back(firstSample(frame));
      state_ = State::kCapturing;
      std::lock_guard<std::mutex> lock(mutex_);
      live_.roi = tracker.roi();
      live_.durationS = cfg.durationS;
      live_.recent.clear();
      continue;
    }
    if (state_ != State::kCapturing) continue;
    const CaptureSample s = tracker.track(frame);
    samples_.push_back(s);
    if (refActive) refSamples_.push_back(refTracker.track(frame));
    if (cfg.fieldSpectrum) field_.add(frame.pixels.data(), frame.width, frame.height);
    const double elapsed = (frame.timestampNs - captureStartNs) * 1e-9;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      live_.elapsedS = elapsed;
      live_.lastDisp = cfg.axis == 'x' ? s.dx : s.dy;
      live_.lastScore = s.score;
      live_.recent.push_back(static_cast<float>(live_.lastDisp));
      if (live_.recent.size() > 600) live_.recent.erase(live_.recent.begin(), live_.recent.begin() + 100);
    }
    if (end || elapsed >= cfg.durationS) {
      state_ = State::kAnalyzing;
      MeasureResult r = analyzeWithReference(samples_, refActive ? &refSamples_ : nullptr, cfg, src_->nominalFps());
      r.roi = tracker.roi();
      if (refActive) r.reference.roi = refTracker.roi();
      r.frameWidth = frame.width;
      r.frameHeight = frame.height;
      if (cfg.fieldSpectrum) {
        // Needs the same uniform sampling as the displacement spectrum.
        if (r.fpsMeasured > 0 && !r.dropped && r.reason != "bad_timestamps" && r.reason != "irregular_sampling")
          r.field = field_.analyze(r.fpsMeasured, cfg.fMinHz, 0);
        else
          r.field.reason = "sampling_invalid";
      }
      samples_.clear();
      refSamples_.clear();
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
