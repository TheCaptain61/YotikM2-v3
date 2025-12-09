// === FILE: TelegramAsync.cpp ===
#include "TelegramAsync.h"
#include "Config.h"
#include "Globals.h"
#include "DeviceManager.h"
#include "Automation.h"

#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <math.h>

namespace {

  WiFiClientSecure      client;
  UniversalTelegramBot* bot = nullptr;

  unsigned long       lastCheckMs     = 0;
  const unsigned long BOT_INTERVAL_MS = 2000;

  // Текст кнопок (reply-клавиатура)
  const char* BTN_STATUS   = "🌡 Статус";
  const char* BTN_CONTROL  = "🎛 Управление";
  const char* BTN_HISTORY  = "📈 История";
  const char* BTN_DIAG     = "🩺 Диагностика";
  const char* BTN_PROFILE  = "🌱 Профиль";
  const char* BTN_HELP     = "❓ Помощь";

  // Кнопки управления
  const char* BTN_LIGHT    = "💡 Свет";
  const char* BTN_PUMP     = "🚿 Полив";
  const char* BTN_FAN      = "💨 Вентилятор";
  const char* BTN_AUTO     = "🤖 Авто/ручной";
  const char* BTN_BACK     = "⬅️ Меню";

  // ---------- вспомогательные функции ----------

  String okIcon(bool ok) {
    return ok ? "✅" : "⚠️";
  }

  String onOffIcon(bool on) {
    return on ? "🟢 ВКЛ" : "⚪️ ВЫКЛ";
  }

  String cropProfileToName(CropProfile p) {
    switch (p) {
      case CropProfile::Tomatoes:  return "🍅 Томаты";
      case CropProfile::Cucumbers: return "🥒 Огурцы";
      case CropProfile::Greens:    return "🥬 Зелень";
      case CropProfile::Hibiscus:  return "🌺 Гибискус";
      case CropProfile::Custom:
      default:                     return "⚙️ Пользовательский";
    }
  }

  String cropProfileHint(CropProfile p) {
    switch (p) {
      case CropProfile::Tomatoes:
        return "Томаты любят тёплый и более сухой воздух, почва — достаточно влажная.";
      case CropProfile::Cucumbers:
        return "Огурцы любят высокую влажность воздуха и более влажную почву.";
      case CropProfile::Greens:
        return "Листовая зелень чувствительна к перегреву, предпочитает умеренный климат.";
      case CropProfile::Hibiscus:
        return "Гибискус любит тепло и умеренную влажность, не терпит переохлаждения.";
      case CropProfile::Custom:
      default:
        return "Настройки заданы вручную под вашу культуру.";
    }
  }

  String formatStressBar(float totalStress) {
    if (isnan(totalStress)) return "нет данных";

    float s = totalStress;
    if (s < 0.0f)   s = 0.0f;
    if (s > 300.0f) s = 300.0f;
    float t = s / 300.0f;

    const char* levels[5] = { "▁", "▃", "▅", "▇", "█" };
    int idx = (int)(t * 4.0f + 0.5f);
    if (idx < 0) idx = 0;
    if (idx > 4) idx = 4;

    String bar = levels[idx];
    if      (s <  50.0f) bar += " низкий";
    else if (s < 150.0f) bar += " средний";
    else                 bar += " высокий";
    return bar;
  }

  String formatFloatOrDash(float v, uint8_t digits = 1) {
    if (isnan(v)) return "-";
    return String(v, (int)digits);
  }

  // ---------- клавиатуры (JSON) ----------

  // Главное меню (одна и та же клавиатура для большинства экранов)
  String makeMainKeyboard() {
    String kb;
    kb.reserve(256);
    kb  = "[[\"";
    kb += BTN_STATUS;
    kb += "\"],[\"";
    kb += BTN_CONTROL;
    kb += "\",\"";
    kb += BTN_HISTORY;
    kb += "\"],[\"";
    kb += BTN_DIAG;
    kb += "\",\"";
    kb += BTN_PROFILE;
    kb += "\"],[\"";
    kb += BTN_HELP;
    kb += "\"]]";
    return kb;
  }

  // Клавиатура управления устройствами
  String makeControlKeyboard() {
    String kb;
    kb.reserve(256);
    kb  = "[[\"";
    kb += BTN_LIGHT;
    kb += "\",\"";
    kb += BTN_PUMP;
    kb += "\"],[\"";
    kb += BTN_FAN;
    kb += "\",\"";
    kb += BTN_AUTO;
    kb += "\"],[\"";
    kb += BTN_BACK;
    kb += "\"]]";
    return kb;
  }

  // ---------- экраны ----------

  void sendMainMenu(const String& chatId) {
    if (!bot) return;

    String text;
    text.reserve(512);
    text  = "🌿 *ЙоТик M2 — умная теплица*\n\n";
    text += "Выберите раздел:\n";
    text += "• *Статус* — текущий климат и устройства\n";
    text += "• *Управление* — свет, полив, вентилятор\n";
    text += "• *История за сутки* — полив / свет / почва\n";
    text += "• *Диагностика* — датчики и автоматика\n";
    text += "• *Профиль культуры* — целевые диапазоны\n";

    String kb = makeMainKeyboard();
    bot->sendMessageWithReplyKeyboard(chatId, text, "Markdown", kb, true);
  }

  void sendStatus(const String& chatId) {
    if (!bot) return;

    Automation::DiagInfo d = Automation::getDiagInfo();

    String msg;
    msg.reserve(512);
    msg  = "🌿 *Статус теплицы*\n\n";

    // Воздух
    if (!isnan(g_sensors.airTemp)) {
      msg += "🌡 *Воздух:* ";
      msg += String(g_sensors.airTemp, 1);
      msg += " °C";
      if (!isnan(g_sensors.airHum)) {
        msg += " / ";
        msg += String(g_sensors.airHum, 0);
        msg += " %";
      }
      msg += "\n";
    } else {
      msg += "🌡 *Воздух:* нет данных\n";
    }

    // Почва
    if (!isnan(g_sensors.soilMoisture)) {
      msg += "🌱 *Почва:* ";
      msg += String(g_sensors.soilMoisture, 0);
      msg += " %";
      if (!isnan(g_sensors.soilTemp)) {
        msg += " / ";
        msg += String(g_sensors.soilTemp, 1);
        msg += " °C";
      }
      msg += "\n";
    }

    // Свет
    if (!isnan(g_sensors.lux)) {
      msg += "💡 *Освещённость:* ";
      msg += String(g_sensors.lux, 0);
      msg += " лк\n";
    }

    msg += "\n";

    // Устройства
    msg += "🔌 *Устройства:*\n";
    msg += "• Свет: ";
    msg += onOffIcon(g_sensors.lightOn);
    msg += "\n";

    msg += "• Помпа: ";
    msg += onOffIcon(g_sensors.pumpOn);
    msg += "\n";

    msg += "• Вентилятор: ";
    msg += onOffIcon(g_sensors.fanOn);
    msg += "\n";

    // Режим, профиль, стресс
    msg += "\n🤖 *Режим:* ";
    msg += (g_settings.automationEnabled ? "авто" : "ручной");
    msg += "\n";

    msg += "🌱 *Профиль:* ";
    msg += cropProfileToName(g_settings.cropProfile);
    msg += "\n";

    msg += "📊 *Стресс растений:* ";
    msg += formatStressBar(d.stressTotal);
    msg += "\n";

    String kb = makeMainKeyboard();
    bot->sendMessageWithReplyKeyboard(chatId, msg, "Markdown", kb, true);
  }

  void sendControlMenu(const String& chatId) {
    if (!bot) return;

    String msg;
    msg.reserve(256);
    msg  = "🎛 *Ручное управление*\n\n";
    msg += "Состояние:\n";

    msg += "• Свет: ";
    msg += onOffIcon(g_sensors.lightOn);
    msg += "\n";

    msg += "• Помпа: ";
    msg += onOffIcon(g_sensors.pumpOn);
    msg += "\n";

    msg += "• Вентилятор: ";
    msg += onOffIcon(g_sensors.fanOn);
    msg += "\n\n";

    msg += "Нажимайте кнопки ниже для включения/выключения.\n";

    String kb = makeControlKeyboard();
    bot->sendMessageWithReplyKeyboard(chatId, msg, "Markdown", kb, true);
  }

  void sendDiag(const String& chatId) {
    if (!bot) return;

    Automation::DiagInfo d = Automation::getDiagInfo();

    String msg;
    msg.reserve(512);
    msg  = "🩺 *Диагностика системы*\n\n";

    msg += "📡 *Датчики:*\n";
    msg += "• BME280 (t/влажн/давл): ";
    msg += okIcon(g_sensors.bmeOk);
    msg += "\n";

    msg += "• BH1750 (освещённость): ";
    msg += okIcon(g_sensors.bhOk);
    msg += "\n";

    msg += "• Датчик почвы: ";
    msg += okIcon(g_sensors.soilOk);
    msg += "\n";

    msg += "• RTC (часы): ";
    msg += okIcon(g_sensors.rtcOk);
    msg += "\n\n";

    msg += "🚿 *Насос:*\n";
    msg += "• Текущее состояние: ";
    msg += onOffIcon(g_sensors.pumpOn);
    msg += "\n";

    msg += "• Блокировка safety: ";
    msg += (d.pumpLocked ? "⚠️ включена" : "✅ нет");
    msg += "\n\n";

    msg += "💡 *Свет и адаптация:*\n";

    msg += "• Сдвиг по свету (ON/OFF): ";
    msg += formatFloatOrDash(d.luxOnOffset, 0);
    msg += " / ";
    msg += formatFloatOrDash(d.luxOffOffset, 0);
    msg += " лк\n";

    msg += "• Диапазон сдвига света: ";
    msg += formatFloatOrDash(d.luxAdaptMin, 0);
    msg += " … ";
    msg += formatFloatOrDash(d.luxAdaptMax, 0);
    msg += " лк\n";

    msg += "• Адаптивный порог: ON ";
    msg += formatFloatOrDash(d.dynamicLuxOn, 0);
    msg += " / OFF ";
    msg += formatFloatOrDash(d.dynamicLuxOff, 0);
    msg += " лк\n\n";

    msg += "🌱 *Почва и адаптация:*\n";

    msg += "• Сдвиг setpoint'а: ";
    msg += formatFloatOrDash(d.soilSetpointOffset, 1);
    msg += " %\n";

    msg += "• Диапазон адаптации: ";
    msg += formatFloatOrDash(d.soilAdaptMin, 1);
    msg += " … ";
    msg += formatFloatOrDash(d.soilAdaptMax, 1);
    msg += " %\n\n";

    msg += "📊 *Стресс по факторам:*\n";

    msg += "• Температура: ";
    msg += formatFloatOrDash(d.stressTemp, 1);
    msg += "\n";

    msg += "• Влажность: ";
    msg += formatFloatOrDash(d.stressHum, 1);
    msg += "\n";

    msg += "• Почва: ";
    msg += formatFloatOrDash(d.stressSoil, 1);
    msg += "\n";

    msg += "• Свет: ";
    msg += formatFloatOrDash(d.stressLight, 1);
    msg += "\n";

    msg += "• Итого: ";
    msg += formatStressBar(d.stressTotal);
    msg += "\n";

    String kb = makeMainKeyboard();
    bot->sendMessageWithReplyKeyboard(chatId, msg, "Markdown", kb, true);
  }

  void sendHistory(const String& chatId) {
    if (!bot) return;

    Automation::DiagInfo d = Automation::getDiagInfo();

    String msg;
    msg.reserve(512);
    msg  = "📈 *История за ~24 часа*\n\n";

    msg += "💧 *Полив:*\n";
    if (d.pumpMsDay > 0) {
      uint32_t totalSec = (d.pumpMsDay + 500) / 1000;
      uint32_t min      = totalSec / 60;
      uint32_t sec      = totalSec % 60;

      msg += "• Насос работал ~";
      msg += String(min);
      msg += " мин ";
      msg += String(sec);
      msg += " с\n";
    } else {
      msg += "• За последние 24ч насос не включался\n";
    }

    if (!isnan(d.avgDeltaMoisture)) {
      msg += "• Средний прирост влажности после полива: +";
      msg += String(d.avgDeltaMoisture, 1);
      msg += " %\n";
    }

    if (!isnan(d.avgDrySpeed)) {
      msg += "• Средняя скорость высыхания почвы: ";
      msg += String(d.avgDrySpeed, 1);
      msg += " %/ч\n";
    }

    msg += "\n🌡 *Климат:*\n";

    msg += "• Целевой диапазон по воздуху: ";
    msg += String(g_settings.comfortTempMin, 1);
    msg += "…";
    msg += String(g_settings.comfortTempMax, 1);
    msg += " °C, ";
    msg += String(g_settings.comfortHumMin, 0);
    msg += "…";
    msg += String(g_settings.comfortHumMax, 0);
    msg += " %\n";

    if (!isnan(g_sensors.airTemp) && !isnan(g_sensors.airHum)) {
      msg += "• Сейчас: ";
      msg += String(g_sensors.airTemp, 1);
      msg += " °C / ";
      msg += String(g_sensors.airHum, 0);
      msg += " %\n";
    }

    msg += "\n💡 *Свет:*\n";
    if (d.dailyLuxIntegral > 0.01f) {
      float kLuxHours = d.dailyLuxIntegral / 1000.0f;
      msg += "• Интеграл освещённости за день: ";
      msg += String(kLuxHours, 1);
      msg += " клк·ч\n";
    } else {
      msg += "• Пока недостаточно данных по освещённости\n";
    }

    msg += "• Текущие пороги: ON ";
    msg += formatFloatOrDash(d.dynamicLuxOn, 0);
    msg += " / OFF ";
    msg += formatFloatOrDash(d.dynamicLuxOff, 0);
    msg += " лк\n";

    String kb = makeMainKeyboard();
    bot->sendMessageWithReplyKeyboard(chatId, msg, "Markdown", kb, true);
  }

  void sendProfile(const String& chatId) {
    if (!bot) return;

    CropProfile profile = g_settings.cropProfile;
    String name  = cropProfileToName(profile);
    String hint  = cropProfileHint(profile);

    String msg;
    msg.reserve(512);
    msg  = "🌱 *Профиль культуры*\n\n";

    msg += "Текущий профиль: *";
    msg += name;
    msg += "*\n";

    msg += hint;
    msg += "\n\n";

    msg += "🌡 *Воздух:*\n";
    msg += "• Комфортный диапазон: ";
    msg += String(g_settings.comfortTempMin, 1);
    msg += "…";
    msg += String(g_settings.comfortTempMax, 1);
    msg += " °C\n";

    msg += "• Влажность: ";
    msg += String(g_settings.comfortHumMin, 0);
    msg += "…";
    msg += String(g_settings.comfortHumMax, 0);
    msg += " %\n\n";

    msg += "🌱 *Почва:*\n";
    msg += "• Целевая влажность: ";
    msg += String(g_settings.soilMoistureSetpoint);
    msg += " %\n";

    msg += "• Гистерезис: ±";
    msg += String(g_settings.soilMoistureHyst);
    msg += " %\n\n";

    msg += "💧 *Полив:*\n";
    msg += "• Окно полива: ";
    msg += String(g_settings.waterStartHour);
    msg += ":00…";
    msg += String(g_settings.waterEndHour);
    msg += ":00\n\n";

    msg += "💡 *Свет:*\n";
    msg += "• Ночь (для автоматики света) начинается с ";
    msg += String(g_settings.nightCutoffHour);
    msg += ":00\n\n";

    msg += "Изменение профиля сейчас делается через веб-интерфейс.";

    String kb = makeMainKeyboard();
    bot->sendMessageWithReplyKeyboard(chatId, msg, "Markdown", kb, true);
  }

  void sendHelp(const String& chatId) {
    if (!bot) return;

    String msg;
    msg.reserve(512);
    msg  = "❓ *Справка по боту ЙоТик M2*\n\n";
    msg += "Используйте главное меню и кнопки.\n\n";

    msg += "Доступные разделы:\n";
    msg += "• *Статус* — быстрый обзор климата и устройств\n";
    msg += "• *Управление* — ручное управление светом, помпой, вентилятором\n";
    msg += "• *История за сутки* — сводка по поливам, климату и свету\n";
    msg += "• *Диагностика* — состояние датчиков и адаптации\n";
    msg += "• *Профиль культуры* — целевые диапазоны по выбранной культуре\n\n";

    msg += "Команды (на всякий случай):\n";
    msg += "• /start — открыть меню\n";
    msg += "• /status — статус\n";
    msg += "• /control — управление\n";
    msg += "• /history — история за сутки\n";
    msg += "• /diag — диагностика\n";
    msg += "• /profile — профиль культуры\n";

    String kb = makeMainKeyboard();
    bot->sendMessageWithReplyKeyboard(chatId, msg, "Markdown", kb, true);
  }

  // ---------- обработчик текстов ----------

  void handleTextCommand(const String& chatId, const String& text) {
    // системные команды
    if (text == "/start" || text == "/menu") {
      sendMainMenu(chatId);
      return;
    }
    if (text == "/status" || text == BTN_STATUS) {
      sendStatus(chatId);
      return;
    }
    if (text == "/control" || text == BTN_CONTROL) {
      sendControlMenu(chatId);
      return;
    }
    if (text == "/history" || text == BTN_HISTORY) {
      sendHistory(chatId);
      return;
    }
    if (text == "/diag" || text == BTN_DIAG) {
      sendDiag(chatId);
      return;
    }
    if (text == "/profile" || text == BTN_PROFILE) {
      sendProfile(chatId);
      return;
    }
    if (text == "/help" || text == BTN_HELP) {
      sendHelp(chatId);
      return;
    }

    // кнопка "Назад" из управления
    if (text == BTN_BACK) {
      sendMainMenu(chatId);
      return;
    }

    // управление устройствами
    if (text == BTN_LIGHT) {
      bool st = !g_sensors.lightOn;
      DeviceManager::setLight(st);
      Automation::registerManualLight();
      sendControlMenu(chatId);
      return;
    }
    if (text == BTN_PUMP) {
      DeviceManager::setPump(true);
      Automation::registerManualPump();
      sendControlMenu(chatId);
      return;
    }
    if (text == BTN_FAN) {
      bool st = !g_sensors.fanOn;
      DeviceManager::setFan(st);
      Automation::registerManualFan();
      sendControlMenu(chatId);
      return;
    }
    if (text == BTN_AUTO) {
      g_settings.automationEnabled = !g_settings.automationEnabled;
      sendControlMenu(chatId);
      return;
    }

    // старые команды для совместимости
    if (text == "/auto_on") {
      g_settings.automationEnabled = true;
      sendControlMenu(chatId);
      return;
    }
    if (text == "/auto_off") {
      g_settings.automationEnabled = false;
      sendControlMenu(chatId);
      return;
    }
    if (text == "/water") {
      DeviceManager::setPump(true);
      Automation::registerManualPump();
      sendControlMenu(chatId);
      return;
    }
    if (text == "/light_toggle") {
      bool st = !g_sensors.lightOn;
      DeviceManager::setLight(st);
      Automation::registerManualLight();
      sendControlMenu(chatId);
      return;
    }
    if (text == "/fan_toggle") {
      bool st = !g_sensors.fanOn;
      DeviceManager::setFan(st);
      Automation::registerManualFan();
      sendControlMenu(chatId);
      return;
    }
    if (text == "/auto_toggle") {
      g_settings.automationEnabled = !g_settings.automationEnabled;
      sendControlMenu(chatId);
      return;
    }

    bot->sendMessage(chatId, "Я не понял команду. Нажмите /start, чтобы открыть меню.", "");
  }

} // namespace

// ---------- публичный интерфейс ----------

void TelegramAsync::begin() {
  if (strlen(TelegramConfig::BOT_TOKEN) == 0) {
    Serial.println("[TG] No token, disabled");
    return;
  }
  client.setInsecure();  // упрощённо, как в твоём проекте
  bot = new UniversalTelegramBot(TelegramConfig::BOT_TOKEN, client);
  Serial.println("[TG] Bot initialized");
}

void TelegramAsync::loop() {
  if (!bot) return;

  unsigned long now = millis();
  if (now - lastCheckMs < BOT_INTERVAL_MS) return;
  lastCheckMs = now;

  int numNew = bot->getUpdates(bot->last_message_received + 1);
  while (numNew) {
    for (int i = 0; i < numNew; i++) {
      telegramMessage &m = bot->messages[i];
      String chat_id = String(m.chat_id);
      String type    = m.type;
      String text    = m.text;

      // для твоей версии библиотеки все клавиши идут как type="message"
      if (type == "message") {
        handleTextCommand(chat_id, text);
      }

      // на всякий случай выведем в порт
      Serial.print("[TG] type=");
      Serial.print(type);
      Serial.print(" chat=");
      Serial.print(chat_id);
      Serial.print(" text='");
      Serial.print(text);
      Serial.println("'");
    }
    numNew = bot->getUpdates(bot->last_message_received + 1);
  }
}

void TelegramAsync::sendAlert(const String& text) {
  if (!bot) return;
  if (strlen(TelegramConfig::CHAT_ID) == 0) return;
  bot->sendMessage(TelegramConfig::CHAT_ID, text, "");
}