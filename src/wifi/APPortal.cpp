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
  input,select{width:100%;padding:10px 14px;border-radius:8px;border:1px solid #333;background:#0f3460;color:#eee;font-size:1rem}
  select option{background:#0f3460}
  button{margin-top:24px;width:100%;padding:12px;background:#e94560;color:#fff;border:none;border-radius:8px;font-size:1rem;cursor:pointer;transition:opacity .2s}
  button:hover{opacity:.85}
  button:disabled{opacity:.4;cursor:default}
  #status{margin-top:16px;text-align:center;font-size:.9rem;min-height:1.2em}
  .ok{color:#4caf50}.err{color:#e94560}.spin{color:#aaa}
  #refresh{background:none;border:1px solid #555;color:#aaa;margin-top:8px;padding:8px;font-size:.8rem}
</style>
</head>
<body>
<div class="card">
  <h1>&#x1F4F6; mpcb-iot Setup</h1>
  <p class="sub">Выберите WiFi сеть и введите пароль</p>

  <label>Сеть</label>
  <select id="ssid"><option value="">Сканирование...</option></select>
  <button id="refresh" onclick="scan()">&#x21BA; Обновить</button>

  <label>Пароль</label>
  <input type="password" id="pass" placeholder="Пароль WiFi">

  <button id="btn" onclick="connect()" disabled>Подключить</button>
  <div id="status"></div>
</div>
<script>
function scan(){
  document.getElementById('status').innerHTML='<span class="spin">Сканирование...</span>';
  fetch('/scan').then(r=>r.json()).then(nets=>{
    const sel=document.getElementById('ssid');
    sel.innerHTML=nets.map(n=>`<option value="${n.ssid}">${n.ssid} (${n.rssi} dBm)${n.enc?' 🔒':''}</option>`).join('');
    document.getElementById('btn').disabled=false;
    document.getElementById('status').innerHTML='';
  }).catch(()=>{
    document.getElementById('status').innerHTML='<span class="err">Ошибка сканирования</span>';
  });
}
function connect(){
  const ssid=document.getElementById('ssid').value;
  const pass=document.getElementById('pass').value;
  if(!ssid)return;
  document.getElementById('btn').disabled=true;
  document.getElementById('status').innerHTML='<span class="spin">Подключаемся...</span>';
  fetch('/connect',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:`ssid=${encodeURIComponent(ssid)}&pass=${encodeURIComponent(pass)}`})
  .then(r=>r.json()).then(d=>{
    if(d.ok){
      document.getElementById('status').innerHTML='<span class="ok">&#x2713; Подключено! Устройство перезагружается...</span>';
    } else {
      document.getElementById('status').innerHTML='<span class="err">&#x2717; Неверный пароль или сеть недоступна</span>';
      document.getElementById('btn').disabled=false;
    }
  });
}
window.onload=scan;
</script>
</body>
</html>
)rawhtml";

APPortal::APPortal(const String& apName) : _apName(apName) {}

void APPortal::begin() {
    WiFi.softAP(_apName.c_str());
    Serial.println("[AP] Started: " + _apName + " @ " + WiFi.softAPIP().toString());

    _dns.start(53, "*", WiFi.softAPIP());

    _server.on("/",        [this](){ _handleRoot(); });
    _server.on("/scan",    [this](){ _handleScan(); });
    _server.on("/connect", HTTP_POST, [this](){ _handleConnect(); });
    _server.onNotFound(    [this](){ _handleNotFound(); });
    _server.begin();
}

void APPortal::loop() {
    _dns.processNextRequest();
    _server.handleClient();
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
    _server.send(200, "application/json", json);
}

void APPortal::_handleConnect() {
    String ssid = _server.arg("ssid");
    String pass = _server.arg("pass");

    WiFi.begin(ssid.c_str(), pass.c_str());

    uint8_t tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries++ < 20) {
        delay(500);
    }

    if (WiFi.status() == WL_CONNECTED) {
        _server.send(200, "application/json", "{\"ok\":true}");
        Serial.println("[AP] Connected to: " + ssid);
        delay(500);
        if (_onConnect) _onConnect(ssid, pass);
    } else {
        WiFi.disconnect();
        _server.send(200, "application/json", "{\"ok\":false}");
        Serial.println("[AP] Failed to connect: " + ssid);
    }
}

void APPortal::_handleNotFound() {
    _server.sendHeader("Location", "http://192.168.4.1/", true);
    _server.send(302, "text/plain", "");
}
