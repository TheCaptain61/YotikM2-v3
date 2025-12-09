// === FILE: TelemetryLogger.h ===
#pragma once
#include <Arduino.h>

namespace TelemetryLogger {
  void begin();
  void loop();

  // Отдаём историю в виде JSON-массива:
  // [{ts, airTemp, airHum, soilMoisture, soilTemp, airPressure, lux}, ...]
  void exportJson(String& out);
}