// === FILE: Storage.cpp ===
#include "Storage.h"
#include "Config.h"
#include <EEPROM.h>

namespace {
  constexpr size_t EEPROM_SIZE = 1024;

  uint32_t crc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    while (len--) {
      uint8_t b = *data++;
      crc ^= b;
      for (int i = 0; i < 8; ++i) {
        if (crc & 1) crc = (crc >> 1) ^ 0xEDB88320UL;
        else crc >>= 1;
      }
    }
    return ~crc;
  }

  struct EepromBlob {
    uint32_t      magic;
    uint16_t      version;
    uint16_t      size;
    SystemSettings settings;
    uint32_t      crc;
  };
}

void Storage::begin() {
  EEPROM.begin(EEPROM_SIZE);
}

void Storage::resetDefaults(SystemSettings& out) {
  out = SystemSettings{};
  out.version          = StorageConfig::SETTINGS_VER;
  out.waterStartHour   = AutomationConfig::DEFAULT_WATER_START;
  out.waterEndHour     = AutomationConfig::DEFAULT_WATER_END;
  out.nightCutoffHour  = 20;
  out.cropProfile      = CropProfile::Tomatoes;
}

bool Storage::loadSettings(SystemSettings& out) {
  EepromBlob blob;
  EEPROM.get(0, blob);

  if (blob.magic != StorageConfig::MAGIC) {
    Serial.println("[Storage] MAGIC mismatch, resetting defaults");
    resetDefaults(out);
    saveSettings(out);
    return false;
  }

  if (blob.version != StorageConfig::SETTINGS_VER ||
      blob.size != sizeof(SystemSettings)) {
    Serial.println("[Storage] Version/size mismatch, resetting defaults");
    resetDefaults(out);
    saveSettings(out);
    return false;
  }

  uint32_t calcCrc = crc32((uint8_t*)&blob.settings, sizeof(SystemSettings));
  if (calcCrc != blob.crc) {
    Serial.println("[Storage] CRC mismatch, resetting defaults");
    resetDefaults(out);
    saveSettings(out);
    return false;
  }

  out = blob.settings;
  Serial.println("[Storage] Settings loaded OK");
  return true;
}

bool Storage::saveSettings(const SystemSettings& in) {
  EepromBlob blob{};
  blob.magic    = StorageConfig::MAGIC;
  blob.version  = StorageConfig::SETTINGS_VER;
  blob.size     = sizeof(SystemSettings);
  blob.settings = in;
  blob.settings.version = StorageConfig::SETTINGS_VER;
  blob.crc      = crc32((uint8_t*)&blob.settings, sizeof(SystemSettings));

  EEPROM.put(0, blob);
  if (!EEPROM.commit()) {
    Serial.println("[Storage] EEPROM commit failed");
    return false;
  }
  Serial.println("[Storage] Settings saved");
  return true;
}