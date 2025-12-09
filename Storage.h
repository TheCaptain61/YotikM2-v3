// === FILE: Storage.h ===
#pragma once
#include "Types.h"

namespace Storage {
  void begin();
  bool loadSettings(SystemSettings& out);
  bool saveSettings(const SystemSettings& in);
  void resetDefaults(SystemSettings& out);
}