#include "ui/fb_ui.h"

#include <poll.h>

#include <algorithm>
#include <cstdlib>

#include "app/app.h"
#include "app/storage.h"
#include "util/clock.h"
#include "util/log.h"

namespace vm {

FbUi::~FbUi() {
  running_ = false;
  if (thread_.joinable()) thread_.join();
}

bool FbUi::start() {
  const AppConfig& cfg = app_.config();
  if (!display_.open(cfg.ui.fbdev)) {
    LOGW("ui", "no framebuffer; touch UI disabled (vibmonctl still works)");
    return false;
  }
  canvas_.resize(display_.width(), display_.height(), theme::kBg);
  if (touch_.open(cfg.ui.touchDev)) {
    touch_.setScreen(display_.width(), display_.height(), cfg.ui.swapXY, cfg.ui.invertX, cfg.ui.invertY);
  } else {
    LOGW("ui", "no touch input device found");
  }
  running_ = true;
  thread_ = std::thread(&FbUi::run, this);
  return true;
}

void FbUi::onEncoder(int detents) {
  std::lock_guard<std::mutex> lock(encMutex_);
  encSteps_ += detents;
}

void FbUi::onEncoderPress(bool longPress) {
  std::lock_guard<std::mutex> lock(encMutex_);
  ++(longPress ? encLong_ : encShort_);
}

JsonValue FbUi::applyAction(UiState& st, const UiAction& a, const Rect& region, int x, int y, const UiData& d,
                            int64_t nowMs) {
  (void)nowMs;
  if (a.kind == "cmd") return a.cmd;
  if (a.kind == "nav") {
    st.page = static_cast<Page>(std::max(0, std::min(kPageCount - 1, a.arg)));
    if (st.page == Page::kResult) st.openRecordId.clear();
  } else if (a.kind == "roi" || a.kind == "depth_probe") {
    const double nx = a.nx0 + (x - region.x + 0.5) / std::max(1, region.w) * a.nw;
    const double ny = a.ny0 + (y - region.y + 0.5) / std::max(1, region.h) * a.nh;
    if (a.kind == "depth_probe") {
      st.probeX = nx;
      st.probeY = ny;
      return JsonValue();
    }
    JsonValue cmd = JsonValue::makeObject();
    cmd.set("cmd", "roi");
    cmd.set("x", nx);
    cmd.set("y", ny);
    return cmd;
  } else if (a.kind == "hist_page") {
    st.historyPage = std::max(0, st.historyPage + a.arg);
  } else if (a.kind == "open_record") {
    st.openRecordId = a.text;
    st.page = Page::kResult;
  } else if (a.kind == "close_record") {
    st.openRecordId.clear();
    st.page = Page::kHistory;
  } else if (a.kind == "vision_view") {
    st.visionView = a.arg;
  } else if (a.kind == "depth_range") {
    st.depthRange = a.arg;
  } else if (a.kind == "live_heat") {
    st.liveHeat = !st.liveHeat;
  } else if (a.kind == "dialog") {
    st.dialog = static_cast<Dialog>(a.arg);
    if (st.dialog == Dialog::kWifi) {
      st.ssid = d.s.wifiSsid;
      st.password.clear();
      st.field = st.ssid.empty() ? 0 : 1;
      st.shift = st.symbols = st.showPassword = false;
    }
  } else if (a.kind == "kb_field") {
    st.field = a.arg;
  } else if (a.kind == "kb") {
    std::string& t = st.field == 0 ? st.ssid : st.password;
    const size_t maxLen = st.field == 0 ? 32 : 63;
    if (t.size() + a.text.size() <= maxLen) t += a.text;
    st.shift = false;
  } else if (a.kind == "kb_back") {
    std::string& t = st.field == 0 ? st.ssid : st.password;
    if (!t.empty()) t.pop_back();
  } else if (a.kind == "kb_shift") {
    st.shift = !st.shift;
  } else if (a.kind == "kb_sym") {
    st.symbols = !st.symbols;
    st.shift = false;
  } else if (a.kind == "kb_show") {
    st.showPassword = !st.showPassword;
  }
  return JsonValue();
}

void FbUi::toast(const std::string& text) {
  st_.toast = text;
  st_.toastUntilMs = monoMs() + 4000;
}

void FbUi::perform(const UiAction& a, const Rect& region, int x, int y, const UiData& d) {
  const JsonValue cmd = applyAction(st_, a, region, x, y, d, monoMs());
  dirty_ = true;
  if (cmd.isNull()) return;
  const std::string name = jStr(cmd, "cmd", "");
  const JsonValue reply = app_.command(cmd);
  if (!jBool(reply, "ok", false)) {
    const std::string err = jStr(reply, "error", "");
    LOGI("ui", "%s: %s", name.c_str(), err.c_str());
    toast(errorText(err));
    return;
  }
  if (name == "set_net") {
    st_.dialog = Dialog::kNone;
    toast(jStr(cmd, "ssid", "").empty() ? "已关闭 Wi-Fi" : "正在连接 " + jStr(cmd, "ssid", ""));
  } else if (name == "shutdown") {
    st_.dialog = Dialog::kNone;
  } else if (name == "roi") {
    toast("测点位置已更新");
  }
}

void FbUi::handleTouch(const TouchEvent& e, const UiData& d) {
  switch (e.type) {
    case TouchEvent::kDown: {
      fingerDown_ = true;
      moved_ = false;
      downX_ = e.x;
      downY_ = e.y;
      Rect r;
      const UiAction a = hitTest(hits_, e.x, e.y, &r);
      st_.pressing = !a.isNull();
      st_.pressed = r;
      dirty_ = true;
      break;
    }
    case TouchEvent::kMove:
      if (fingerDown_ && (std::abs(e.x - downX_) > 24 || std::abs(e.y - downY_) > 24)) {
        moved_ = true;
        if (st_.pressing) {
          st_.pressing = false;
          dirty_ = true;
        }
      }
      break;
    case TouchEvent::kUp: {
      if (!fingerDown_) break;
      fingerDown_ = false;
      st_.pressing = false;
      dirty_ = true;
      const int dx = e.x - downX_, dy = e.y - downY_;
      if (st_.dialog == Dialog::kNone && std::abs(dx) > 140 && std::abs(dx) > 2 * std::abs(dy)) {
        const int p = (static_cast<int>(st_.page) + (dx < 0 ? 1 : kPageCount - 1)) % kPageCount;
        UiAction nav = uiAction("nav", p);
        perform(nav, Rect(), 0, 0, d);
      } else if (!moved_ && std::abs(dx) <= 24 && std::abs(dy) <= 24) {
        Rect r;
        const UiAction a = hitTest(hits_, downX_, downY_, &r);
        if (!a.isNull()) perform(a, r, downX_, downY_, d);
      }
      break;
    }
  }
}

void FbUi::handleEncoder() {
  int steps, shorts, longs;
  {
    std::lock_guard<std::mutex> lock(encMutex_);
    steps = encSteps_;
    shorts = encShort_;
    longs = encLong_;
    encSteps_ = encShort_ = encLong_ = 0;
  }
  if (!steps && !shorts && !longs) return;
  dirty_ = true;
  if (st_.dialog != Dialog::kNone) return;
  for (int i = 0; i < longs; ++i) {
    st_.page = static_cast<Page>((static_cast<int>(st_.page) + 1) % kPageCount);
    if (st_.page == Page::kResult) st_.openRecordId.clear();
  }
  if (st_.page == Page::kLive) {
    if (steps) {
      JsonValue z = JsonValue::makeObject();
      z.set("cmd", "zoom");
      z.set("steps", steps);
      app_.command(z);
    }
    if (shorts) {
      JsonValue z = JsonValue::makeObject();
      z.set("cmd", "zoom_reset");
      app_.command(z);
    }
  } else {
    if (st_.page == Page::kHistory && steps) st_.historyPage = std::max(0, st_.historyPage + steps);
    if (shorts) st_.page = Page::kLive;
  }
}

void FbUi::gather(UiData& d) {
  const AppConfig& cfg = app_.config();
  d.s = app_.snapshot();
  d.live = app_.cameraLive();
  d.depth = app_.depthLive();
  d.nowMs = monoMs();
  d.wallMs = realtimeMs();
  d.tzOffsetMin = cfg.tzOffsetMin;
  if (st_.page == Page::kHistory &&
      (d.nowMs - recordsMs_ > 3000 || recordsKey_ != d.s.last.measurementId)) {
    records_ = listRecords(cfg.recordsDir, 200);
    recordsMs_ = d.nowMs;
    recordsKey_ = d.s.last.measurementId;
  }
  d.records = records_;
  const std::string want = !st_.openRecordId.empty() ? st_.openRecordId
                                                     : (d.s.last.saved ? d.s.last.measurementId : std::string());
  if (st_.page == Page::kResult && !want.empty() && record_.id != want) {
    record_ = RecordView();
    record_.id = want;
    record_.loaded = loadRecord(cfg.recordsDir, want, record_.rec, record_.t, record_.disp, record_.f, record_.amp);
  }
  d.record = record_;
}

void FbUi::run() {
  UiData d;
  int64_t nextFrame = 0;
  std::vector<TouchEvent> events;
  while (running_) {
    const int64_t now = monoMs();
    if (now >= nextFrame || dirty_) {
      gather(d);
      handleEncoder();
      renderUi(canvas_, st_, d, hits_);
      display_.present(canvas_);
      dirty_ = false;
      if (now >= nextFrame) nextFrame = now + 125;  // ~8 fps; touch feedback draws in between
    }
    const int wait = static_cast<int>(std::max<int64_t>(0, nextFrame - monoMs()));
    if (touch_.fd() >= 0) {
      pollfd p{touch_.fd(), POLLIN, 0};
      if (poll(&p, 1, std::min(wait, 125)) > 0) {
        events.clear();
        touch_.read(events);
        for (const TouchEvent& e : events) handleTouch(e, d);
      }
    } else {
      sleepMs(std::min(wait, 125));
    }
  }
}

}  // namespace vm
