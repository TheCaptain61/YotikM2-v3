// === FILE: Automation.cpp ===
#include "Automation.h"
#include "Globals.h"
#include "Config.h"
#include "TimeManager.h"
#include "DeviceManager.h"
#include "Storage.h"
#include "SunPosition.h"

#include <math.h>

// Многоуровневая автоматика с адаптацией и диагностикой.

namespace {

constexpr uint32_t MANUAL_HOLD_MS      = 5UL * 60UL * 1000UL; // 5 минут
constexpr uint32_t STATS_UPDATE_MIN_MS = 10UL * 1000UL;

// Ограничения safety для насоса (можно подправить при желании в коде)
constexpr uint32_t PUMP_MAX_DAY_MS     = 20UL * 60UL * 1000UL; // суммарно 20 мин за 24ч
constexpr uint32_t PUMP_MAX_RUN_MS     = 5UL  * 60UL * 1000UL; // одна сессия не более 5 мин

template<typename T>
T clampT(T v, T lo, T hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

float lerp(float a, float b, float t) {
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  return a + (b - a) * t;
}

// ---------- ручной режим ----------

uint32_t manualPumpUntil  = 0;
uint32_t manualLightUntil = 0;
uint32_t manualFanUntil   = 0;
uint32_t manualDoorUntil  = 0;

bool isManualActive(uint32_t until) {
  return (until != 0 && millis() < until);
}

// ---------- свет / lux ----------

float    luxFiltered       = NAN;
uint32_t lightLastToggleMs = 0;

struct LightAdaptiveState {
  float dynamicLuxOn       = AutomationConfig::LIGHT_LUX_ON_THRESHOLD;
  float dynamicLuxOff      = AutomationConfig::LIGHT_LUX_OFF_THRESHOLD;
  float dailyLuxIntegral   = 0.0f;
  uint32_t lastSampleMs    = 0;
  uint32_t lastTuneMs      = 0;
};

LightAdaptiveState g_light;

// ---------- safety насоса ----------

struct SafetyState {
  uint32_t pumpWindowMs      = 0;  // накопленное время работы насоса в окне
  uint32_t pumpWindowStartMs = 0;  // начало окна
  uint32_t pumpLastStateMs   = 0;  // время последнего обновления
  bool     pumpLastStateOn   = false;
  uint32_t pumpRunStartMs    = 0;  // когда текущий запуск начался
  bool     pumpLocked        = false;
};

SafetyState g_safety;

// ---------- история климата ----------

struct ClimateHistory {
  float lastAirTemp     = NAN;
  float lastAirHum      = NAN;
  uint32_t lastSampleMs = 0;
  float dTdt            = 0.0f; // °C/час
  float dHdt            = 0.0f; // %/час
};

ClimateHistory g_climateHist;

// ---------- статистика полива ----------

struct WateringStats {
  bool     lastPumpOn         = false;
  uint32_t lastPumpToggleMs   = 0;

  float lastBeforeMoisture    = NAN;
  float lastAfterMoisture     = NAN;

  float avgDelta              = NAN; // средний прирост влажности за полив
  float avgDrySpeed           = NAN; // средняя скорость высыхания, %/ч

  uint32_t lastDrySampleMs    = 0;
  float    lastDryMoisture    = NAN;
};

WateringStats g_waterStats;

// ---------- стресс-индекс ----------

struct StressState {
  float tempStress   = 0.0f;
  float humStress    = 0.0f;
  float soilStress   = 0.0f;
  float lightStress  = 0.0f;
  float totalStress  = 0.0f;
};

StressState g_stress;

// ---------- адаптивные параметры и их диапазоны ----------

struct AdaptiveParams {
  float soilSetpointOffset = 0.0f; // -X..+Y % к setpoint'у почвы
  float luxOnOffset        = 0.0f; // -X..+Y лк к порогу включения света
  float luxOffOffset       = 0.0f; // -X..+Y лк к порогу выключения света
};

AdaptiveParams g_adapt;

struct AdaptLimits {
  float soilOffsetMin = -10.0f;
  float soilOffsetMax =  10.0f;
  float luxOffsetMin  = -20.0f;
  float luxOffsetMax  =  20.0f;
};

AdaptLimits g_limits;

// ---------- профили культур ----------

void applyCropProfile() {
  switch (g_settings.cropProfile) {
    case CropProfile::Tomatoes:
      g_settings.comfortTempMin       = 22.0f;
      g_settings.comfortTempMax       = 28.0f;
      g_settings.comfortHumMin        = 55.0f;
      g_settings.comfortHumMax        = 75.0f;
      g_settings.soilMoistureSetpoint = 65;
      g_settings.soilMoistureHyst     = 5;
      g_settings.waterStartHour       = 7;
      g_settings.waterEndHour         = 20;
      break;

    case CropProfile::Cucumbers:
      g_settings.comfortTempMin       = 23.0f;
      g_settings.comfortTempMax       = 29.0f;
      g_settings.comfortHumMin        = 65.0f;
      g_settings.comfortHumMax        = 85.0f;
      g_settings.soilMoistureSetpoint = 70;
      g_settings.soilMoistureHyst     = 5;
      g_settings.waterStartHour       = 6;
      g_settings.waterEndHour         = 21;
      break;

    case CropProfile::Greens:
      g_settings.comfortTempMin       = 18.0f;
      g_settings.comfortTempMax       = 24.0f;
      g_settings.comfortHumMin        = 50.0f;
      g_settings.comfortHumMax        = 70.0f;
      g_settings.soilMoistureSetpoint = 60;
      g_settings.soilMoistureHyst     = 5;
      g_settings.waterStartHour       = 7;
      g_settings.waterEndHour         = 19;
      break;

    case CropProfile::Hibiscus:
      g_settings.comfortTempMin       = 20.0f;
      g_settings.comfortTempMax       = 26.0f;
      g_settings.comfortHumMin        = 45.0f;
      g_settings.comfortHumMax        = 65.0f;
      g_settings.soilMoistureSetpoint = 55;
      g_settings.soilMoistureHyst     = 5;
      g_settings.waterStartHour       = 8;
      g_settings.waterEndHour         = 19;
      break;

    case CropProfile::Custom:
    default:
      break;
  }
}

// ---------- окно полива ----------

bool isWithinWaterWindowInternal() {
  uint8_t h      = TimeManager::getHour();
  uint8_t startH = g_settings.waterStartHour;
  uint8_t endH   = g_settings.waterEndHour;

  if (startH == endH) {
    return true;
  }
  if (startH < endH) {
    return (h >= startH && h < endH);
  } else {
    return (h >= startH || h < endH);
  }
}

// ---------- safety насоса ----------

void updatePumpSafety() {
  uint32_t now = millis();
  bool pumpNow = g_sensors.pumpOn;

  if (g_safety.pumpWindowStartMs == 0) {
    g_safety.pumpWindowStartMs = now;
    g_safety.pumpLastStateMs   = now;
    g_safety.pumpLastStateOn   = pumpNow;
    g_safety.pumpRunStartMs    = pumpNow ? now : 0;
  }

  // скользящее окно ~24 часа
  if (now - g_safety.pumpWindowStartMs > 24UL * 60UL * 60UL * 1000UL) {
    g_safety.pumpWindowStartMs = now;
    g_safety.pumpWindowMs      = 0;
    g_safety.pumpLocked        = false;
  }

  uint32_t dt = now - g_safety.pumpLastStateMs;
  g_safety.pumpLastStateMs = now;

  // накапливаем суммарное время работы насоса
  if (g_safety.pumpLastStateOn) {
    g_safety.pumpWindowMs += dt;
  }

  // отследим момент включения насоса
  if (pumpNow && !g_safety.pumpLastStateOn) {
    g_safety.pumpRunStartMs = now;
  }

  g_safety.pumpLastStateOn = pumpNow;

  // лимит по суммарному времени за сутки
  if (!g_safety.pumpLocked &&
      g_safety.pumpWindowMs > PUMP_MAX_DAY_MS) {
    g_safety.pumpLocked = true;
  }

  // лимит по одной сессии работы
  if (!g_safety.pumpLocked &&
      pumpNow &&
      g_safety.pumpRunStartMs != 0 &&
      (now - g_safety.pumpRunStartMs > PUMP_MAX_RUN_MS)) {
    g_safety.pumpLocked = true;
  }

  if (g_safety.pumpLocked && pumpNow) {
    DeviceManager::setPump(false);
  }
}

// ---------- история климата ----------

void updateClimateHistory() {
  uint32_t now = millis();

  if (isnan(g_sensors.airTemp) || isnan(g_sensors.airHum)) {
    g_climateHist.lastSampleMs = now;
    g_climateHist.lastAirTemp  = g_sensors.airTemp;
    g_climateHist.lastAirHum   = g_sensors.airHum;
    return;
  }

  if (g_climateHist.lastSampleMs == 0) {
    g_climateHist.lastSampleMs = now;
    g_climateHist.lastAirTemp  = g_sensors.airTemp;
    g_climateHist.lastAirHum   = g_sensors.airHum;
    return;
  }

  uint32_t dtMs = now - g_climateHist.lastSampleMs;
  if (dtMs < STATS_UPDATE_MIN_MS) return;

  float dtHours = float(dtMs) / 3600000.0f;
  if (dtHours <= 0.0f) dtHours = 0.0001f;

  g_climateHist.dTdt =
    (g_sensors.airTemp - g_climateHist.lastAirTemp) / dtHours;
  g_climateHist.dHdt =
    (g_sensors.airHum  - g_climateHist.lastAirHum)  / dtHours;

  g_climateHist.lastAirTemp    = g_sensors.airTemp;
  g_climateHist.lastAirHum     = g_sensors.airHum;
  g_climateHist.lastSampleMs   = now;
}

// ---------- статистика полива ----------

void updateWateringStatsOnPumpToggle() {
  bool pumpNow = g_sensors.pumpOn;
  uint32_t now = millis();

  if (g_waterStats.lastPumpToggleMs == 0) {
    g_waterStats.lastPumpToggleMs = now;
    g_waterStats.lastPumpOn       = pumpNow;
  }

  if (pumpNow != g_waterStats.lastPumpOn) {
    if (pumpNow) {
      // насос только что включился
      g_waterStats.lastBeforeMoisture = g_sensors.soilMoisture;
    } else {
      // насос только что выключился
      g_waterStats.lastAfterMoisture = g_sensors.soilMoisture;
      if (!isnan(g_waterStats.lastBeforeMoisture) &&
          !isnan(g_waterStats.lastAfterMoisture)) {
        float delta = g_waterStats.lastAfterMoisture -
                      g_waterStats.lastBeforeMoisture;
        if (!isnan(g_waterStats.avgDelta)) {
          g_waterStats.avgDelta = lerp(g_waterStats.avgDelta, delta, 0.3f);
        } else {
          g_waterStats.avgDelta = delta;
        }
      }
    }

    g_waterStats.lastPumpOn       = pumpNow;
    g_waterStats.lastPumpToggleMs = now;
  }
}

void updateDryingStats() {
  if (isnan(g_sensors.soilMoisture)) return;

  uint32_t now = millis();
  if (g_waterStats.lastDrySampleMs == 0) {
    g_waterStats.lastDrySampleMs = now;
    g_waterStats.lastDryMoisture = g_sensors.soilMoisture;
    return;
  }

  uint32_t dtMs = now - g_waterStats.lastDrySampleMs;
  if (dtMs < 5UL * 60UL * 1000UL) return; // раз в ~5 минут

  float dtHours = float(dtMs) / 3600000.0f;
  if (dtHours <= 0.0f) dtHours = 0.0001f;

  float d = g_waterStats.lastDryMoisture - g_sensors.soilMoisture;
  if (d > 0.0f) {
    float speed = d / dtHours; // %/час
    if (!isnan(g_waterStats.avgDrySpeed)) {
      g_waterStats.avgDrySpeed = lerp(g_waterStats.avgDrySpeed, speed, 0.3f);
    } else {
      g_waterStats.avgDrySpeed = speed;
    }
  }

  g_waterStats.lastDrySampleMs = now;
  g_waterStats.lastDryMoisture = g_sensors.soilMoisture;
}

// ---------- адаптация по поливу ----------

void adaptiveTuneWatering() {
  if (isnan(g_waterStats.avgDrySpeed) ||
      isnan(g_waterStats.avgDelta)) {
    return;
  }

  const float FAST_DRY_SPEED = 8.0f; // %/час
  const float SLOW_DRY_SPEED = 3.0f; // %/час

  float offset = g_adapt.soilSetpointOffset;

  if (g_waterStats.avgDrySpeed > FAST_DRY_SPEED) {
    offset += 0.3f; // почва быстро высыхает — держим чуть влажнее
  } else if (g_waterStats.avgDrySpeed < SLOW_DRY_SPEED) {
    offset -= 0.3f; // долго мокрая — держим чуть суше
  }

  offset = clampT(offset, g_limits.soilOffsetMin, g_limits.soilOffsetMax);
  g_adapt.soilSetpointOffset = offset;
}

// ---------- статистика света и адаптация ----------

void updateLightStats() {
  uint32_t now = millis();

  if (g_light.lastSampleMs == 0) {
    g_light.lastSampleMs = now;
    return;
  }

  uint32_t dtMs = now - g_light.lastSampleMs;
  if (dtMs < STATS_UPDATE_MIN_MS) return;

  g_light.lastSampleMs = now;

  if (!isnan(g_sensors.lux)) {
    float dtHours = float(dtMs) / 3600000.0f;
    g_light.dailyLuxIntegral += g_sensors.lux * dtHours;
  }

  // примерно раз в час делаем подстройку порогов
  if (now - g_light.lastTuneMs > 60UL * 60UL * 1000UL) {
    g_light.lastTuneMs = now;

    const float TARGET_LOW  = 50000.0f;
    const float TARGET_HIGH = 120000.0f;

    if (g_light.dailyLuxIntegral < TARGET_LOW) {
      // света мало — снижаем порог включения
      g_adapt.luxOnOffset  -= 2.0f;
      g_adapt.luxOffOffset -= 1.0f;
    } else if (g_light.dailyLuxIntegral > TARGET_HIGH) {
      // света много — чуть уменьшаем досветку
      g_adapt.luxOnOffset  += 2.0f;
      g_adapt.luxOffOffset += 1.0f;
    }

    g_adapt.luxOnOffset  = clampT(g_adapt.luxOnOffset,
                                  g_limits.luxOffsetMin,
                                  g_limits.luxOffsetMax);
    g_adapt.luxOffOffset = clampT(g_adapt.luxOffOffset,
                                  g_limits.luxOffsetMin,
                                  g_limits.luxOffsetMax);

    g_light.dynamicLuxOn  =
      AutomationConfig::LIGHT_LUX_ON_THRESHOLD  + g_adapt.luxOnOffset;
    g_light.dynamicLuxOff =
      AutomationConfig::LIGHT_LUX_OFF_THRESHOLD + g_adapt.luxOffOffset;

    g_light.dailyLuxIntegral = 0.0f;
  }
}

// ---------- стресс ----------

void updateStress() {
  const float t  = g_sensors.airTemp;
  const float h  = g_sensors.airHum;
  const float sm = g_sensors.soilMoisture;
  const float lx = g_sensors.lux;

  if (!isnan(t)) {
    if (t < g_settings.comfortTempMin) {
      g_stress.tempStress += (g_settings.comfortTempMin - t) * 0.1f;
    } else if (t > g_settings.comfortTempMax) {
      g_stress.tempStress += (t - g_settings.comfortTempMax) * 0.1f;
    } else {
      g_stress.tempStress *= 0.95f;
    }
  }

  if (!isnan(h)) {
    if (h < g_settings.comfortHumMin) {
      g_stress.humStress += (g_settings.comfortHumMin - h) * 0.05f;
    } else if (h > g_settings.comfortHumMax) {
      g_stress.humStress += (h - g_settings.comfortHumMax) * 0.05f;
    } else {
      g_stress.humStress *= 0.95f;
    }
  }

  if (!isnan(sm)) {
    float sp = g_settings.soilMoistureSetpoint +
               g_adapt.soilSetpointOffset;
    sp = clampT(sp, 30.0f, 90.0f);

    if (sm < sp - 15.0f) {
      g_stress.soilStress += (sp - sm) * 0.08f;
    } else if (sm > sp + 15.0f) {
      g_stress.soilStress += (sm - sp) * 0.08f;
    } else {
      g_stress.soilStress *= 0.95f;
    }
  }

  if (!isnan(lx)) {
    if (!SunPosition::isDaylight()) {
      g_stress.lightStress *= 0.98f;
    } else {
      if (lx < 1000.0f) {
        g_stress.lightStress += (1000.0f - lx) * 0.0005f;
      } else if (lx > 40000.0f) {
        g_stress.lightStress += (lx - 40000.0f) * 0.00002f;
      } else {
        g_stress.lightStress *= 0.97f;
      }
    }
  }

  g_stress.totalStress =
    g_stress.tempStress +
    g_stress.humStress  +
    g_stress.soilStress +
    g_stress.lightStress;

  if (g_stress.totalStress > 300.0f) {
    g_adapt.soilSetpointOffset *= 0.99f;
  }
}

// ---------- ночь по солнцу ----------

bool isNightBySun() {
  return !SunPosition::isDaylight();
}

} // namespace

// -----------------------------------------------------------------------------
// PUBLIC
// -----------------------------------------------------------------------------

void Automation::begin() {
  applyCropProfile();

  luxFiltered       = NAN;
  lightLastToggleMs = 0;

  g_light.dynamicLuxOn  = AutomationConfig::LIGHT_LUX_ON_THRESHOLD;
  g_light.dynamicLuxOff = AutomationConfig::LIGHT_LUX_OFF_THRESHOLD;

  g_safety       = SafetyState{};
  g_climateHist  = ClimateHistory{};
  g_waterStats   = WateringStats{};
  g_stress       = StressState{};
  g_adapt        = AdaptiveParams{};
  g_limits       = AdaptLimits{}; // вернёт значения по умолчанию
}

void Automation::stepCritical() {
  if (!g_settings.automationEnabled) return;

  if (!isnan(g_sensors.airTemp)) {
    if (g_sensors.airTemp > 40.0f) {
      DeviceManager::setFan(true);
      DeviceManager::setDoorAngle(100);
    }
    if (g_sensors.airTemp < 5.0f) {
      DeviceManager::setFan(false);
      DeviceManager::setDoorAngle(0);
    }
  }

  updatePumpSafety();
  updateStress();
}

void Automation::stepHigh() {
  if (!g_settings.automationEnabled) return;

  updatePumpSafety();
  updateDryingStats();
  adaptiveTuneWatering();
  updateWateringStatsOnPumpToggle();

  if (g_safety.pumpLocked) {
    if (g_sensors.pumpOn) {
      DeviceManager::setPump(false);
    }
    updateStress();
    return;
  }

  if (isManualActive(manualPumpUntil)) {
    updateStress();
    return;
  }

  if (!isWithinWaterWindowInternal()) {
    if (g_sensors.pumpOn) {
      DeviceManager::setPump(false);
    }
    updateStress();
    return;
  }

  if (isnan(g_sensors.soilMoisture)) {
    if (g_sensors.pumpOn) {
      DeviceManager::setPump(false);
    }
    updateStress();
    return;
  }

  float setpBase = float(g_settings.soilMoistureSetpoint);
  float setp     = setpBase + g_adapt.soilSetpointOffset;
  setp = clampT(setp, 30.0f, 90.0f);

  float hyst = float(g_settings.soilMoistureHyst);
  float lowThresh  = setp - hyst;
  float highThresh = setp + hyst;

  float sm = g_sensors.soilMoisture;

  if (!g_sensors.pumpOn && sm < lowThresh) {
    DeviceManager::setPump(true);
  } else if (g_sensors.pumpOn && sm > highThresh) {
    DeviceManager::setPump(false);
  }

  updateStress();
}

void Automation::stepMedium() {
  if (!g_settings.automationEnabled) return;

  updateClimateHistory();

  if (isManualActive(manualFanUntil) ||
      isManualActive(manualDoorUntil)) {
    updateStress();
    return;
  }

  if (isnan(g_sensors.airTemp) || isnan(g_sensors.airHum)) {
    updateStress();
    return;
  }

  float t = g_sensors.airTemp;
  float h = g_sensors.airHum;

  float tMin = g_settings.comfortTempMin;
  float tMax = g_settings.comfortTempMax;
  float hMin = g_settings.comfortHumMin;
  float hMax = g_settings.comfortHumMax;

  float tempOver  = 0.0f;
  float tempUnder = 0.0f;
  if (t > tMax) tempOver  = t - tMax;
  if (t < tMin) tempUnder = tMin - t;

  float humOver   = 0.0f;
  float humUnder  = 0.0f;
  if (h > hMax) humOver   = h - hMax;
  if (h < hMin) humUnder  = hMin - h;

  float trendBoost = 0.0f;
  if (g_climateHist.dTdt > 1.0f) {
    trendBoost += (g_climateHist.dTdt - 1.0f) * 0.5f;
  }
  if (g_climateHist.dHdt > 3.0f) {
    trendBoost += (g_climateHist.dHdt - 3.0f) * 0.1f;
  }

  float scoreHotHumid =
    tempOver * 2.0f + humOver * 0.7f + trendBoost;

  float scoreColdDry =
    tempUnder * 2.0f + humUnder * 0.7f;

  bool    needVent  = false;
  uint8_t doorAngle = 0;

  if (scoreHotHumid > 0.5f) {
    needVent = true;
    float norm = clampT(scoreHotHumid / 20.0f, 0.0f, 1.0f);
    doorAngle = (uint8_t)(norm * 100.0f);
  }

  if (scoreColdDry > 1.0f) {
    needVent  = false;
    doorAngle = 0;
  }

  DeviceManager::setFan(needVent);
  DeviceManager::setDoorAngle(doorAngle);

  updateStress();
}

void Automation::stepLow() {
  if (!g_settings.automationEnabled) return;

  updateLightStats();

  if (isManualActive(manualLightUntil)) {
    updateStress();
    return;
  }

  bool daylight = SunPosition::isDaylight();
  bool night    = !daylight;

  if (!isnan(g_sensors.lux)) {
    if (isnan(luxFiltered)) {
      luxFiltered = g_sensors.lux;
    } else {
      luxFiltered = luxFiltered +
        AutomationConfig::LUX_FILTER_ALPHA *
        (g_sensors.lux - luxFiltered);
    }
  }

  uint32_t now = millis();
  bool     wantLight = g_sensors.lightOn;

  if (night) {
    wantLight = false;
  } else {
    if (isnan(luxFiltered)) {
      wantLight = false;
    } else {
      float onThr  = g_light.dynamicLuxOn;
      float offThr = g_light.dynamicLuxOff;

      if (offThr < onThr + 5.0f) {
        offThr = onThr + 5.0f;
      }

      if (!g_sensors.lightOn) {
        if (luxFiltered < onThr) {
          wantLight = true;
        }
      } else {
        if (luxFiltered > offThr) {
          wantLight = false;
        }
      }
    }
  }

  if (wantLight != g_sensors.lightOn) {
    uint32_t dt = now - lightLastToggleMs;

    if (g_sensors.lightOn &&
        dt < AutomationConfig::LIGHT_MIN_ON_TIME_MS) {
      // было включено слишком мало
    } else if (!g_sensors.lightOn &&
               dt < AutomationConfig::LIGHT_MIN_OFF_TIME_MS) {
      // было выключено слишком мало
    } else {
      DeviceManager::setLight(wantLight);
      lightLastToggleMs = now;
    }
  }

  updateStress();
}

// ---------- ручной режим ----------

void Automation::registerManualPump() {
  manualPumpUntil = millis() + MANUAL_HOLD_MS;
}

void Automation::registerManualLight() {
  manualLightUntil = millis() + MANUAL_HOLD_MS;
}

void Automation::registerManualFan() {
  manualFanUntil = millis() + MANUAL_HOLD_MS;
}

void Automation::registerManualDoor() {
  manualDoorUntil = millis() + MANUAL_HOLD_MS;
}

// ---------- вспомогательные ----------

bool Automation::isNightTime() {
  return isNightBySun();
}

bool Automation::isWithinWaterWindow() {
  return isWithinWaterWindowInternal();
}

void Automation::updateDynamicWaterWindow() {
  g_adapt.soilSetpointOffset = 0.0f;
  g_waterStats = WateringStats{};
}

// ---------- диагностика / адаптация ----------

Automation::DiagInfo Automation::getDiagInfo() {
  DiagInfo d{};
  d.pumpMsDay          = g_safety.pumpWindowMs;
  d.pumpLocked         = g_safety.pumpLocked;

  d.soilSetpointOffset = g_adapt.soilSetpointOffset;
  d.soilAdaptMin       = g_limits.soilOffsetMin;
  d.soilAdaptMax       = g_limits.soilOffsetMax;

  d.luxOnOffset        = g_adapt.luxOnOffset;
  d.luxOffOffset       = g_adapt.luxOffOffset;
  d.luxAdaptMin        = g_limits.luxOffsetMin;
  d.luxAdaptMax        = g_limits.luxOffsetMax;

  d.avgDrySpeed        = g_waterStats.avgDrySpeed;
  d.avgDeltaMoisture   = g_waterStats.avgDelta;

  d.dailyLuxIntegral   = g_light.dailyLuxIntegral;
  d.dynamicLuxOn       = g_light.dynamicLuxOn;
  d.dynamicLuxOff      = g_light.dynamicLuxOff;

  d.stressTemp         = g_stress.tempStress;
  d.stressHum          = g_stress.humStress;
  d.stressSoil         = g_stress.soilStress;
  d.stressLight        = g_stress.lightStress;
  d.stressTotal        = g_stress.totalStress;

  return d;
}

void Automation::setAdaptationLimits(float soilMin, float soilMax,
                                     float luxMin,  float luxMax) {
  if (soilMin > soilMax) {
    float t = soilMin; soilMin = soilMax; soilMax = t;
  }
  if (luxMin > luxMax) {
    float t = luxMin; luxMin = luxMax; luxMax = t;
  }

  g_limits.soilOffsetMin = soilMin;
  g_limits.soilOffsetMax = soilMax;
  g_limits.luxOffsetMin  = luxMin;
  g_limits.luxOffsetMax  = luxMax;

  // пересжимаем текущие оффсеты под новый диапазон
  g_adapt.soilSetpointOffset = clampT(g_adapt.soilSetpointOffset,
                                      g_limits.soilOffsetMin,
                                      g_limits.soilOffsetMax);
  g_adapt.luxOnOffset  = clampT(g_adapt.luxOnOffset,
                                g_limits.luxOffsetMin,
                                g_limits.luxOffsetMax);
  g_adapt.luxOffOffset = clampT(g_adapt.luxOffOffset,
                                g_limits.luxOffsetMin,
                                g_limits.luxOffsetMax);

  g_light.dynamicLuxOn  =
    AutomationConfig::LIGHT_LUX_ON_THRESHOLD  + g_adapt.luxOnOffset;
  g_light.dynamicLuxOff =
    AutomationConfig::LIGHT_LUX_OFF_THRESHOLD + g_adapt.luxOffOffset;
}