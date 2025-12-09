// === FILE: TelemetryLogger.cpp ===
#include "TelemetryLogger.h"
#include "Globals.h"

// Здесь намеренно используем собственную структуру, не завязанную
// на Types.h::TelemetryPoint, чтобы не ломать другие модули.
namespace {

  struct TelemetrySample {
    uint32_t ts;          // UNIX time (секунды)
    float    airTemp;     // °C
    float    airHum;      // %
    float    soilMoisture;// %
    float    soilTemp;    // °C
    float    airPressure; // гПа
    float    lux;         // лк
  };

  // 288 точек по 5 минут = ровно сутки
  constexpr uint16_t MAX_POINTS       = 288;
  constexpr uint32_t LOG_INTERVAL_MS  = 5UL * 60UL * 1000UL;

  TelemetrySample buf[MAX_POINTS];
  uint16_t head   = 0;
  uint16_t count  = 0;

  uint32_t lastLogMs = 0;
}

void TelemetryLogger::begin() {
  head      = 0;
  count     = 0;
  lastLogMs = millis();
}

void TelemetryLogger::loop() {
  uint32_t now = millis();
  if (now - lastLogMs < LOG_INTERVAL_MS) return;
  lastLogMs = now;

  TelemetrySample p{};
  p.ts           = now / 1000;
  p.airTemp      = g_sensors.airTemp;
  p.airHum       = g_sensors.airHum;
  p.soilMoisture = g_sensors.soilMoisture;
  p.soilTemp     = g_sensors.soilTemp;
  p.airPressure  = g_sensors.airPressure;
  p.lux          = g_sensors.lux;

  buf[head] = p;
  head = (head + 1) % MAX_POINTS;
  if (count < MAX_POINTS) {
    count++;
  }
}

void TelemetryLogger::exportJson(String& out) {
  out.reserve(4096);
  out = "[";

  for (uint16_t i = 0; i < count; ++i) {
    uint16_t idx = (head + MAX_POINTS - count + i) % MAX_POINTS;
    const TelemetrySample& p = buf[idx];

    if (i > 0) out += ",";

    out += "{";
    out += "\"ts\":"           + String(p.ts);
    out += ",\"airTemp\":"     + String(p.airTemp, 2);
    out += ",\"airHum\":"      + String(p.airHum, 2);
    out += ",\"soilMoisture\":"+ String(p.soilMoisture, 2);
    out += ",\"soilTemp\":"    + String(p.soilTemp, 2);
    out += ",\"airPressure\":" + String(p.airPressure, 2);
    out += ",\"lux\":"         + String(p.lux, 2);
    out += "}";
  }

  out += "]";
}