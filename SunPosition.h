// === FILE: SunPosition.h ===
#pragma once
#include <Arduino.h>
#include <time.h>

// Результат расчёта положения солнца
struct SunPositionData {
  float altitudeDeg;  // высота над горизонтом, градусы (>0 — солнце над горизонтом)
  float azimuthDeg;   // азимут, градусы от севера по часовой (0..360)
  bool  isDay;        // true, если солнце над горизонтом
};

namespace SunPosition {

  // Инициализация модуля (для совместимости со старым кодом)
  // Сейчас ничего не делает, но оставлен, чтобы не было ошибок компиляции.
  void begin();

  // Упрощённый расчёт положения солнца:
  //  nowUtc        — UNIX-время в секундах (UTC)
  //  latitudeDeg   — широта (+N)
  //  longitudeDeg  — долгота (+E)
  //  tzOffsetMin   — смещение по времени, минуты (например UTC+3 = 180)
  SunPositionData calculate(time_t nowUtc,
                            float latitudeDeg,
                            float longitudeDeg,
                            int   tzOffsetMin);

  // Удобная обёртка для автоматики:
  // берёт текущее время (time(nullptr)), координаты и часовой пояс из Config.h
  // и возвращает true, если сейчас "день" (солнце над горизонтом).
  bool isDaylight();
}