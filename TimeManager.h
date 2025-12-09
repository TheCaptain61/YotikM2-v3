#pragma once
#include <Arduino.h>

namespace TimeManager {
  void begin();
  void syncTimeAsync();
  bool isTimeValid();
  bool getLocalTimeSafe(struct tm& out);
  uint8_t getHour();
  void loadTimeFromRTCIfNeeded();
  void saveTimeToRTC();
}