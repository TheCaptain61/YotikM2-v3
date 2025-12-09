#pragma once
#include <Arduino.h>

namespace TelegramAsync {
  void begin();
  void loop();
  void sendAlert(const String& text);
}