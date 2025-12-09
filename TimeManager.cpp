#include "TimeManager.h"
#include "Config.h"
#include "Globals.h"
#include <time.h>
#include <Wire.h>
#include <RTClib.h>

namespace {
  RTC_DS3231 rtc;
  bool rtcAvailable = false;
}

void TimeManager::begin() {
  configTzTime("MSK-3", "pool.ntp.org", "time.nist.gov");
  Wire.begin(Pins::I2C_SDA, Pins::I2C_SCL);

  if (rtc.begin()) {
    rtcAvailable = true;
    if (rtc.lostPower()) {
      Serial.println("[RTC] Lost power, wait for NTP");
    }
  } else {
    Serial.println("[RTC] Not found");
  }
}

void TimeManager::syncTimeAsync() {
  // configTzTime уже запускает NTP-клиент
}

bool TimeManager::isTimeValid() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 5)) {
    return false;
  }
  return timeinfo.tm_year > (2016 - 1900);
}

bool TimeManager::getLocalTimeSafe(struct tm& out) {
  if (!getLocalTime(&out, 5)) return false;
  return true;
}

uint8_t TimeManager::getHour() {
  struct tm t;
  if (!getLocalTimeSafe(t)) return 0;
  return t.tm_hour;
}

void TimeManager::saveTimeToRTC() {
  if (!rtcAvailable) return;
  time_t nowSec = time(nullptr);
  if (nowSec < 100000) return;
  rtc.adjust(DateTime(nowSec));
}

void TimeManager::loadTimeFromRTCIfNeeded() {
  if (!rtcAvailable) return;
  if (isTimeValid()) return;

  DateTime dt = rtc.now();
  if (dt.year() < 2020) return;
  struct tm t{};
  t.tm_year = dt.year() - 1900;
  t.tm_mon  = dt.month() - 1;
  t.tm_mday = dt.day();
  t.tm_hour = dt.hour();
  t.tm_min  = dt.minute();
  t.tm_sec  = dt.second();
  time_t tt = mktime(&t);
  struct timeval now = { .tv_sec = tt, .tv_usec = 0 };
  settimeofday(&now, nullptr);
  Serial.println("[RTC] Time loaded from RTC");
}