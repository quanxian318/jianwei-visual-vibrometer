// Vision page: motion magnification, vibration heat map and stereo depth.
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "gfx/draw.h"
#include "ui/ui_core.h"

namespace vm {

using namespace theme;

namespace {

// Indices 0..63: the rectified left image, dimmed, where there is no depth
// (instead of a field of dark dots); 64..255: depth colours.
struct DepthLut {
  uint32_t v[256];
  DepthLut() {
    const uint32_t* heat = heatLut();
    for (uint32_t i = 0; i < 64; ++i) {
      const uint32_t g = i * 4 * 45 / 100;
      v[i] = (g << 16) | (g << 8) | g;
    }
    for (int i = 64; i < 256; ++i) v[i] = heat[1 + (i - 64) * 254 / 191];
  }
};

const uint32_t* depthLut() {
  static const DepthLut lut;  // thread-safe initialisation
  return lut.v;
}

// 2nd..98th percentile of the valid depths (sub-sampled).
bool autoDepthRange(const DepthMap& m, double& lo, double& hi, int& validPct) {
  std::vector<uint16_t> v;
  v.reserve(m.mm.size() / 4 + 1);
  for (size_t i = 0; i < m.mm.size(); i += 4) {
    if (m.mm[i]) v.push_back(m.mm[i]);
  }
  const size_t sampled = (m.mm.size() + 3) / 4;
  validPct = sampled ? static_cast<int>(100 * v.size() / sampled) : 0;
  if (v.size() < 8) return false;
  const size_t k0 = v.size() * 2 / 100, k1 = v.size() * 98 / 100;
  std::nth_element(v.begin(), v.begin() + k0, v.end());
  lo = v[k0];
  std::nth_element(v.begin(), v.begin() + k1, v.end());
  hi = v[k1];
  if (hi - lo < 20) hi = lo + 20;
  return true;
}

// Near = warm colours, far = cool colours; no depth = the dimmed left image.
void depthIndex(const DepthMap& m, double lo, double hi, std::vector<uint8_t>& out) {
  out.resize(m.mm.size());
  const bool image = m.left.size() == m.mm.size();
  for (size_t i = 0; i < m.mm.size(); ++i) {
    const uint16_t z = m.mm[i];
    if (!z) {
      out[i] = image ? static_cast<uint8_t>(m.left[i] >> 2) : 0;
      continue;
    }
    const double t = std::max(0.0, std::min(1.0, (z - lo) / (hi - lo)));
    out[i] = static_cast<uint8_t>(64 + (1.0 - t) * 191.0 + 0.5);
  }
}

std::string distanceText(double mm) {
  char b[32];
  if (mm >= 1000) {
    snprintf(b, sizeof(b), "%.2f m", mm / 1000);
  } else {
    snprintf(b, sizeof(b), "%.0f mm", mm);
  }
  return b;
}

void colourBar(Canvas& c, const Rect& img, const std::string& lo, const std::string& hi, bool nearHot) {
  const uint32_t* lut = heatLut();
  const FontFace& f = fontFace(14);
  const int bw = 120, wl = textWidth(f, lo), wh = textWidth(f, hi);
  const int total = wl + 8 + bw + 8 + wh + 20;
  const int x0 = img.x + img.w - total - 8, y0 = img.y + img.h - 34;
  fillRoundRect(c, x0, y0, total, 26, 8, 0x05080C, 205);
  drawText(c, f, x0 + 10, y0 + 3, lo, kText);
  const int bx = x0 + 10 + wl + 8;
  for (int i = 0; i < bw; ++i) {
    const int k = i * 255 / (bw - 1);
    c.fillRect(bx + i, y0 + 9, 1, 8, lut[nearHot ? 255 - k : k]);
  }
  drawText(c, f, bx + bw + 8, y0 + 3, hi, kText);
}

void placeholder(Canvas& c, const Rect& t, Icon icon, const std::string& title, const std::string& hint) {
  drawIcon(c, icon, t.x + t.w / 2.f, t.y + t.h / 2.f - 34, 52, kText3);
  drawTextCentered(c, fontFace(18), t.x + t.w / 2, t.y + t.h / 2 + 2, title, kText2);
  if (!hint.empty()) drawTextCentered(c, fontFace(14), t.x + t.w / 2, t.y + t.h / 2 + 32, hint, kText3);
}

void probeMarker(Canvas& c, const Rect& img, double nx, double ny, uint32_t col, const std::string& label) {
  const float px = static_cast<float>(img.x + nx * img.w), py = static_cast<float>(img.y + ny * img.h);
  strokeCircle(c, px, py, 10, 2, col);
  fillCircle(c, px, py, 3, col);
  const FontFace& f = fontFace(14);
  const int tw = textWidth(f, label) + 14;
  const int bx = std::max(img.x, std::min(static_cast<int>(px) + 14, img.x + img.w - tw));
  const int by = std::max(img.y, static_cast<int>(py) - 32);
  fillRoundRect(c, bx, by, tw, 22, 6, 0x05080C, 215);
  drawText(c, f, bx + 7, by + 2, label, col);
}

}  // namespace

void renderVisionPage(Ui& ui, const UiData& d) {
  Canvas& c = ui.c;
  const SystemState& s = d.s;
  const VisionLive& v = d.live.vision;
  const int view = ui.st.visionView;
  const bool haveVision = d.live.cameraOk && v.w > 0 && v.source.size() == static_cast<size_t>(v.w) * v.h;
  const DepthMap* dm = d.depth.get();
  const bool haveDepth = dm && dm->w > 0 && dm->mm.size() == static_cast<size_t>(dm->w) * dm->h;
  char buf[160];
  const int cw = (kCw - 8) / 2, ch = 344;
  const Rect left{kCx, kCy, cw, ch}, right{kCx + cw + 8, kCy, cw, ch};
  ui.card(left, 0x05080C);
  ui.card(right, 0x05080C);

  // Image areas follow the aspect of what is shown.
  int aw = 4, ah = 3;
  if (view == 2 && haveDepth) {
    aw = dm->w;
    ah = dm->h;
  } else if (view != 2 && haveVision) {
    aw = v.w;
    ah = v.h;
  }
  const Rect li = fitAspect(Rect{left.x + 6, left.y + 38, left.w - 12, left.h - 44}, aw, ah);
  const Rect ri = fitAspect(Rect{right.x + 6, right.y + 38, right.w - 12, right.h - 44}, aw, ah);
  const std::string band = fmtNum(s.settings.visionLowHz, 3) + "–" + fmtNum(s.settings.visionHighHz, 3) + " Hz";

  if (view == 2) {
    drawText(c, fontFace(18), left.x + 14, left.y + 8, "左目画面", kText2);
    if (haveDepth) {
      snprintf(buf, sizeof(buf), "双目深度 · 有效 %d%%", static_cast<int>(dm->validFraction * 100 + 0.5));
      drawText(c, fontFace(18), right.x + 14, right.y + 8, buf, kText2);
    } else {
      drawText(c, fontFace(18), right.x + 14, right.y + 8, "双目深度", kText2);
    }
    if (haveDepth && dm->left.size() == dm->mm.size()) {
      blitGray(c, li.x, li.y, li.w, li.h, dm->left.data(), dm->w, dm->h, 0, 0, dm->w, dm->h);
    } else if (d.live.cameraOk && !d.live.preview.empty() && d.live.previewW > 0) {
      blitGray(c, li.x, li.y, li.w, li.h, d.live.preview.data(), d.live.previewW, d.live.previewH, 0, 0,
               d.live.previewW, d.live.previewH);
    } else {
      placeholder(c, li, Icon::kCamera, "无画面", "");
    }
    if (haveDepth) {
      double lo = 0, hi = 0;
      int validPct = 0;
      static const double kRanges[4][2] = {{0, 0}, {200, 1000}, {500, 2000}, {1000, 5000}};
      const bool autoOk = autoDepthRange(*dm, lo, hi, validPct);
      if (ui.st.depthRange > 0 && ui.st.depthRange < 4) {
        lo = kRanges[ui.st.depthRange][0];
        hi = kRanges[ui.st.depthRange][1];
      }
      if (autoOk || ui.st.depthRange > 0) {
        std::vector<uint8_t> idx;
        depthIndex(*dm, lo, hi, idx);
        blitGray(c, ri.x, ri.y, ri.w, ri.h, idx.data(), dm->w, dm->h, 0, 0, dm->w, dm->h, depthLut(), 255, true);
        colourBar(c, ri, "近 " + distanceText(lo), distanceText(hi) + " 远", true);
      } else {
        placeholder(c, ri, Icon::kLayers, "有效深度太少", "检查双目标定、纹理和照明");
      }
      const uint16_t roiZ = dm->at(s.roiCx, s.roiCy);
      probeMarker(c, ri, s.roiCx, s.roiCy, kGreen, roiZ ? "测点 " + distanceText(roiZ) : std::string("测点 无效"));
      if (ui.st.probeX >= 0) {
        const uint16_t z = dm->at(ui.st.probeX, ui.st.probeY);
        probeMarker(c, ri, ui.st.probeX, ui.st.probeY, kAmber, z ? distanceText(z) : std::string("无效"));
      }
      ui.hit(ri, uiAction("depth_probe"));
    } else {
      placeholder(c, ri, Icon::kLayers, "双目深度未接入", "");
    }
  } else {
    drawText(c, fontFace(18), left.x + 14, left.y + 8, "原始画面", kText2);
    if (view == 0) {
      snprintf(buf, sizeof(buf), "运动放大 ×%d · %s", s.settings.visionGain, band.c_str());
    } else {
      snprintf(buf, sizeof(buf), "振动能量 · %s", band.c_str());
    }
    drawText(c, fontFace(18), right.x + 14, right.y + 8, buf, kText2);
    if (haveVision) {
      blitGray(c, li.x, li.y, li.w, li.h, v.source.data(), v.w, v.h, 0, 0, v.w, v.h);
      if (view == 0 && v.magnified.size() == v.source.size()) {
        blitGray(c, ri.x, ri.y, ri.w, ri.h, v.magnified.data(), v.w, v.h, 0, 0, v.w, v.h);
      } else if (view == 1 && v.ew > 0 && v.energy.size() == static_cast<size_t>(v.ew) * v.eh) {
        blitGray(c, ri.x, ri.y, ri.w, ri.h, v.source.data(), v.w, v.h, 0, 0, v.w, v.h);
        blitGray(c, ri.x, ri.y, ri.w, ri.h, v.energy.data(), v.ew, v.eh, 0, 0, v.ew, v.eh, heatLut(), 150);
        colourBar(c, ri, "弱", "强", false);
      }
      if (v.peakX >= 0 && v.peakY >= 0) {
        for (const Rect& t : {li, ri}) {
          const float px = t.x + (v.peakX + 0.5f) * t.w / v.w, py = t.y + (v.peakY + 0.5f) * t.h / v.h;
          strokeCircle(c, px, py, 14, 2, kAmber);
          lineAA(c, px - 22, py, px - 14, py, 2, kAmber);
          lineAA(c, px + 14, py, px + 22, py, 2, kAmber);
        }
      }
      if (v.paused || !v.settled) {
        const char* note = v.paused ? "测量期间暂停" : "滤波器预热中";
        const int w = textWidth(fontFace(18), note) + 24;
        fillRoundRect(c, ri.x + 10, ri.y + 10, w, 32, 16, 0x05080C, 200);
        drawText(c, fontFace(18), ri.x + 22, ri.y + 14, note, kAmber);
      }
      // Tapping a vibrating spot moves the measuring ROI there.
      const bool busy = s.measureState == "capturing" || s.measureState == "analyzing";
      const Enable moveOk(s.measureState == "preview", busy ? "测量中不能移动测点框" : "");
      ui.hit(ri, uiAction("roi"), moveOk);
      ui.hit(li, uiAction("roi"), moveOk);
    } else {
      const std::string why = d.live.cameraOk ? "视觉处理未运行" : "相机未就绪";
      placeholder(c, li, Icon::kCamera, why, "");
      placeholder(c, ri, Icon::kVision, why, "");
    }
  }

  // Controls.
  const Rect cr{kCx, kCy + ch + 8, kCw, kCh - ch - 8};
  ui.card(cr);
  const int lx = cr.x + 16, cx0 = cr.x + 96, r1 = cr.y + 22, r2 = cr.y + 74, r3 = cr.y + 126;
  auto label = [&](int x, int y, const char* t) { drawText(c, fontFace(18), x, y + 6, t, kText2); };
  label(lx, r1, "显示");
  ui.chip(Rect{cx0, r1, 120, 36}, "运动放大", view == 0, true, uiAction("vision_view", 0));
  ui.chip(Rect{cx0 + 128, r1, 120, 36}, "振动热力", view == 1, true, uiAction("vision_view", 1));
  ui.chip(Rect{cx0 + 256, r1, 120, 36}, "双目深度", view == 2, true, uiAction("vision_view", 2));
  if (view == 2) {
    const Enable depthOk(haveDepth, "双目深度未接入");
    label(lx, r2, "范围");
    static const char* kNames[4] = {"自动", "0.2–1 m", "0.5–2 m", "1–5 m"};
    for (int i = 0; i < 4; ++i) {
      ui.chip(Rect{cx0 + i * 118, r2, 110, 36}, kNames[i], ui.st.depthRange == i, depthOk, uiAction("depth_range", i));
    }
    label(lx, r3, "投影");
    JsonValue lz = JsonValue::makeObject();
    lz.set("cmd", "laser");
    lz.set("on", !s.laserOn);
    ui.toggle(Rect{cx0, r3 + 1, 64, 34}, s.laserOn, unless(laserBlock(s)), cmdAction(lz));
    // A target at a measured distance at the probe point (else the measuring
    // box) corrects the depth scale offset of the module.
    label(cx0 + 118, r3, "距离校正");
    static const int kMm[4] = {500, 1000, 2000, 3000};
    static const char* kNamesMm[4] = {"0.5 m", "1 m", "2 m", "3 m"};
    const bool probe = ui.st.probeX >= 0;
    for (int i = 0; i < 4; ++i) {
      JsonValue j = JsonValue::makeObject();
      j.set("cmd", "stereo_distance");
      j.set("distance_mm", kMm[i]);
      j.set("x", probe ? ui.st.probeX : s.roiCx);
      j.set("y", probe ? ui.st.probeY : s.roiCy);
      ui.chip(Rect{cx0 + 214 + i * 90, r3, 82, 36}, kNamesMm[i], false, depthOk, cmdAction(j));
    }
  } else {
    label(lx, r2, "频带");
    static const double kBands[4][2] = {{0.5, 2}, {2, 5}, {5, 10}, {10, 20}};
    for (int i = 0; i < 4; ++i) {
      snprintf(buf, sizeof(buf), "%s–%s Hz", fmtNum(kBands[i][0], 2).c_str(), fmtNum(kBands[i][1], 2).c_str());
      JsonValue j = JsonValue::makeObject();
      j.set("cmd", "set");
      j.set("vision_low_hz", kBands[i][0]);
      j.set("vision_high_hz", kBands[i][1]);
      const bool sel = std::fabs(s.settings.visionLowHz - kBands[i][0]) < 1e-6 &&
                       std::fabs(s.settings.visionHighHz - kBands[i][1]) < 1e-6;
      ui.chip(Rect{cx0 + i * 118, r2, 110, 36}, buf, sel, true, cmdAction(j));
    }
    const bool follow = s.last.have && s.last.valid && s.last.dominantHz > 0;
    JsonValue j = JsonValue::makeObject();
    j.set("cmd", "set");
    if (follow) {
      j.set("vision_low_hz", std::max(0.2, s.last.dominantHz * 0.7));
      j.set("vision_high_hz", s.last.dominantHz * 1.4);
    }
    ui.chip(Rect{cx0 + 4 * 118, r2, 110, 36}, "跟随主频", false, Enable(follow, "需要先有一次有效测量"),
            cmdAction(j));
    label(lx, r3, "放大");
    const Enable gainOk(view == 0, "放大倍数只用于运动放大");
    static const int kGains[4] = {5, 10, 20, 50};
    for (int i = 0; i < 4; ++i) {
      snprintf(buf, sizeof(buf), "×%d", kGains[i]);
      JsonValue g = JsonValue::makeObject();
      g.set("cmd", "set");
      g.set("vision_gain", kGains[i]);
      ui.chip(Rect{cx0 + i * 90, r3, 82, 36}, buf, s.settings.visionGain == kGains[i], gainOk, cmdAction(g));
    }
  }
}

}  // namespace vm
