// === FILE: DeviceManager.cpp ===
#include "DeviceManager.h"
#include "Config.h"
#include "Globals.h"
#include "Storage.h"

#include <Wire.h>
#include <Adafruit_BME280.h>
#include <BH1750.h>
#include <TM1637Display.h>
#include <ESP32Servo.h>
#include <Adafruit_NeoPixel.h>
#include <math.h>

namespace {

  // ---------- ПОЛЯРНОСТЬ РЕЛЕ ----------
  constexpr bool LIGHT_ACTIVE_HIGH = true;
  constexpr bool PUMP_ACTIVE_HIGH  = true;
  constexpr bool FAN_ACTIVE_HIGH   = true;

  inline void relayWritePolarity(uint8_t pin, bool on, bool activeHigh) {
    if (activeHigh) {
      digitalWrite(pin, on ? HIGH : LOW);
    } else {
      digitalWrite(pin, on ? LOW : HIGH);
    }
  }

  // ---------- ДАТЧИКИ / ИСПОЛНИТЕЛИ ----------
  Adafruit_BME280 bme;
  BH1750          bh;
  TM1637Display   display(Pins::TM1637_CLK, Pins::TM1637_DIO);
  Servo           doorServo;

  // ---------- LED-матрица WS2812B 8x8 ----------
  constexpr uint16_t LED_COUNT          = 64;
  constexpr bool     LED_MATRIX_ENABLED = true; // выключи, если матрицы нет

  Adafruit_NeoPixel ledStrip(
    LED_COUNT,
    Pins::LED_DATA,
    NEO_GRB + NEO_KHZ800
  );

  bool ledInitDone = false;

  void applyLedFromSettings(bool on) {
    if (!LED_MATRIX_ENABLED) return;

    if (!ledInitDone) {
      ledStrip.begin();
      ledStrip.clear();
      ledInitDone = true;
    }

    // Яркость из настроек 0–100 → 0–255
    uint8_t br = g_settings.lightBrightness;
    if (br > 100) br = 100;
    uint8_t neoBr = map(br, 0, 100, 0, 255);
    ledStrip.setBrightness(neoBr);

    // Цвет из настроек
    uint8_t r = 0, g = 0, b = 0;
    if (on) {
      r = g_settings.lightColorR;
      g = g_settings.lightColorG;
      b = g_settings.lightColorB;
    }

    for (uint16_t i = 0; i < LED_COUNT; ++i) {
      ledStrip.setPixelColor(i, ledStrip.Color(r, g, b));
    }
    ledStrip.show();
  }

  // ---------- КАЛИБРОВКА ПОЧВЫ ----------
  uint16_t soilDryRaw = 3500;
  uint16_t soilWetRaw = 1800;

  // ---------- ПЕРИОДИКА ОПРОСА ----------
  uint32_t lastSensorMs                 = 0;
  constexpr uint32_t SENSOR_INTERVAL_MS = 2000;

  // ---------- ЛИМИТЫ НАСОСА ----------
  uint32_t pumpStartMs    = 0;
  uint32_t pumpDayMs      = 0;
  uint32_t pumpDayStartMs = 0;

} // namespace

// -----------------------------------------------------------------------------
// ИНИЦИАЛИЗАЦИЯ
// -----------------------------------------------------------------------------

void DeviceManager::begin() {
  // --- РЕЛЕ ---
  pinMode(Pins::RELAY_LIGHT, OUTPUT);
  pinMode(Pins::RELAY_PUMP,  OUTPUT);
  pinMode(Pins::RELAY_FAN,   OUTPUT);

  relayWritePolarity(Pins::RELAY_LIGHT, false, LIGHT_ACTIVE_HIGH);
  relayWritePolarity(Pins::RELAY_PUMP,  false, PUMP_ACTIVE_HIGH);
  relayWritePolarity(Pins::RELAY_FAN,   false, FAN_ACTIVE_HIGH);

  g_sensors.lightOn = false;
  g_sensors.pumpOn  = false;
  g_sensors.fanOn   = false;

  // --- СЕРВО ДВЕРИ ---
  doorServo.attach(Pins::SERVO_DOOR);
  g_sensors.doorOpen = false;

  // --- I2C и датчики ---
  Wire.begin(Pins::I2C_SDA, Pins::I2C_SCL);

  g_sensors.bmeOk        = false;
  g_sensors.bhOk         = false;
  g_sensors.soilOk       = false;
  g_sensors.airTemp      = NAN;
  g_sensors.airHum       = NAN;
  g_sensors.airPressure  = NAN;
  g_sensors.soilMoisture = NAN;
  g_sensors.soilTemp     = NAN;
  g_sensors.lux          = NAN;

  // BME280 — пробуем 0x76/0x77
  bool bmeFound = false;
  for (uint8_t addr : {0x76, 0x77}) {
    if (bme.begin(addr)) {
      bmeFound        = true;
      g_sensors.bmeOk = true;
      Serial.printf("[BME280] detected at 0x%02X\n", addr);
      break;
    }
  }
  if (!bmeFound) {
    Serial.println("[BME280] Not found on 0x76/0x77");
  }

  // BH1750 — 0x23/0x5C
  bool bhFound = false;
  for (uint8_t addr : {0x23, 0x5C}) {
    if (bh.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, addr)) {
      bhFound        = true;
      g_sensors.bhOk = true;
      Serial.printf("[BH1750] detected at 0x%02X\n", addr);
      break;
    }
  }
  if (!bhFound) {
    Serial.println("[BH1750] Not found on 0x23/0x5C");
  }

  // --- TM1637 ---
  display.setBrightness(0x0f);
  display.clear();

  // --- КАЛИБРОВКА ПОЧВЫ (из настроек) ---
  soilDryRaw = g_settings.soilDryRaw;
  soilWetRaw = g_settings.soilWetRaw;
  if (soilDryRaw == 0 || soilWetRaw == 0 || soilDryRaw == soilWetRaw) {
    soilDryRaw = 3500;
    soilWetRaw = 1800;
  }

  // --- Автоопределение датчика почвы MGS/MGH-TH50 ---
  {
    uint16_t rawMoist = analogRead(Pins::SOIL_ANALOG);
    uint16_t rawTemp  = analogRead(Pins::SOIL_TEMP_ANALOG);
    delay(5);
    rawMoist = (rawMoist + analogRead(Pins::SOIL_ANALOG))      / 2;
    rawTemp  = (rawTemp  + analogRead(Pins::SOIL_TEMP_ANALOG)) / 2;

    if (rawMoist > 50 && rawMoist < 4090 &&
        rawTemp  > 50 && rawTemp  < 4090) {
      g_sensors.soilOk = true;
      Serial.printf("[Soil] probe OK, raw=%u/%u\n", rawMoist, rawTemp);
    } else {
      g_sensors.soilOk = false;
      Serial.printf("[Soil] probe NOT detected, raw=%u/%u\n", rawMoist, rawTemp);
    }
  }

  // --- LED-матрица ---
  applyLedFromSettings(false);

  // --- Счётчики насоса ---
  pumpDayStartMs = millis();
  pumpDayMs      = 0;
  pumpStartMs    = 0;

  Serial.println("[DeviceManager] init done");
}

// -----------------------------------------------------------------------------
// ПЕРИОДИЧЕСКИЙ ОПРОС
// -----------------------------------------------------------------------------

void DeviceManager::loopFast() {
  uint32_t now = millis();

  // --- Опрос датчиков раз в SENSOR_INTERVAL_MS ---
  if (now - lastSensorMs >= SENSOR_INTERVAL_MS) {
    lastSensorMs = now;

    // BME280: температура, влажность, давление
    if (g_sensors.bmeOk) {
      g_sensors.airTemp     = bme.readTemperature();
      g_sensors.airHum      = bme.readHumidity();
      g_sensors.airPressure = bme.readPressure() / 100.0f; // Па → гПа
    } else {
      g_sensors.airTemp     = NAN;
      g_sensors.airHum      = NAN;
      g_sensors.airPressure = NAN;
    }

    // BH1750: освещённость
    if (g_sensors.bhOk) {
      g_sensors.lux = bh.readLightLevel();
    } else {
      g_sensors.lux = NAN;
    }

    // Датчик почвы: влажность + температура
    if (g_sensors.soilOk) {
      // --- Влажность ---
      int raw = analogRead(Pins::SOIL_ANALOG);
      if (soilDryRaw != soilWetRaw) {
        float norm = (float)(raw - soilWetRaw) / (float)(soilDryRaw - soilWetRaw);
        norm = constrain(norm, 0.0f, 1.0f);
        g_sensors.soilMoisture = (1.0f - norm) * 100.0f;
      } else {
        g_sensors.soilMoisture = NAN;
      }

      // --- Температура почвы ---
      // Простая модель: считаем напряжение на АЦП и переводим в °C.
      // Например, для LM35-подобного выхода: 10 мВ/°C, 0.5 В при 0 °C.
      // T(°C) = (V - 0.5) * 100
      int rawT = analogRead(Pins::SOIL_TEMP_ANALOG);
      const float vRef = 3.3f; // опорное для АЦП ESP32
      float voltage = (rawT / 4095.0f) * vRef;
      float temp    = (voltage - 0.5f) * 100.0f;

      // Калибровочный оффсет из настроек (можно задать +10.0 °C, если надо)
      g_sensors.soilTemp = temp + g_settings.soilTempOffset;
    } else {
      g_sensors.soilMoisture = NAN;
      g_sensors.soilTemp     = NAN;
    }

    // Вывод на TM1637 — просто температура воздуха
    int tInt = isnan(g_sensors.airTemp) ? 0 : (int)lroundf(g_sensors.airTemp);
    display.showNumberDec(tInt, true);
  }

  // --- Ограничение времени работы насоса за цикл ---
  if (g_sensors.pumpOn) {
    if (pumpStartMs == 0) pumpStartMs = now;
    uint32_t runMs = now - pumpStartMs;
    if (runMs > AutomationConfig::MAX_PUMP_RUN_MS) {
      Serial.println("[Pump] Max run per cycle exceeded, stopping");
      setPump(false);
    }
  }

  // --- Суточный лимит насоса ---
  if (now - pumpDayStartMs > 24UL * 60UL * 60UL * 1000UL) {
    pumpDayStartMs = now;
    pumpDayMs      = 0;
  }
}

// -----------------------------------------------------------------------------
// УСТРОЙСТВА
// -----------------------------------------------------------------------------

void DeviceManager::setLight(bool on) {
  relayWritePolarity(Pins::RELAY_LIGHT, on, LIGHT_ACTIVE_HIGH);
  g_sensors.lightOn = on;

  applyLedFromSettings(on);

  Serial.printf("[Light] %s (pin=%d)\n", on ? "ON" : "OFF", Pins::RELAY_LIGHT);
}

void DeviceManager::setPump(bool on) {
  uint32_t now = millis();

  if (on) {
    if (pumpDayMs >= AutomationConfig::MAX_PUMP_DAY_MS) {
      Serial.println("[Pump] Daily limit exceeded, cannot start");
      g_sensors.pumpOn = false;
      relayWritePolarity(Pins::RELAY_PUMP, false, PUMP_ACTIVE_HIGH);
      return;
    }
    pumpStartMs = now;
    relayWritePolarity(Pins::RELAY_PUMP, true, PUMP_ACTIVE_HIGH);
    g_sensors.pumpOn = true;
    Serial.println("[Pump] ON");
  } else {
    if (g_sensors.pumpOn && pumpStartMs > 0) {
      uint32_t runMs = now - pumpStartMs;
      pumpDayMs += runMs;
    }
    pumpStartMs = 0;
    relayWritePolarity(Pins::RELAY_PUMP, false, PUMP_ACTIVE_HIGH);
    g_sensors.pumpOn = false;
    Serial.println("[Pump] OFF");
  }
}

void DeviceManager::setFan(bool on) {
  relayWritePolarity(Pins::RELAY_FAN, on, FAN_ACTIVE_HIGH);
  g_sensors.fanOn = on;
  Serial.printf("[Fan] %s (pin=%d)\n", on ? "ON" : "OFF", Pins::RELAY_FAN);
}

void DeviceManager::setDoorAngle(uint8_t angle) {
  angle = constrain(angle, 0, 100);
  int servoAngle = map(angle, 0, 100, 0, 180);
  doorServo.write(servoAngle);
  g_sensors.doorOpen = (angle > 10);
  Serial.printf("[Door] angle=%u (open=%d)\n", angle, g_sensors.doorOpen ? 1 : 0);
}

// -----------------------------------------------------------------------------
// КАЛИБРОВКА ПОЧВЫ
// -----------------------------------------------------------------------------

void DeviceManager::setSoilCalibration(uint16_t dry, uint16_t wet) {
  soilDryRaw = dry;
  soilWetRaw = wet;
  g_settings.soilDryRaw = dry;
  g_settings.soilWetRaw = wet;
  Storage::saveSettings(g_settings);
  Serial.printf("[SoilCal] dry=%u wet=%u\n", dry, wet);
}

void DeviceManager::getSoilCalibration(uint16_t& dry, uint16_t& wet) {
  dry = soilDryRaw;
  wet = soilWetRaw;
}