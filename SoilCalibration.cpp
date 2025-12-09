// === FILE: SoilCalibration.cpp ===
#include "SoilCalibration.h"
#include "DeviceManager.h"
#include "Config.h"

void SoilCalibration::handleMode(const String& mode) {
  int raw = analogRead(Pins::SOIL_ANALOG);
  if (mode == "dry") {
    uint16_t wet, dry;
    DeviceManager::getSoilCalibration(dry, wet);
    dry = raw;
    DeviceManager::setSoilCalibration(dry, wet);
    Serial.printf("[SoilCal] Dry = %d\n", raw);
  } else if (mode == "wet") {
    uint16_t wet, dry;
    DeviceManager::getSoilCalibration(dry, wet);
    wet = raw;
    DeviceManager::setSoilCalibration(dry, wet);
    Serial.printf("[SoilCal] Wet = %d\n", raw);
  }
}
