#include "stereo/stereo_rectify.h"

#include <algorithm>
#include <cmath>

#include "util/json_util.h"

namespace vm {

namespace {

bool readArray(const JsonValue& obj, const char* key, double* out, size_t n, std::string& err) {
  const JsonValue* a = obj.find(key);
  if (!a || !a->isArray() || a->size() != n) {
    err = std::string(key) + ": expected " + std::to_string(n) + " numbers";
    return false;
  }
  for (size_t i = 0; i < n; ++i) {
    if (!a->at(i).isNumber()) {
      err = std::string(key) + ": not a number";
      return false;
    }
    out[i] = a->at(i).asDouble();
  }
  return true;
}

bool readCamera(const JsonValue& root, const char* key, StereoCamera& c, std::string& err) {
  const JsonValue* o = root.find(key);
  if (!o || !o->isObject()) {
    err = std::string(key) + " missing";
    return false;
  }
  return readArray(*o, "K", c.K, 9, err) && readArray(*o, "D", c.D, 5, err) && readArray(*o, "R", c.R, 9, err) &&
         readArray(*o, "P", c.P, 12, err);
}

}  // namespace

bool loadStereoCalibration(const std::string& path, StereoCalibration& c, std::string& err) {
  c = StereoCalibration();
  JsonValue root;
  if (!loadJsonFile(path, root, &err)) return false;
  double size[2];
  if (!readArray(root, "image_size", size, 2, err) || !readCamera(root, "left", c.left, err) ||
      !readCamera(root, "right", c.right, err))
    return false;
  c.width = static_cast<int>(size[0]);
  c.height = static_cast<int>(size[1]);
  c.id = jStr(root, "id", "");
  c.rmsPx = jNum(root, "rms_px", 0);
  if (c.width <= 0 || c.height <= 0 || c.left.P[0] <= 0 || c.right.P[0] <= 0) {
    err = "image_size or rectified focal length not positive";
    return false;
  }
  // Rectified pair: P_right = [f 0 cx -f*B; ...] with B in the unit of the calibration target (mm).
  c.baselineMm = -c.right.P[3] / c.right.P[0];
  if (!(c.baselineMm > 0)) {
    err = "baseline from P_right not positive (left/right swapped?)";
    return false;
  }
  c.valid = true;
  return true;
}

RectifyMap buildRectifyMap(const StereoCalibration& c, bool rightCamera, int srcW, int srcH, int w, int h) {
  RectifyMap m;
  m.w = w;
  m.h = h;
  m.sx.resize(static_cast<size_t>(w) * h);
  m.sy.resize(m.sx.size());
  const int factor = std::max(1, srcW / w);
  const double pw = static_cast<double>(srcW) / factor, ph = static_cast<double>(srcH) / factor;  // pre-scaled source
  for (int v = 0; v < h; ++v)
    for (int u = 0; u < w; ++u) {
      const size_t i = static_cast<size_t>(v) * w + u;
      if (!c.valid) {
        m.sx[i] = static_cast<float>((u + 0.5) * pw / w - 0.5);
        m.sy[i] = static_cast<float>((v + 0.5) * ph / h - 0.5);
        continue;
      }
      const StereoCamera& cam = rightCamera ? c.right : c.left;
      // Working pixel -> calibration-size rectified pixel -> normalised ray.
      const double uc = (u + 0.5) * c.width / w - 0.5, vc = (v + 0.5) * c.height / h - 0.5;
      const double x = (uc - cam.P[2]) / cam.P[0], y = (vc - cam.P[6]) / cam.P[5];
      // Undo the rectifying rotation: X = R^T [x y 1].
      const double X = cam.R[0] * x + cam.R[3] * y + cam.R[6];
      const double Y = cam.R[1] * x + cam.R[4] * y + cam.R[7];
      const double Z = cam.R[2] * x + cam.R[5] * y + cam.R[8];
      const double x0 = X / Z, y0 = Y / Z, r2 = x0 * x0 + y0 * y0;
      const double k = 1 + cam.D[0] * r2 + cam.D[1] * r2 * r2 + cam.D[4] * r2 * r2 * r2;
      const double xd = x0 * k + 2 * cam.D[2] * x0 * y0 + cam.D[3] * (r2 + 2 * x0 * x0);
      const double yd = y0 * k + cam.D[2] * (r2 + 2 * y0 * y0) + 2 * cam.D[3] * x0 * y0;
      const double sxCal = cam.K[0] * xd + cam.K[1] * yd + cam.K[2], syCal = cam.K[4] * yd + cam.K[5];
      // Calibration-size source pixel -> pre-scaled source pixel.
      m.sx[i] = static_cast<float>((sxCal + 0.5) * pw / c.width - 0.5);
      m.sy[i] = static_cast<float>((syCal + 0.5) * ph / c.height - 0.5);
    }
  return m;
}

StereoImage rectifyImage(const GrayFrame& src, const RectifyMap& m, int factor) {
  factor = std::max(1, factor);
  const int pw = src.width / factor, ph = src.height / factor;
  std::vector<uint8_t> pre;
  const uint8_t* base = src.pixels.data();
  if (factor > 1) {
    pre.resize(static_cast<size_t>(pw) * ph);
    const int area = factor * factor;
    for (int y = 0; y < ph; ++y)
      for (int x = 0; x < pw; ++x) {
        int s = 0;
        for (int j = 0; j < factor; ++j) {
          const uint8_t* row = &src.pixels[static_cast<size_t>(y * factor + j) * src.width + x * factor];
          for (int i = 0; i < factor; ++i) s += row[i];
        }
        pre[static_cast<size_t>(y) * pw + x] = static_cast<uint8_t>((s + area / 2) / area);
      }
    base = pre.data();
  }
  StereoImage out;
  out.w = m.w;
  out.h = m.h;
  out.px.assign(static_cast<size_t>(m.w) * m.h, 0);
  for (size_t i = 0; i < out.px.size(); ++i) {
    const float x = m.sx[i], y = m.sy[i];
    const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
    if (x0 < 0 || y0 < 0 || x0 + 1 >= pw || y0 + 1 >= ph) continue;
    const float ax = x - x0, ay = y - y0;
    const uint8_t* r0 = base + static_cast<size_t>(y0) * pw + x0;
    const uint8_t* r1 = r0 + pw;
    const float v = (1 - ay) * ((1 - ax) * r0[0] + ax * r0[1]) + ay * ((1 - ax) * r1[0] + ax * r1[1]);
    out.px[i] = static_cast<uint8_t>(v + 0.5f);
  }
  return out;
}

double rectifiedFocalPx(const StereoCalibration& c, int workW) {
  return c.valid ? c.left.P[0] * workW / c.width : 0;
}

}  // namespace vm
