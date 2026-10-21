#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "measure/depth.h"
#include "measure/frame_source.h"
#include "stereo/stereo_match.h"
#include "stereo/stereo_rectify.h"

namespace vm {

// Per-pixel temporal consistency for a static or slowly changing scene:
// a measured disparity is shown once two consecutive frames agree within
// 1 px; agreeing values are averaged (IIR, weight 1/4), a jump restarts the
// pixel, a missing measurement holds the last value for one frame at most.
// Suppresses frame-to-frame flicker of isolated wrong matches.
class TemporalDisparityFilter {
 public:
  void reset() { avg_.clear(); }
  DisparityMap apply(const DisparityMap& in);

 private:
  int w_ = 0, h_ = 0;
  std::vector<int32_t> avg_;   // Q8 of px
  std::vector<uint8_t> count_, miss_;
};

struct StereoConfig {
  bool enabled = false;
  // "pl": the PL stereo core algorithm (ax7010/pl/stereo, census 9x7 +
  //       2-path SGM + LR check; parameters fixed to the RTL). Until the AXI
  //       DMA wrapper and driver exist it runs as the bit-exact model on the
  //       PS and is reported as "PL-model".
  // "ps": software SGM with the configurable SgmParams (e.g. 4/8 paths).
  std::string backend = "pl";
  bool temporal = true;  // TemporalDisparityFilter on the published map
  std::string calibrationFile = "/etc/vibmon/stereo_calibration.json";
  int workWidth = 320, workHeight = 180;  // matching resolution (as the team's BM path)
  double rateHz = 1;                      // depth updates per second (PS: ~1 s per pair on the A9, estimate)
  double maxSkewMs = 8;                   // reject pairs further apart (no hardware sync)
  SgmParams sgm;                          // defaults: center-symmetric census, 4 paths
  StereoConfig() {
    sgm.census = 9;
    sgm.paths = 4;
  }
};

// Rectified depth from the left camera (frames offered by the measurement
// engine) and the right camera (own FrameSource). Runs on the PS today; the
// matcher is the reference model for a PL port. Publishes only metric depth,
// i.e. with a valid stereo calibration; without one it keeps statistics but
// publishes nothing, so the UI never shows unscaled disparity as millimetres.
class StereoService {
 public:
  using PublishFn = std::function<void(std::shared_ptr<const DepthMap>)>;
  struct Stats {
    bool calibrated = false;
    std::string calibrationId, error;
    uint64_t pairs = 0, skewRejected = 0, published = 0;
    double lastSkewMs = 0, lastProcMs = 0, lastValid = 0;
  };

  StereoService(const StereoConfig& cfg, std::unique_ptr<FrameSource> right, PublishFn publish);
  ~StereoService();
  bool start();
  void stop();
  void setPaused(bool paused) { paused_ = paused; }
  // Called from the camera thread with every left frame; cheap when not due.
  void offerLeft(const GrayFrame& f);
  Stats stats();
  const StereoCalibration& calibration() const { return calib_; }

  // Processes one pair synchronously (used by the thread and by tests).
  std::shared_ptr<DepthMap> process(const GrayFrame& left, const GrayFrame& right);

 private:
  void rightLoop();
  void workLoop();

  StereoConfig cfg_;
  std::unique_ptr<FrameSource> right_;
  PublishFn publish_;
  StereoCalibration calib_;
  RectifyMap mapL_, mapR_;
  TemporalDisparityFilter temporal_;
  int mapSrcW_ = 0, mapSrcH_ = 0;
  std::atomic<bool> running_{false}, paused_{false};
  std::thread rightThread_, workThread_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<GrayFrame> rightFrames_;  // most recent few
  GrayFrame pendingLeft_;
  bool havePending_ = false;
  int64_t lastOfferNs_ = 0;
  Stats stats_;
};

// mm per image pixel at a measurement ROI from stereo depth: the median valid
// depth Z inside the ROI (normalised centre and half sizes, left image) and
// the left camera focal length at the measurement resolution give
// mm/px = Z / fx. Assumes the ROI surface faces the camera (route proposal §7).
bool stereoScaleAt(const DepthMap& d, double cx, double cy, double halfW, double halfH, double fxCameraPx,
                   double& mmPerPx, double& zMm, int minValid = 20);

}  // namespace vm
