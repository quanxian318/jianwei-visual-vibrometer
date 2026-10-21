#include "measure/pl_camera_source.h"

#include <atomic>
#include <cstring>
#include <mutex>
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

#include "hw/phys_window.h"
#include "util/clock.h"
#include "util/log.h"

namespace vm {
namespace {

// RGB888 (byte 0 = R) to luma, (77 R + 150 G + 29 B + 128) >> 8, read straight
// from the uncached frame store: one pass, no intermediate copy.
void rgbToGray(const uint8_t* rgb, uint8_t* gray, size_t n) {
  size_t i = 0;
#if defined(__ARM_NEON)
  const uint8x8_t kr = vdup_n_u8(77), kg = vdup_n_u8(150), kb = vdup_n_u8(29);
  for (; i + 16 <= n; i += 16) {
    const uint8x16x3_t p = vld3q_u8(rgb + 3 * i);
    uint16x8_t lo = vmull_u8(vget_low_u8(p.val[0]), kr);
    lo = vmlal_u8(lo, vget_low_u8(p.val[1]), kg);
    lo = vmlal_u8(lo, vget_low_u8(p.val[2]), kb);
    uint16x8_t hi = vmull_u8(vget_high_u8(p.val[0]), kr);
    hi = vmlal_u8(hi, vget_high_u8(p.val[1]), kg);
    hi = vmlal_u8(hi, vget_high_u8(p.val[2]), kb);
    vst1q_u8(gray + i, vcombine_u8(vrshrn_n_u16(lo, 8), vrshrn_n_u16(hi, 8)));
  }
#endif
  for (; i < n; ++i) {
    const uint8_t* q = rgb + 3 * i;
    gray[i] = static_cast<uint8_t>((77u * q[0] + 150u * q[1] + 29u * q[2] + 128u) >> 8);
  }
}

struct Capture {
  explicit Capture(const PlCameraConfig& c) : cfg(c) {}
  PlCameraConfig cfg;
  std::mutex mutex;
  std::unique_ptr<PlCameraRig> rig;
  std::atomic<bool> active[2] = {{false}, {false}};
  // Store each writer was last seen on and when it moved there, i.e. when the
  // store before it completed. Every polling reader updates both cameras, so
  // a camera read only now and then (the stereo right camera) still gets
  // completion times from the measurement camera's 1 ms polling.
  std::mutex seenMutex;
  int seenStore[2] = {-1, -1};
  int64_t seenNs[2] = {0, 0};

  void observe() {
    const int64_t now = monoNs();
    std::lock_guard<std::mutex> lock(seenMutex);
    for (int c = 0; c < 2; ++c) {
      if (!active[c]) continue;
      const int s = rig->writingStore(c);
      if (s != seenStore[c]) {
        seenStore[c] = s;
        seenNs[c] = now;
      }
    }
  }

  // When the writer of camera c moved to `store`; 0 if that was not seen.
  int64_t movedAt(int c, int store) {
    std::lock_guard<std::mutex> lock(seenMutex);
    return seenStore[c] == store ? seenNs[c] : 0;
  }

  bool start(int cam) {
    std::lock_guard<std::mutex> lock(mutex);
    std::string err;
    if (!rig) {
      if (cfg.frameStride < PlCameraRig::kFrameBytes ||
          !reservedForDevices(cfg.bufferBase, 2ull * PlCameraRig::kStores * cfg.frameStride)) {
        LOGE("plcam", "camera DDR overlaps Linux RAM; prepare_an5642_image.py and reboot first");
        return false;
      }
      rig = PlCameraRig::open(cfg, err);
      if (!rig || !rig->initPair(err)) {
        LOGE("plcam", "%s", err.c_str());
        rig.reset();
        return false;
      }
    }
    if (!rig->startCapture(cam, err)) {
      LOGE("plcam", "camera %d: %s", cam, err.c_str());
      return false;
    }
    {
      std::lock_guard<std::mutex> seen(seenMutex);
      seenStore[cam] = -1;
    }
    active[cam] = true;
    return true;
  }

  void stop(int cam) {
    std::lock_guard<std::mutex> lock(mutex);
    if (rig && active[cam]) rig->stopCapture(cam);
    active[cam] = false;
  }
};

class PlCameraSource : public FrameSource {
 public:
  PlCameraSource(std::shared_ptr<Capture> cap, int cam) : cap_(std::move(cap)), cam_(cam) {}
  ~PlCameraSource() override { stop(); }

  bool start() override {
    if (started_) return true;
    if (!cap_->start(cam_)) return false;
    const auto& cfg = cap_->cfg;
    if (!memory_.open(cap_->rig->storeAddress(cam_, 0), 3u * cfg.frameStride)) {
      cap_->stop(cam_);
      return false;
    }
    lastStore_ = cap_->rig->writingStore(cam_);
    sequence_ = 0;
    started_ = true;
    return true;
  }

  void stop() override {
    if (started_) cap_->stop(cam_);
    started_ = false;
    memory_.close();
  }

  bool next(GrayFrame& out, int timeoutMs) override { return read(out, timeoutMs, false); }
  bool latest(GrayFrame& out, int timeoutMs) override { return read(out, timeoutMs, true); }

  std::string describe() const override { return "AN5642 PL/VDMA camera " + std::to_string(cam_); }
  double nominalFps() const override { return cap_->cfg.fps; }

 private:
  bool read(GrayFrame& out, int timeoutMs, bool latest) {
    if (!started_) return false;
    // A store is rewritten two frame times after it completed.
    const int64_t reuseNs = static_cast<int64_t>(2e9 / cap_->cfg.fps) - 4000000;
    const int64_t deadline = monoMs() + timeoutMs;
    while (monoMs() < deadline) {
      cap_->observe();
      const int store = cap_->rig->writingStore(cam_);
      const int64_t moved = cap_->movedAt(cam_, store);
      if (store < 0 || store >= 3 || (store == lastStore_ && !(latest && moved > 0))) {
        // A partial first frame (SOF/EOL timing error) stops the writer until
        // the error bits are cleared, as the team's start-up loop does.
        if (monoMs() - lastClearMs_ >= 100) {
          if (cap_->rig->errors(cam_)) cap_->rig->clearErrors(cam_);
          lastClearMs_ = monoMs();
        }
        sleepMs(1);
        continue;
      }
      sequence_ += (store - lastStore_ + 3) % 3;
      lastStore_ = store;
      const int done = (store + 2) % 3;
      const int64_t stamp = moved > 0 ? moved : monoNs();
      out.width = PlCameraRig::kWidth;
      out.height = PlCameraRig::kHeight;
      out.pixels.resize(static_cast<size_t>(out.width) * out.height);
      rgbToGray(memory_.bytes() + done * cap_->cfg.frameStride, out.pixels.data(), out.pixels.size());
      // The writer comes back to `done` two frame times after it completed;
      // a read that ended later may mix two frames.
      if (cap_->rig->writingStore(cam_) == done || monoNs() - stamp > reuseNs) continue;
      const uint32_t errors = cap_->rig->errors(cam_);
      if (errors) {
        cap_->rig->clearErrors(cam_);
        LOGW("plcam", "camera %d VDMA errors 0x%x", cam_, errors);
        continue;
      }
      out.sequence = sequence_;
      // When the writer moved on (1 ms polling), like a V4L2 buffer time: a
      // completion time, not the exposure; analysis bounds the jitter.
      out.timestampNs = stamp;
      out.timestampValid = true;
      return true;
    }
    return false;
  }

  std::shared_ptr<Capture> cap_;
  int cam_, lastStore_ = 0;
  int64_t lastClearMs_ = 0;
  uint32_t sequence_ = 0;
  bool started_ = false;
  PhysWindow memory_;
};

}  // namespace

std::array<std::unique_ptr<FrameSource>, 2> makePlCameraSources(const PlCameraConfig& cfg) {
  auto cap = std::make_shared<Capture>(cfg);
  const int left = cfg.leftCamera == 1 ? 1 : 0;
  return {std::unique_ptr<FrameSource>(new PlCameraSource(cap, left)),
          std::unique_ptr<FrameSource>(new PlCameraSource(cap, 1 - left))};
}

}  // namespace vm
