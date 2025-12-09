// === FILE: SmartGreenhouse_v3.ino ===
#include <Arduino.h>
#include <SPIFFS.h>
#include "Config.h"
#include "Types.h"
#include "Globals.h"
#include "Storage.h"
#include "TimeManager.h"
#include "DeviceManager.h"
#include "Automation.h"
#include "StateMachine.h"
#include "WebUiAsync.h"
#include "TelemetryLogger.h"
#include "SoilCalibration.h"
#include "OtaHandler.h"
#include "TelegramAsync.h"
#include "Diagnostics.h"
#include "SunPosition.h"

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n[YotikM2 v3] Booting...");

  if (!SPIFFS.begin(true)) {
    Serial.println("[FS] Failed to mount SPIFFS");
  }

  Storage::begin();
  Storage::loadSettings(g_settings);
  DeviceManager::begin();
  TimeManager::begin();
  TimeManager::syncTimeAsync();
  TimeManager::loadTimeFromRTCIfNeeded();
  TelemetryLogger::begin();
  Automation::begin();
  Diagnostics::begin();
  SunPosition::begin();
  WebUiAsync::begin();
  OtaHandler::begin();
  TelegramAsync::begin();
  StateMachine::startTask();

  Serial.println("[YotikM2 v3] Setup done");
}

void loop() {
  OtaHandler::loop();
  TelegramAsync::loop();
  WebUiAsync::loop();
  delay(5);
}