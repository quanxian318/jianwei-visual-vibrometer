#include "stereo/stereo_service.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "util/clock.h"
#include "util/log.h"

namespace vm {

namespace {
const char* kTag = "stereo";
}

StereoService::StereoService(const StereoConfig& cfg, std::unique_ptr<FrameSource> right, PublishFn publish,
                             std::unique_ptr<PlStereo> pl)
    : cfg_(cfg), right_(std::move(right)), pl_(std::move(pl)), publish_(std::move(publish)) {
  if (cfg_.backend == "pl" && !pl_) stats_.plError = "pl_device_missing: bit-exact model on the PS";
  std::string err;
  if (loadStereoCalibration(cfg_.calibrationFile, calib_, err)) {
    stats_.calibrated = true;
    stats_.calibrationId = calib_.id;
    LOGI(kTag, "calibration %s: baseline %.1f mm, rms %.2f px", calib_.id.c_str(), calib_.baselineMm, calib_.rmsPx);
  } else {
    stats_.error = "no_calibration: " + err;
    LOGW(kTag, "no stereo calibration (%s): depth is not published", err.c_str());
  }
}

StereoService::~StereoService() { stop(); }

bool StereoService::start() {
  if (running_) return true;
  if (!right_ || !right_->start()) {
    std::lock_guard<std::mutex> lock(mutex_);
    stats_.error = "right_camera_start_failed";
    return false;
  }
  running_ = true;
  workThread_ = std::thread(&StereoService::workLoop, this);
  return true;
}

void StereoService::stop() {
  if (!running_) return;
  running_ = false;
  cv_.notify_all();
  if (workThread_.joinable()) workThread_.join();
  right_->stop();
}

void StereoService::offerLeft(const GrayFrame& f) {
  // While a pair is processed no frame is copied, so the next pair starts
  // from a fresh left frame (its right frame is read when the pair starts).
  if (!running_ || paused_ || busy_) return;
  if (cfg_.rateHz > 0 && f.timestampNs - lastOfferNs_ < static_cast<int64_t>(1e9 / cfg_.rateHz)) return;
  lastOfferNs_ = f.timestampNs;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pendingLeft_ = f;
    havePending_ = true;
    busy_ = true;
  }
  cv_.notify_one();
}

void StereoService::workLoop() {
  const int64_t periodNs = static_cast<int64_t>(1e9 / std::max(1.0, right_->nominalFps()));
  int64_t lastPublishNs = 0;
  while (running_) {
    GrayFrame left, right;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait_for(lock, std::chrono::milliseconds(200), [this] { return havePending_ || !running_; });
      if (!havePending_) {
        if (paused_) lastPublishNs = 0;
        continue;
      }
      left = std::move(pendingLeft_);
      havePending_ = false;
    }
    // Software pairing, no hardware exposure sync: the right frame completed
    // last, or the next one when that is nearer to the left frame's time.
    // Only the right frames of the pairs are converted, at normal priority:
    // a conversion that the busy CPU delays past the store's reuse is lost.
    bool ok = right_->latest(right, 200);
    if (ok && left.timestampNs - right.timestampNs > periodNs / 2) ok = right_->next(right, 200);
    const double skewMs = ok ? std::llabs(right.timestampNs - left.timestampNs) * 1e-6 : 0;
    bool pairOk;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stats_.pairs++;
      if (ok) stats_.lastSkewMs = skewMs;
      pairOk = ok && skewMs <= cfg_.maxSkewMs && left.timestampValid && right.timestampValid;
      if (!pairOk) stats_.skewRejected++;
    }
    if (pairOk) {
      // Depth is display work: it yields the CPU to tracking.
      setThreadNice(10);
      std::shared_ptr<DepthMap> d = process(left, right);
      setThreadNice(0);
      const int64_t publishedNs = monoNs();
      d->fps = lastPublishNs > 0 && publishedNs > lastPublishNs ? 1e9 / (publishedNs - lastPublishNs) : 0;
      lastPublishNs = publishedNs;
      std::lock_guard<std::mutex> lock(mutex_);
      stats_.lastProcMs = d->procMs;
      stats_.lastValid = d->validFraction;
      if (calib_.valid && publish_) {
        stats_.published++;
        publish_(d);
      }
    }
    busy_ = false;
  }
}

StereoCalibration StereoService::calibration() {
  std::lock_guard<std::mutex> lock(calibMutex_);
  return calibChanged_ ? pendingCalib_ : calib_;
}

uint64_t StereoService::calibrationVersion() {
  std::lock_guard<std::mutex> lock(calibMutex_);
  return calibVersion_;
}

bool StereoService::correctOffset(double offsetPx, uint64_t mapVersion, const std::string& tag, std::string& err) {
  std::lock_guard<std::mutex> lock(calibMutex_);
  if (mapVersion != calibVersion_) {
    err = "depth_stale";
    return false;
  }
  StereoCalibration c = calibChanged_ ? pendingCalib_ : calib_;
  if (!c.valid) {
    err = "no_calibration";
    return false;
  }
  c.right.K[2] -= offsetPx;
  const size_t at = c.id.find("-dist");
  c.id = (at == std::string::npos ? c.id : c.id.substr(0, at)) + "-dist" + tag;
  if (!saveRightPrincipalX(cfg_.calibrationFile, c.right.K[2], c.id, err)) return false;
  LOGI(kTag, "right image moved by %.2f px (cx %.2f), calibration %s", offsetPx, c.right.K[2], c.id.c_str());
  pendingCalib_ = c;
  calibChanged_ = true;
  ++calibVersion_;
  return true;
}

std::shared_ptr<DepthMap> StereoService::process(const GrayFrame& left, const GrayFrame& right) {
  const auto t0 = std::chrono::steady_clock::now();
  {
    std::lock_guard<std::mutex> lock(calibMutex_);
    if (calibChanged_) {  // new rectification: maps and the temporal history start over
      calib_ = pendingCalib_;
      activeVersion_ = calibVersion_;
      calibChanged_ = false;
      mapSrcW_ = mapSrcH_ = 0;
      temporal_.reset();
      std::lock_guard<std::mutex> slock(mutex_);
      stats_.calibrationId = calib_.id;
    }
  }
  if (left.width != mapSrcW_ || left.height != mapSrcH_) {
    mapL_ = buildRectifyMap(calib_, false, left.width, left.height, cfg_.workWidth, cfg_.workHeight);
    mapR_ = buildRectifyMap(calib_, true, left.width, left.height, cfg_.workWidth, cfg_.workHeight);
    mapSrcW_ = left.width;
    mapSrcH_ = left.height;
  }
  const int factor = std::max(1, left.width / cfg_.workWidth);
  const StereoImage l = rectifyImage(left, mapL_, factor);
  const StereoImage r = rectifyImage(right, mapR_, factor);
  DisparityMap disp;
  std::string backend = "PS";
  if (cfg_.backend == "pl") {
    // Reuse the same PL core in both vertical directions. Matching remains
    // on the PL; PS only checks the two candidate disparities and filters.
    const SgmParams pl = plCoreParams(cfg_.sgm);
    backend = "PL";
    auto matchRaw = [&](const StereoImage& a, const StereoImage& b) {
      DisparityMap raw;
      bool done = false;
      if (pl_) {
        std::string err;
        if (pl_->match(a, b, raw, err)) {
          done = true;
          bool verify = false;
          {
            std::lock_guard<std::mutex> lock(mutex_);
            stats_.plFrames++;
            stats_.plCycles = pl_->lastCycles();
            verify = cfg_.plVerifyEvery > 0 && stats_.plFrames % static_cast<uint64_t>(cfg_.plVerifyEvery) == 0;
          }
          if (verify) {
            // On-board PS/PL consistency check against the bit-exact model.
            const bool same = plCoreModel(a, b).q4 == raw.q4;
            std::lock_guard<std::mutex> lock(mutex_);
            stats_.plVerified++;
            if (!same) {
              stats_.plMismatchFrames++;
              LOGW(kTag, "PL output differs from the model (frame %llu)", static_cast<unsigned long long>(stats_.plFrames));
            }
          }
        } else {
          std::lock_guard<std::mutex> lock(mutex_);
          stats_.plFailures++;
          stats_.plError = err;
          LOGW(kTag, "PL stereo failed: %s; using the model for this frame", err.c_str());
        }
      }
      if (!done) {
        raw = plCoreModel(a, b);  // == stereo_core.v output
        backend = "PL-model";
      }
      return raw;
    };
    disp = matchRaw(l, r);
    if (cfg_.plVerticalCheck) {
      const DisparityMap reverse = matchRaw(flipStereoRows(l), flipStereoRows(r));
      disp = fuseVerticalPasses(disp, reverse);
    }
    stereoPostProcess(l, r, 4 /* census 9x7 border */, pl, disp);
  } else {
    disp = matchSgm(l, r, cfg_.sgm);
  }
  if (cfg_.temporal) disp = temporal_.apply(disp);

  auto d = std::make_shared<DepthMap>();
  d->w = disp.w;
  d->h = disp.h;
  d->left = l.px;
  d->dispQ4 = disp.q4;
  d->filled = disp.filled;
  d->backend = backend;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stats_.backend = backend;
  }
  d->monoNs = left.timestampNs;
  d->pairSkewMs = std::llabs(left.timestampNs - right.timestampNs) * 1e-6;
  d->calibrationId = calib_.id;
  d->calibrationVersion = activeVersion_;
  d->focalPx = rectifiedFocalPx(calib_, cfg_.workWidth);
  d->baselineMm = calib_.baselineMm;
  d->mm.assign(disp.q4.size(), 0);
  int valid = 0;
  if (calib_.valid) {
    const double k = d->focalPx * d->baselineMm * 16;
    for (size_t i = 0; i < disp.q4.size(); ++i) {
      if (disp.q4[i] <= 0) continue;  // zero disparity: infinitely far, not a depth
      const double z = k / disp.q4[i];
      if (z >= 65535) continue;
      d->mm[i] = static_cast<uint16_t>(z + 0.5);
      valid += disp.measured(i);
    }
  } else {
    valid = disp.valid();
  }
  d->validFraction = disp.q4.empty() ? 0 : static_cast<double>(valid) / disp.q4.size();
  d->procMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  return d;
}

StereoService::Stats StereoService::stats() {
  std::lock_guard<std::mutex> lock(mutex_);
  return stats_;
}

DisparityMap TemporalDisparityFilter::apply(const DisparityMap& in) {
  const size_t n = in.q4.size();
  if (in.w != w_ || in.h != h_ || avg_.size() != n) {
    w_ = in.w;
    h_ = in.h;
    avg_.assign(n, 0);
    count_.assign(n, 0);
    miss_.assign(n, 0);
  }
  DisparityMap out = in;
  for (size_t i = 0; i < n; ++i) {
    if (!in.measured(i)) {
      // Invalid this frame: hold a confirmed value, the first frame as a
      // measurement, then for display only. Filled pixels keep their fill.
      if (count_[i] >= 2 && miss_[i] < kHoldFrames && in.q4[i] < 0) {
        ++miss_[i];
        out.q4[i] = static_cast<int16_t>((avg_[i] + 8) >> 4);
        if (miss_[i] > 1 && out.filled.empty()) out.filled.assign(n, 0);
        if (!out.filled.empty()) out.filled[i] = miss_[i] > 1;
        continue;
      }
      if (in.q4[i] < 0) count_[i] = 0;
      continue;
    }
    const int32_t v = static_cast<int32_t>(in.q4[i]) << 4;  // Q8
    if (count_[i] > 0 && std::abs(v - avg_[i]) <= 256) {
      avg_[i] += (v - avg_[i]) / 4;
      if (count_[i] < 255) ++count_[i];
    } else {
      avg_[i] = v;  // first sample or a jump: restart
      count_[i] = 1;
    }
    miss_[i] = 0;
    if (count_[i] >= 2) out.q4[i] = static_cast<int16_t>((avg_[i] + 8) >> 4);
    else out.q4[i] = kInvalidDisparity;  // not yet confirmed by a second frame
  }
  return out;
}

bool stereoScaleAt(const DepthMap& d, double cx, double cy, double halfW, double halfH, double fxCameraPx,
                   double& mmPerPx, double& zMm, int minValid) {
  if (d.w <= 0 || d.h <= 0 || d.mm.size() != static_cast<size_t>(d.w) * d.h || fxCameraPx <= 0) return false;
  const int x0 = std::max(0, static_cast<int>((cx - halfW) * d.w)), x1 = std::min(d.w - 1, static_cast<int>((cx + halfW) * d.w));
  const int y0 = std::max(0, static_cast<int>((cy - halfH) * d.h)), y1 = std::min(d.h - 1, static_cast<int>((cy + halfH) * d.h));
  std::vector<uint16_t> z;
  for (int y = y0; y <= y1; ++y)
    for (int x = x0; x <= x1; ++x) {
      const size_t i = static_cast<size_t>(y) * d.w + x;
      const uint16_t v = d.mm[i];
      if (v && (d.filled.empty() || !d.filled[i])) z.push_back(v);  // measured pixels only
    }
  if (static_cast<int>(z.size()) < minValid) return false;
  std::nth_element(z.begin(), z.begin() + z.size() / 2, z.end());
  zMm = z[z.size() / 2];
  mmPerPx = zMm / fxCameraPx;
  return true;
}

}  // namespace vm
