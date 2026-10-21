#pragma once

#include <vector>

#include "measure/frame_source.h"

namespace vm {

struct Roi {
  int x = 0, y = 0, w = 0, h = 0;
};

// Normalised cross-correlation template tracker with parabolic sub-pixel
// refinement. The template is taken once from the reference frame and never
// updated, so displacement stays relative to the start (no drift).
class NccTracker {
 public:
  struct Result {
    bool ok = false;
    bool atEdge = false;   // best match on the search border: displacement may be larger
    double dx = 0, dy = 0; // pixels relative to the reference position
    double score = 0;      // NCC at the peak, -1..1
    bool saturated = false;
  };

  bool init(const GrayFrame& ref, const Roi& roi, int searchRadius);
  Result track(const GrayFrame& frame);
  const Roi& roi() const { return roi_; }
  double templateContrast() const { return tStd_; }

 private:
  Roi roi_;
  int radius_ = 8;
  std::vector<float> tmpl_;  // zero-mean template
  float tNorm_ = 0;
  double tStd_ = 0;
  int cx_ = 0, cy_ = 0;      // integer offset of the previous match
  std::vector<float> scores_;
};

}  // namespace vm
