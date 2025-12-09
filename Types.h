// === FILE: Types.h ===
#pragma once
#include <Arduino.h>

enum class ClimateMode : uint8_t {
  Eco = 0,
  Normal = 1,
  Aggressive = 2
};

enum class CropProfile : uint8_t {
  Custom = 0,
  Tomatoes,
  Cucumbers,
  Greens,
  Hibiscus
};

struct SensorData {
  // Воздух
  float airTemp      = NAN;
  float airHum       = NAN;
  float airPressure  = NAN;

  // Почва
  float soilMoisture = NAN;
  float soilTemp     = NAN;
  bool  soilOk       = false;   // автоопределение MGS/MGH-TH50

  // Свет
  float lux          = NAN;

  // Состояние исполнительных устройств
  bool  lightOn      = false;
  bool  pumpOn       = false;
  bool  fanOn        = false;
  bool  doorOpen     = false;

  // Наличие датчиков
  bool  bmeOk        = false;
  bool  bhOk         = false;
  bool  rtcOk        = false;

  // Динамический сетпоинт по почве (подстраивается профилями/логикой)
  float soilSetpointDynamic = NAN;

  // Производная по влажности почвы (как быстро сохнет)
  float soilDryingSlope     = NAN;  // %/час
};

// Алиас, если где-то используется другое имя
using SensorState = SensorData;

struct SystemSettings {
  uint16_t version = 0;

  // Климат (комфортные диапазоны)
  float comfortTempMin = 20.0f;
  float comfortTempMax = 26.0f;
  float comfortHumMin  = 45.0f;
  float comfortHumMax  = 70.0f;

  // Аварийные границы (защита от перегрева/переохлаждения)
  float safetyTempMin  = 5.0f;
  float safetyTempMax  = 40.0f;

  // Режим климата
  ClimateMode climateMode = ClimateMode::Normal;

  // Полив
  uint8_t waterStartHour       = 7;
  uint8_t waterEndHour         = 21;
  uint8_t soilMoistureSetpoint = 60; // базовый сетпоинт (%)
  uint8_t soilMoistureHyst     = 5;  // гистерезис (%)

  // Свет
  uint8_t nightCutoffHour      = 20; // с какого часа считаем «ночь»
  uint8_t lightBrightness      = 80; // яркость матрицы (0–100 %)
  uint8_t lightColorR          = 255;
  uint8_t lightColorG          = 180;
  uint8_t lightColorB          = 120;

  // Профиль культуры
  CropProfile cropProfile      = CropProfile::Tomatoes;

  // Калибровка датчика почвы (сыро/сухо)
  uint16_t soilDryRaw          = 3500;
  uint16_t soilWetRaw          = 1800;

  // ⚙️ Калибровка температуры почвы (°C)
  // позволяет сдвинуть показания вверх/вниз, например +10.0 °C
  float soilTempOffset         = 0.0f;

  // Флаги автоматизации
  bool automationEnabled       = true;
  bool notificationsEnabled    = false;

  // Wi-Fi STA
  char wifiSsid[32]            = "";
  char wifiPass[64]            = "";
};

struct TelemetryPoint {
  uint32_t ts;           // UNIX time (сек)
  float    airTemp;      // °C
  float    airHum;       // %
  float    soilMoisture; // %
  float    soilTemp;     // °C
  float    airPressure;  // гПа
  float    lux;          // лк
};