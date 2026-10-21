#pragma once

#include <cstdint>

#include "app/board_service.h"
#include "app/config.h"
#include "app/state.h"

namespace vm {

// External tower light and buzzer (J302 via TCA P1.0..P1.3). Keeps three
// separate facts: the level from the latest result, whether it was muted with
// K101, and the physical outputs. Muting silences the buzzer only; the light
// stays until a later result clears the condition.
class AlarmService {
 public:
  AlarmService(BoardService& board, const AppConfig::Alarm& cfg) : board_(board), cfg_(cfg) {}
  void setLevel(AlarmLevel level, int64_t nowMs);
  void mute() { muted_ = true; }
  void suspend(bool on) { suspended_ = on; }  // power fail / shutdown: everything off
  void tick(int64_t nowMs);
  AlarmLevel level() const { return level_; }
  bool muted() const { return muted_; }
  static AlarmLevel classify(double peakToPeak, double warn, double alarm) {
    return peakToPeak >= alarm ? AlarmLevel::kAlarm : (peakToPeak >= warn ? AlarmLevel::kWarn : AlarmLevel::kNormal);
  }

 private:
  BoardService& board_;
  const AppConfig::Alarm& cfg_;
  AlarmLevel level_ = AlarmLevel::kUnknown;
  bool muted_ = false;
  bool suspended_ = false;
  int64_t levelSinceMs_ = 0;
  int lastOut_ = -1;
};

}  // namespace vm
