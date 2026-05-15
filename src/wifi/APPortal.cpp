#include "APPortal.h"

static const char PORTAL_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>mpcb-iot — WiFi Setup</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{font-family:sans-serif;background:#1a1a2e;color:#eee;min-height:100vh;display:flex;align-items:center;justify-content:center}
  .card{background:#16213e;border-radius:16px;padding:32px;width:100%;max-width:400px;box-shadow:0 8px 32px rgba(0,0,0,.4)}
  h1{font-size:1.4rem;margin-bottom:8px;color:#e94560}
  p.sub{font-size:.85rem;color:#888;margin-bottom:24px}
  label{display:block;font-size:.85rem;color:#aaa;margin-bottom:4px;margin-top:16px}
  input{width:100%;padding:10px 14px;border-radius:8px;border:1px solid #333;background:#0f3460;color:#eee;font-size:1rem}
  button{width:100%;padding:12px;border:none;border-radius:8px;font-size:1rem;cursor:pointer;transition:opacity .2s}
  button:hover{opacity:.85}
  button:disabled{opacity:.4;cursor:default}
  .primary{margin-top:24px;background:#e94560;color:#fff}
  .danger{margin-top:8px;background:#7b2feb;color:#fff}
  #status{margin-top:16px;text-align:center;font-size:.9rem;min-height:1.2em}
  #sta-info{display:none;margin-top:16px;padding:12px;background:#0f3460;border-radius:8px;text-align:center}
  #sta-info .ip{font-size:1.3rem;font-weight:700;color:#4caf50;margin:8px 0}
  .ok{color:#4caf50}.err{color:#e94560}.spin{color:#aaa}
  #hint{font-size:.75rem;color:#666;margin-top:4px}
</style>
</head>
<body>
<div class="card">
  <h1>&#x1F4F6; mpcb-iot Setup</h1>
  <p class="sub">Выберите WiFi сеть или введите вручную</p>

  <div id="step1">
    <label>Сеть</label>
    <input type="text" id="ssid" list="netlist" placeholder="SSID сети" autocomplete="off">
    <datalist id="netlist"></datalist>
    <div id="hint">Можно выбрать из списка или вписать вручную</div>

    <label>Пароль</label>
    <input type="password" id="pass" placeholder="Пароль WiFi">

    <button id="btn" class="primary" onclick="connect()">Подключить</button>
    <div id="status"></div>
  </div>

  <div id="sta-info">
    <span class="ok">&#x2713; Подключено!</span>
    <div class="ip" id="sta-ip"></div>
    <div style="font-size:.85rem;color:#888">Устройство доступно по этому IP</div>
    <button class="danger" onclick="closeAP()">Закрыть точку доступа</button>
  </div>
</div>
<script>
var pollTimer=null;
function loadNetworks(){
  fetch('/scan').then(function(r){return r.json();}).then(function(nets){
    var list=document.getElementById('netlist');
    list.innerHTML=nets.map(function(n){return '<option value="'+n.ssid+'">'+n.ssid+' ('+n.rssi+' dBm)'+(n.enc?' &#x1F512;':'')+'</option>';}).join('');
    if(nets.length>0) document.getElementById('ssid').placeholder='SSID сети ('+nets.length+' найдено)';
  }).catch(function(){});
}
function connect(){
  var ssid=document.getElementById('ssid').value.trim();
  var pass=document.getElementById('pass').value;
  if(!ssid)return;
  document.getElementById('btn').disabled=true;
  document.getElementById('status').innerHTML='<span class="spin">Подключаемся...</span>';
  fetch('/connect',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:'ssid='+encodeURIComponent(ssid)+'&pass='+encodeURIComponent(pass)})
  .then(function(r){return r.json();}).then(function(d){
    if(d.ok){
      document.getElementById('status').innerHTML='<span class="spin">Ожидаем подключения к WiFi...</span>';
      startPolling();
    } else {
      document.getElementById('status').innerHTML='<span class="err">&#x2717; Ошибка подключения</span>';
      document.getElementById('btn').disabled=false;
    }
  });
}
function startPolling(){
  if(pollTimer)clearInterval(pollTimer);
  pollTimer=setInterval(function(){
    fetch('/status').then(function(r){return r.json();}).then(function(d){
      if(d.connected){
        clearInterval(pollTimer);
        document.getElementById('step1').style.display='none';
        document.getElementById('sta-info').style.display='block';
        document.getElementById('sta-ip').textContent=d.ip;
      }
      if(d.failed){
        clearInterval(pollTimer);
        document.getElementById('status').innerHTML='<span class="err">&#x2717; Неверный пароль или сеть недоступна</span>';
        document.getElementById('btn').disabled=false;
      }
    });
  },1500);
}
function closeAP(){
  fetch('/close-ap',{method:'POST'}).then(function(r){return r.json();}).then(function(d){
    if(d.ok){
      document.getElementById('sta-info').innerHTML='<span class="ok">Точка доступа закрыта. Устройство в сети.</span>';
    }
  });
}
window.onload=loadNetworks;
</script>
</body>
</html>
)rawhtml";

APPortal::APPortal(const String& apName) : _apName(apName) {}

void APPortal::begin() {
    // ── Scan BEFORE starting AP — ESP32-C6 can't scan in pure AP mode ──
    WiFi.mode(WIFI_STA);
    delay(200);
    int n = WiFi.scanNetworks();
    String json = "[";
    for (int i = 0; i < n; i++) {
        if (i) json += ",";
        json += "{\"ssid\":\"" + WiFi.SSID(i) + "\","
                "\"rssi\":"   + WiFi.RSSI(i) + ","
                "\"enc\":"    + (WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false") + "}";
    }
    json += "]";
    WiFi.scanDelete();
    _cachedScan = json;
    Serial.println("[AP] Pre-scan: " + String(n) + " networks");

    // ── Start AP ──
    WiFi.mode(WIFI_AP);
    WiFi.softAP(_apName.c_str());
    Serial.println("[AP] Started: " + _apName + " @ " + WiFi.softAPIP().toString());

    _dns.start(53, "*", WiFi.softAPIP());

    _server.on("/",         [this](){ _handleRoot(); });
    _server.on("/scan",     [this](){ _handleScan(); });
    _server.on("/connect",  HTTP_POST, [this](){ _handleConnect(); });
    _server.on("/status",   [this](){ _handleStatus(); });
    _server.on("/close-ap", HTTP_POST, [this](){ _handleCloseAP(); });
    _server.onNotFound(     [this](){ _handleNotFound(); });
    _server.begin();
}

void APPortal::loop() {
    _dns.processNextRequest();
    _server.handleClient();
    _sta.loop();

    if (_staConnecting && !_staConnected) {
        if (_sta.isConnected()) {
            _staConnected = true;
            _staIP = _sta.ip();
            Serial.println("[AP] STA connected, IP: " + _staIP);
        }
    }
}

void APPortal::stop() {
    _server.stop();
    _dns.stop();
    WiFi.softAPdisconnect(true);
    Serial.println("[AP] Stopped");
}

void APPortal::_handleRoot() {
    _server.send_P(200, "text/html", PORTAL_HTML);
}

void APPortal::_handleScan() {
    _server.send(200, "application/json", _cachedScan);
}

void APPortal::_handleConnect() {
    String ssid = _server.arg("ssid");
    String pass = _server.arg("pass");

    _sta.beginSTA(ssid, pass);
    _staConnecting = true;
    _staConnected  = false;

    _server.send(200, "application/json", "{\"ok\":true}");

    if (_onConnect) _onConnect(ssid, pass);
}

void APPortal::_handleStatus() {
    bool connected = _staConnected;
    String json = "{\"connected\":" + String(connected ? "true" : "false");
    if (connected) {
        json += ",\"ip\":\"" + _staIP + "\"";
        json += ",\"rssi\":" + String(WiFi.RSSI());
    }
    if (_staConnecting && !_staConnected) {
        if (WiFi.status() == WL_CONNECT_FAILED || WiFi.status() == WL_NO_SSID_AVAIL) {
            json += ",\"failed\":true";
        } else {
            json += ",\"connecting\":true";
        }
    }
    json += "}";
    _server.send(200, "application/json", json);
}

void APPortal::_handleCloseAP() {
    _server.send(200, "application/json", "{\"ok\":true}");
    if (_onClose) _onClose();
}

void APPortal::_handleNotFound() {
    _server.sendHeader("Location", "http://192.168.4.1/", true);
    _server.send(302, "text/plain", "");
}
