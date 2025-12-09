// === FILE: SunPosition.cpp ===
#include "SunPosition.h"
#include "Config.h"

#include <math.h>
#include <time.h>

namespace {

constexpr double DEG2RAD = M_PI / 180.0;
constexpr double RAD2DEG = 180.0 / M_PI;

// Нормализация угла в 0..360
double norm360(double xDeg) {
  double r = fmod(xDeg, 360.0);
  if (r < 0) r += 360.0;
  return r;
}

} // namespace

// Для совместимости: сейчас инициализация не нужна,
// но старый код может вызывать SunPosition::begin()
void SunPosition::begin() {
  // Ничего не делаем
}

SunPositionData SunPosition::calculate(time_t nowUtc,
                                       float latitudeDeg,
                                       float longitudeDeg,
                                       int   tzOffsetMin) {
  SunPositionData out{};
  out.altitudeDeg = -90.0f;
  out.azimuthDeg  = 0.0f;
  out.isDay       = false;

  // Перевод времени в "локальное"
  time_t nowLocal = nowUtc + tzOffsetMin * 60;

  struct tm t;
  gmtime_r(&nowLocal, &t);

  int year  = t.tm_year + 1900;
  int month = t.tm_mon + 1;
  int day   = t.tm_mday;
  double hour = t.tm_hour + t.tm_min / 60.0 + t.tm_sec / 3600.0;

  // Юлианская дата
  int   A = (14 - month) / 12;
  int   Y = year + 4800 - A;
  int   Mcalc = month + 12 * A - 3;
  long  JDN = day + (153 * Mcalc + 2) / 5 + 365L * Y + Y / 4 - Y / 100 + Y / 400 - 32045;
  double JD = (double)JDN + (hour - 12.0) / 24.0;
  double T  = (JD - 2451545.0) / 36525.0;

  // Средняя долгота и средняя аномалия (meanAnomaly вместо "M", чтобы не конфликтовать с макросами)
  double L0 = norm360(280.46646 + 36000.76983 * T + 0.0003032 * T * T);
  double meanAnomaly = norm360(357.52911 + 35999.05029 * T - 0.0001537 * T * T);
  double e  = 0.016708634 - 0.000042037 * T - 0.0000001267 * T * T;

  double M_rad = meanAnomaly * DEG2RAD;

  // Уравнение центра
  double C = (1.914602 - 0.004817 * T - 0.000014 * T * T) * sin(M_rad)
           + (0.019993 - 0.000101 * T) * sin(2 * M_rad)
           + 0.000289 * sin(3 * M_rad);

  double trueLong  = L0 + C;

  double Omega     = 125.04 - 1934.136 * T;
  double lambda    = trueLong - 0.00569 - 0.00478 * sin(Omega * DEG2RAD);

  // Наклон эклиптики
  double epsilon0  = 23.439291 - 0.0130042 * T;
  double epsilon   = epsilon0 + 0.00256 * cos(Omega * DEG2RAD);

  double epsilonRad = epsilon * DEG2RAD;
  double lambdaRad  = lambda * DEG2RAD;

  // Прямое восхождение и склонение
  double alpha = atan2(cos(epsilonRad) * sin(lambdaRad),
                       cos(lambdaRad));
  double delta = asin(sin(epsilonRad) * sin(lambdaRad));

  // Уравнение времени
  double y = tan(epsilonRad / 2.0);
  y *= y;

  double L0rad = L0 * DEG2RAD;

  double Etime =
      y * sin(2 * L0rad)
    - 2.0 * e * sin(M_rad)
    + 4.0 * e * y * sin(M_rad) * cos(2 * L0rad)
    - 0.5 * y * y * sin(4 * L0rad)
    - 1.25 * e * e * sin(2 * M_rad);

  Etime = Etime * RAD2DEG * 4.0; // минуты

  // Часовой угол
  double solarTimeMin = fmod(hour * 60.0 + Etime + 4.0 * longitudeDeg, 1440.0);
  if (solarTimeMin < 0) solarTimeMin += 1440.0;

  double hourAngleDeg = (solarTimeMin / 4.0) - 180.0;
  if (hourAngleDeg < -180.0) hourAngleDeg += 360.0;

  double hourAngleRad = hourAngleDeg * DEG2RAD;

  // Высота и азимут
  double latRad = latitudeDeg * DEG2RAD;

  double sinAlt = sin(latRad) * sin(delta)
                + cos(latRad) * cos(delta) * cos(hourAngleRad);

  if (sinAlt > 1.0) sinAlt = 1.0;
  if (sinAlt < -1.0) sinAlt = -1.0;

  double altitudeRad = asin(sinAlt);
  double altitudeDeg = altitudeRad * RAD2DEG;

  double cosAz =
    (sin(delta) - sin(altitudeRad) * sin(latRad)) /
    (cos(altitudeRad) * cos(latRad));

  if (cosAz > 1.0) cosAz = 1.0;
  if (cosAz < -1.0) cosAz = -1.0;

  double azRad = acos(cosAz);
  double azDeg = azRad * RAD2DEG;

  if (hourAngleDeg > 0) {
    azDeg = 360.0 - azDeg;
  }

  out.altitudeDeg = (float)altitudeDeg;
  out.azimuthDeg  = (float)norm360(azDeg);
  out.isDay       = (altitudeDeg > 0.0);

  return out;
}

bool SunPosition::isDaylight() {
  time_t nowUtc = time(nullptr);
  if (nowUtc < 100000) {
    // времени ещё нет (RTC/NTP не подняты) — считаем, что "ночь"
    return false;
  }

  SunPositionData sun = calculate(
      nowUtc,
      LocationConfig::LATITUDE_DEG,
      LocationConfig::LONGITUDE_DEG,
      LocationConfig::TZ_OFFSET_MIN
  );

  return sun.isDay;
}