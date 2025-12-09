// === FILE: StateMachine.cpp ===
#include "StateMachine.h"
#include "Automation.h"
#include "DeviceManager.h"
#include "TelemetryLogger.h"
#include "Diagnostics.h"
#include "Config.h"
#include <Arduino.h>

namespace {
  void automationTask(void* pv) {
    for (;;) {
      Automation::stepCritical();
      Automation::stepHigh();
      Automation::stepMedium();
      Automation::stepLow();

      DeviceManager::loopFast();
      TelemetryLogger::loop();
      Diagnostics::loop();

      vTaskDelay(pdMS_TO_TICKS(AutomationConfig::AUTOMATION_INTERVAL_MS));
    }
  }
}

void StateMachine::startTask() {
  xTaskCreatePinnedToCore(
    automationTask,
    "automationTask",
    8192,
    nullptr,
    2,
    nullptr,
    1
  );
}