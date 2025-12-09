// === FILE: WebUiAsync.cpp ===

#include "WebUiAsync.h"
#include "Globals.h"
#include "Config.h"
#include "DeviceManager.h"
#include "Automation.h"
#include "TelemetryLogger.h"
#include "SoilCalibration.h"
#include "Storage.h"

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <cstring>

namespace {

AsyncWebServer server(80);

// ----------------- Wi-Fi -----------------

void setupWifi() {
  WiFi.disconnect(true, true);
  delay(100);

  if (g_settings.wifiSsid[0] != '\0') {
    WiFi.mode(WIFI_STA);
    Serial.printf("[WiFi] Connecting to STA '%s'...\n", g_settings.wifiSsid);
    WiFi.begin(g_settings.wifiSsid, g_settings.wifiPass);

    unsigned long startMs = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startMs < 15000) {
      delay(200);
      Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("[WiFi] STA IP: ");
      Serial.println(WiFi.localIP());
      Serial.println("[WiFi] STA mode only (AP disabled)");
      return;
    } else {
      Serial.println("[WiFi] STA connect failed, switching to AP");
    }
  } else {
    Serial.println("[WiFi] No STA credentials, starting AP");
  }

  WiFi.disconnect(true, true);
  delay(100);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(WifiConfig::AP_SSID, WifiConfig::AP_PASS);
  Serial.print("[WiFi] AP IP: ");
  Serial.println(WiFi.softAPIP());
}

// ----------------- HTML UI -----------------

const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <title>Теплица ЙоТик М2</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
  :root{
    --bg:#05060a;
    --bg-card:#101218;
    --bg-chip:#181b22;
    --accent:#22c55e;
    --accent-soft:rgba(34,197,94,.18);
    --border:#262a33;
    --fg:#f5f7ff;
    --fg-muted:#9ca3af;
    --danger:#ef4444;
  }
  *{box-sizing:border-box;}
  body{
    margin:0;
    font-family:system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Arial,sans-serif;
    background:radial-gradient(circle at top,#111827 0,#020617 45%,#020617 100%);
    color:var(--fg);
  }
  .app{max-width:1200px;margin:0 auto;padding:16px;}
  .topbar{
    display:flex;
    justify-content:space-between;
    align-items:flex-start;
    margin-bottom:16px;
    gap:8px;
    flex-wrap:wrap;
  }
  .topbar h1{margin:0;font-size:1.4rem;}
  .topbar-sub{font-size:.8rem;opacity:.7;}
  .cards{
    display:grid;
    grid-template-columns:repeat(auto-fit,minmax(260px,1fr));
    gap:16px;
  }
  .card{
    background:var(--bg-card);
    border-radius:20px;
    padding:16px;
    border:1px solid rgba(148,163,184,.18);
    box-shadow:0 18px 45px rgba(15,23,42,.8);
  }
  .card h2{margin:0 0 8px;font-size:1.05rem;}
  .metrics{
    display:flex;
    flex-wrap:wrap;
    gap:10px;
    margin-top:8px;
  }
  .metric{
    flex:1 1 45%;
    min-width:120px;
    background:var(--bg-chip);
    border-radius:12px;
    padding:8px;
  }
  .metric-label{font-size:.7rem;opacity:.7;}
  .metric-value{font-size:1.15rem;margin-top:4px;}
  .metric-unit{font-size:.7rem;opacity:.7;margin-left:4px;}

  .row{
    display:flex;
    flex-wrap:wrap;
    gap:12px;
    margin-top:8px;
  }
  .row>div{flex:1 1 120px;}
  label{
    display:block;
    font-size:.8rem;
    margin-bottom:4px;
    color:var(--fg-muted);
  }
  input[type=number],
  input[type=text],
  input[type=password],
  select{
    width:100%;
    padding:6px 9px;
    border-radius:999px;
    border:1px solid var(--border);
    background:#020617;
    color:var(--fg);
    font-size:.85rem;
  }
  input[type=range]{width:100%;}
  button{
    border:none;
    border-radius:999px;
    padding:6px 12px;
    font-size:.85rem;
    cursor:pointer;
    background:rgba(15,23,42,.9);
    border:1px solid var(--border);
    color:white;
    display:inline-flex;
    align-items:center;
    gap:6px;
    transition:background .15s,transform .05s,box-shadow .15s,border-color .15s,color .15s;
  }
  button:hover{
    transform:translateY(-1px);
    box-shadow:0 10px 30px rgba(15,23,42,.9);
  }
  button.outline{background:transparent;border:1px solid var(--border);}
  button.small{font-size:.75rem;padding:4px 10px;}
  button.danger{background:var(--danger);}

  .btn-toggle{
    min-width:110px;
    justify-content:center;
  }
  .btn-toggle.on{
    background:var(--accent-soft);
    border-color:var(--accent);
    color:var(--accent);
    box-shadow:0 0 0 1px rgba(34,197,94,.35);
  }
  .btn-toggle.on:hover{
    box-shadow:0 10px 30px rgba(34,197,94,.45);
  }

  .status{font-size:.75rem;margin-top:6px;color:var(--fg-muted);}
  .flag-ok{color:#4ade80;}
  .flag-bad{color:#f97373;}
  </style>
</head>
<body>
<div class="app">
  <div class="topbar">
    <div>
      <h1>Теплица ЙоТик М2</h1>
      <div class="topbar-sub">Мониторинг, управление, OTA по Wi-Fi</div>
    </div>
    <div id="top-status" class="topbar-sub"></div>
  </div>

  <div class="cards">

    <!-- Показания датчиков -->
    <div class="card">
      <h2>Показания датчиков</h2>
      <div id="metrics" class="metrics"></div>
      <div id="sensor-flags" class="status"></div>
    </div>

    <!-- Ручное управление -->
    <div class="card">
      <h2>Ручное управление</h2>
      <div>
        <button id="btn-light" class="btn-toggle" onclick="toggleDevice('light')">Свет</button>
        <button id="btn-pump" class="btn-toggle outline" onclick="pulsePump()">Пульс насоса</button>
        <button id="btn-fan" class="btn-toggle outline" onclick="toggleDevice('fan')">Вентилятор</button>
      </div>
      <div style="margin-top:10px;">
        <label>Окно / форточка</label>
        <button class="small" onclick="doorAction('open')">Открыть</button>
        <button class="small" onclick="doorAction('close')">Закрыть</button>
      </div>
    </div>

    <!-- Подсветка -->
    <div class="card">
      <h2>Подсветка</h2>
      <div class="row">
        <div>
          <label>Яркость, %</label>
          <input type="range" id="lightBrightness" min="0" max="100" step="5" oninput="updateLightUi()">
          <div class="status">Текущее значение: <span id="lightBrightnessValue">0</span>%</div>
        </div>
      </div>
      <div class="row">
        <div>
          <label>Цвет R (красный)</label>
          <input type="range" id="lightColorR" min="0" max="255" step="5" oninput="updateLightUi()">
          <div class="status">R: <span id="lightColorRValue">0</span></div>
        </div>
      </div>
      <div class="row">
        <div>
          <label>Цвет G (зелёный)</label>
          <input type="range" id="lightColorG" min="0" max="255" step="5" oninput="updateLightUi()">
          <div class="status">G: <span id="lightColorGValue">0</span></div>
        </div>
      </div>
      <div class="row">
        <div>
          <label>Цвет B (синий)</label>
          <input type="range" id="lightColorB" min="0" max="255" step="5" oninput="updateLightUi()">
          <div class="status">B: <span id="lightColorBValue">0</span></div>
        </div>
      </div>
      <div style="margin-top:12px;">
        <button type="button" onclick="saveSettings()">Сохранить</button>
      </div>
    </div>

    <!-- Настройки автоматики -->
    <div class="card">
      <h2>Настройки автоматики</h2>
      <form id="settings-form" onsubmit="saveSettings();return false;">
        <div class="row">
          <div>
            <label>Температура комфорт MIN, °C</label>
            <input type="number" step="0.5" id="comfortTempMin">
          </div>
          <div>
            <label>Температура комфорт MAX, °C</label>
            <input type="number" step="0.5" id="comfortTempMax">
          </div>
        </div>
        <div class="row">
          <div>
            <label>Влажность комфорт MIN, %</label>
            <input type="number" step="1" id="comfortHumMin">
          </div>
          <div>
            <label>Влажность комфорт MAX, %</label>
            <input type="number" step="1" id="comfortHumMax">
          </div>
        </div>
        <div class="row">
          <div>
            <label>Влажность почвы, целевое значение, %</label>
            <input type="number" step="1" id="soilMoistureSetpoint">
          </div>
          <div>
            <label>Гистерезис почвы, %</label>
            <input type="number" step="1" id="soilMoistureHyst">
          </div>
        </div>
        <div class="row">
          <div>
            <label>Окно полива: начало, час</label>
            <input type="number" step="1" min="0" max="23" id="waterStartHour">
          </div>
          <div>
            <label>Окно полива: конец, час</label>
            <input type="number" step="1" min="0" max="23" id="waterEndHour">
          </div>
        </div>
        <div class="row">
          <div>
            <label>Режим климата</label>
            <select id="climateMode">
              <option value="0">Эко</option>
              <option value="1">Норма</option>
              <option value="2">Агрессивный</option>
            </select>
          </div>
          <div>
            <label>Профиль культуры</label>
            <select id="cropProfile">
              <option value="1">Томаты</option>
              <option value="2">Огурцы</option>
              <option value="3">Зелень</option>
              <option value="4">Гибискус</option>
              <option value="0">Своя настройка</option>
            </select>
          </div>
        </div>
        <div class="row">
          <div>
            <label><input type="checkbox" id="automationEnabled"> Автоматика включена</label>
          </div>
          <div>
            <label><input type="checkbox" id="notificationsEnabled"> Телеграм-уведомления</label>
          </div>
        </div>
        <div class="row">
          <div>
            <label>Смещение T почвы, °C</label>
            <input type="number" step="0.1" id="soilTempOffset">
          </div>
        </div>
        <div style="margin-top:12px;">
          <button type="submit">Сохранить всё</button>
        </div>
      </form>
    </div>

    <!-- Wi-Fi -->
    <div class="card">
      <h2>Wi-Fi</h2>
      <div class="row">
        <div>
          <label>SSID (имя сети)</label>
          <input type="text" id="wifiSsid" placeholder="MyWiFi">
        </div>
      </div>
      <div class="row">
        <div>
          <label>Пароль</label>
          <input type="password" id="wifiPass" placeholder="********">
        </div>
      </div>
      <div class="status">
        После изменения SSID/пароля устройство применит настройки после перезагрузки.
      </div>
      <div style="margin-top:12px;">
        <button type="button" onclick="saveSettings()">Сохранить Wi-Fi</button>
      </div>
    </div>

    <!-- Калибровка почвы -->
    <div class="card">
      <h2>Калибровка датчика почвы</h2>
      <div class="status">
        Сначала запишите <b>сухой</b> уровень (датчик вне горшка), затем <b>мокрый</b> (во влажной земле).
      </div>
      <div style="margin-top:8px;">
        <button class="small outline" onclick="soilCalib('dry')">Записать сухой</button>
        <button class="small outline" onclick="soilCalib('wet')">Записать мокрый</button>
      </div>
    </div>

    <!-- Диагностика автоматики -->
    <div class="card">
      <h2>Диагностика автоматики</h2>
      <div class="status">
        Насос за ~24 часа: <span id="diagPumpMinutes">0</span> мин
        (<span id="diagPumpLocked">норма</span>)
      </div>
      <div class="row">
        <div>
          <label>Скорость высыхания почвы, %/ч</label>
          <div class="status"><span id="diagDrySpeed">—</span></div>
        </div>
        <div>
          <label>Прирост влажности от полива, %</label>
          <div class="status"><span id="diagDeltaMoisture">—</span></div>
        </div>
      </div>
      <div class="row">
        <div>
          <label>Адаптация полива (сдвиг setpoint), %</label>
          <div class="status"><span id="diagSoilOffset">0</span></div>
        </div>
        <div>
          <label>Пределы адаптации полива, % (мин / макс)</label>
          <input type="number" step="0.5" id="soilAdaptMin">
          <input type="number" step="0.5" id="soilAdaptMax" style="margin-top:4px;">
        </div>
      </div>
      <div class="row">
        <div>
          <label>Адаптация света (сдвиг порогов On/Off), лк</label>
          <div class="status">
            On: <span id="diagLuxOnOffset">0</span>,
            Off: <span id="diagLuxOffOffset">0</span>
          </div>
        </div>
        <div>
          <label>Пределы адаптации света, лк (мин / макс)</label>
          <input type="number" step="1" id="luxAdaptMin">
          <input type="number" step="1" id="luxAdaptMax" style="margin-top:4px;">
        </div>
      </div>
      <div class="row">
        <div>
          <label>Текущие пороги света, лк (On / Off)</label>
          <div class="status">
            <span id="diagDynamicLuxOn">—</span> /
            <span id="diagDynamicLuxOff">—</span>
          </div>
        </div>
        <div>
          <label>Интеграл света (условный световой день), lux·ч</label>
          <div class="status"><span id="diagDailyLuxIntegral">—</span></div>
        </div>
      </div>
      <div class="row">
        <div>
          <label>Стресс (T / RH / почва / свет / всего)</label>
          <div class="status">
            <span id="diagStressTemp">0</span> /
            <span id="diagStressHum">0</span> /
            <span id="diagStressSoil">0</span> /
            <span id="diagStressLight">0</span> /
            <b><span id="diagStressTotal">0</span></b>
          </div>
        </div>
      </div>
      <div style="margin-top:12px;">
        <button type="button" onclick="saveDiagLimits()">Сохранить пределы адаптации</button>
      </div>
    </div>

    <!-- OTA -->
    <div class="card">
      <h2>OTA-обновление прошивки</h2>
      <div class="status">
        Откройте страницу <code>/update</code>, чтобы загрузить новый бинарник.
      </div>
      <a href="/update">
        <button class="small outline" type="button">Перейти к OTA</button>
      </a>
    </div>

  </div>
</div>

<script>
  function el(id){return document.getElementById(id);}

  function fmt1(v){
    if(v===null || v===undefined || isNaN(v)) return '—';
    return Number(v).toFixed(1);
  }
  function fmt0(v){
    if(v===null || v===undefined || isNaN(v)) return '—';
    return Number(v).toFixed(0);
  }

  function metric(label,value,unit,badge){
    return '<div class="metric">'
      +'<div class="metric-label">'+label+'</div>'
      +'<div class="metric-value">'+value+(unit?'<span class="metric-unit">'+unit+'</span>':'')+'</div>'
      +(badge?'<div class="metric-badge">'+badge+'</div>':'')
      +'</div>';
  }

  async function fetchJson(url, opts){
    const r = await fetch(url, opts || {});
    if(!r.ok) throw new Error(await r.text());
    return await r.json();
  }

  async function loadSensors(){
    try{
      const s = await fetchJson('/api/sensors');
      const m = s.metrics || {};
      const out = s.outputs || {};
      const fl = s.flags   || {};

      let html = '';
      html += metric('T воздуха', fmt1(m.airTemp), '°C', '');
      html += metric('Влажность', fmt1(m.airHum), '%', '');
      html += metric('Почва', fmt1(m.soilMoisture), '%', '');
      html += metric('T почвы', fmt1(m.soilTemp), '°C', '');
      html += metric('Давление', fmt1(m.airPressure), 'гПа', '');
      html += metric('Освещённость', fmt1(m.lux), 'лк', '');
      html += metric('Насос', out.pumpOn ? 'ВКЛ' : 'ВЫКЛ', '', '');
      html += metric('Свет', out.lightOn ? 'ВКЛ' : 'ВЫКЛ', '', '');
      html += metric('Вентилятор', out.fanOn ? 'ВКЛ' : 'ВЫКЛ', '', '');
      el('metrics').innerHTML = html;

      const btnLight = el('btn-light');
      const btnPump  = el('btn-pump');
      const btnFan   = el('btn-fan');

      if (btnLight) {
        btnLight.textContent = out.lightOn ? 'Свет: ВКЛ' : 'Свет: ВЫКЛ';
        btnLight.classList.toggle('on', !!out.lightOn);
      }

      if (btnPump) {
        btnPump.textContent = out.pumpOn ? 'Пульс насоса (ВКЛ)' : 'Пульс насоса';
        btnPump.classList.toggle('on', !!out.pumpOn);
      }

      if (btnFan) {
        btnFan.textContent = out.fanOn ? 'Вентилятор: ВКЛ' : 'Вентилятор: ВЫКЛ';
        btnFan.classList.toggle('on', !!out.fanOn);
      }

      const flagsStr = [
        'BME280: ' + (fl.bmeOk ? 'OK' : 'нет'),
        'BH1750: ' + (fl.bhOk ? 'OK' : 'нет'),
        'Почва: ' + (fl.soilOk ? 'OK' : 'нет'),
        'RTC: '    + (fl.rtcOk ? 'OK' : 'нет')
      ].join(' · ');
      el('sensor-flags').textContent = flagsStr;

    }catch(e){
      console.error(e);
    }
  }

  async function toggleDevice(target){
    try{
      await fetch('/api/control',{
        method:'POST',
        headers:{'Content-Type':'application/json'},
        body:JSON.stringify({target,action:'toggle'})
      });
      loadSensors();
    }catch(e){console.error(e);}
  }

  async function pulsePump(){
    try{
      await fetch('/api/control',{
        method:'POST',
        headers:{'Content-Type':'application/json'},
        body:JSON.stringify({target:'pump',action:'pulse'})
      });
      loadSensors();
    }catch(e){console.error(e);}
  }

  async function doorAction(action){
    try{
      await fetch('/api/control',{
        method:'POST',
        headers:{'Content-Type':'application/json'},
        body:JSON.stringify({target:'door',action})
      });
      loadSensors();
    }catch(e){console.error(e);}
  }

  function updateLightUi(){
    const bEl  = el('lightBrightness');
    const rEl  = el('lightColorR');
    const gEl  = el('lightColorG');
    const b2El = el('lightColorB');

    if (bEl)  el('lightBrightnessValue').textContent = bEl.value;
    if (rEl)  el('lightColorRValue').textContent     = rEl.value;
    if (gEl)  el('lightColorGValue').textContent     = gEl.value;
    if (b2El) el('lightColorBValue').textContent     = b2El.value;
  }

  async function loadSettings(){
    try{
      const s = await fetchJson('/api/settings');
      el('comfortTempMin').value       = s.comfortTempMin;
      el('comfortTempMax').value       = s.comfortTempMax;
      el('comfortHumMin').value        = s.comfortHumMin;
      el('comfortHumMax').value        = s.comfortHumMax;
      el('soilMoistureSetpoint').value = s.soilMoistureSetpoint;
      el('soilMoistureHyst').value     = s.soilMoistureHyst;
      el('waterStartHour').value       = s.waterStartHour;
      el('waterEndHour').value         = s.waterEndHour;
      el('climateMode').value          = s.climateMode;
      el('cropProfile').value          = s.cropProfile;
      el('soilTempOffset').value       = s.soilTempOffset;

      el('lightBrightness').value      = s.lightBrightness;
      el('lightColorR').value          = s.lightColorR;
      el('lightColorG').value          = s.lightColorG;
      el('lightColorB').value          = s.lightColorB;
      updateLightUi();

      el('automationEnabled').checked    = !!s.automationEnabled;
      el('notificationsEnabled').checked = !!s.notificationsEnabled;

      if (el('wifiSsid')) el('wifiSsid').value = s.wifiSsid || '';
      if (el('wifiPass')) el('wifiPass').value = s.wifiPass || '';

    }catch(e){console.error(e);}
  }

  async function saveSettings(){
    const payload = {
      comfortTempMin: parseFloat(el('comfortTempMin').value),
      comfortTempMax: parseFloat(el('comfortTempMax').value),
      comfortHumMin:  parseFloat(el('comfortHumMin').value),
      comfortHumMax:  parseFloat(el('comfortHumMax').value),
      soilMoistureSetpoint: parseInt(el('soilMoistureSetpoint').value || '0',10),
      soilMoistureHyst:     parseInt(el('soilMoistureHyst').value || '0',10),
      waterStartHour:       parseInt(el('waterStartHour').value || '0',10),
      waterEndHour:         parseInt(el('waterEndHour').value || '0',10),
      climateMode:          parseInt(el('climateMode').value || '0',10),
      cropProfile:          parseInt(el('cropProfile').value || '0',10),
      soilTempOffset:       parseFloat(el('soilTempOffset').value),

      lightBrightness:      parseInt(el('lightBrightness').value || '0',10),
      lightColorR:          parseInt(el('lightColorR').value || '0',10),
      lightColorG:          parseInt(el('lightColorG').value || '0',10),
      lightColorB:          parseInt(el('lightColorB').value || '0',10),

      automationEnabled:    !!el('automationEnabled').checked,
      notificationsEnabled: !!el('notificationsEnabled').checked,
      wifiSsid:             el('wifiSsid') ? (el('wifiSsid').value || '') : '',
      wifiPass:             el('wifiPass') ? (el('wifiPass').value || '') : ''
    };

    try{
      await fetch('/api/settings',{
        method:'POST',
        headers:{'Content-Type':'application/json'},
        body:JSON.stringify(payload)
      });
      el('top-status').textContent = 'Настройки сохранены';
      setTimeout(()=>{el('top-status').textContent='';},3000);
    }catch(e){
      console.error(e);
      el('top-status').textContent = 'Ошибка сохранения';
      setTimeout(()=>{el('top-status').textContent='';},3000);
    }
  }

  async function soilCalib(mode){
    try{
      const fd = new FormData();
      fd.append('mode', mode);
      await fetch('/api/soil_calibration',{method:'POST',body:fd});
      el('top-status').textContent = (mode==='dry'?'Сухой':'Мокрый')+' уровень сохранён';
      setTimeout(()=>{el('top-status').textContent='';},3000);
    }catch(e){console.error(e);}
  }

  async function loadDiag(){
    try{
      const d = await fetchJson('/api/diag');

      const pumpMinutes = Math.round((d.pumpMsDay || 0) / 60000);
      el('diagPumpMinutes').textContent = pumpMinutes;
      el('diagPumpLocked').textContent  = d.pumpLocked ? 'блокирован' : 'норма';
      el('diagPumpLocked').className    = d.pumpLocked ? 'status flag-bad' : 'status flag-ok';

      el('diagDrySpeed').textContent =
        (d.avgDrySpeed === null || d.avgDrySpeed === undefined || isNaN(d.avgDrySpeed))
          ? '—' : d.avgDrySpeed.toFixed(2);

      el('diagDeltaMoisture').textContent =
        (d.avgDeltaMoisture === null || d.avgDeltaMoisture === undefined || isNaN(d.avgDeltaMoisture))
          ? '—' : d.avgDeltaMoisture.toFixed(1);

      el('diagSoilOffset').textContent  = d.soilSetpointOffset.toFixed(2);
      el('soilAdaptMin').value          = d.soilAdaptMin.toFixed(1);
      el('soilAdaptMax').value          = d.soilAdaptMax.toFixed(1);

      el('diagLuxOnOffset').textContent  = d.luxOnOffset.toFixed(1);
      el('diagLuxOffOffset').textContent = d.luxOffOffset.toFixed(1);
      el('luxAdaptMin').value            = d.luxAdaptMin.toFixed(1);
      el('luxAdaptMax').value            = d.luxAdaptMax.toFixed(1);

      el('diagDynamicLuxOn').textContent  = d.dynamicLuxOn.toFixed(1);
      el('diagDynamicLuxOff').textContent = d.dynamicLuxOff.toFixed(1);
      el('diagDailyLuxIntegral').textContent = d.dailyLuxIntegral.toFixed(0);

      el('diagStressTemp').textContent   = d.stressTemp.toFixed(1);
      el('diagStressHum').textContent    = d.stressHum.toFixed(1);
      el('diagStressSoil').textContent   = d.stressSoil.toFixed(1);
      el('diagStressLight').textContent  = d.stressLight.toFixed(1);
      el('diagStressTotal').textContent  = d.stressTotal.toFixed(1);

    }catch(e){
      console.error(e);
    }
  }

  async function saveDiagLimits(){
    const payload = {
      soilAdaptMin: parseFloat(el('soilAdaptMin').value),
      soilAdaptMax: parseFloat(el('soilAdaptMax').value),
      luxAdaptMin:  parseFloat(el('luxAdaptMin').value),
      luxAdaptMax:  parseFloat(el('luxAdaptMax').value)
    };
    try{
      await fetch('/api/diag_limits',{
        method:'POST',
        headers:{'Content-Type':'application/json'},
        body:JSON.stringify(payload)
      });
      el('top-status').textContent = 'Пределы адаптации сохранены';
      setTimeout(()=>{el('top-status').textContent='';},3000);
      loadDiag();
    }catch(e){
      console.error(e);
      el('top-status').textContent = 'Ошибка сохранения пределов адаптации';
      setTimeout(()=>{el('top-status').textContent='';},3000);
    }
  }

  async function init(){
    await loadSettings();
    await loadSensors();
    await loadDiag();
    setInterval(loadSensors, 3000);
    setInterval(loadDiag, 10000);
  }

  document.addEventListener('DOMContentLoaded', init);
</script>
</body>
</html>
)HTML";

// ----------------- API handlers -----------------

void handleRoot(AsyncWebServerRequest *request) {
  request->send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

void handleApiSensors(AsyncWebServerRequest *request) {
  DynamicJsonDocument doc(1024);

  auto m   = doc.createNestedObject("metrics");
  auto fl  = doc.createNestedObject("flags");
  auto out = doc.createNestedObject("outputs");

  m["airTemp"]      = g_sensors.airTemp;
  m["airHum"]       = g_sensors.airHum;
  m["soilMoisture"] = g_sensors.soilMoisture;
  m["soilTemp"]     = g_sensors.soilTemp;
  m["airPressure"]  = g_sensors.airPressure;
  m["lux"]          = g_sensors.lux;

  fl["bmeOk"]   = g_sensors.bmeOk;
  fl["bhOk"]    = g_sensors.bhOk;
  fl["rtcOk"]   = g_sensors.rtcOk;
  fl["soilOk"]  = g_sensors.soilOk;

  out["lightOn"] = g_sensors.lightOn;
  out["pumpOn"]  = g_sensors.pumpOn;
  out["fanOn"]   = g_sensors.fanOn;

  String outStr;
  serializeJson(doc, outStr);
  request->send(200, "application/json", outStr);
}

void handleApiSettingsGet(AsyncWebServerRequest *request) {
  DynamicJsonDocument doc(1024);

  doc["comfortTempMin"]       = g_settings.comfortTempMin;
  doc["comfortTempMax"]       = g_settings.comfortTempMax;
  doc["comfortHumMin"]        = g_settings.comfortHumMin;
  doc["comfortHumMax"]        = g_settings.comfortHumMax;
  doc["soilMoistureSetpoint"] = g_settings.soilMoistureSetpoint;
  doc["soilMoistureHyst"]     = g_settings.soilMoistureHyst;
  doc["waterStartHour"]       = g_settings.waterStartHour;
  doc["waterEndHour"]         = g_settings.waterEndHour;
  doc["climateMode"]          = (int)g_settings.climateMode;
  doc["cropProfile"]          = (int)g_settings.cropProfile;
  doc["soilTempOffset"]       = g_settings.soilTempOffset;

  doc["lightBrightness"]      = g_settings.lightBrightness;
  doc["lightColorR"]          = g_settings.lightColorR;
  doc["lightColorG"]          = g_settings.lightColorG;
  doc["lightColorB"]          = g_settings.lightColorB;

  doc["automationEnabled"]    = g_settings.automationEnabled;
  doc["notificationsEnabled"] = g_settings.notificationsEnabled;
  doc["wifiSsid"]             = g_settings.wifiSsid;
  doc["wifiPass"]             = g_settings.wifiPass;

  String outStr;
  serializeJson(doc, outStr);
  request->send(200, "application/json", outStr);
}

void handleApiSettingsPost(AsyncWebServerRequest *request, uint8_t *data, size_t len,
                           size_t index, size_t total) {
  (void)index;
  (void)total;

  StaticJsonDocument<1024> doc;
  DeserializationError err = deserializeJson(doc, data, len);
  if (err) {
    Serial.print(F("[API] settings JSON error: "));
    Serial.println(err.c_str());
    request->send(400, "text/plain", "Bad JSON");
    return;
  }

  auto getF = [&](const char* key, float def)->float {
    return doc.containsKey(key) ? doc[key].as<float>() : def;
  };
  auto getI = [&](const char* key, int def)->int {
    return doc.containsKey(key) ? doc[key].as<int>() : def;
  };
  auto getB = [&](const char* key, bool def)->bool {
    return doc.containsKey(key) ? doc[key].as<bool>() : def;
  };

  g_settings.comfortTempMin       = getF("comfortTempMin", g_settings.comfortTempMin);
  g_settings.comfortTempMax       = getF("comfortTempMax", g_settings.comfortTempMax);
  g_settings.comfortHumMin        = getF("comfortHumMin",  g_settings.comfortHumMin);
  g_settings.comfortHumMax        = getF("comfortHumMax",  g_settings.comfortHumMax);
  g_settings.soilMoistureSetpoint = (uint8_t)getI("soilMoistureSetpoint", g_settings.soilMoistureSetpoint);
  g_settings.soilMoistureHyst     = (uint8_t)getI("soilMoistureHyst",     g_settings.soilMoistureHyst);
  g_settings.waterStartHour       = (uint8_t)getI("waterStartHour", g_settings.waterStartHour);
  g_settings.waterEndHour         = (uint8_t)getI("waterEndHour",   g_settings.waterEndHour);
  g_settings.climateMode          = (ClimateMode)getI("climateMode", (int)g_settings.climateMode);
  g_settings.cropProfile          = (CropProfile)getI("cropProfile", (int)g_settings.cropProfile);
  g_settings.soilTempOffset       = getF("soilTempOffset", g_settings.soilTempOffset);

  g_settings.lightBrightness      = (uint8_t)getI("lightBrightness", g_settings.lightBrightness);
  g_settings.lightColorR          = (uint8_t)getI("lightColorR", g_settings.lightColorR);
  g_settings.lightColorG          = (uint8_t)getI("lightColorG", g_settings.lightColorG);
  g_settings.lightColorB          = (uint8_t)getI("lightColorB", g_settings.lightColorB);

  g_settings.automationEnabled    = getB("automationEnabled", g_settings.automationEnabled);
  g_settings.notificationsEnabled = getB("notificationsEnabled", g_settings.notificationsEnabled);

  if (doc.containsKey("wifiSsid")) {
    const char* s = doc["wifiSsid"];
    std::strncpy(g_settings.wifiSsid, s ? s : "", sizeof(g_settings.wifiSsid)-1);
    g_settings.wifiSsid[sizeof(g_settings.wifiSsid)-1] = '\0';
  }
  if (doc.containsKey("wifiPass")) {
    const char* s = doc["wifiPass"];
    std::strncpy(g_settings.wifiPass, s ? s : "", sizeof(g_settings.wifiPass)-1);
    g_settings.wifiPass[sizeof(g_settings.wifiPass)-1] = '\0';
  }

  Storage::saveSettings(g_settings);
  Automation::updateDynamicWaterWindow();

  request->send(200, "text/plain", "OK");
}

void handleApiControl(AsyncWebServerRequest *request, uint8_t *data, size_t len,
                      size_t index, size_t total) {
  (void)index;
  (void)total;

  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, data, len);
  if (err) {
    request->send(400, "text/plain", "Bad JSON");
    return;
  }

  String target = doc["target"] | "";
  String action = doc["action"] | "";

  if (target == "light") {
    if (action == "toggle") {
      DeviceManager::setLight(!g_sensors.lightOn);
      Automation::registerManualLight();
    }
  } else if (target == "pump") {
    if (action == "toggle") {
      DeviceManager::setPump(!g_sensors.pumpOn);
      Automation::registerManualPump();
    } else if (action == "pulse") {
      DeviceManager::setPump(true);
      Automation::registerManualPump();
    }
  } else if (target == "fan") {
    if (action == "toggle") {
      DeviceManager::setFan(!g_sensors.fanOn);
      Automation::registerManualFan();
    }
  } else if (target == "door") {
    if (action == "open") {
      DeviceManager::setDoorAngle(100);
      Automation::registerManualDoor();
    } else if (action == "close") {
      DeviceManager::setDoorAngle(0);
      Automation::registerManualDoor();
    }
  } else {
    request->send(400, "text/plain", "Unknown target");
    return;
  }

  request->send(200, "text/plain", "OK");
}

void handleApiSoilCalibration(AsyncWebServerRequest *request) {
  if (!request->hasParam("mode", true)) {
    request->send(400, "text/plain", "Missing mode");
    return;
  }
  String mode = request->getParam("mode", true)->value();
  if (mode != "dry" && mode != "wet") {
    request->send(400, "text/plain", "Unknown mode");
    return;
  }
  SoilCalibration::handleMode(mode);
  request->send(200, "text/plain",
    mode == "dry" ? "Сухой уровень сохранён" : "Мокрый уровень сохранён");
}

// --- Диагностика автоматики ---

void handleApiDiagGet(AsyncWebServerRequest *request) {
  Automation::DiagInfo info = Automation::getDiagInfo();
  DynamicJsonDocument doc(512);

  doc["pumpMsDay"]          = info.pumpMsDay;
  doc["pumpLocked"]         = info.pumpLocked;

  doc["soilSetpointOffset"] = info.soilSetpointOffset;
  doc["soilAdaptMin"]       = info.soilAdaptMin;
  doc["soilAdaptMax"]       = info.soilAdaptMax;

  doc["luxOnOffset"]        = info.luxOnOffset;
  doc["luxOffOffset"]       = info.luxOffOffset;
  doc["luxAdaptMin"]        = info.luxAdaptMin;
  doc["luxAdaptMax"]        = info.luxAdaptMax;

  doc["avgDrySpeed"]        = info.avgDrySpeed;
  doc["avgDeltaMoisture"]   = info.avgDeltaMoisture;

  doc["dailyLuxIntegral"]   = info.dailyLuxIntegral;
  doc["dynamicLuxOn"]       = info.dynamicLuxOn;
  doc["dynamicLuxOff"]      = info.dynamicLuxOff;

  doc["stressTemp"]         = info.stressTemp;
  doc["stressHum"]          = info.stressHum;
  doc["stressSoil"]         = info.stressSoil;
  doc["stressLight"]        = info.stressLight;
  doc["stressTotal"]        = info.stressTotal;

  String out;
  serializeJson(doc, out);
  request->send(200, "application/json", out);
}

void handleApiDiagLimitsPost(AsyncWebServerRequest *request,
                             uint8_t *data, size_t len,
                             size_t index, size_t total) {
  (void)index;
  (void)total;

  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, data, len);
  if (err) {
    request->send(400, "text/plain", "Bad JSON");
    return;
  }

  float soilMin = doc["soilAdaptMin"] | -10.0f;
  float soilMax = doc["soilAdaptMax"] |  10.0f;
  float luxMin  = doc["luxAdaptMin"]  | -20.0f;
  float luxMax  = doc["luxAdaptMax"]  |  20.0f;

  Automation::setAdaptationLimits(soilMin, soilMax, luxMin, luxMax);
  request->send(200, "text/plain", "OK");
}

// -------- OTA /update --------

void setupOtaRoutes() {
  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request){
    const char* html =
      "<!DOCTYPE html><html><head><meta charset='utf-8'><title>OTA update</title></head>"
      "<body><h2>OTA обновление прошивки</h2>"
      "<form method='POST' action='/update' enctype='multipart/form-data'>"
      "<input type='file' name='firmware'>"
      "<input type='submit' value='Прошить'>"
      "</form></body></html>";
    request->send(200, "text/html", html);
  });

  server.on("/update", HTTP_POST,
    [](AsyncWebServerRequest *request){
      bool ok = !Update.hasError();
      AsyncWebServerResponse *resp =
        request->beginResponse(200, "text/plain", ok ? "OK" : "Update failed");
      resp->addHeader("Connection", "close");
      request->send(resp);
      if (ok) {
        Serial.println("[OTA] Update ok, restarting");
        delay(500);
        ESP.restart();
      } else {
        Serial.println("[OTA] Update failed");
      }
    },
    [](AsyncWebServerRequest *request, const String& filename, size_t index,
       uint8_t *data, size_t len, bool final){
      if (index == 0) {
        Serial.printf("[OTA] Upload start: %s\n", filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
          Serial.println("[OTA] Update.begin failed");
        }
      }
      if (!Update.hasError()) {
        if (Update.write(data, len) != len) {
          Serial.println("[OTA] Write failed");
        }
      }
      if (final) {
        if (Update.end(true)) {
          Serial.printf("[OTA] Update success, %u bytes\n", index + len);
        } else {
          Serial.println("[OTA] Update.end failed");
        }
      }
    });
}

} // namespace

// ----------------- PUBLIC -----------------

void WebUiAsync::begin() {
  setupWifi();

  server.on("/", HTTP_GET, handleRoot);

  server.on("/api/sensors", HTTP_GET, handleApiSensors);

  server.on("/api/settings", HTTP_GET, handleApiSettingsGet);
  server.on("/api/settings", HTTP_POST, [](AsyncWebServerRequest *r){},
            NULL, handleApiSettingsPost);

  server.on("/api/control", HTTP_POST, [](AsyncWebServerRequest *r){},
            NULL, handleApiControl);

  server.on("/api/soil_calibration", HTTP_POST, handleApiSoilCalibration);

  // диагностика
  server.on("/api/diag", HTTP_GET, handleApiDiagGet);
  server.on("/api/diag_limits", HTTP_POST, [](AsyncWebServerRequest *r){},
            NULL, handleApiDiagLimitsPost);

  // OTA /update
  setupOtaRoutes();

  server.begin();
  Serial.println("[Web] Async server started");
}

void WebUiAsync::loop() {
  // AsyncWebServer сам крутится, тут ничего не нужно
}