// === FILE: Config.h ===
#pragma once
#include <Arduino.h>

// Пины — подправь под своё железо при необходимости
namespace Pins {
  // Реле
  constexpr uint8_t RELAY_LIGHT   = 4;
  constexpr uint8_t RELAY_PUMP    = 17;
  constexpr uint8_t RELAY_FAN     = 16;

  // Серво двери
  constexpr uint8_t SERVO_DOOR    = 19;

  // Датчики почвы (MGS-TH50)
  constexpr uint8_t SOIL_ANALOG      = 34; // влажность
  constexpr uint8_t SOIL_TEMP_ANALOG = 35; // температура почвы

  // TM1637
  constexpr uint8_t TM1637_CLK    = 15;
  constexpr uint8_t TM1637_DIO    = 14;

  // I2C (ESP32)
  constexpr uint8_t I2C_SDA       = 21;
  constexpr uint8_t I2C_SCL       = 22;

  // LED-матрица / лента (WS2812B 8x8)
  constexpr uint8_t LED_DATA      = 18;
}

namespace StorageConfig {
  constexpr uint32_t MAGIC         = 0x594F544B; // 'YOTK'
  // Версия настроек — поднята, т.к. добавляли soilTempOffset и прочее
  constexpr uint16_t SETTINGS_VER  = 0x0005;
}

namespace AutomationConfig {
  constexpr uint32_t AUTOMATION_INTERVAL_MS = 1000;

  // Пороги освещённости (включение/выключение с гистерезисом)
  constexpr float    LIGHT_LUX_ON_THRESHOLD  = 60.0f;  // включать досветку, если ниже
  constexpr float    LIGHT_LUX_OFF_THRESHOLD = 80.0f;  // выключать досветку, если выше

  // Фильтрация датчика освещённости (экспоненциальное сглаживание)
  constexpr float    LUX_FILTER_ALPHA        = 0.2f;   // 0..1, чем больше, тем быстрее реакция

  // Минимальное время работы света, чтобы избежать "мигания"
  constexpr uint32_t LIGHT_MIN_ON_TIME_MS    = 3UL * 60UL * 1000UL;
  constexpr uint32_t LIGHT_MIN_OFF_TIME_MS   = 2UL * 60UL * 1000UL;

  // Границы "ночи" для логики света (резерв, если нужно по часам)
  constexpr uint8_t  NIGHT_START_HOUR = 20; // вечер
  constexpr uint8_t  NIGHT_END_HOUR   = 7;  // утро

  // Ограничения насоса
  constexpr uint32_t MAX_PUMP_RUN_MS  = 60UL * 1000UL;        // макс. разовый запуск
  constexpr uint32_t MAX_PUMP_DAY_MS  = 15UL * 60UL * 1000UL; // макс. за сутки

  constexpr uint8_t  DEFAULT_WATER_START = 7;
  constexpr uint8_t  DEFAULT_WATER_END   = 21;
}

// Координаты теплицы для SunPosition (пример: Москва)
// поменяй под себя при желании
namespace LocationConfig {
  constexpr float LATITUDE_DEG   = 46.70f;   // широта (+N)
  constexpr float LONGITUDE_DEG  = 41.72f;   // долгота (+E)
  constexpr int   TZ_OFFSET_MIN  = 180;      // смещение по времени, минуты (UTC+3 = 180)
}

namespace WifiConfig {
  constexpr const char* AP_SSID = "YotikM2-Setup";
  constexpr const char* AP_PASS = "YotikM2pass";
}

// ⚠️ Рекомендуется заменить на свои значения перед сборкой
namespace TelegramConfig {
  constexpr const char* BOT_TOKEN = "";
  constexpr const char* CHAT_ID   = "";
}

namespace OTAConfig {
  constexpr const char* HOSTNAME = "YotikM2";
  constexpr const char* PASSWORD = "YotikM2ota"; // для OTA по сети, если понадобится
}