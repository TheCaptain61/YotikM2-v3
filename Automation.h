// === FILE: Automation.h ===
#pragma once
#include <stdint.h>

namespace Automation {

  void begin();

  void stepCritical();
  void stepHigh();
  void stepMedium();
  void stepLow();

  void registerManualPump();
  void registerManualLight();
  void registerManualFan();
  void registerManualDoor();

  bool isNightTime();
  bool isWithinWaterWindow();

  // Вызывается после изменения настроек (из WebUi),
  // сбрасывает адаптивные сдвиги и статистику полива.
  void updateDynamicWaterWindow();

  // Структура с диагностической информацией для Web UI
  struct DiagInfo {
    uint32_t pumpMsDay;          // сколько насос работал за ~последние 24ч (мс)
    bool     pumpLocked;         // заблокирован ли насос по safety

    float soilSetpointOffset;    // адаптивный сдвиг setpoint'а почвы, %
    float soilAdaptMin;          // текущий минимум диапазона адаптации (%)
    float soilAdaptMax;          // текущий максимум диапазона адаптации (%)

    float luxOnOffset;           // адаптивный сдвиг порога включения света, лк
    float luxOffOffset;          // адаптивный сдвиг порога выключения света, лк
    float luxAdaptMin;           // минимальный допустимый сдвиг порогов света
    float luxAdaptMax;           // максимальный допустимый сдвиг порогов света

    float avgDrySpeed;           // средняя скорость высыхания почвы, %/час
    float avgDeltaMoisture;      // средний прирост % влажности после одного полива

    float dailyLuxIntegral;      // интеграл света за "день" (lux*часы)
    float dynamicLuxOn;          // текущий порог включения света, лк
    float dynamicLuxOff;         // текущий порог выключения света, лк

    float stressTemp;            // вклад температуры в стресс
    float stressHum;             // вклад влажности воздуха
    float stressSoil;            // вклад почвы
    float stressLight;           // вклад света
    float stressTotal;           // суммарный стресс
  };

  // Получить актуальную диагностику для веб-а
  DiagInfo getDiagInfo();

  // Задать диапазоны адаптации (например, soil [-15..+15], свет [-30..+30])
  // Если min > max — они переставятся местами.
  void setAdaptationLimits(float soilMin, float soilMax,
                           float luxMin,  float luxMax);
}