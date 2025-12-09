// === FILE: Diagnostics.cpp ===
#include "Diagnostics.h"
#include "Globals.h"
#include "TelegramAsync.h"
#include <Arduino.h>

namespace {
  uint32_t lastDiagMs = 0;
  const uint32_t DIAG_INTERVAL_MS = 10000;
  bool bmeAlertSent = false;
  bool bhAlertSent  = false;
}

void Diagnostics::begin() {
  lastDiagMs = millis();
}

void Diagnostics::loop() {
  uint32_t now = millis();
  if (now - lastDiagMs < DIAG_INTERVAL_MS) return;
  lastDiagMs = now;

  if (!g_sensors.bmeOk && !bmeAlertSent) {
    TelegramAsync::sendAlert("BME280 не найден");
    bmeAlertSent = true;
  }
  if (!g_sensors.bhOk && !bhAlertSent) {
    TelegramAsync::sendAlert("BH1750 не найден");
    bhAlertSent = true;
  }

  if (!isnan(g_sensors.airTemp)) {
    if (g_sensors.airTemp > g_settings.safetyTempMax + 2) {
      TelegramAsync::sendAlert("Перегрев теплицы!");
    }
    if (g_sensors.airTemp < g_settings.safetyTempMin - 2) {
      TelegramAsync::sendAlert("Переохлаждение теплицы!");
    }
  }
}
