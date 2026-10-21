// Offline harness for the unchanged production NccTracker/analyzeCapture.
// stdin: VIS1 header + decoded gray frames. PTS text: seconds, one per frame.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include "measure/session.h"
#include "util/json_util.h"

struct Target {
  std::string name;
  vm::Roi roi;
  vm::NccTracker tracker;
  bool initialized = false;
  std::vector<vm::CaptureSample> samples;
};

int main(int argc, char** argv) {
  if (argc < 5) return 2;
  std::ifstream times(argv[1]);
  std::filesystem::path out(argv[2]);
  std::filesystem::create_directories(out);
  const int radius = std::atoi(argv[3]);
  std::vector<Target> targets;
  for (int i = 4; i < argc; ++i) {
    Target t;
    char name[64];
    if (std::sscanf(argv[i], "%63[^:]:%d:%d:%d", name, &t.roi.x, &t.roi.y, &t.roi.w) != 4) return 2;
    t.name = name;
    t.roi.h = t.roi.w;
    if (t.name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos) return 2;
    targets.push_back(std::move(t));
  }
  unsigned char header[18];
  if (fread(header, 1, 18, stdin) != 18 || std::string(reinterpret_cast<char*>(header), 4) != "VIS1") return 2;
  auto u16 = [&](int k) { return header[k] | (header[k + 1] << 8); };
  const int width = u16(4), height = u16(6), frames = u16(8);
  vm::GrayFrame f;
  f.width = width;
  f.height = height;
  f.pixels.resize(static_cast<size_t>(width) * height);
  for (int i = 0; i < frames; ++i) {
    double ts;
    if (!(times >> ts) || fread(f.pixels.data(), 1, f.pixels.size(), stdin) != f.pixels.size()) return 3;
    f.sequence = i;
    f.timestampNs = static_cast<int64_t>(ts * 1e9 + .5);
    for (auto& t : targets) {
      vm::CaptureSample s;
      s.tNs = f.timestampNs;
      s.seq = i;
      if (i == 0) {
        t.initialized = t.tracker.init(f, t.roi, radius);
        s.ok = t.initialized;
        s.score = t.initialized ? 1 : 0;
      } else if (t.initialized) {
        const auto r = t.tracker.track(f);
        s.ok = r.ok;
        s.dx = r.dx;
        s.dy = r.dy;
        s.score = r.score;
        s.edge = r.atEdge;
        s.saturated = r.saturated;
      }
      t.samples.push_back(s);
    }
  }
  vm::JsonValue summary = vm::JsonValue::makeArray();
  for (auto& t : targets) {
    std::ofstream csv(out / (t.name + "_samples.csv"));
    csv << "t_s,dx_px,dy_px,ncc,ok,edge,saturated\n" << std::setprecision(12);
    for (const auto& s : t.samples)
      csv << (s.tNs - t.samples.front().tNs) * 1e-9 << ',' << s.dx << ',' << s.dy << ',' << s.score << ','
          << s.ok << ',' << s.edge << ',' << s.saturated << '\n';
    for (char axis : {'x', 'y'}) {
      vm::MeasureConfig cfg;
      cfg.axis = axis;
      const auto r = vm::analyzeCapture(t.samples, cfg, 30);
      vm::JsonValue j = vm::JsonValue::makeObject();
      j.set("name", t.name);
      j.set("axis", std::string(1, axis));
      j.set("initialized", t.initialized);
      j.set("roi_x", t.roi.x); j.set("roi_y", t.roi.y); j.set("roi_size", t.roi.w);
      j.set("radius", radius); j.set("valid", r.valid); j.set("reason", r.reason);
      j.set("frames", static_cast<int>(r.frames)); j.set("fps", r.fpsMeasured);
      j.set("bad_frames", static_cast<int>(r.badFrames)); j.set("edge_hits", static_cast<int>(r.edgeHits));
      j.set("saturated_frames", static_cast<int>(r.saturatedFrames));
      j.set("dropped", static_cast<int>(r.dropped)); j.set("jitter_ms", r.jitterMs);
      j.set("min_ncc", r.minScore);
      j.set("dominant_hz", r.valid ? vm::JsonValue(r.dominantHz) : vm::JsonValue());
      j.set("peak_to_peak_px", r.valid ? vm::JsonValue(r.ppPx) : vm::JsonValue());
      j.set("rms_px", r.valid ? vm::JsonValue(r.rmsPx) : vm::JsonValue());
      if (r.valid) {
        std::ofstream spec(out / (t.name + "_" + axis + "_spectrum.csv"));
        spec << "frequency_hz,amplitude_px\n" << std::setprecision(12);
        for (size_t k = 0; k < r.spectrum.freq.size(); ++k)
          spec << r.spectrum.freq[k] << ',' << r.spectrum.amp[k] << '\n';
      }
      summary.push(j);
    }
  }
  std::ofstream result(out / "tracking.json");
  result << vm::jsonPretty(summary);
  return 0;
}
