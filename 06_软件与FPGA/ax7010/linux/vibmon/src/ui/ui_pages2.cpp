// History, device, settings pages and the Wi-Fi dialog.
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "app/report.h"
#include "gfx/draw.h"
#include "ui/ui_core.h"

namespace vm {

using namespace theme;

namespace {

JsonValue setCmd(const char* key, const JsonValue& v) {
  JsonValue j = JsonValue::makeObject();
  j.set("cmd", "set");
  j.set(key, v);
  return j;
}

// Preferred threshold values (mm) for the steppers.
const double kThresholds[] = {0.01, 0.02, 0.05, 0.1, 0.2, 0.3, 0.5, 0.8, 1, 1.5, 2, 3, 4, 5, 8, 10, 15, 20, 30, 50};

double nextUp(const double* list, size_t n, double v) {
  for (size_t i = 0; i < n; ++i) {
    if (list[i] > v * (1 + 1e-9) + 1e-12) return list[i];
  }
  return -1;
}

double nextDown(const double* list, size_t n, double v) {
  for (size_t i = n; i-- > 0;) {
    if (list[i] < v * (1 - 1e-9) - 1e-12) return list[i];
  }
  return -1;
}

}  // namespace

// ---------------------------------------------------------------------------
// History
// ---------------------------------------------------------------------------

void renderHistoryPage(Ui& ui, const UiData& d) {
  Canvas& c = ui.c;
  const std::vector<RecordSummary>& recs = d.records;
  char buf[160];
  const int perPage = 8;
  const int pages = std::max(1, static_cast<int>((recs.size() + perPage - 1) / perPage));
  const int page = std::max(0, std::min(pages - 1, ui.st.historyPage));

  drawText(c, fontFace(22, true), kCx + 4, kCy + 4, "测量记录", kText);
  snprintf(buf, sizeof(buf), "共 %zu 条", recs.size());
  drawText(c, fontFace(18), kCx + 110, kCy + 9, buf, kText2);
  snprintf(buf, sizeof(buf), "%d / %d", page + 1, pages);
  drawTextCentered(c, fontFace(18), kCx + kCw - 78, kCy + 9, buf, kText2);
  ui.iconButton(Rect{kCx + kCw - 150, kCy, 44, 40}, Icon::kUp, page > 0, uiAction("hist_page", -1));
  ui.iconButton(Rect{kCx + kCw - 44, kCy, 44, 40}, Icon::kDown, page + 1 < pages, uiAction("hist_page", 1));

  if (recs.empty()) {
    ui.emptyState(Rect{kCx, kCy + 50, kCw, kCh - 50}, Icon::kHistory, "暂无测量记录", "完成一次测量后会出现在这里");
    return;
  }
  // Columns: time | point | frequency | p-p | rms | level.
  const int cx[6] = {kCx + 16, kCx + 270, kCx + 430, kCx + 560, kCx + 690, kCx + 800};
  const char* heads[6] = {"时间 / 编号", "测点", "主频", "峰峰值", "有效值", "判级"};
  for (int i = 0; i < 6; ++i) drawText(c, fontFace(14), cx[i], kCy + 52, heads[i], kText3);
  for (int i = 0; i < perPage; ++i) {
    const size_t k = static_cast<size_t>(page * perPage + i);
    if (k >= recs.size()) break;
    const RecordSummary& r = recs[k];
    const Rect row{kCx, kCy + 76 + i * 57, kCw, 52};
    const bool down = ui.pressed(row);
    fillRoundRect(c, row.x, row.y, row.w, row.h, 10, down ? kCardHi : kCard);
    strokeRoundRect(c, row.x, row.y, row.w, row.h, 10, 1, kBorder);
    drawText(c, fontFace(18), cx[0], row.y + 4, r.startLocal.empty() ? std::string("时间未校准") : r.startLocal, kText);
    drawText(c, fontFace(14), cx[0], row.y + 29, fitText(fontFace(14), r.id, 240), kText3);
    const std::string point = r.pointId.empty() ? std::string("—") : r.pointId + (r.pointName.empty() ? "" : " " + r.pointName);
    drawText(c, fontFace(18), cx[1], row.y + 14, fitText(fontFace(18), point, 150), kText);
    if (r.valid) {
      drawText(c, fontFace(22, true), cx[2], row.y + 10, fmtNum(r.dominantHz, 4), kText);
      drawText(c, fontFace(14), cx[2] + 4 + textWidth(fontFace(22, true), fmtNum(r.dominantHz, 4)), row.y + 20, "Hz",
               kText2);
      drawText(c, fontFace(18), cx[3], row.y + 14, fmtNum(r.peakToPeak, 3) + " " + r.unit, kText);
      drawText(c, fontFace(18), cx[4], row.y + 14, fmtNum(r.rms, 3) + " " + r.unit, kText);
    } else {
      drawText(c, fontFace(18), cx[2], row.y + 14, fitText(fontFace(18), "无效 · " + reasonText(r.reason), 380), kAmber);
    }
    ui.badge(cx[5], row.y + 12, levelText(r.level), levelColor(r.level), r.level != AlarmLevel::kUnknown, 18);
    drawIcon(c, Icon::kRight, row.x + row.w - 24.f, row.y + 26.f, 16, kText3);
    ui.hit(row, uiAction("open_record", 0, r.id));
  }
}

// ---------------------------------------------------------------------------
// Devices
// ---------------------------------------------------------------------------

namespace {
enum TileState { kOk, kWarnS, kErr, kOff };

struct Tile {
  Icon icon;
  std::string name, state, detail;
  TileState st;
};

uint32_t tileColor(TileState s) {
  switch (s) {
    case kOk: return kGreen;
    case kWarnS: return kAmber;
    case kErr: return kRed;
    default: return kText3;
  }
}
}  // namespace

void renderDevicePage(Ui& ui, const UiData& d) {
  Canvas& c = ui.c;
  const SystemState& s = d.s;
  char buf[160];
  std::vector<Tile> tiles;
  tiles.push_back({Icon::kChip, "PL 载板控制", s.carrierOk ? "正常" : "未连接",
                   s.carrierOk ? s.plVersion : std::string("AXI 0x44000000 未发现"), s.carrierOk ? kOk : kErr});
  snprintf(buf, sizeof(buf), "%.2f V · %.2f A · %.1f W", s.power.busV, s.power.currentA, s.power.powerW);
  tiles.push_back({Icon::kBolt, "电源监测", !s.inaOk ? "无数据" : (s.pfail ? "掉电预警" : (s.powerWarn ? "电流偏高" : "正常")),
                   s.inaOk ? std::string(buf) : std::string("INA226 未响应"),
                   !s.inaOk ? kErr : (s.pfail || s.overCurrentLatched ? kErr : (s.powerWarn ? kWarnS : kOk))});
  tiles.push_back({Icon::kBoard, "负载控制", s.tcaOk ? "正常" : "未连接", "报警灯 · 蜂鸣器 · 风扇 · 打印供电",
                   s.tcaOk ? kOk : kErr});
  {
    const std::string src = s.timeSource == "ntp" ? "网络校时" : (s.timeSource == "rtc" ? "RTC" : (s.timeSource == "manual" ? "手动" : "未校准"));
    tiles.push_back({Icon::kClock, "实时时钟", s.rtcOk ? (s.timeValid ? "正常" : "未校准") : "未连接", "时间来源 " + src,
                     s.rtcOk ? (s.timeValid ? kOk : kWarnS) : kErr});
  }
  snprintf(buf, sizeof(buf), "%s · %.1f fps", s.cameraSource.empty() ? "相机" : s.cameraSource.c_str(), d.live.fps);
  tiles.push_back({Icon::kCamera, "相机", s.cameraOk ? "采集中" : "未就绪", buf, s.cameraOk ? kOk : kErr});
  snprintf(buf, sizeof(buf), "请求 %lu · 重试 %lu · CRC错 %lu", s.nanoRequests, s.nanoRetries, s.nanoCrcErrors);
  tiles.push_back({Icon::kBoard, "协处理器 Nano", s.nanoOnline ? "在线" : "离线", buf, s.nanoOnline ? kOk : kErr});
  {
    TileState st = kOff;
    std::string state = "未配置", detail = "在设置页配置 Wi-Fi";
    if (!s.nanoOnline) {
      state = "未知";
      detail = "协处理器离线";
      st = kWarnS;
    } else if (s.wifiState == "CONNECTED") {
      state = "已连接";
      detail = s.wifiSsid + (s.wifiIp.empty() ? "" : " · " + s.wifiIp);
      st = kOk;
    } else if (s.wifiState == "CONNECTING") {
      state = "连接中";
      detail = s.wifiSsid;
      st = kWarnS;
    } else if (s.wifiState == "DISCONNECTED") {
      state = "未连接";
      detail = s.wifiSsid;
      st = kWarnS;
    }
    tiles.push_back({Icon::kWifi, "无线网络", state, detail, st});
  }
  tiles.push_back({Icon::kSd, "语音 SD 卡", s.sdMounted ? "已挂载" : "未挂载", s.sdMounted ? "语音资源可用" : "插入语音卡后自动挂载",
                   s.sdMounted ? kOk : (s.nanoOnline ? kWarnS : kOff)});
  tiles.push_back({Icon::kTag, "NFC 读卡", s.nfcReady ? "就绪" : "未就绪",
                   s.pointId.empty() ? std::string("刷卡选择测点") : "当前测点 " + s.pointId, s.nfcReady ? kOk : kWarnS});
  {
    TileState st = kOk;
    std::string state = "空闲";
    if (!s.printerEnabled) {
      st = kOff;
      state = "未启用";
    } else if (!s.printerError.empty()) {
      st = kErr;
      state = "错误";
    } else if (s.printerState != "idle") {
      st = kWarnS;
      state = "打印中";
    }
    tiles.push_back({Icon::kPrinter, "打印机", state, s.printerError.empty() ? std::string("EM5820H 12V") : errorText(s.printerError), st});
  }
  tiles.push_back({Icon::kAxes, "相机 IMU", !s.imuEnabled ? "未启用" : (s.imuOk ? "正常" : "无响应"), "LSM6DSV 机身运动记录",
                   !s.imuEnabled ? kOff : (s.imuOk ? kOk : kErr)});
  tiles.push_back({Icon::kAxes, "参考加速度计", !s.adxlEnabled ? "未启用" : (s.adxlOk ? "正常" : "无响应"),
                   s.adxlEnabled ? std::string("ADXL345 参考通道") : std::string("待共享 SPI 资格测试"),
                   !s.adxlEnabled ? kOff : (s.adxlOk ? kOk : kErr)});
  if (s.distanceValid) {
    snprintf(buf, sizeof(buf), "距离 %d mm", s.distanceMm);
  } else {
    snprintf(buf, sizeof(buf), "%s", s.tofNote.empty() ? "无测距数据" : s.tofNote.c_str());
  }
  tiles.push_back({Icon::kRuler, "激光测距", !s.tofEnabled ? "未启用" : (s.tofOk ? "正常" : "未就绪"), buf,
                   !s.tofEnabled ? kOff : (s.tofOk && s.distanceValid ? kOk : kWarnS)});
  if (s.envValid) {
    snprintf(buf, sizeof(buf), "%.1f °C · %.0f %%RH", s.tempC, s.rhPercent);
  } else {
    snprintf(buf, sizeof(buf), "无数据");
  }
  tiles.push_back({Icon::kThermo, "温湿度", !s.shtEnabled ? "未启用" : (s.envValid ? "正常" : "无数据"), buf,
                   !s.shtEnabled ? kOff : (s.envValid ? kOk : kWarnS)});
  {
    const std::string mode = s.settings.fanMode == "on" ? "常开" : (s.settings.fanMode == "off" ? "关闭" : "自动");
    std::string detail = "模式 " + mode;
    if (s.tachValid) {
      snprintf(buf, sizeof(buf), " · J705 %.1f 脉冲/s", s.tachHz);
      detail += buf;
    }
    tiles.push_back({Icon::kFan, "风扇与转速", s.fanOn ? "运行" : "停止", detail, s.fanOn ? kOk : kOff});
  }
  snprintf(buf, sizeof(buf), "照明 %s %.0f%% · 激光 %s", s.lightOn ? "开" : "关", s.lightPercent, s.laserOn ? "开" : "关");
  tiles.push_back({Icon::kLaser, "照明与激光", s.lightAllowed ? "SW301 已允许" : "SW301 未允许", buf,
                   !s.carrierOk ? kErr : (s.lightAllowed ? kOk : kOff)});

  const int tw = (kCw - 3 * 8) / 4, th = 100;
  for (size_t i = 0; i < tiles.size() && i < 16; ++i) {
    const Tile& t = tiles[i];
    const Rect r{kCx + static_cast<int>(i % 4) * (tw + 8), kCy + static_cast<int>(i / 4) * (th + 8), tw, th};
    ui.card(r);
    const uint32_t col = tileColor(t.st);
    fillCircle(c, r.x + 34.f, r.y + 34.f, 22, kCardHi);
    drawIcon(c, t.icon, r.x + 34.f, r.y + 34.f, 24, t.st == kOff ? kText3 : kText);
    drawText(c, fontFace(18), r.x + 66, r.y + 10, fitText(fontFace(18), t.name, tw - 76), kText);
    fillCircle(c, r.x + 71.f, r.y + 49.f, 5, col);
    drawText(c, fontFace(14), r.x + 82, r.y + 39, t.state, col);
    drawText(c, fontFace(14), r.x + 14, r.y + 70, fitText(fontFace(14), t.detail, tw - 28), kText2);
  }

  // Power budget and versions.
  const int y = kCy + 4 * (th + 8);
  const int cw = (kCw - 8) / 2;
  {
    const Rect r{kCx, y, cw, kCh - (y - kCy)};
    ui.card(r);
    ui.cardTitle(r, "整机电源", Icon::kBolt);
    snprintf(buf, sizeof(buf), "%.2f V   %.2f A   %.1f W", s.power.busV, s.power.currentA, s.power.powerW);
    drawText(c, fontFace(22, true), r.x + 160, r.y + 6, s.inaOk ? buf : "无数据", kText);
    const int bx = r.x + 16, by = r.y + 50, bw = r.w - 32;
    fillRoundRect(c, bx, by, bw, 14, 7, kCardHi);
    const double maxA = std::max(1.0, s.powerDenyA * 1.1);
    const double frac = std::max(0.0, std::min(1.0, s.power.currentA / maxA));
    const uint32_t col = s.power.currentA >= s.powerDenyA ? kRed : (s.power.currentA >= s.powerWarnA ? kAmber : kGreen);
    if (s.inaOk && frac > 0) fillRoundRect(c, bx, by, std::max(14, static_cast<int>(bw * frac)), 14, 7, col);
    const int wx = bx + static_cast<int>(bw * s.powerWarnA / maxA), dx = bx + static_cast<int>(bw * s.powerDenyA / maxA);
    c.fillRect(wx, by - 4, 2, 22, kAmber);
    c.fillRect(dx, by - 4, 2, 22, kRed);
    fillRoundRect(c, bx, by + 26, 10, 10, 2, kAmber);
    snprintf(buf, sizeof(buf), "预警 %.1f A", s.powerWarnA);
    int lx = drawText(c, fontFace(14), bx + 16, by + 21, buf, kText2);
    fillRoundRect(c, lx + 18, by + 26, 10, 10, 2, kRed);
    snprintf(buf, sizeof(buf), "大负载禁开 %.1f A", s.powerDenyA);
    drawText(c, fontFace(14), lx + 34, by + 21, buf, kText2);
  }
  {
    const Rect r{kCx + cw + 8, y, cw, kCh - (y - kCy)};
    ui.card(r);
    ui.cardTitle(r, "版本信息", Icon::kLayers);
    drawText(c, fontFace(14), r.x + 16, r.y + 38, "应用  " + s.appVersion, kText);
    drawText(c, fontFace(14), r.x + 16, r.y + 60, "PL  " + (s.plVersion.empty() ? std::string("—") : s.plVersion), kText);
    drawText(c, fontFace(14), r.x + 240, r.y + 38, "Nano  " + (s.nanoVersion.empty() ? std::string("—") : s.nanoVersion),
             kText);
    drawText(c, fontFace(14), r.x + 240, r.y + 60, "设备  " + s.deviceId, kText);
  }
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

void renderSettingsPage(Ui& ui, const UiData& d) {
  Canvas& c = ui.c;
  const SystemState& s = d.s;
  const UserSettings& u = s.settings;
  const bool busy = s.measureState == "capturing" || s.measureState == "analyzing";
  char buf[96];
  const int cw = (kCw - 8) / 2, rh = 60, step = 68;
  auto row = [&](int col, int i, const char* label, const char* hint = nullptr) {
    const Rect r{kCx + col * (cw + 8), kCy + i * step, cw, rh};
    ui.card(r);
    if (hint) {
      drawText(c, fontFace(18), r.x + 16, r.y + 6, label, kText);
      drawText(c, fontFace(14), r.x + 16, r.y + 34, hint, kText3);
    } else {
      drawText(c, fontFace(18), r.x + 16, r.y + 17, label, kText);
    }
    return r;
  };

  // Left column: measurement and lighting.
  {
    const Rect r = row(0, 0, "测量时长");
    static const int kDur[4] = {5, 10, 30, 60};
    for (int i = 0; i < 4; ++i) {
      snprintf(buf, sizeof(buf), "%d s", kDur[i]);
      ui.chip(Rect{r.x + r.w - 4 * 64 - 8 + i * 64, r.y + 12, 58, 36}, buf, std::fabs(u.durationS - kDur[i]) < 0.01,
              !busy, cmdAction(setCmd("duration_s", JsonValue(kDur[i]))));
    }
  }
  {
    // Square presets; on the live preview the box is dragged to any width and height.
    char hint[48];
    snprintf(hint, sizeof(hint), "当前 %d×%d px", u.roiW, u.roiH);
    const Rect r = row(0, 1, "测点框尺寸", hint);
    static const int kRoi[4] = {32, 48, 64, 96};
    for (int i = 0; i < 4; ++i) {
      snprintf(buf, sizeof(buf), "%d", kRoi[i]);
      JsonValue square = setCmd("roi_width_px", JsonValue(kRoi[i]));
      square.set("roi_height_px", kRoi[i]);
      ui.chip(Rect{r.x + r.w - 4 * 64 - 8 + i * 64, r.y + 12, 58, 36}, buf, u.roiW == kRoi[i] && u.roiH == kRoi[i],
              !busy && kRoi[i] <= u.roiMaxPx, cmdAction(square));
    }
  }
  {
    const bool locked = s.calibrated;
    const Rect r = row(0, 2, "位移方向", locked ? "由当前标定决定" : nullptr);
    ui.chip(Rect{r.x + r.w - 2 * 104 - 8, r.y + 12, 98, 36}, "水平 X", u.axis == 'x', !busy && !locked,
            cmdAction(setCmd("axis", JsonValue("x"))));
    ui.chip(Rect{r.x + r.w - 104 - 8, r.y + 12, 98, 36}, "垂直 Y", u.axis == 'y', !busy && !locked,
            cmdAction(setCmd("axis", JsonValue("y"))));
  }
  {
    const Rect r = row(0, 3, "测量时自动照明");
    ui.toggle(Rect{r.x + r.w - 82, r.y + 13, 64, 34}, u.autoLight, true,
              cmdAction(setCmd("auto_light", JsonValue(!u.autoLight))));
  }
  {
    const Rect r = row(0, 4, "照明亮度", "LED1 全部照明灯，PWM 调光");
    const double p = s.lightPercent;
    snprintf(buf, sizeof(buf), "%.0f%%", p);
    ui.stepper(Rect{r.x + r.w - 196, r.y + 8, 180, 44}, buf, p > 10.5, p < 99.5,
               cmdAction(setCmd("light_percent", JsonValue(std::max(10.0, p - 10)))),
               cmdAction(setCmd("light_percent", JsonValue(std::min(100.0, p + 10)))));
  }
  {
    const Rect r = row(0, 5, "测量时关闭投影", "固定十字光线可能干扰表面位移跟踪");
    const bool laserPolicyEditable = s.measureState != "capturing" && s.measureState != "analyzing";
    ui.toggle(Rect{r.x + r.w - 82, r.y + 13, 64, 34}, u.laserOffDuringMeasure, laserPolicyEditable,
              cmdAction(setCmd("laser_off_during_measurement", JsonValue(!u.laserOffDuringMeasure))));
  }

  // Right column: alarm, voice, fan.
  const size_t nt = sizeof(kThresholds) / sizeof(kThresholds[0]);
  {
    const Rect r = row(1, 0, "预警阈值", "峰峰值，毫米");
    const double dn = nextDown(kThresholds, nt, u.warnPp), up = nextUp(kThresholds, nt, u.warnPp);
    ui.stepper(Rect{r.x + r.w - 196, r.y + 8, 180, 44}, fmtNum(u.warnPp, 3), dn > 0, up > 0 && up < u.alarmPp,
               cmdAction(setCmd("warn_pp", JsonValue(dn))), cmdAction(setCmd("warn_pp", JsonValue(up))));
  }
  {
    const Rect r = row(1, 1, "报警阈值", "峰峰值，毫米");
    const double dn = nextDown(kThresholds, nt, u.alarmPp), up = nextUp(kThresholds, nt, u.alarmPp);
    ui.stepper(Rect{r.x + r.w - 196, r.y + 8, 180, 44}, fmtNum(u.alarmPp, 3), dn > u.warnPp, up > 0,
               cmdAction(setCmd("alarm_pp", JsonValue(dn))), cmdAction(setCmd("alarm_pp", JsonValue(up))));
  }
  {
    const Rect r = row(1, 2, "报警蜂鸣器");
    ui.toggle(Rect{r.x + r.w - 82, r.y + 13, 64, 34}, u.buzzer, true, cmdAction(setCmd("buzzer", JsonValue(!u.buzzer))));
  }
  {
    const Rect r = row(1, 3, "语音提示");
    ui.toggle(Rect{r.x + r.w - 82, r.y + 13, 64, 34}, u.voice, true, cmdAction(setCmd("voice", JsonValue(!u.voice))));
  }
  {
    const Rect r = row(1, 4, "语音音量");
    snprintf(buf, sizeof(buf), "%d", u.volume);
    ui.stepper(Rect{r.x + r.w - 196, r.y + 8, 180, 44}, buf, u.voice && u.volume > 0, u.voice && u.volume < 100,
               cmdAction(setCmd("volume", JsonValue(std::max(0, u.volume - 10)))),
               cmdAction(setCmd("volume", JsonValue(std::min(100, u.volume + 10)))));
  }
  {
    const Rect r = row(1, 5, "散热风扇");
    static const char* kModes[3][2] = {{"auto", "自动"}, {"on", "常开"}, {"off", "关闭"}};
    for (int i = 0; i < 3; ++i) {
      ui.chip(Rect{r.x + r.w - 3 * 76 - 8 + i * 76, r.y + 12, 70, 36}, kModes[i][1], u.fanMode == kModes[i][0], true,
              cmdAction(setCmd("fan_mode", JsonValue(kModes[i][0]))));
    }
  }

  // Bottom: network and system.
  const int y = kCy + 6 * step;
  {
    const Rect r{kCx, y, cw, kCh - (y - kCy)};
    ui.card(r);
    ui.cardTitle(r, "无线网络", Icon::kWifi);
    std::string st = "协处理器离线";
    if (s.nanoOnline) {
      st = s.wifiState == "CONNECTED" ? "已连接 " + s.wifiSsid
                                       : (s.wifiState == "CONNECTING" ? "正在连接 " + s.wifiSsid
                                                                      : (s.wifiState == "OFF" ? "未配置" : "未连接"));
    }
    drawText(c, fontFace(18), r.x + 16, r.y + 44, fitText(fontFace(18), st, r.w - 210), kText);
    drawText(c, fontFace(14), r.x + 16, r.y + 76, s.wifiIp.empty() ? "无 IP 地址" : "IP " + s.wifiIp, kText2);
    ui.button(Rect{r.x + r.w - 180, r.y + 36, 164, 56}, "配置 Wi-Fi", Icon::kWifi, kAccent, s.nanoOnline,
              uiAction("dialog", static_cast<int>(Dialog::kWifi)), 18);
  }
  {
    const Rect r{kCx + cw + 8, y, cw, kCh - (y - kCy)};
    ui.card(r);
    ui.cardTitle(r, "前面板指示灯", Icon::kBulb);
    static const int kPct[] = {5, 10, 15, 20, 30, 40, 50, 70, 100};
    const int pct = static_cast<int>(std::lround(u.ledBrightness * 100.0 / 255.0));
    int dn = -1, up = -1;
    for (int p : kPct) {
      if (p < pct) dn = p;
      if (p > pct && up < 0) up = p;
    }
    snprintf(buf, sizeof(buf), "%d%%", pct);
    ui.stepper(Rect{r.x + 16, r.y + 44, 180, 48}, buf, dn > 0, up > 0,
               cmdAction(setCmd("led_brightness", JsonValue(static_cast<int>(std::lround(dn * 2.55))))),
               cmdAction(setCmd("led_brightness", JsonValue(static_cast<int>(std::lround(up * 2.55))))));
    ui.button(Rect{r.x + r.w - 180, r.y + 36, 164, 56}, "关机", Icon::kPower, kRed, true,
              uiAction("dialog", static_cast<int>(Dialog::kShutdown)), 18);
  }
}

// ---------------------------------------------------------------------------
// Wi-Fi dialog with an on-screen keyboard
// ---------------------------------------------------------------------------

void renderWifiDialog(Ui& ui, const UiData& d) {
  Canvas& c = ui.c;
  const UiState& st = ui.st;
  const Rect r{36, 28, kW - 72, kH - 56};
  ui.card(r, kCardHi);
  drawIcon(c, Icon::kWifi, r.x + 36.f, r.y + 34.f, 26, kAccent);
  drawText(c, fontFace(22, true), r.x + 60, r.y + 18, "配置 Wi-Fi", kText);
  drawText(c, fontFace(14), r.x + 200, r.y + 26, "通过协处理器连接，只保存在 Nano 中", kText3);
  ui.iconButton(Rect{r.x + r.w - 58, r.y + 12, 44, 44}, Icon::kCross, true, uiAction("dialog", 0));

  // Fields.
  auto field = [&](int i, const char* label, const std::string& value) {
    const Rect f{r.x + 24, r.y + 70 + i * 62, 600, 54};
    const bool active = st.field == i;
    fillRoundRect(c, f.x, f.y, f.w, f.h, 10, kCard);
    strokeRoundRect(c, f.x, f.y, f.w, f.h, 10, active ? 2 : 1, active ? kAccent : kBorder);
    drawText(c, fontFace(18), f.x + 16, f.y + 14, label, kText2);
    const int x = drawText(c, fontFace(22, true), f.x + 120, f.y + 10, fitText(fontFace(22, true), value, 440), kText);
    if (active && (d.nowMs / 500) % 2 == 0) c.fillRect(x + 2, f.y + 14, 2, 28, kAccent);
    ui.hit(f, uiAction("kb_field", i));
  };
  field(0, "名称", st.ssid);
  field(1, "密码", st.showPassword ? st.password : std::string(st.password.size(), '*'));
  ui.chip(Rect{r.x + 636, r.y + 79, 92, 36}, st.showPassword ? "隐藏" : "显示", false, true,
          uiAction("kb_show"));
  JsonValue conn = JsonValue::makeObject();
  conn.set("cmd", "set_net");
  conn.set("ssid", st.ssid);
  conn.set("password", st.password);
  ui.button(Rect{r.x + r.w - 196, r.y + 70, 172, 54}, "连接", Icon::kCheck, kGreen, !st.ssid.empty(), cmdAction(conn),
            18);
  JsonValue off = JsonValue::makeObject();
  off.set("cmd", "set_net");
  off.set("ssid", "");
  off.set("password", "");
  ui.button(Rect{r.x + r.w - 196, r.y + 132, 172, 54}, "关闭 Wi-Fi", Icon::kNone, kNeutral, true, cmdAction(off), 18);

  // Keyboard.
  struct Key {
    std::string label, text;
    float units;
    const char* kind;  // "kb", "kb_shift", "kb_sym", "kb_back"
  };
  std::vector<std::vector<Key>> rows;
  auto chars = [](const std::string& s, bool upper) {
    std::vector<Key> v;
    for (char ch : s) {
      const char k = upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(ch))) : ch;
      v.push_back(Key{std::string(1, k), std::string(1, k), 1, "kb"});
    }
    return v;
  };
  if (!st.symbols) {
    rows.push_back(chars("1234567890", false));
    rows.push_back(chars("qwertyuiop", st.shift));
    rows.push_back(chars("asdfghjkl", st.shift));
    std::vector<Key> r3;
    r3.push_back(Key{"⇧", "", 1.5f, "kb_shift"});
    for (const Key& k : chars("zxcvbnm", st.shift)) r3.push_back(k);
    r3.push_back(Key{"⌫", "", 1.5f, "kb_back"});
    rows.push_back(r3);
    rows.push_back({Key{"符号", "", 1.5f, "kb_sym"}, Key{",", ",", 1, "kb"}, Key{".", ".", 1, "kb"},
                    Key{"空格", " ", 3.5f, "kb"}, Key{"-", "-", 1, "kb"}, Key{"_", "_", 1, "kb"},
                    Key{"@", "@", 1, "kb"}});
  } else {
    rows.push_back(chars("1234567890", false));
    rows.push_back(chars("!@#$%^&*()", false));
    rows.push_back(chars("~`-_=+[]{}", false));
    std::vector<Key> r3 = chars("\\|;:'\"<>", false);
    r3.push_back(Key{"⌫", "", 1.5f, "kb_back"});
    rows.push_back(r3);
    rows.push_back({Key{"字母", "", 1.5f, "kb_sym"}, Key{",", ",", 1, "kb"}, Key{".", ".", 1, "kb"},
                    Key{"空格", " ", 3.5f, "kb"}, Key{"/", "/", 1, "kb"}, Key{"?", "?", 1, "kb"}});
  }
  const int kx = r.x + 24, kw = r.w - 48, ky = r.y + 204, kh = 58, gap = 8;
  const float unit = (kw - 9 * gap) / 10.f;
  for (size_t ri = 0; ri < rows.size(); ++ri) {
    float units = 0;
    for (const Key& k : rows[ri]) units += k.units;
    const float rowW = units * unit + (rows[ri].size() - 1) * gap;
    float x = kx + (kw - rowW) / 2;
    const int y = ky + static_cast<int>(ri) * (kh + gap);
    for (const Key& k : rows[ri]) {
      const int w = static_cast<int>(k.units * unit + (k.units - 1) * gap);
      const Rect kr{static_cast<int>(x), y, w, kh};
      const bool special = std::string(k.kind) != "kb";
      const bool on = std::string(k.kind) == "kb_shift" && st.shift;
      uint32_t fill = special ? (on ? kAccent : kNeutral) : 0x22303F;
      if (ui.pressed(kr)) fill = mixColor(fill, 0xFFFFFF, 60);
      fillRoundRect(c, kr.x, kr.y, kr.w, kr.h, 8, fill);
      if (k.label == "⇧") {
        drawIcon(c, Icon::kShift, kr.x + kr.w / 2.f, kr.y + kh / 2.f, 24, kText);
      } else if (k.label == "⌫") {
        drawIcon(c, Icon::kBackspace, kr.x + kr.w / 2.f, kr.y + kh / 2.f, 28, kText);
      } else {
        const FontFace& f = fontFace(k.label.size() > 1 ? 18 : 22, k.label.size() == 1);
        drawTextCentered(c, f, kr.x + kr.w / 2, kr.y + (kh - f.lineHeight) / 2, k.label, kText);
      }
      ui.hit(kr, uiAction(k.kind, 0, k.text));
      x += w + gap;
    }
  }
}

}  // namespace vm
