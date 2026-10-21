#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vm {

// Depth map of the stereo pipeline (AN5642 left/right cameras). The stereo
// module publishes it through Application::publishDepth(); the UI shows it.
struct DepthMap {
  int w = 0, h = 0;
  std::vector<uint16_t> mm;   // depth per pixel in millimetres, 0 = invalid
  std::vector<uint8_t> left;  // rectified left image of the same size (optional)
  std::string backend;        // "PS" or "PL"
  double fps = 0, procMs = 0;
  int64_t monoNs = 0;         // capture time of the left frame (CLOCK_MONOTONIC)
  // Stereo provenance: depth = focalPx * baselineMm / disparity.
  std::string calibrationId;
  double focalPx = 0, baselineMm = 0;
  double pairSkewMs = 0;      // |t_left - t_right| of the frames used (software pairing)
  double validFraction = 0;
  std::vector<int16_t> dispQ4;  // disparity in 1/16 px, -1 invalid (same size)
  std::vector<uint8_t> filled;  // 1 = hole-filled for display, not measured (may be empty)

  // Measured depth at normalised image coordinates; 0 when invalid, hole-filled or outside.
  uint16_t at(double nx, double ny) const {
    if (w <= 0 || h <= 0 || mm.size() != static_cast<size_t>(w) * h) return 0;
    const int x = static_cast<int>(nx * w), y = static_cast<int>(ny * h);
    if (x < 0 || y < 0 || x >= w || y >= h) return 0;
    const size_t i = static_cast<size_t>(y) * w + x;
    return (!filled.empty() && filled[i]) ? 0 : mm[i];
  }
};

}  // namespace vm
