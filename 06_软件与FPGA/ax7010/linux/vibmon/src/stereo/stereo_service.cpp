#include "stereo/stereo_service.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "util/log.h"

namespace vm {

namespace {
const char* kTag = "stereo";
}

StereoService::StereoService(const StereoConfig& cfg, std::unique_ptr<FrameSource> right, PublishFn publish)
    : cfg_(cfg), right_(std::move(right)), publish_(std::move(publish)) {
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
  rightThread_ = std::thread(&StereoService::rightLoop, this);
  workThread_ = std::thread(&StereoService::workLoop, this);
  return true;
}

void StereoService::stop() {
  if (!running_) return;
  running_ = false;
  cv_.notify_all();
  if (rightThread_.joinable()) rightThread_.join();
  if (workThread_.joinable()) workThread_.join();
  right_->stop();
}

void StereoService::rightLoop() {
  GrayFrame f;
  while (running_) {
    if (!right_->next(f, 200)) continue;
    std::lock_guard<std::mutex> lock(mutex_);
    rightFrames_.push_back(f);
    while (rightFrames_.size() > 4) rightFrames_.pop_front();
  }
}

void StereoService::offerLeft(const GrayFrame& f) {
  if (!running_ || paused_) return;
  if (cfg_.rateHz > 0 && f.timestampNs - lastOfferNs_ < static_cast<int64_t>(1e9 / cfg_.rateHz)) return;
  lastOfferNs_ = f.timestampNs;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pendingLeft_ = f;
    havePending_ = true;
  }
  cv_.notify_one();
}

void StereoService::workLoop() {
  while (running_) {
    GrayFrame left, right;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait_for(lock, std::chrono::milliseconds(200), [this] { return havePending_ || !running_; });
      if (!havePending_) continue;
      left = pendingLeft_;
      havePending_ = false;
      // Software pairing: nearest right frame in time. Not a hardware exposure sync.
      int best = -1;
      int64_t bestDt = 0;
      for (size_t i = 0; i < rightFrames_.size(); ++i) {
        const int64_t dt = std::llabs(rightFrames_[i].timestampNs - left.timestampNs);
        if (best < 0 || dt < bestDt) {
          best = static_cast<int>(i);
          bestDt = dt;
        }
      }
      stats_.pairs++;
      if (best < 0 || bestDt * 1e-6 > cfg_.maxSkewMs || !left.timestampValid || !rightFrames_[best].timestampValid) {
        stats_.skewRejected++;
        if (best >= 0) stats_.lastSkewMs = bestDt * 1e-6;
        continue;
      }
      right = rightFrames_[best];
      stats_.lastSkewMs = bestDt * 1e-6;
    }
    std::shared_ptr<DepthMap> d = process(left, right);
    std::lock_guard<std::mutex> lock(mutex_);
    stats_.lastProcMs = d->procMs;
    stats_.lastValid = d->validFraction;
    if (calib_.valid && publish_) {
      stats_.published++;
      publish_(d);
    }
  }
}

std::shared_ptr<DepthMap> StereoService::process(const GrayFrame& left, const GrayFrame& right) {
  const auto t0 = std::chrono::steady_clock::now();
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
    // PL stereo core: census and raw matching with the RTL's fixed parameters,
    // then the PS post-processing with the configured filters.
    SgmParams pl = cfg_.sgm;
    pl.maxDisp = 64;
    pl.census = 9;
    pl.paths = 2;
    pl.p1 = 7;
    pl.p2 = 86;
    pl.adaptiveP2 = true;
    pl.uniquenessPct = 5;
    pl.lrCheck = true;
    pl.lrTolerance = 1;
    int bits = 0, border = 0;
    const auto cl = censusTransform(l, pl.census, bits, border);
    const auto cr = censusTransform(r, pl.census, bits, border);
    disp = sgmRaw(cl, cr, l, bits, border, pl);  // == stereo_core.v output
    stereoPostProcess(l, r, border, pl, disp);
    backend = "PL-model";
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
  d->monoNs = left.timestampNs;
  d->pairSkewMs = std::llabs(left.timestampNs - right.timestampNs) * 1e-6;
  d->calibrationId = calib_.id;
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
      // Filled or invalid this frame: hold a confirmed value once, then drop it.
      if (count_[i] >= 2 && miss_[i] == 0 && in.q4[i] < 0) {
        miss_[i] = 1;
        out.q4[i] = static_cast<int16_t>((avg_[i] + 8) >> 4);
        if (!out.filled.empty()) out.filled[i] = 0;
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
