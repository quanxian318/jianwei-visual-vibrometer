#pragma once
#include <Arduino.h>

#include <string>

// Persistent configuration in NVS (Preferences namespace "vibmon").
struct Settings {
  std::string wifiSsid;
  std::string wifiPassword;
  uint32_t linkBaud = 0;
  bool flowControl = true;
  std::string ntp1, ntp2, ntp3;

  void load();
  bool saveWifi(const std::string& ssid, const std::string& password);
  bool saveLink(uint32_t baud, bool flow);
};

extern Settings g_settings;
