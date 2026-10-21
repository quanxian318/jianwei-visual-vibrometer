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
#include "drv/stereo_pl.h"
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
  // "pl": the PL stereo core (ax7010/pl/stereo: stereo_axi + AXI DMA, census
  //       9x7 + 2-path SGM + LR check, parameters fixed in the RTL). If the
  //       UIO device is missing or fails, the bit-exact model runs on the PS
  //       and the map is reported as "PL-model".
  // "ps": software SGM with the configurable SgmParams (e.g. 4/8 paths).
  std::string backend = "pl";
  uint64_t plCoreBase = 0x44070000;  // stereo_0 in add_stereo.tcl
  int plVerifyEvery = 0;             // >0: every N-th PL frame is recomputed on the PS and compared
  bool temporal = true;  // TemporalDisparityFilter on the published map
  std::string calibrationFile = "/etc/vibmon/stereo_calibration.json";
  int workWidth = 320, workHeight = 180;  // matching resolution (as the team's BM path)
  double rateHz = 4;                      // depth updates per second (PL ~39 ms per pair; the PS model takes ~1 s)
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
    std::string backend;            // "PL", "PL-model" or "PS" of the last frame
    std::string plError;            // why the PL device is not used / last PL failure
    uint64_t pairs = 0, skewRejected = 0, published = 0;
    uint64_t plFrames = 0, plFailures = 0, plVerified = 0, plMismatchFrames = 0;
    uint32_t plCycles = 0;          // core clocks of the last PL frame
    double lastSkewMs = 0, lastProcMs = 0, lastValid = 0;
  };

  // pl: opened PL stereo device (nullptr: use the model for backend "pl").
  StereoService(const StereoConfig& cfg, std::unique_ptr<FrameSource> right, PublishFn publish,
                std::unique_ptr<PlStereo> pl = nullptr);
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
  std::unique_ptr<PlStereo> pl_;
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
