// === FILE: DeviceManager.h ===
#pragma once
#include "Types.h"

namespace DeviceManager {
  void begin();
  void loopFast();   // опрос датчиков/ограничения помпы

  void setLight(bool on);
  void setPump(bool on);
  void setFan(bool on);
  void setDoorAngle(uint8_t angle); // 0-100 %

  void setSoilCalibration(uint16_t dry, uint16_t wet);
  void getSoilCalibration(uint16_t& dry, uint16_t& wet);
}
