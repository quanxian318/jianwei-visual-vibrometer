// Live and result pages.
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "app/report.h"
#include "gfx/draw.h"
#include "ui/ui_core.h"

namespace vm {

using namespace theme;

namespace {

constexpr float kPi = 3.14159265f;

// Normalised image window shown at the current display zoom.
void zoomWindow(const SystemState& s, double& x0, double& y0, double& w, double& h) {
  w = 1.0 / std::max(1.0, s.zoom);
  h = w;
  x0 = std::max(0.0, std::min(1.0 - w, s.zoomCx - w / 2));
  y0 = std::max(0.0, std::min(1.0 - h, s.zoomCy - h / 2));
}

bool measureEnabled(const SystemState& s) {
  const bool capturing = s.measureState == "capturing";
  return !s.shutdownPrepared && s.cameraOk && !s.pfail && s.printerState == "idle" &&
         (s.measureState == "preview" || capturing);
}

// Measuring box b clipped to the image area (zoomed in, it may reach past the
// view): optional light fill, dashed or solid 2 px outline, and drag handles
// at the corners (and edge centres once the box is large enough).
void measureBox(Canvas& c, const Rect& clip, const Rect& b, uint32_t color, bool dashed, int fillAlpha,
                bool handles) {
  auto put = [&](int x, int y, int w, int h, int alpha) {
    const int ax = std::max(x, clip.x), ay = std::max(y, clip.y);
    const int bx = std::min(x + w, clip.x + clip.w), by = std::min(y + h, clip.y + clip.h);
    if (bx > ax && by > ay) fillRoundRect(c, ax, ay, bx - ax, by - ay, 0, color, alpha);
  };
  if (fillAlpha > 0) put(b.x, b.y, b.w, b.h, fillAlpha);
  const int step = dashed ? 12 : std::max(b.w, b.h) + 1, len = dashed ? 7 : step;
  for (int i = 0; i < b.w; i += step) {
    put(b.x + i, b.y, std::min(len, b.w - i), 2, 255);
    put(b.x + i, b.y + b.h - 2, std::min(len, b.w - i), 2, 255);
  }
  for (int j = 0; j < b.h; j += step) {
    put(b.x, b.y + j, 2, std::min(len, b.h - j), 255);
    put(b.x + b.w - 2, b.y + j, 2, std::min(len, b.h - j), 255);
  }
  if (!handles) return;
  // Smaller dots on small boxes so the box itself stays visible.
  const float r = std::max(3.f, std::min(6.f, std::min(b.w, b.h) / 6.f));
  auto handle = [&](int x, int y, float hr) {
    if (!clip.contains(x, y)) return;
    fillCircle(c, static_cast<float>(x), static_cast<float>(y), hr + 2.5f, 0x05080C, 160);
    fillCircle(c, static_cast<float>(x), static_cast<float>(y), hr, color);
  };
  const int x1 = b.x + b.w, y1 = b.y + b.h;
  for (int x : {b.x, x1}) {
    for (int y : {b.y, y1}) handle(x, y, r);
  }
  if (b.w >= 56) {
    handle(b.x + b.w / 2, b.y, 0.75f * r);
    handle(b.x + b.w / 2, y1, 0.75f * r);
  }
  if (b.h >= 56) {
    handle(b.x, b.y + b.h / 2, 0.75f * r);
    handle(x1, b.y + b.h / 2, 0.75f * r);
  }
}

// Multi-point layout on the preview: grid templates (green), reference
// templates (amber), the reference group outline while it is being edited,
// a layout problem, and the chip that switches dragging to the references.
void drawMultiPoint(Ui& ui, const UiData& d, const Rect& img, const UiAction& roi, bool editable) {
  Canvas& c = ui.c;
  const SystemState& s = d.s;
  const MeasurementEngine::Live& live = d.live;
  const int fw = live.frameW, fh = live.frameH;
  std::vector<std::array<float, 2>> pts = live.mpPoints, refs = live.mpRefs;
  const bool dragRef = editable && ui.st.refEdit && ui.st.roiGrab != 0;
  if (editable) {
    const bool dragBox = ui.st.roiGrab != 0 && !ui.st.refEdit;
    const NormBox nb = dragBox ? ui.st.roiDrag : roiBox(s, fw, fh);
    const Roi box{static_cast<int>(std::lround(nb.x0 * fw)), static_cast<int>(std::lround(nb.y0 * fh)),
                  static_cast<int>(std::lround((nb.x1 - nb.x0) * fw)), static_cast<int>(std::lround((nb.y1 - nb.y0) * fh))};
    const double rcx = dragRef ? 0.5 * (ui.st.roiDrag.x0 + ui.st.roiDrag.x1) : s.refCx;
    const double rcy = dragRef ? 0.5 * (ui.st.roiDrag.y0 + ui.st.roiDrag.y1) : s.refCy;
    const MultiPointLayout l = multiPointLayout(fw, fh, box, rcx, rcy, d.multipoint);
    pts = l.points;
    refs = l.refs;
  }
  const auto patch = [&](const std::array<float, 2>& p, uint32_t col) {
    const NormBox pb{(p[0] - 12.5) / fw, (p[1] - 12.5) / fh, (p[0] + 12.5) / fw, (p[1] + 12.5) / fh};
    measureBox(c, img, roiScreenRect(pb, roi, img), col, false, 0, false);
  };
  for (const auto& p : pts) patch(p, kGreen);
  for (const auto& p : refs) patch(p, kAmber);
  if (!editable) return;
  if (ui.st.refEdit) {
    const NormBox rb = dragRef ? ui.st.roiDrag : refGroupBox(s, fw, fh, d.multipoint.refPitchPx);
    measureBox(c, img, roiScreenRect(rb, roi, img), kAmber, !dragRef, dragRef ? 40 : 0, false);
  }
  if (!s.layoutError.empty() && s.layoutError != "camera_not_ready") {
    const std::string text = reasonText(s.layoutError);
    const FontFace& f = fontFace(18);
    const int tw = textWidth(f, text) + 28;
    fillRoundRect(c, img.x + img.w / 2 - tw / 2, img.y + 56, tw, 32, 16, kRed, 220);
    drawTextCentered(c, f, img.x + img.w / 2, img.y + 59, text, 0xFFFFFF);
  }
}

JsonValue setCmd(const char* key, const JsonValue& v) {
  JsonValue j = JsonValue::makeObject();
  j.set("cmd", "set");
  j.set(key, v);
  return j;
}

// Result summary for the result page, from the live state or a saved record.
struct ResultInfo {
  bool have = false, valid = false, saved = false;
  std::string id, reason, unit = "px", timeText, point, calibration;
  std::string method = "roi";
  int points = 0, passed = 0;  // multipoint
  double pointRms = 0;
  double freq = 0, pp = 0, rms = 0, peak = 0, fps = 0, jitterMs = 0, minNcc = 0;
  unsigned long dropped = 0;
  AlarmLevel level = AlarmLevel::kUnknown;
  double warnPp = 0, alarmPp = 0;
};

AlarmLevel levelFromKey(const std::string& k) {
  if (k == "alarm") return AlarmLevel::kAlarm;
  if (k == "warn") return AlarmLevel::kWarn;
  if (k == "normal") return AlarmLevel::kNormal;
  return AlarmLevel::kUnknown;
}

ResultInfo fromRecord(const RecordView& v) {
  ResultInfo r;
  if (!v.loaded) return r;
  const JsonValue& rec = v.rec;
  const JsonValue& res = jObj(rec, "result");
  r.have = true;
  r.saved = true;
  r.id = v.id;
  r.valid = jBool(res, "valid", false);
  r.reason = jStr(res, "reason", "");
  r.unit = jStr(res, "unit", "px");
  const bool mm = r.unit == "mm";
  r.freq = jNum(res, "dominant_frequency_hz", 0);
  r.pp = jNum(res, mm ? "displacement_peak_to_peak_mm" : "displacement_peak_to_peak_px", 0);
  r.rms = jNum(res, mm ? "displacement_rms_mm" : "displacement_rms_px", 0);
  r.peak = jNum(res, mm ? "spectral_peak_mm" : "spectral_peak_px", 0);
  r.level = levelFromKey(jStr(res, "level", "unknown"));
  const JsonValue& th = jObj(res, "thresholds");
  r.warnPp = jNum(th, "warn_peak_to_peak", 0);
  r.alarmPp = jNum(th, "alarm_peak_to_peak", 0);
  r.timeText = jStr(rec, "start_local", "");
  if (r.timeText.empty()) r.timeText = "时间未校准";
  r.point = jStr(rec, "point_id", "");
  const std::string name = jStr(rec, "point_name", "");
  if (!name.empty()) r.point += (r.point.empty() ? "" : " ") + name;
  const JsonValue& cam = jObj(rec, "camera");
  r.fps = jNum(cam, "fps_measured", 0);
  r.dropped = static_cast<unsigned long>(jInt(cam, "frames_dropped", 0));
  r.jitterMs = jNum(cam, "frame_interval_jitter_ms", 0);
  r.minNcc = jNum(jObj(rec, "quality"), "min_ncc", 0);
  r.calibration = mm ? jStr(jObj(rec, "calibration"), "id", "已标定") : std::string("未标定");
  ResultSummary mp;
  readMultiPointSummary(res, mp);
  r.method = mp.method;
  r.points = mp.points;
  r.passed = mp.passedPoints;
  r.pointRms = mp.pointRmsPx;
  return r;
}

ResultInfo fromState(const SystemState& s) {
  ResultInfo r;
  const ResultSummary& l = s.last;
  if (!l.have) return r;
  r.have = true;
  r.valid = l.valid;
  r.saved = l.saved;
  r.id = l.measurementId;
  r.reason = l.reason;
  r.unit = l.unit;
  r.freq = l.dominantHz;
  r.pp = l.peakToPeak;
  r.rms = l.rms;
  r.peak = l.peak;
  r.fps = l.fps;
  r.dropped = l.dropped;
  r.level = l.level;
  r.point = l.pointId;
  r.warnPp = s.settings.warnPp;
  r.alarmPp = s.settings.alarmPp;
  r.calibration = l.unit == "mm" ? s.calibrationId : std::string("未标定");
  r.method = l.method;
  r.points = l.points;
  r.passed = l.passedPoints;
  r.pointRms = l.pointRmsPx;
  return r;
}

}  // namespace

// ---------------------------------------------------------------------------
// Live page
// ---------------------------------------------------------------------------

void renderLivePage(Ui& ui, const UiData& d) {
  Canvas& c = ui.c;
  const SystemState& s = d.s;
  const MeasurementEngine::Live& live = d.live;
  const bool capturing = s.measureState == "capturing";
  const bool analyzing = s.measureState == "analyzing";
  const bool multipoint = s.method == "multipoint";
  char buf[160];

  // Camera preview.
  const Rect pc{kCx, kCy, 600, 348};
  ui.card(pc, 0x05080C);
  const Rect img = fitAspect(Rect{pc.x + 4, pc.y + 4, pc.w - 8, pc.h - 8}, live.frameW, live.frameH);
  double zx, zy, zw, zh;
  zoomWindow(s, zx, zy, zw, zh);
  if (ui.st.panning) {  // the view follows the finger until the pan command is applied
    zx = ui.st.panCx - zw / 2;
    zy = ui.st.panCy - zh / 2;
  }
  if (live.cameraOk && !live.preview.empty() && live.previewW > 0) {
    blitGray(c, img.x, img.y, img.w, img.h, live.preview.data(), live.previewW, live.previewH, zx * live.previewW,
             zy * live.previewH, zw * live.previewW, zh * live.previewH);
    const VisionLive& v = live.vision;
    if (ui.st.liveHeat && v.ew > 0 && v.energy.size() == static_cast<size_t>(v.ew) * v.eh) {
      blitGray(c, img.x, img.y, img.w, img.h, v.energy.data(), v.ew, v.eh, zx * v.ew, zy * v.eh, zw * v.ew,
               zh * v.eh, heatLut(), 120);
    }
    // Measuring box: tracked position while capturing; in preview it can be
    // dragged (inside = move, edges and corners = resize) or moved by a tap.
    UiAction roi = uiAction("roi_box");
    roi.nx0 = zx;
    roi.ny0 = zy;
    roi.nw = zw;
    roi.nh = zh;
    const bool editable = s.measureState == "preview";
    if ((capturing || analyzing) && live.frameW > 0 && live.roi.w > 0) {
      const NormBox tracked{static_cast<double>(live.roi.x) / live.frameW,
                            static_cast<double>(live.roi.y) / live.frameH,
                            static_cast<double>(live.roi.x + live.roi.w) / live.frameW,
                            static_cast<double>(live.roi.y + live.roi.h) / live.frameH};
      measureBox(c, img, roiScreenRect(tracked, roi, img), kAmber, false, 0, false);
    } else if (live.frameW > 0) {
      const bool dragging = editable && ui.st.roiGrab != 0 && !ui.st.refEdit;
      const NormBox nb = dragging ? ui.st.roiDrag : roiBox(s, live.frameW, live.frameH);
      const Rect b = roiScreenRect(nb, roi, img);
      const uint32_t col = dragging ? kAccent : kGreen;
      measureBox(c, img, b, col, !dragging, dragging ? 40 : 0, editable && !ui.st.refEdit);
      const float px = b.x + b.w / 2.f, py = b.y + b.h / 2.f;
      if (img.contains(static_cast<int>(px), static_cast<int>(py))) {
        lineAA(c, px - 16, py, px - 5, py, 2, col);
        lineAA(c, px + 5, py, px + 16, py, 2, col);
        lineAA(c, px, py - 16, px, py - 5, 2, col);
        lineAA(c, px, py + 5, px, py + 16, 2, col);
      }
      if (dragging) {
        snprintf(buf, sizeof(buf), "%ld × %ld px", std::lround((nb.x1 - nb.x0) * live.frameW),
                 std::lround((nb.y1 - nb.y0) * live.frameH));
        const FontFace& f = fontFace(18);
        const int tw = textWidth(f, buf) + 24, th = 32;
        int ly = b.y - th - 8;
        if (ly < img.y + 4) ly = b.y + b.h + 8;
        if (ly + th > img.y + img.h - 4) ly = std::max(img.y + 4, b.y + 4);
        const int lx = std::max(img.x + 4, std::min(img.x + img.w - tw - 4, b.x + b.w / 2 - tw / 2));
        fillRoundRect(c, lx, ly, tw, th, 16, 0x05080C, 210);
        drawTextCentered(c, f, lx + tw / 2, ly + 3, buf, kText);
      }
    }
    if (multipoint && live.frameW > 0) drawMultiPoint(ui, d, img, roi, editable);
    ui.hit(img, roi, editable);
    if (!editable && s.zoom > 1.01) {  // the zoomed view can be moved while the box is locked
      UiAction pan = uiAction("pan_view");
      pan.nx0 = zx;
      pan.ny0 = zy;
      pan.nw = zw;
      pan.nh = zh;
      ui.hit(img, pan, true);
    }
    // After the image's own hit region, so a tap on it switches what is dragged.
    if (multipoint && editable && live.frameW > 0) {
      ui.chip(Rect{img.x + img.w - 128, img.y + img.h - 44, 120, 34}, "移动参考点", ui.st.refEdit, true,
              uiAction("ref_edit"));
    }
  } else {
    drawIcon(c, Icon::kCamera, pc.x + pc.w / 2.f, pc.y + pc.h / 2.f - 30, 56, kText3);
    drawTextCentered(c, fontFace(22, true), pc.x + pc.w / 2, pc.y + pc.h / 2 + 10,
                     live.cameraOk ? "等待画面" : "相机未就绪", kText2);
    if (!live.error.empty()) {
      drawTextCentered(c, fontFace(14), pc.x + pc.w / 2, pc.y + pc.h / 2 + 46, live.error, kText3);
    }
  }
  // Overlays: state, zoom controls, frame info.
  {
    uint32_t col = kGreen;
    std::string text = "预览";
    if (capturing) {
      col = kRed;
      snprintf(buf, sizeof(buf), "%s %.1f / %.0f s", s.baselineCapture ? "基线采集" : "采集中", s.measureElapsedS,
               s.measureDurationS);
      text = buf;
    } else if (analyzing) {
      col = kAccent;
      text = "计算中";
    } else if (!s.cameraOk) {
      col = kText3;
      text = "相机未就绪";
    }
    const FontFace& f = fontFace(18);
    const int w = textWidth(f, text) + 38;
    fillRoundRect(c, pc.x + 12, pc.y + 12, w, 32, 16, 0x05080C, 200);
    fillCircle(c, pc.x + 28.f, pc.y + 28.f, 6, col);
    drawText(c, f, pc.x + 40, pc.y + 15, text, kText);
  }
  {
    const int bx = pc.x + pc.w - 12 - 3 * 40 - 70;
    const Rect minus{bx, pc.y + 12, 40, 36}, plus{bx + 110, pc.y + 12, 40, 36}, fit{bx + 156, pc.y + 12, 40, 36};
    JsonValue zin = JsonValue::makeObject(), zout = JsonValue::makeObject();
    zin.set("cmd", "zoom");
    zin.set("steps", 1);
    zout.set("cmd", "zoom");
    zout.set("steps", -1);
    ui.iconButton(minus, Icon::kMinus, s.zoom > 1.01, cmdAction(zout), 0x1A2430);
    fillRoundRect(c, bx + 44, pc.y + 12, 62, 36, 10, 0x05080C, 200);
    snprintf(buf, sizeof(buf), "%.1f×", s.zoom);
    drawTextCentered(c, fontFace(18), bx + 75, pc.y + 17, buf, s.zoom > 1.01 ? kAccent : kText2);
    ui.iconButton(plus, Icon::kPlus, s.zoom < 7.99, cmdAction(zin), 0x1A2430);
    ui.iconButton(fit, Icon::kFit, s.zoom > 1.01, cmdAction("zoom_reset"), 0x1A2430);
  }
  if (live.frameW > 0) {
    snprintf(buf, sizeof(buf), "%d×%d · %.1f fps", live.frameW, live.frameH, live.fps);
    const FontFace& f = fontFace(14);
    const int w = textWidth(f, buf) + 20;
    fillRoundRect(c, pc.x + 12, pc.y + pc.h - 36, w, 24, 12, 0x05080C, 190);
    drawText(c, f, pc.x + 22, pc.y + pc.h - 34, buf, kText2);
  }

  // Live displacement.
  {
    const Rect wr{kCx, kCy + 356, 600, kCh - 356};
    ui.card(wr);
    std::vector<double> xs, ys;
    const double fps = live.fps > 1 ? live.fps : 30;
    const size_t n = live.recent.size();
    double lo = 0, hi = 0;
    for (size_t i = 0; i < n; ++i) {
      xs.push_back((static_cast<double>(i) - (n - 1)) / fps);
      ys.push_back(live.recent[i]);
      lo = i ? std::min(lo, ys.back()) : ys.back();
      hi = i ? std::max(hi, ys.back()) : ys.back();
    }
    ChartOptions o;
    o.title = "实时位移";
    o.unitY = "px";
    o.unitX = "s";
    o.symmetric = true;
    o.line = kGreen;
    if (n > 1) {
      snprintf(buf, sizeof(buf), "最近 %.1f s · 峰峰 %s px", (n - 1) / fps, fmtNum(hi - lo, 3).c_str());
      o.rightNote = buf;
    }
    drawChart(c, wr, xs, ys, o);
  }

  // Measurement control.
  const int rx = kCx + 608, rw = kCw - 608;
  {
    const Rect r{rx, kCy, rw, 228};
    ui.card(r);
    ui.cardTitle(r, "测量控制", Icon::kTimer);
    const bool enabled = measureEnabled(s) && (!multipoint || capturing || s.baselineValid);
    const float cx = r.x + 82.f, cy = r.y + 118.f;
    strokeCircle(c, cx, cy, 66, 6, kNeutral);
    if (capturing && s.measureDurationS > 0) {
      const float frac = static_cast<float>(std::min(1.0, s.measureElapsedS / s.measureDurationS));
      if (frac > 0.002f) strokeArc(c, cx, cy, 66, 6, 0, frac * 2 * kPi, kAccent);
    } else if (analyzing) {
      const float a = (d.nowMs % 1200) / 1200.f * 2 * kPi;
      strokeArc(c, cx, cy, 66, 6, a, a + 1.6f, kAccent);
    }
    const Rect btn{static_cast<int>(cx - 56), static_cast<int>(cy - 56), 112, 112};
    uint32_t fill = enabled ? (capturing ? kRed : kGreen) : kDisabled;
    if (enabled && ui.pressed(btn)) fill = mixColor(fill, 0xFFFFFF, 50);
    fillCircle(c, cx, cy, 56, fill);
    drawIcon(c, capturing ? Icon::kStop : Icon::kPlay, cx + (capturing ? 0 : 4), cy, 46, enabled ? 0xFFFFFF : kText3);
    ui.hit(btn, cmdAction(capturing ? "measure_stop" : "measure_start"), enabled);
    const int tx = r.x + 162;
    drawText(c, fontFace(22, true), tx, r.y + 58,
             capturing ? (s.baselineCapture ? "停止采集" : "停止测量") : "开始测量", enabled ? kText : kText3);
    if (capturing) {
      snprintf(buf, sizeof(buf), "%.1f / %.0f s", s.measureElapsedS, s.measureDurationS);
    } else if (analyzing) {
      snprintf(buf, sizeof(buf), "正在计算");
    } else {
      snprintf(buf, sizeof(buf), "时长 %s s", fmtNum(s.settings.durationS, 3).c_str());
    }
    drawText(c, fontFace(18), tx, r.y + 94, buf, capturing ? kAccent : kText2);
    if (multipoint) {
      // Motor-off baseline first; it belongs to this box and reference group.
      snprintf(buf, sizeof(buf), "多点测振 · 测点框 %d×%d", s.settings.roiW, s.settings.roiH);
      drawText(c, fontFace(14), tx, r.y + 124, buf, kText2);
      if (s.baselineValid) snprintf(buf, sizeof(buf), "停机基线已采集 · %d 窗口", s.baselineWindows);
      drawText(c, fontFace(14), tx, r.y + 146, s.baselineValid ? buf : "需先停机采集基线",
               s.baselineValid ? kGreen : kAmber);
      static const int kDur[2] = {10, 30};
      for (int i = 0; i < 2; ++i) {
        const Rect cr{r.x + 14 + i * 72, r.y + r.h - 46, 66, 34};
        snprintf(buf, sizeof(buf), "%d s", kDur[i]);
        ui.chip(cr, buf, std::fabs(s.settings.durationS - kDur[i]) < 0.01, !capturing && !analyzing,
                cmdAction(setCmd("duration_s", JsonValue(kDur[i]))));
      }
      const bool baseOk = measureEnabled(s) && !capturing && s.layoutError.empty();
      ui.chip(Rect{r.x + 14 + 2 * 72, r.y + r.h - 46, 138, 34}, "采集停机基线", false, baseOk,
              cmdAction("baseline_start"));
    } else {
      snprintf(buf, sizeof(buf), "测点框 %d×%d · %c 向", s.settings.roiW, s.settings.roiH,
               static_cast<char>(std::toupper(static_cast<unsigned char>(s.settings.axis))));
      drawText(c, fontFace(14), tx, r.y + 124, buf, kText2);
      drawText(c, fontFace(14), tx, r.y + 146, s.calibrated ? "已标定 · 毫米结果" : "未标定 · 像素结果",
               s.calibrated ? kGreen : kAmber);
      static const int kDur[4] = {5, 10, 30, 60};
      for (int i = 0; i < 4; ++i) {
        const Rect cr{r.x + 14 + i * 72, r.y + r.h - 46, 66, 34};
        snprintf(buf, sizeof(buf), "%d s", kDur[i]);
        ui.chip(cr, buf, std::fabs(s.settings.durationS - kDur[i]) < 0.01, !capturing && !analyzing,
                cmdAction(setCmd("duration_s", JsonValue(kDur[i]))));
      }
    }
  }

  // Live readouts.
  {
    const Rect r{rx, kCy + 236, rw, 152};
    ui.card(r);
    ui.cardTitle(r, "实时数据", Icon::kGauge);
    const bool track = capturing && live.frameCount > 0;
    if (multipoint) {
      // Latest 128-frame window of the running capture.
      const MultiPointWindow& w = live.mpWindow;
      const bool sig = track && w.status == 1;
      snprintf(buf, sizeof(buf), "%.2f", w.frequencyHz);
      ui.metric(r.x + 16, r.y + 36, "窗口主频", sig ? buf : "--", sig ? "Hz" : "");
      snprintf(buf, sizeof(buf), "%d/%zu", w.passed, live.mpPoints.size());
      ui.metric(r.x + 160, r.y + 36, "超过基线", track && w.status >= 0 ? buf : "--", "",
                sig ? kGreen : kText);
    } else {
      snprintf(buf, sizeof(buf), "%+.3f", live.lastDisp);
      ui.metric(r.x + 16, r.y + 36, "位移", track ? buf : "--", "px");
      snprintf(buf, sizeof(buf), "%.3f", live.lastScore);
      ui.metric(r.x + 160, r.y + 36, "相关度", track ? buf : "--", "",
                track && live.lastScore < 0.6 ? kAmber : kText);
    }
    snprintf(buf, sizeof(buf), "%.1f", live.fps);
    ui.metric(r.x + 16, r.y + 92, "帧率", live.fps > 0 ? buf : "--", "fps");
    if (s.distanceValid) {
      snprintf(buf, sizeof(buf), "%d", s.distanceMm);
      ui.metric(r.x + 160, r.y + 92, "工作距离", buf, "mm");
    } else {
      ui.metric(r.x + 160, r.y + 92, "工作距离", "--", "");
    }
  }

  // Quick actions.
  {
    const int y0 = kCy + 396, bw = (rw - 8) / 2, bh = (kCh - 396 - 8) / 2;
    const bool outOk = s.printerState == "idle" && !s.pfail;
    JsonValue light = JsonValue::makeObject();
    light.set("cmd", "light");
    light.set("on", !s.lightOn);
    ui.button(Rect{rx, y0, bw, bh}, s.lightOn ? "关闭照明" : "打开照明", Icon::kBulb, s.lightOn ? kAmber : kNeutral,
              (s.lightOn || s.lightAllowed) && outOk, cmdAction(light), 18);
    JsonValue laser = JsonValue::makeObject();
    laser.set("cmd", "laser");
    laser.set("on", !s.laserOn);
    const bool laserLocked = s.settings.laserOffDuringMeasure &&
                             (s.measureState == "capturing" || s.measureState == "analyzing");
    ui.button(Rect{rx + bw + 8, y0, bw, bh}, s.laserOn ? "关闭投影" : "十字投影", Icon::kLaser,
              s.laserOn ? kRed : kNeutral, (s.laserOn || (s.lightAllowed && !laserLocked)) && outOk,
              cmdAction(laser), 18);
    const bool muteOk = (s.alarmLevel == AlarmLevel::kWarn || s.alarmLevel == AlarmLevel::kAlarm) && !s.alarmMuted;
    ui.button(Rect{rx, y0 + bh + 8, bw, bh}, "报警静音", Icon::kBellOff, kNeutral, muteOk, cmdAction("mute"), 18);
    const bool heatOk = live.vision.w > 0;
    ui.button(Rect{rx + bw + 8, y0 + bh + 8, bw, bh}, ui.st.liveHeat ? "关闭热力" : "振动热力", Icon::kHeat,
              ui.st.liveHeat ? kViolet : kNeutral, heatOk, uiAction("live_heat"), 18);
  }
}

// ---------------------------------------------------------------------------
// Result page
// ---------------------------------------------------------------------------

void renderResultPage(Ui& ui, const UiData& d) {
  Canvas& c = ui.c;
  const SystemState& s = d.s;
  const bool fromHistory = !ui.st.openRecordId.empty();
  ResultInfo r = fromHistory ? fromRecord(d.record) : fromState(s);
  if (!fromHistory && d.record.loaded && d.record.id == r.id) {
    // The saved record adds the timing and quality details.
    const ResultInfo saved = fromRecord(d.record);
    r.timeText = saved.timeText;
    r.point = saved.point;
    r.jitterMs = saved.jitterMs;
    r.minNcc = saved.minNcc;
    r.calibration = saved.calibration;
  }
  char buf[200];
  if (!r.have) {
    if (fromHistory) {
      ui.emptyState(Rect{kCx, kCy, kCw, kCh}, Icon::kWarn, "记录无法读取", ui.st.openRecordId);
      ui.button(Rect{kCx + kCw / 2 - 90, kCy + kCh - 90, 180, 56}, "返回记录", Icon::kLeft, kNeutral, true,
                uiAction("close_record"));
    } else {
      ui.emptyState(Rect{kCx, kCy, kCw, kCh}, Icon::kChart, "尚无测量结果", "在实时页点击“开始测量”");
      ui.button(Rect{kCx + kCw / 2 - 90, kCy + kCh - 110, 180, 56}, "去测量", Icon::kPlay, kGreen, true,
                uiAction("nav", static_cast<int>(Page::kLive)));
    }
    return;
  }

  // KPI row.
  const int kw = (kCw - 3 * 8) / 4, kh = 104;
  if (r.valid) {
    // Multi-point: points above the motor-off baseline and their median RMS
    // (image-plane pixels) instead of the single box's peak-to-peak and RMS.
    const bool mp = r.method == "multipoint";
    const char* labels[3] = {"主频", mp ? "超过基线测点" : "峰峰值", mp ? "测点 RMS" : "有效值"};
    snprintf(buf, sizeof(buf), "%d/%d", r.passed, r.points);
    const std::string values[3] = {fmtNum(r.freq, 4), mp ? std::string(buf) : fmtNum(r.pp, 3),
                                   fmtNum(mp ? r.pointRms : r.rms, 3)};
    const std::string units[3] = {"Hz", mp ? "" : r.unit, r.unit};
    for (int i = 0; i < 3; ++i) {
      const Rect k{kCx + i * (kw + 8), kCy, kw, kh};
      ui.card(k);
      drawText(c, fontFace(18), k.x + 16, k.y + 10, labels[i], kText2);
      const int vx = drawText(c, fontFace(44, true), k.x + 14, k.y + 38, values[i], kText);
      drawText(c, fontFace(18), vx + 6, k.y + 64, units[i], kText2);
    }
  } else {
    const Rect k{kCx, kCy, 3 * kw + 16, kh};
    ui.card(k);
    fillCircle(c, k.x + 52.f, k.y + kh / 2.f, 30, kAmber, 46);
    drawIcon(c, Icon::kWarn, k.x + 52.f, k.y + kh / 2.f, 34, kAmber);
    drawText(c, fontFace(32, true), k.x + 100, k.y + 14, "结果无效", kAmber);
    drawText(c, fontFace(22), k.x + 100, k.y + 60, reasonText(r.reason), kText);
    if (r.saved) drawTextRight(c, fontFace(14), k.x + k.w - 16, k.y + 76, "原始数据已保存，可用于诊断", kText2);
  }
  {
    const Rect k{kCx + 3 * (kw + 8), kCy, kw, kh};
    const uint32_t col = levelColor(r.level);
    ui.card(k);
    drawText(c, fontFace(18), k.x + 16, k.y + 10, "判级", kText2);
    fillRoundRect(c, k.x + 16, k.y + 42, 116, 46, 12, col, r.level == AlarmLevel::kUnknown ? 60 : 255);
    drawTextCentered(c, fontFace(32, true), k.x + 74, k.y + 44, levelText(r.level),
                     r.level == AlarmLevel::kUnknown ? kText2 : 0xFFFFFF);
    if (r.unit == "mm" && r.alarmPp > 0) {
      snprintf(buf, sizeof(buf), "预警 %s", fmtNum(r.warnPp, 3).c_str());
      drawText(c, fontFace(14), k.x + 142, k.y + 46, buf, kText2);
      snprintf(buf, sizeof(buf), "报警 %s mm", fmtNum(r.alarmPp, 3).c_str());
      drawText(c, fontFace(14), k.x + 142, k.y + 66, buf, kText2);
    } else {
      drawText(c, fontFace(14), k.x + 142, k.y + 56, "未标定不判级", kText2);
    }
  }

  // Details line.
  {
    std::string line = r.id;
    if (!r.timeText.empty()) line += "  ·  " + r.timeText;
    line += "  ·  测点 " + (r.point.empty() ? std::string("未选") : r.point);
    snprintf(buf, sizeof(buf), "  ·  %.1f fps  ·  丢帧 %lu", r.fps, r.dropped);
    line += buf;
    if (r.jitterMs > 0) {
      snprintf(buf, sizeof(buf), "  ·  抖动 %.2f ms", r.jitterMs);
      line += buf;
    }
    if (r.minNcc > 0) {
      snprintf(buf, sizeof(buf), "  ·  最低相关 %.2f", r.minNcc);
      line += buf;
    }
    line += "  ·  " + r.calibration;
    drawText(c, fontFace(14), kCx + 4, kCy + kh + 8, fitText(fontFace(14), line, kCw - 8), kText2);
  }

  // Waveform.
  const RecordView& rv = d.record;
  const bool haveData = rv.loaded && rv.id == r.id;
  {
    const Rect w{kCx, kCy + kh + 34, kCw, 196};
    ui.card(w);
    ChartOptions o;
    o.title = r.method == "multipoint" ? "代表测点位移" : "位移波形";
    o.unitY = r.unit;
    o.unitX = "s";
    o.line = kGreen;
    o.symmetric = false;
    if (haveData) {
      snprintf(buf, sizeof(buf), "%zu 个样本", rv.t.size());
      o.rightNote = buf;
    }
    static const std::vector<double> kEmpty;
    drawChart(c, w, haveData ? rv.t : kEmpty, haveData ? rv.disp : kEmpty, o);
  }
  // Spectrum and actions.
  {
    const int y = kCy + kh + 34 + 204;
    const Rect sp{kCx, y, kCw - 174, kCh - (y - kCy)};
    ui.card(sp);
    ChartOptions o;
    o.title = "幅值谱";
    o.unitY = r.unit;
    o.unitX = "Hz";
    o.fromZero = true;
    o.fill = true;
    std::vector<double> f, a;
    if (haveData) {
      const double fmax = r.fps > 0 ? r.fps * 0.4 : 1e9;  // analysed band
      for (size_t i = 0; i < rv.f.size() && i < rv.amp.size(); ++i) {
        if (rv.f[i] > fmax) break;
        f.push_back(rv.f[i]);
        a.push_back(rv.amp[i]);
      }
    }
    if (r.valid && r.freq > 0) {
      o.markerX = r.freq;
      snprintf(buf, sizeof(buf), "%s Hz · %s %s", fmtNum(r.freq, 4).c_str(), fmtNum(r.peak, 3).c_str(),
               r.unit.c_str());
      o.markerLabel = buf;
    }
    drawChart(c, sp, f, a, o);
    const int bx = kCx + kCw - 166, bh = (sp.h - 8) / 2;
    const bool printOk = r.saved && s.measureState == "preview" && s.printerState == "idle" && !s.pfail;
    JsonValue pr = JsonValue::makeObject();
    pr.set("cmd", "print");
    pr.set("measurement_id", r.id);
    ui.button(Rect{bx, y, 166, bh}, s.printerState == "idle" ? "打印报告" : "打印中", Icon::kPrinter, kAccent,
              printOk, cmdAction(pr), 18);
    if (fromHistory) {
      ui.button(Rect{bx, y + bh + 8, 166, bh}, "返回记录", Icon::kLeft, kNeutral, true, uiAction("close_record"), 18);
    } else {
      ui.button(Rect{bx, y + bh + 8, 166, bh}, "再测一次", Icon::kPlay, kNeutral, true,
                uiAction("nav", static_cast<int>(Page::kLive)), 18);
    }
  }
}

}  // namespace vm
