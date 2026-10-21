// Renders every UI page, the dialogs and a printed slip off-screen and checks
// that touches map to the right actions. With an output directory argument
// the images are written as PPM for visual review.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "app/report.h"
#include "gfx/font.h"
#include "test_util.h"
#include "ui/fb_ui.h"
#include "ui/ui_core.h"

using namespace vm;

namespace {

void writePpm(const Canvas& c, const std::string& path) {
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) return;
  fprintf(f, "P6\n%d %d\n255\n", c.width(), c.height());
  for (int y = 0; y < c.height(); ++y) {
    for (int x = 0; x < c.width(); ++x) {
      const uint32_t p = c.get(x, y);
      const unsigned char rgb[3] = {static_cast<unsigned char>(p >> 16), static_cast<unsigned char>(p >> 8),
                                    static_cast<unsigned char>(p)};
      fwrite(rgb, 1, 3, f);
    }
  }
  fclose(f);
}

// Textured scene with a bright beam that moves vertically by `dy` pixels.
std::vector<uint8_t> scene(int w, int h, double dy) {
  std::vector<uint8_t> img(static_cast<size_t>(w) * h);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      double v = 70 + 25 * std::sin(x * 0.11) * std::cos(y * 0.07) + 12 * std::sin((x + 2 * y) * 0.031);
      const double by = h * 0.45 + dy;
      if (x > w * 0.25 && x < w * 0.8 && std::fabs(y - by) < h * 0.05) v = 200 + 30 * std::sin(x * 0.4);
      if (std::hypot(x - w * 0.18, y - h * 0.7) < h * 0.12) v = 150 + 40 * std::cos(x * 0.5);
      img[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(std::max(0.0, std::min(255.0, v)));
    }
  }
  return img;
}

// Topmost action at (x, y) after rendering st/d.
UiAction tap(const UiState& st, const UiData& d, int x, int y, Rect* r = nullptr) {
  Canvas c(1024, 600);
  std::vector<HitRegion> hits;
  renderUi(c, st, d, hits);
  return hitTest(hits, x, y, r);
}

std::string cmdOf(const UiAction& a) { return a.kind == "cmd" ? jStr(a.cmd, "cmd", "") : std::string(); }

}  // namespace

int main(int argc, char** argv) {
  const std::string out = argc > 1 ? argv[1] : "";

  // ---------------------------------------------------------------- sample data
  UiData d;
  SystemState& s = d.s;
  s.initDone = s.carrierOk = s.tcaOk = s.inaOk = s.rtcOk = true;
  s.timeValid = true;
  s.timeSource = "ntp";
  s.nanoOnline = true;
  s.wifiState = "CONNECTED";
  s.wifiSsid = "Lab-5G";
  s.wifiIp = "192.168.1.57";
  s.sdMounted = s.nfcReady = true;
  s.power.busV = 12.05;
  s.power.currentA = 2.31;
  s.power.powerW = 27.8;
  s.pointId = "P-01";
  s.pointName = "电机A";
  s.lightAllowed = true;
  s.lightOn = true;
  s.laserOn = true;
  s.cameraOk = true;
  s.envValid = true;
  s.tempC = 24.6;
  s.rhPercent = 48;
  s.distanceValid = true;
  s.distanceMm = 512;
  s.imuOk = true;
  s.tofOk = true;
  s.measureState = "capturing";
  s.measureElapsedS = 3.4;
  s.measureDurationS = 10;
  s.zoom = 1.5;
  s.calibrated = true;
  s.calibrationId = "CAL-01";
  s.deviceId = "vibmon-01";
  s.appVersion = "vibmon 0.2.0";
  s.plVersion = "carrier_ctrl v1.0 · 100 MHz";
  s.nanoVersion = "vibmon-nano 1.0.0";
  s.cameraSource = "v4l2 /dev/video0";
  s.imuEnabled = s.tofEnabled = s.shtEnabled = s.printerEnabled = true;
  s.powerWarnA = 4.2;
  s.powerDenyA = 4.5;
  s.nanoRequests = 1234;
  s.fanOn = true;
  s.last.have = s.last.valid = s.last.saved = true;
  s.last.unit = "mm";
  s.last.dominantHz = 12.48;
  s.last.peakToPeak = 0.1234;
  s.last.rms = 0.0431;
  s.last.peak = 0.061;
  s.last.fps = 59.9;
  s.last.level = AlarmLevel::kWarn;
  s.alarmLevel = AlarmLevel::kWarn;
  s.last.measurementId = "vibmon-01-20261020-130709";
  s.last.pointId = "P-01";
  d.nowMs = 100000;
  d.wallMs = 1792561629000LL;

  MeasurementEngine::Live& live = d.live;
  live.cameraOk = true;
  live.fps = 59.9;
  live.frameW = 640;
  live.frameH = 480;
  live.previewW = 320;
  live.previewH = 240;
  live.preview = scene(320, 240, 0);
  live.roi = Roi{288, 208, 64, 64};
  live.frameCount = 200;
  live.lastDisp = 1.234;
  live.lastScore = 0.973;
  for (int i = 0; i < 400; ++i) live.recent.push_back(static_cast<float>(2 * std::sin(i * 2 * M_PI * 12.48 / 59.9)));

  // Real vision output: 90 frames of a 5 Hz vertical motion through the PS chain.
  {
    VisionPipeline vp;
    VisionConfig vc;
    vc.lowHz = 3;
    vc.highHz = 8;
    vp.configure(vc);
    for (int i = 0; i < 90; ++i) {
      const std::vector<uint8_t> f = scene(640, 480, 2.0 * std::sin(2 * M_PI * 5 * i / 60.0));
      vp.process(f.data(), 640, 480, static_cast<int64_t>(i) * 1000000000LL / 60);
    }
    live.vision = vp.live();
    CHECK(live.vision.w == 160 && live.vision.h == 120);
    CHECK(live.vision.ew == 80 && live.vision.eh == 60);
    CHECK(live.vision.settled);
    // The strongest band energy lies on the moving beam (rows 0.4..0.5 of the image).
    CHECK(live.vision.peakY >= 120 * 0.33 && live.vision.peakY <= 120 * 0.58);
    CHECK(live.vision.peakX >= 160 * 0.2 && live.vision.peakX <= 160 * 0.85);
  }
  s.settings.visionLowHz = 2;
  s.settings.visionHighHz = 5;

  // Stereo depth: a plane at 0.9..1.3 m with a box at 0.6 m and an invalid strip.
  {
    auto dm = std::make_shared<DepthMap>();
    dm->w = 320;
    dm->h = 240;
    dm->backend = "PS";
    dm->fps = 15;
    dm->procMs = 41.5;
    dm->mm.resize(320 * 240);
    for (int y = 0; y < 240; ++y) {
      for (int x = 0; x < 320; ++x) {
        uint16_t z = static_cast<uint16_t>(900 + x * 400 / 320);
        if (x > 120 && x < 220 && y > 80 && y < 170) z = 600;
        if (x < 18) z = 0;
        dm->mm[y * 320 + x] = z;
      }
    }
    dm->left = scene(320, 240, 0);
    d.depth = dm;
  }

  // History list and the record shown on the result page.
  for (int i = 0; i < 11; ++i) {
    RecordSummary r;
    char id[64];
    snprintf(id, sizeof(id), "vibmon-01-20261020-1%02d709", 30 - i);
    r.id = id;
    char t[32];
    snprintf(t, sizeof(t), "2026-10-20 1%d:%02d:09", 3 - i / 6, 59 - i * 5);
    r.startLocal = t;
    r.pointId = i % 3 ? "P-01" : "P-02";
    r.pointName = i % 3 ? "电机A" : "风机B";
    r.valid = i != 2;
    r.reason = "tracking_lost";
    r.unit = "mm";
    r.dominantHz = 12.48 - i * 0.3;
    r.peakToPeak = 0.12 + i * 0.4;
    r.rms = 0.04 + i * 0.13;
    r.level = !r.valid ? AlarmLevel::kUnknown : (r.peakToPeak >= 5 ? AlarmLevel::kAlarm : (r.peakToPeak >= 2 ? AlarmLevel::kWarn : AlarmLevel::kNormal));
    d.records.push_back(r);
  }
  d.record.id = s.last.measurementId;
  d.record.loaded = true;
  {
    const std::string js =
        "{\"start_local\":\"2026-10-20 13:07:09\",\"point_id\":\"P-01\",\"point_name\":\"电机A\","
        "\"camera\":{\"fps_measured\":59.9,\"frames_dropped\":0,\"frame_interval_jitter_ms\":0.42},"
        "\"quality\":{\"min_ncc\":0.973},\"calibration\":{\"id\":\"CAL-01\"},"
        "\"result\":{\"valid\":true,\"unit\":\"mm\",\"dominant_frequency_hz\":12.48,"
        "\"displacement_peak_to_peak_mm\":0.1234,\"displacement_rms_mm\":0.0431,\"spectral_peak_mm\":0.061,"
        "\"level\":\"warn\",\"thresholds\":{\"warn_peak_to_peak\":0.1,\"alarm_peak_to_peak\":0.5}}}";
    std::string err;
    CHECK(vp::jsonParse(js, d.record.rec, &err));
  }
  for (int i = 0; i < 600; ++i) {
    d.record.t.push_back(i / 59.9);
    d.record.disp.push_back(0.06 * std::sin(2 * M_PI * 12.48 * i / 59.9) + 0.004 * std::sin(i * 0.9));
  }
  for (int k = 0; k < 300; ++k) {
    d.record.f.push_back(k * 0.1);
    d.record.amp.push_back(0.061 * std::exp(-std::pow((k * 0.1 - 12.48) / 0.25, 2)) + 0.0015 * std::fabs(std::sin(k * 1.7)));
  }

  // ------------------------------------------------------------- interactions
  UiState st;
  // Measure button: stop while capturing, start in preview, disabled while analysing or printing.
  const int bx = 100 + 608 + 82, by = 56 + 118;
  CHECK(cmdOf(tap(st, d, bx, by)) == "measure_stop");
  UiData pd = d;
  pd.s.measureState = "preview";
  CHECK(cmdOf(tap(st, pd, bx, by)) == "measure_start");
  pd.s.printerState = "printing";
  CHECK(tap(st, pd, bx, by).isNull());
  pd.s.printerState = "idle";
  pd.s.measureState = "analyzing";
  CHECK(tap(st, pd, bx, by).isNull());
  // Preview tap maps through the display zoom to normalised image coordinates.
  pd.s.measureState = "preview";
  pd.s.zoom = 2;
  {
    Rect r;
    const UiAction a = tap(st, pd, 105 + 76, 61 + 1, &r);  // image starts at (104 + 76, 60) for 4:3 frames
    CHECK(a.kind == "roi");
    UiState s2;
    const JsonValue cmd = FbUi::applyAction(s2, a, r, r.x, r.y, pd, 0);
    CHECK(jStr(cmd, "cmd", "") == "roi");
    CHECK_NEAR(jNum(cmd, "x", 0), 0.25, 0.01);
    CHECK_NEAR(jNum(cmd, "y", 0), 0.25, 0.01);
  }
  // No ROI changes during a capture.
  CHECK(tap(st, d, 400, 230).isNull());
  // Lighting (LED1) and the laser projector (LED2) are separate switches.
  {
    const UiAction a = tap(st, d, 708 + 158 + 40, 56 + 396 + 20);
    CHECK(cmdOf(a) == "laser" && !jBool(a.cmd, "on", true));
    const UiAction l = tap(st, d, 708 + 40, 56 + 396 + 20);
    CHECK(cmdOf(l) == "light" && !jBool(l.cmd, "on", true));
    UiData locked = d;
    locked.s.measureState = "capturing";
    locked.s.laserOn = false;
    locked.s.settings.laserOffDuringMeasure = true;
    CHECK(tap(st, locked, 708 + 154 + 30, 56 + 396 + 20).isNull());
    locked.s.measureState = "analyzing";
    CHECK(tap(st, locked, 708 + 154 + 30, 56 + 396 + 20).isNull());
    locked.s.measureState = "preview";
    CHECK(cmdOf(tap(st, locked, 708 + 154 + 30, 56 + 396 + 20)) == "laser");
  }
  // Navigation rail.
  {
    const UiAction a = tap(st, d, 40, 48 + 3 * 92 + 40);
    CHECK(a.kind == "nav" && a.arg == 3);
    UiState s2;
    FbUi::applyAction(s2, a, Rect(), 0, 0, d, 0);
    CHECK(s2.page == Page::kHistory);
  }
  // Result page: printing needs a saved record, the preview state and an idle printer.
  {
    UiState rs;
    rs.page = Page::kResult;
    UiData rd = d;
    rd.s.measureState = "preview";
    const int px = 100 + 916 - 166 + 40, py = 56 + 104 + 34 + 204 + 30;
    const UiAction a = tap(rs, rd, px, py);
    CHECK(cmdOf(a) == "print" && jStr(a.cmd, "measurement_id", "") == rd.s.last.measurementId);
    rd.s.measureState = "capturing";
    CHECK(tap(rs, rd, px, py).isNull());
  }
  // History: rows open the record, pages turn.
  {
    UiState hs;
    hs.page = Page::kHistory;
    const UiAction a = tap(hs, d, 500, 56 + 76 + 20);
    CHECK(a.kind == "open_record" && a.text == d.records[0].id);
    FbUi::applyAction(hs, a, Rect(), 0, 0, d, 0);
    CHECK(hs.page == Page::kResult && hs.openRecordId == d.records[0].id);
  }
  // Settings: a duration chip sends "set"; the shutdown button opens the confirmation.
  {
    UiState ss;
    ss.page = Page::kSettings;
    UiData sd = d;
    sd.s.measureState = "preview";
    const int cw = (916 - 8) / 2;
    const UiAction a = tap(ss, sd, 100 + cw - 4 * 64 - 8 + 2 * 64 + 20, 56 + 30);  // "30 s"
    CHECK(cmdOf(a) == "set" && std::fabs(jNum(a.cmd, "duration_s", 0) - 30) < 1e-9);
    const UiAction sh = tap(ss, sd, 100 + cw + 8 + cw - 100, 56 + 6 * 68 + 60);
    CHECK(sh.kind == "dialog" && sh.arg == static_cast<int>(Dialog::kShutdown));
  }
  // Shutdown dialog is modal: the rail does not react, confirm sends "shutdown".
  {
    UiState ds;
    ds.dialog = Dialog::kShutdown;
    CHECK(tap(ds, d, 40, 300).isNull());
    CHECK(cmdOf(tap(ds, d, 1024 / 2 + 250 - 136, 600 / 2 - 130 + 200)) == "shutdown");
  }
  // Wi-Fi dialog: typing and connecting.
  {
    UiState ws;
    UiData wd = d;
    wd.s.wifiSsid.clear();
    FbUi::applyAction(ws, uiAction("dialog", static_cast<int>(Dialog::kWifi)), Rect(), 0, 0, wd, 0);
    CHECK(ws.dialog == Dialog::kWifi && ws.field == 0);
    for (const char* k : {"l", "a", "b"}) FbUi::applyAction(ws, uiAction("kb", 0, k), Rect(), 0, 0, wd, 0);
    FbUi::applyAction(ws, uiAction("kb_field", 1), Rect(), 0, 0, wd, 0);
    for (const char* k : {"p", "w", "!"}) FbUi::applyAction(ws, uiAction("kb", 0, k), Rect(), 0, 0, wd, 0);
    FbUi::applyAction(ws, uiAction("kb_back"), Rect(), 0, 0, wd, 0);
    CHECK(ws.ssid == "lab" && ws.password == "pw");
    const UiAction conn = tap(ws, wd, 1024 - 36 - 196 + 80, 28 + 70 + 20);
    CHECK(cmdOf(conn) == "set_net" && jStr(conn.cmd, "ssid", "") == "lab" && jStr(conn.cmd, "password", "") == "pw");
    CHECK(tap(ws, wd, 40, 300).isNull());  // background blocked
  }
  // Vision page: depth probe stores the point; mode chips switch views.
  {
    UiState vs;
    vs.page = Page::kVision;
    vs.visionView = 2;
    const int cw = (916 - 8) / 2;
    Rect r;
    const UiAction a = tap(vs, d, 100 + cw + 8 + cw / 2, 56 + 38 + 150, &r);
    CHECK(a.kind == "depth_probe");
    FbUi::applyAction(vs, a, r, r.x + r.w / 2, r.y + r.h / 2, d, 0);
    CHECK_NEAR(vs.probeX, 0.5, 0.01);
    CHECK_NEAR(vs.probeY, 0.5, 0.01);
    CHECK(d.depth->at(0.5, 0.5) == 600);
  }

  // Every glyph of the shown text exists in the generated fonts.
  for (const char* txt : {"开始测量", "停止测量", "打印报告", "双目深度未接入", "振动热力", "测量记录", "配置 Wi-Fi",
                          "应用已停止, 等待Linux关机", "视觉测振报告", "未判级"}) {
    const std::string str(txt);
    for (bool bold : {false, true}) {
      const FontFace& f = fontFace(22, bold);
      for (size_t i = 0; i < str.size();) CHECK(hasGlyph(f, nextCodepoint(str, i)));
    }
  }

  // ------------------------------------------------------------------- render
  struct Shot {
    const char* name;
    Page page;
    int view;
    Dialog dialog;
  };
  const Shot shots[] = {
      {"ui_live", Page::kLive, 0, Dialog::kNone},         {"ui_result", Page::kResult, 0, Dialog::kNone},
      {"ui_vision", Page::kVision, 0, Dialog::kNone},     {"ui_heat", Page::kVision, 1, Dialog::kNone},
      {"ui_depth", Page::kVision, 2, Dialog::kNone},      {"ui_history", Page::kHistory, 0, Dialog::kNone},
      {"ui_device", Page::kDevice, 0, Dialog::kNone},     {"ui_settings", Page::kSettings, 0, Dialog::kNone},
      {"ui_wifi", Page::kSettings, 0, Dialog::kWifi},     {"ui_shutdown", Page::kSettings, 0, Dialog::kShutdown},
  };
  for (const Shot& sh : shots) {
    UiState rs;
    rs.page = sh.page;
    rs.visionView = sh.view;
    rs.dialog = sh.dialog;
    rs.probeX = 0.72;
    rs.probeY = 0.3;
    if (sh.dialog == Dialog::kWifi) {
      rs.ssid = "Lab-5G";
      rs.password = "secret";
      rs.field = 1;
    }
    UiData rd = d;
    if (sh.page == Page::kVision || sh.page == Page::kSettings || sh.page == Page::kResult) rd.s.measureState = "preview";
    Canvas c(1024, 600);
    std::vector<HitRegion> hits;
    renderUi(c, rs, rd, hits);
    CHECK(c.get(10, 10) != 0);
    CHECK(!hits.empty());
    if (!out.empty()) writePpm(c, out + "/" + sh.name + ".ppm");
  }
  {
    UiData sd = d;
    sd.s.shutdownPrepared = true;
    sd.s.message = "应用已停止, 等待Linux关机";
    Canvas c(1024, 600);
    std::vector<HitRegion> hits;
    renderUi(c, UiState(), sd, hits);
    CHECK(hits.empty());  // nothing reacts after the application stopped
    if (!out.empty()) writePpm(c, out + "/ui_shutdown_done.ppm");
  }

  // Printed slip.
  ReportData rep;
  rep.deviceId = "vibmon-01";
  rep.measurementId = s.last.measurementId;
  rep.timeText = "2026-10-20 13:07:09 (UTC+8)";
  rep.pointText = "P-01 电机A";
  rep.summary = s.last;
  rep.calibrationText = "CAL-01";
  rep.fps = 59.9;
  rep.envText = "环境 24.6 °C  48 %RH";
  rep.t = d.record.t;
  rep.disp = d.record.disp;
  rep.f = d.record.f;
  rep.amp = d.record.amp;
  Canvas slip = renderReport(rep, 384);
  CHECK(slip.width() == 384 && slip.height() > 400);
  int wb = 0;
  std::vector<uint8_t> bits = toMonoBits(slip, wb);
  CHECK(wb == 48 && bits.size() == static_cast<size_t>(48 * slip.height()));
  if (!out.empty()) {
    writePpm(slip, out + "/report.ppm");
    printf("wrote UI pages and report.ppm to %s\n", out.c_str());
  }
  return testSummary("render");
}
