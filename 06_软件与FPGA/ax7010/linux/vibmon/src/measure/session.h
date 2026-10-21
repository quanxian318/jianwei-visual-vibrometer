#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "measure/frame_source.h"
#include "measure/spectrum.h"
#include "measure/tracker.h"
#include "measure/vision.h"

namespace vm {

struct MeasureConfig {
  int roiSize = 64;
  int searchRadius = 8;
  double durationS = 10;
  char axis = 'y';          // displacement axis in image coordinates
  double minScore = 0.6;    // NCC below this counts as a lost frame
  double mmPerPx = 0;       // 0 = not calibrated: results stay in pixels
  std::string calibrationId;
  double fMinHz = 0.5;
};

struct CaptureSample {
  int64_t tNs = 0;
  uint32_t seq = 0;
  double dx = 0, dy = 0, score = 0;
  bool timestampValid = true;
  bool ok = false, edge = false, saturated = false;
};

struct MeasureResult {
  bool valid = false;
  std::string reason;  // why the result is not valid
  int64_t startMonoNs = 0;
  double durationS = 0;
  uint32_t frames = 0, dropped = 0, badFrames = 0, edgeHits = 0, saturatedFrames = 0;
  double fpsMeasured = 0, fpsNominal = 0, jitterMs = 0, minScore = 1;
  char axis = 'y';
  double dominantHz = 0;
  double peakAmpPx = 0;     // spectral peak amplitude (0-peak) at dominantHz
  double rmsPx = 0, ppPx = 0;  // time domain, linear trend removed
  double mmPerPx = 0;
  std::string calibrationId;
  Roi roi;
  int frameWidth = 0, frameHeight = 0;
  std::vector<double> t, disp, dispOther;  // seconds, pixels
  Spectrum spectrum;
};

// Pure analysis of a finished capture (unit-tested on synthetic data).
MeasureResult analyzeCapture(const std::vector<CaptureSample>& samples, const MeasureConfig& cfg, double nominalFps);

class MeasurementEngine {
 public:
  enum class State { kStopped, kPreview, kCapturing, kAnalyzing };
  struct Live {
    State state = State::kStopped;
    bool cameraOk = false;
    std::string source;
    std::string error;
    double elapsedS = 0, durationS = 0, fps = 0;
    double lastDisp = 0, lastScore = 0;
    std::vector<float> recent;  // recent displacement samples (pixels)
    int frameW = 0, frameH = 0;
    int previewW = 0, previewH = 0;
    std::vector<uint8_t> preview;
    Roi roi;
    uint64_t frameCount = 0;
    VisionLive vision;
  };
  using DoneFn = std::function<void(const MeasureResult&)>;

  explicit MeasurementEngine(std::unique_ptr<FrameSource> source, int previewWidth = 320);
  ~MeasurementEngine();

  bool startCamera();
  void stopCamera();
  // roi centre in normalised image coordinates (0..1).
  // Display image processing (vision page); runs beside the tracker.
  void setVision(const VisionConfig& cfg);
  bool beginCapture(const MeasureConfig& cfg, double cx, double cy, DoneFn done);
  void endCapture();  // analyse what was captured so far
  void cancel();      // drop the capture without a result
  Live live();
  State state() const { return state_.load(); }

 private:
  void run();
  void updatePreview(const GrayFrame& f);

  std::unique_ptr<FrameSource> src_;
  int previewWidth_;
  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<State> state_{State::kStopped};
  std::mutex mutex_;
  Live live_;
  MeasureConfig cfg_;
  double cx_ = 0.5, cy_ = 0.5;
  DoneFn done_;
  bool startRequested_ = false, endRequested_ = false, cancelRequested_ = false;
  std::vector<CaptureSample> samples_;
  int64_t lastPreviewNs_ = 0;
  VisionPipeline vision_;
};

}  // namespace vm
