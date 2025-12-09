// === FILE: OtaHandler.cpp ===
#include "OtaHandler.h"
#include "Config.h"
#include <ArduinoOTA.h>

void OtaHandler::begin() {
  ArduinoOTA.setHostname(OTAConfig::HOSTNAME);
  ArduinoOTA.setPassword(OTAConfig::PASSWORD);

  ArduinoOTA.onStart([]() {
    Serial.println("[OTA] Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\n[OTA] End");
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("[OTA] Error %u\n", error);
  });
  ArduinoOTA.begin();
  Serial.println("[OTA] Ready");
}

void OtaHandler::loop() {
  ArduinoOTA.handle();
}