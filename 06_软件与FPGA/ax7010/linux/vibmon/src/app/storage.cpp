#include "app/storage.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "app/report.h"
#include "util/clock.h"

namespace vm {

namespace {

JsonValue numOrNull(double v, bool ok) { return ok && std::isfinite(v) ? JsonValue(v) : JsonValue(); }

std::string fmt(double v) {
  char b[32];
  snprintf(b, sizeof(b), "%.6g", v);
  return b;
}

const char* levelKey(AlarmLevel l) {
  if (l == AlarmLevel::kUnknown) return "unknown";
  return l == AlarmLevel::kAlarm ? "alarm" : (l == AlarmLevel::kWarn ? "warn" : "normal");
}

}  // namespace

bool saveRecord(const std::string& dir, const RecordMeta& m, const MeasureResult& r,
                const std::vector<ImuSample>& imu, const std::vector<AdxlSample>& adxl, std::string& err) {
  const std::string path = dir + "/" + m.measurementId;
  if (!makeDirs(path)) {
    err = "mkdir " + path;
    return false;
  }
  const bool cal = r.mmPerPx > 0;
  const double k = cal ? r.mmPerPx : 1.0;

  // waveform.csv
  {
    std::ostringstream w;
    const char other = r.axis == 'x' ? 'y' : 'x';
    w << "t_s,disp_" << r.axis << "_px,disp_" << other << "_px" << (cal ? std::string(",disp_") + r.axis + "_mm" : "")
      << "\n";
    for (size_t i = 0; i < r.t.size(); ++i) {
      w << fmt(r.t[i]) << ',' << fmt(r.disp[i]) << ',' << fmt(i < r.dispOther.size() ? r.dispOther[i] : 0);
      if (cal) w << ',' << fmt(r.disp[i] * k);
      w << '\n';
    }
    if (!writeFileAtomic(path + "/waveform.csv", w.str())) {
      err = "write waveform.csv";
      return false;
    }
  }
  // spectrum.csv
  {
    std::ostringstream s;
    s << "f_hz,amp_px" << (cal ? ",amp_mm" : "") << "\n";
    for (size_t i = 0; i < r.spectrum.freq.size(); ++i) {
      s << fmt(r.spectrum.freq[i]) << ',' << fmt(r.spectrum.amp[i]);
      if (cal) s << ',' << fmt(r.spectrum.amp[i] * k);
      s << '\n';
    }
    if (!writeFileAtomic(path + "/spectrum.csv", s.str())) {
      err = "write spectrum.csv";
      return false;
    }
  }
  if (!imu.empty()) {
    std::ostringstream s;
    s << "t_s,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps,temp_c\n";
    for (const auto& v : imu) {
      s << fmt((v.monoNs - r.startMonoNs) * 1e-9) << ',' << fmt(v.ax) << ',' << fmt(v.ay) << ',' << fmt(v.az) << ','
        << fmt(v.gx) << ',' << fmt(v.gy) << ',' << fmt(v.gz) << ',' << fmt(v.tempC) << '\n';
    }
    writeFileAtomic(path + "/imu.csv", s.str());
  }
  if (!adxl.empty()) {
    std::ostringstream s;
    s << "t_s,x_g,y_g,z_g\n";
    for (const auto& v : adxl) {
      s << fmt((v.monoNs - r.startMonoNs) * 1e-9) << ',' << fmt(v.x) << ',' << fmt(v.y) << ',' << fmt(v.z) << '\n';
    }
    writeFileAtomic(path + "/reference_adxl345.csv", s.str());
  }

  JsonValue rec = JsonValue::makeObject();
  rec.set("schema", "vision-vibration-record-1");
  rec.set("measurement_id", m.measurementId);
  rec.set("device_id", m.deviceId);
  rec.set("point_id", m.pointId.empty() ? JsonValue() : JsonValue(m.pointId));
  rec.set("point_name", m.pointName.empty() ? JsonValue() : JsonValue(m.pointName));
  rec.set("nfc_uid_hex", m.nfcUid.empty() ? JsonValue() : JsonValue(m.nfcUid));
  rec.set("start_utc", m.timeValid ? JsonValue(formatUtcIso(m.startUtcMs)) : JsonValue());
  rec.set("start_local",
          m.timeValid ? JsonValue(formatLocal(m.startUtcMs, m.tzOffsetMin, "%Y-%m-%d %H:%M:%S")) : JsonValue());
  rec.set("time_valid", m.timeValid);
  rec.set("time_source", m.timeSource);
  rec.set("timezone_offset_min", m.tzOffsetMin);
  JsonValue mono = JsonValue::makeObject();
  mono.set("source", "CLOCK_MONOTONIC");
  mono.set("unit", "ns");
  mono.set("start", static_cast<long long>(r.startMonoNs));
  rec.set("monotonic_clock", mono);

  JsonValue cam = JsonValue::makeObject();
  cam.set("source", m.cameraSource);
  cam.set("width", r.frameWidth);
  cam.set("height", r.frameHeight);
  cam.set("fps_nominal", r.fpsNominal);
  cam.set("fps_measured", numOrNull(r.fpsMeasured, r.fpsMeasured > 0));
  cam.set("frame_interval_jitter_ms", r.jitterMs);
  cam.set("exposure_us", JsonValue());
  cam.set("frames", static_cast<unsigned long>(r.frames));
  cam.set("frames_dropped", static_cast<unsigned long>(r.dropped));
  rec.set("camera", cam);

  JsonValue roi = JsonValue::makeObject();
  roi.set("x", r.roi.x);
  roi.set("y", r.roi.y);
  roi.set("w", r.roi.w);
  roi.set("h", r.roi.h);
  rec.set("roi", roi);

  JsonValue calib = JsonValue::makeObject();
  calib.set("id", r.calibrationId.empty() ? JsonValue() : JsonValue(r.calibrationId));
  calib.set("axis", std::string(1, r.axis));
  calib.set("mm_per_pixel", cal ? JsonValue(r.mmPerPx) : JsonValue());
  calib.set("working_distance_mm", m.distanceValid ? JsonValue(m.distanceMm) : JsonValue());
  rec.set("calibration", calib);

  JsonValue res = JsonValue::makeObject();
  res.set("valid", r.valid);
  res.set("reason", r.valid ? JsonValue() : JsonValue(r.reason));
  res.set("unit", m.unit);
  res.set("dominant_frequency_hz", numOrNull(r.dominantHz, r.valid));
  res.set("displacement_peak_to_peak_px", numOrNull(r.ppPx, r.valid));
  res.set("displacement_rms_px", numOrNull(r.rmsPx, r.valid));
  res.set("spectral_peak_px", numOrNull(r.peakAmpPx, r.valid));
  res.set("displacement_peak_to_peak_mm", numOrNull(r.ppPx * k, r.valid && cal));
  res.set("displacement_rms_mm", numOrNull(r.rmsPx * k, r.valid && cal));
  res.set("spectral_peak_mm", numOrNull(r.peakAmpPx * k, r.valid && cal));
  res.set("definitions", "peak_to_peak and rms of the linearly detrended displacement; spectral_peak is the 0-peak "
                         "amplitude at the dominant frequency (Hann window, coherent gain corrected, single-sided)");
  res.set("window", "hann");
  res.set("level", levelKey(m.level));
  JsonValue th = JsonValue::makeObject();
  th.set("unit", "mm");
  th.set("warn_peak_to_peak", m.warnPp);
  th.set("alarm_peak_to_peak", m.alarmPp);
  res.set("thresholds", th);
  res.set("waveform_file", "waveform.csv");
  res.set("spectrum_file", "spectrum.csv");
  rec.set("result", res);

  JsonValue q = JsonValue::makeObject();
  q.set("bad_frames", static_cast<unsigned long>(r.badFrames));
  q.set("search_edge_hits", static_cast<unsigned long>(r.edgeHits));
  q.set("saturated_frames", static_cast<unsigned long>(r.saturatedFrames));
  q.set("min_ncc", numOrNull(r.minScore, r.frames > 0));
  rec.set("quality", q);

  JsonValue light = JsonValue::makeObject();
  light.set("on", m.lightOn);
  light.set("percent", m.lightPercent);
  rec.set("lighting", light);
  JsonValue laser = JsonValue::makeObject();
  laser.set("on", m.laserOn);
  laser.set("switched_off_for_capture", m.laserOffForCapture);
  rec.set("laser_projector", laser);

  JsonValue ref = JsonValue::makeObject();
  ref.set("model", "ADXL345");
  ref.set("available", !adxl.empty());
  ref.set("valid", false);  // register polling is not yet a qualified reference measurement
  ref.set("timing_validated", false);
  ref.set("reason", "sampling_timing_unqualified");
  ref.set("samples", static_cast<unsigned long>(adxl.size()));
  ref.set("timestamp_source", adxl.empty() ? JsonValue() : JsonValue("PL DRDY events or host polling; sample association unqualified"));
  ref.set("file", adxl.empty() ? JsonValue() : JsonValue("reference_adxl345.csv"));
  rec.set("reference", ref);
  JsonValue im = JsonValue::makeObject();
  im.set("model", "LSM6DSV");
  im.set("timing_validated", false);
  im.set("samples", static_cast<unsigned long>(imu.size()));
  im.set("file", imu.empty() ? JsonValue() : JsonValue("imu.csv"));
  rec.set("camera_imu", im);

  JsonValue env = JsonValue::makeObject();
  env.set("temperature_c", numOrNull(m.tempC, m.envValid));
  env.set("humidity_percent", numOrNull(m.rhPercent, m.envValid));
  env.set("distance_mm", m.distanceValid ? JsonValue(m.distanceMm) : JsonValue());
  env.set("valid", m.envValid);
  rec.set("environment", env);

  JsonValue ver = JsonValue::makeObject();
  ver.set("app", m.appVersion);
  ver.set("pl", m.plVersion);
  ver.set("nano", m.nanoVersion.empty() ? JsonValue() : JsonValue(m.nanoVersion));
  ver.set("hardware", "2026-10-20");
  rec.set("versions", ver);

  if (!writeFileAtomic(path + "/record.json", jsonPretty(rec))) {
    err = "write record.json";
    return false;
  }
  return true;
}

namespace {
bool readCsv(const std::string& file, int col, std::vector<double>& a, std::vector<double>& b) {
  std::ifstream f(file);
  if (!f) return false;
  std::string line;
  std::getline(f, line);  // header
  while (std::getline(f, line)) {
    std::vector<double> cells;
    std::stringstream ss(line);
    std::string cell;
    while (std::getline(ss, cell, ',')) cells.push_back(atof(cell.c_str()));
    if (static_cast<int>(cells.size()) <= col) continue;
    a.push_back(cells[0]);
    b.push_back(cells[col]);
  }
  return true;
}
}  // namespace

bool loadRecord(const std::string& dir, const std::string& id, JsonValue& record, std::vector<double>& t,
                std::vector<double>& disp, std::vector<double>& f, std::vector<double>& amp) {
  const std::string path = dir + "/" + id;
  std::string err;
  if (!loadJsonFile(path + "/record.json", record, &err)) return false;
  const bool mm = jStr(jObj(record, "result"), "unit", "px") == "mm";
  readCsv(path + "/waveform.csv", mm ? 3 : 1, t, disp);
  readCsv(path + "/spectrum.csv", mm ? 2 : 1, f, amp);
  return true;
}

std::string latestRecordId(const std::string& dir) {
  DIR* d = opendir(dir.c_str());
  if (!d) return std::string();
  std::string best;
  time_t bestTime = 0;
  while (dirent* e = readdir(d)) {
    if (e->d_name[0] == '.') continue;
    struct stat st;
    const std::string rec = dir + "/" + e->d_name + "/record.json";
    if (stat(rec.c_str(), &st) == 0 && (best.empty() || st.st_mtime >= bestTime)) {
      best = e->d_name;
      bestTime = st.st_mtime;
    }
  }
  closedir(d);
  return best;
}

std::vector<RecordSummary> listRecords(const std::string& dir, size_t maxCount) {
  std::vector<std::pair<time_t, std::string>> found;
  if (DIR* d = opendir(dir.c_str())) {
    while (dirent* e = readdir(d)) {
      if (e->d_name[0] == '.') continue;
      struct stat st;
      const std::string rec = dir + "/" + e->d_name + "/record.json";
      if (stat(rec.c_str(), &st) == 0) found.emplace_back(st.st_mtime, e->d_name);
    }
    closedir(d);
  }
  std::sort(found.begin(), found.end(), [](const std::pair<time_t, std::string>& a, const std::pair<time_t, std::string>& b) {
    return a.first != b.first ? a.first > b.first : a.second > b.second;
  });
  if (found.size() > maxCount) found.resize(maxCount);
  std::vector<RecordSummary> out;
  for (const auto& f : found) {
    JsonValue rec;
    if (!loadJsonFile(dir + "/" + f.second + "/record.json", rec, nullptr)) continue;
    const JsonValue& res = jObj(rec, "result");
    RecordSummary r;
    r.id = f.second;
    r.startLocal = jStr(rec, "start_local", "");
    r.pointId = jStr(rec, "point_id", "");
    r.pointName = jStr(rec, "point_name", "");
    r.valid = jBool(res, "valid", false);
    r.reason = jStr(res, "reason", "");
    r.unit = jStr(res, "unit", "px");
    const bool mm = r.unit == "mm";
    r.dominantHz = jNum(res, "dominant_frequency_hz", 0);
    r.peakToPeak = jNum(res, mm ? "displacement_peak_to_peak_mm" : "displacement_peak_to_peak_px", 0);
    r.rms = jNum(res, mm ? "displacement_rms_mm" : "displacement_rms_px", 0);
    const std::string lv = jStr(res, "level", "unknown");
    r.level = lv == "alarm" ? AlarmLevel::kAlarm
                            : (lv == "warn" ? AlarmLevel::kWarn : (lv == "normal" ? AlarmLevel::kNormal : AlarmLevel::kUnknown));
    out.push_back(r);
  }
  return out;
}

}  // namespace vm
