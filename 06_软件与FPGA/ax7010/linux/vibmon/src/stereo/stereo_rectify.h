#pragma once

#include <string>
#include <vector>

#include "measure/frame_source.h"
#include "stereo/stereo_match.h"

namespace vm {

// Stereo calibration in the OpenCV convention (cv::stereoCalibrate +
// cv::stereoRectify), produced on a PC by pc_sim/stereo_calibrate.py from
// checkerboard pairs of the actual AN5642 module.
struct StereoCamera {
  double K[9] = {0};  // intrinsics
  double D[5] = {0};  // k1 k2 p1 p2 k3
  double R[9] = {0};  // rectifying rotation
  double P[12] = {0}; // rectified projection
};
struct StereoCalibration {
  bool valid = false;
  std::string id;
  int width = 0, height = 0;  // calibration image size
  StereoCamera left, right;
  double baselineMm = 0;      // from P_right: -P[3] / P[0]
  double rmsPx = 0;           // reprojection error reported by the calibration
};
bool loadStereoCalibration(const std::string& path, StereoCalibration& c, std::string& err);

// Per-pixel source coordinates for one rectified camera at the working size.
struct RectifyMap {
  int w = 0, h = 0;
  std::vector<float> sx, sy;  // in source-image pixels (after the integer pre-scale)
};

// Maps for a working image of w x h from a source of srcW x srcH, which is first
// box-averaged by the integer factor srcW / w (if > 1). Without a valid
// calibration the map only scales (identity rectification).
RectifyMap buildRectifyMap(const StereoCalibration& c, bool rightCamera, int srcW, int srcH, int w, int h);

// Box pre-scale by `factor` then bilinear remap; out-of-image samples become 0.
StereoImage rectifyImage(const GrayFrame& src, const RectifyMap& m, int factor);

// Rectified focal length (px) at the working width.
double rectifiedFocalPx(const StereoCalibration& c, int workW);

}  // namespace vm
