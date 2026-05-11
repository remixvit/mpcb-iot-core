#include "ConfigServer.h"
#include <WiFi.h>
#include <ArduinoJson.h>
#include <Update.h>
#include "../log/RingLog.h"

// ---------------------------------------------------------------------------
// HTML шаблоны (PROGMEM)
// ---------------------------------------------------------------------------

static const char CONFIG_CSS[] PROGMEM = R"css(
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:sans-serif;background:#1a1a2e;color:#eee;padding:16px}
.wrap{max-width:520px;margin:0 auto}
h1{color:#e94560;margin-bottom:4px;font-size:1.4rem}
nav{display:flex;gap:8px;margin:16px 0;flex-wrap:wrap}
nav a{padding:8px 16px;background:#16213e;border-radius:8px;color:#aaa;text-decoration:none;font-size:.85rem;transition:all .2s}
nav a.active,nav a:hover{background:#e94560;color:#fff}
.card{background:#16213e;border-radius:16px;padding:24px;margin-bottom:16px}
h2{font-size:1.1rem;margin-bottom:16px;color:#ccc}
label{display:block;font-size:.85rem;color:#aaa;margin-bottom:4px;margin-top:14px}
input,select{width:100%;padding:9px 13px;border-radius:8px;border:1px solid #333;background:#0f3460;color:#eee;font-size:.95rem}
.row{display:flex;gap:8px;align-items:flex-end}
.row input{flex:1}
button{padding:10px 22px;background:#e94560;color:#fff;border:none;border-radius:8px;font-size:.95rem;cursor:pointer;margin-top:20px;transition:opacity .2s}
button:hover{opacity:.85}
button.sec{background:#0f3460;border:1px solid #555;color:#aaa}
.badge{display:inline-block;padding:3px 10px;border-radius:12px;font-size:.75rem;background:#0f3460}
.badge.ok{background:#1b4332;color:#4caf50}
.badge.err{background:#3b1a1a;color:#e94560}
#toast{position:fixed;bottom:24px;right:24px;padding:12px 20px;border-radius:10px;background:#4caf50;color:#fff;font-size:.9rem;opacity:0;transition:opacity .3s;pointer-events:none}
#toast.show{opacity:1}
.periph-list{margin-top:12px}
.periph-item{display:flex;gap:8px;align-items:center;margin-bottom:8px;background:#0f3460;border-radius:8px;padding:8px 12px}
.periph-item select,.periph-item input{flex:1;margin:0}
.periph-item button{margin:0;padding:6px 12px;background:#3b1a1a;font-size:.8rem}
)css";

static const char CONFIG_JS[] PROGMEM = R"js(
function toast(msg,ok=true){
  const t=document.getElementById('toast');
  t.textContent=msg;t.style.background=ok?'#4caf50':'#e94560';
  t.classList.add('show');setTimeout(()=>t.classList.remove('show'),2500);
}
function post(url,data,cb){
  fetch(url,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)})
  .then(r=>r.json()).then(d=>cb(d)).catch(()=>toast('Ошибка запроса',false));
}
)js";

// ---------------------------------------------------------------------------

static String _page(const String& title, const String& activeTab, const String& body) {
    String nav = "<nav>"
        "<a href='/' "     + String(activeTab=="home"?"class='active'":"") + ">&#x2302; Устройство</a>"
        "<a href='/wifi' " + String(activeTab=="wifi"?"class='active'":"") + ">&#x1F4F6; WiFi</a>"
        "<a href='/mqtt' " + String(activeTab=="mqtt"?"class='active'":"") + ">&#x1F4E1; MQTT</a>"
        "<a href='/gpio' " + String(activeTab=="gpio"?"class='active'":"") + ">&#x26A1; GPIO</a>"
        "<a href='/logs' " + String(activeTab=="logs"?"class='active'":"") + ">&#x1F4CB; Логи</a>"
        "<a href='/ota' "  + String(activeTab=="ota" ?"class='active'":"") + ">&#x1F4E6; OTA</a>"
        "</nav>";

    return "<!DOCTYPE html><html lang='ru'><head>"
           "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
           "<title>mpcb-iot — " + title + "</title>"
           "<style>" + String(FPSTR(CONFIG_CSS)) + "</style></head>"
           "<body><div class='wrap'>"
           "<h1>&#x2699; mpcb-iot Config</h1>" + nav + body +
           "</div><div id='toast'></div>"
           "<script>" + String(FPSTR(CONFIG_JS)) + "</script></body></html>";
}

// ---------------------------------------------------------------------------

ConfigServer::ConfigServer(ConfigStorage& storage) : _storage(storage) {}

void ConfigServer::begin() {
    _server.on("/",            [this](){ _handleRoot(); });
    _server.on("/wifi",        [this](){ _handleWifi(); });
    _server.on("/mqtt",        [this](){ _handleMqtt(); });
    _server.on("/gpio",        [this](){ _handleGpio(); });
    _server.on("/api/wifi",    HTTP_POST, [this](){ _handleSaveWifi(); });
    _server.on("/api/mqtt",    HTTP_POST, [this](){ _handleSaveMqtt(); });
    _server.on("/api/gpio",    HTTP_POST, [this](){ _handleSaveGpio(); });
    _server.on("/api/device",  HTTP_POST, [this](){ _handleSaveDevice(); });
    _server.on("/api/reset",   HTTP_POST, [this](){ _handleReset(); });
    _server.on("/api/status",   [this](){ _handleStatus(); });
    _server.on("/api/log-text", [this](){
        _server.send(200, "text/plain; charset=utf-8", Log.toText());
    });

    _server.on("/logs", [this](){ _handleLogs(); });
    _server.on("/api/logs/clear", HTTP_POST, [this](){
        Log.clear();
        _server.send(200, "application/json", "{\"ok\":true}");
    });

    _server.on("/ota", HTTP_GET, [this](){ _handleOta(); });
    _server.on("/ota", HTTP_POST,
        [this](){
            bool ok = !Update.hasError();
            String msg = ok ? "{\"ok\":true}" : "{\"ok\":false,\"err\":\"" + String(Update.errorString()) + "\"}";
            _server.send(200, "application/json", msg);
            if (ok) { delay(500); ESP.restart(); }
        },
        [this](){ _handleOtaUpload(); }
    );

    _server.begin();
    Serial.println("[Web] Config server started @ http://" + WiFi.localIP().toString());
}

void ConfigServer::loop() {
    _server.handleClient();
}

void ConfigServer::stop() {
    _server.stop();
}

// ---------------------------------------------------------------------------
// Pages
// ---------------------------------------------------------------------------

void ConfigServer::_handleRoot() {
    DeviceConfig dev = _storage.loadDevice();
    MqttConfig   mqtt = _storage.loadMqtt();
    WifiConfig   wifi = _storage.loadWifi();

    String mqttBadge = mqtt.host.isEmpty()
        ? "<span class='badge err'>не настроен</span>"
        : "<span class='badge ok'>" + mqtt.host + ":" + mqtt.port + "</span>";

    String body =
        "<div class='card'>"
        "<h2>Устройство</h2>"
        "<label>Название</label>"
        "<input id='dname' value='" + dev.deviceName + "'>"
        "<label>ID устройства</label>"
        "<input id='did' value='" + dev.deviceId + "' placeholder='auto (MAC)'>"
        "<button onclick=\"post('/api/device',{name:document.getElementById('dname').value,"
        "id:document.getElementById('did').value},d=>toast(d.ok?'Сохранено':'Ошибка',d.ok))\">Сохранить</button>"
        "</div>"

        "<div class='card'>"
        "<h2>Статус</h2>"
        "<label>WiFi</label><p><span class='badge ok'>" + wifi.ssid + " — " + WiFi.localIP().toString() + "</span></p>"
        "<label>MQTT</label><p>" + mqttBadge + "</p>"
        "<button class='sec' style='margin-top:12px' onclick=\"if(confirm('Сбросить WiFi настройки?'))post('/api/reset',{},()=>location.reload())\">&#x267B; Сбросить WiFi</button>"
        "</div>";

    _server.send(200, "text/html", _page("Устройство", "home", body));
}

void ConfigServer::_handleWifi() {
    WifiConfig cfg = _storage.loadWifi();
    String body =
        "<div class='card'>"
        "<h2>WiFi настройки</h2>"
        "<label>SSID</label><input id='ssid' value='" + cfg.ssid + "'>"
        "<label>Пароль</label><input id='pass' type='password' placeholder='••••••••'>"
        "<button onclick=\"post('/api/wifi',{ssid:document.getElementById('ssid').value,"
        "pass:document.getElementById('pass').value},"
        "d=>toast(d.ok?'Сохранено, перезагрузка...':'Ошибка',d.ok))\">Сохранить</button>"
        "</div>";
    _server.send(200, "text/html", _page("WiFi", "wifi", body));
}

void ConfigServer::_handleMqtt() {
    MqttConfig cfg = _storage.loadMqtt();
    String body =
        "<div class='card'>"
        "<h2>MQTT брокер</h2>"
        "<label>Хост</label><input id='host' value='" + cfg.host + "' placeholder='mqtt.example.com'>"
        "<div class='row'>"
        "<div style='flex:2'><label>Порт</label><input id='port' type='number' value='" + cfg.port + "'></div>"
        "<div style='flex:1'><label>TLS</label><select id='tls'>"
        "<option value='0'" + String(!cfg.tls?" selected":"") + ">Нет</option>"
        "<option value='1'" + String(cfg.tls?" selected":"") + ">Да</option>"
        "</select></div></div>"
        "<label>Пользователь</label><input id='user' value='" + cfg.user + "'>"
        "<label>Пароль</label><input id='mpass' type='password' placeholder='••••••••'>"
        "<button onclick=\"post('/api/mqtt',{host:document.getElementById('host').value,"
        "port:parseInt(document.getElementById('port').value),"
        "tls:document.getElementById('tls').value==='1',"
        "user:document.getElementById('user').value,"
        "pass:document.getElementById('mpass').value},"
        "d=>toast(d.ok?'Сохранено':'Ошибка',d.ok))\">Сохранить</button>"
        "</div>";
    _server.send(200, "text/html", _page("MQTT", "mqtt", body));
}

void ConfigServer::_handleGpio() {
    String periphJson = _storage.loadPeripherals();

    String body =
        "<div class='card'>"
        "<h2>Периферия (GPIO конструктор)</h2>"
        "<p style='font-size:.85rem;color:#888;margin-bottom:16px'>Добавьте устройства, подключённые к пинам ESP32</p>"
        "<div id='list' class='periph-list'></div>"
        "<button class='sec' onclick='addItem()'>+ Добавить</button>"
        "<button onclick='savePeriph()' style='margin-left:8px'>Сохранить</button>"
        "</div>"
        "<script>"
        "const TYPES=[{v:'relay',l:'Реле'},{v:'button',l:'Кнопка'},{v:'dht22',l:'DHT22 (темп/влажн)'}"
        ",{v:'ds18b20',l:'DS18B20 (темп)'},{v:'neopixel',l:'NeoPixel RGB'},{v:'analog',l:'Аналог. вход'}"
        ",{v:'pwm',l:'PWM выход'}];"
        "let items=" + periphJson + ";"
        "function render(){"
        "const l=document.getElementById('list');l.innerHTML='';"
        "items.forEach((it,i)=>{"
        "const d=document.createElement('div');d.className='periph-item';"
        "d.innerHTML=`<select onchange='items[${i}].type=this.value'>${TYPES.map(t=>"
        "`<option value='${t.v}'${it.type===t.v?' selected':''}>${t.l}</option>`).join('')}</select>"
        "<input type='number' min='0' max='48' value='${it.pin}' placeholder='Pin' onchange='items[${i}].pin=+this.value' style='max-width:70px'>"
        "<input value='${it.label||''}' placeholder='Название' onchange='items[${i}].label=this.value'>"
        "<button onclick='items.splice(${i},1);render()'>✕</button>`;"
        "l.appendChild(d);});}"
        "function addItem(){items.push({type:'relay',pin:0,label:''});render();}"
        "function savePeriph(){post('/api/gpio',{peripherals:items},d=>toast(d.ok?'Сохранено':'Ошибка',d.ok));}"
        "render();"
        "</script>";

    _server.send(200, "text/html", _page("GPIO", "gpio", body));
}

// ---------------------------------------------------------------------------
// API handlers
// ---------------------------------------------------------------------------

void ConfigServer::_handleSaveWifi() {
    JsonDocument doc;
    if (deserializeJson(doc, _server.arg("plain")) != DeserializationError::Ok) {
        _server.send(400, "application/json", "{\"ok\":false}");
        return;
    }
    _storage.saveWifi(doc["ssid"].as<String>(), doc["pass"].as<String>());
    _server.send(200, "application/json", "{\"ok\":true}");
    if (_onSave) _onSave();
    delay(500);
    ESP.restart();
}

void ConfigServer::_handleSaveMqtt() {
    JsonDocument doc;
    if (deserializeJson(doc, _server.arg("plain")) != DeserializationError::Ok) {
        _server.send(400, "application/json", "{\"ok\":false}");
        return;
    }
    MqttConfig cfg;
    cfg.host     = doc["host"].as<String>();
    cfg.port     = doc["port"] | 1883;
    cfg.user     = doc["user"].as<String>();
    cfg.password = doc["pass"].as<String>();
    cfg.tls      = doc["tls"] | false;
    _storage.saveMqtt(cfg);
    _server.send(200, "application/json", "{\"ok\":true}");
    if (_onSave) _onSave();
}

void ConfigServer::_handleSaveGpio() {
    JsonDocument doc;
    if (deserializeJson(doc, _server.arg("plain")) != DeserializationError::Ok) {
        _server.send(400, "application/json", "{\"ok\":false}");
        return;
    }
    String out;
    serializeJson(doc["peripherals"], out);
    _storage.savePeripherals(out);
    _server.send(200, "application/json", "{\"ok\":true}");
    if (_onSave) _onSave();
}

void ConfigServer::_handleSaveDevice() {
    JsonDocument doc;
    if (deserializeJson(doc, _server.arg("plain")) != DeserializationError::Ok) {
        _server.send(400, "application/json", "{\"ok\":false}");
        return;
    }
    DeviceConfig cfg;
    cfg.deviceName = doc["name"].as<String>();
    cfg.deviceId   = doc["id"].as<String>();
    _storage.saveDevice(cfg);
    _server.send(200, "application/json", "{\"ok\":true}");
}

void ConfigServer::_handleReset() {
    _storage.clearWifi();
    _server.send(200, "application/json", "{\"ok\":true}");
    delay(500);
    ESP.restart();
}

void ConfigServer::_handleStatus() {
    MqttConfig mqtt = _storage.loadMqtt();
    String json = "{\"wifi\":\"" + WiFi.SSID() + "\","
                  "\"ip\":\"" + WiFi.localIP().toString() + "\","
                  "\"mqtt\":\"" + mqtt.host + "\","
                  "\"rssi\":" + WiFi.RSSI() + "}";
    _server.send(200, "application/json", json);
}

// ---------------------------------------------------------------------------
// Logs
// ---------------------------------------------------------------------------

void ConfigServer::_handleLogs() {
    String uptime = String(millis() / 60000) + "m " + String((millis() / 1000) % 60) + "s";
    String rssi   = String(WiFi.RSSI()) + " dBm";

    String body =
        "<div class='card'>"
        "<h2>&#x1F4CB; Лог устройства</h2>"
        "<div style='display:flex;justify-content:space-between;align-items:center;margin-bottom:12px'>"
        "<span style='font-size:.85rem;color:#888'>Uptime: " + uptime + " &nbsp;|&nbsp; WiFi: " + rssi + " &nbsp;|&nbsp; "
        "<span id='cnt'>" + String(Log.count()) + "</span> записей в буфере</span>"
        "<div style='display:flex;gap:8px'>"
        "<button class='sec' style='margin:0;padding:6px 14px;font-size:.8rem' onclick='loadLogs()'>&#x21BA; Обновить</button>"
        "<button style='margin:0;padding:6px 14px;font-size:.8rem;background:#3b1a1a' onclick='clearLogs()'>&#x1F5D1; Очистить</button>"
        "</div></div>"
        "<pre id='log' style='background:#0a0a1a;border-radius:10px;padding:16px;font-size:.78rem;"
        "color:#8fc;overflow-y:auto;max-height:60vh;white-space:pre-wrap;word-break:break-all;min-height:120px'>"
        "Загрузка...</pre>"
        "</div>"
        "<script>"
        "function loadLogs(){"
        "fetch('/api/log-text').then(r=>r.text()).then(t=>{"
        "document.getElementById('log').textContent=t||'-- пусто --';"
        "});"
        "fetch('/api/status').then(r=>r.json()).then(d=>{"
        "// refresh done"
        "});}"
        "function clearLogs(){"
        "fetch('/api/logs/clear',{method:'POST'}).then(()=>{"
        "document.getElementById('log').textContent='-- очищен --';"
        "document.getElementById('cnt').textContent='0';"
        "});}"
        "loadLogs();"
        "setInterval(loadLogs,5000);"  // auto-refresh every 5s
        "</script>";

    _server.send(200, "text/html", _page("Логи", "logs", body));
}

// ---------------------------------------------------------------------------
// OTA
// ---------------------------------------------------------------------------

void ConfigServer::_handleOta() {
    String freeKb = String(ESP.getFreeSketchSpace() / 1024);
    String sketchKb = String(ESP.getSketchSize() / 1024);

    String body =
        "<div class='card'>"
        "<h2>OTA — обновление прошивки</h2>"
        "<p style='font-size:.85rem;color:#888;margin-bottom:16px'>"
        "Скомпилируйте прошивку в PlatformIO (<b>.pio/build/…/firmware.bin</b>) и загрузите сюда.</p>"
        "<table style='width:100%;font-size:.85rem;color:#aaa;margin-bottom:16px'>"
        "<tr><td>Текущая прошивка</td><td>" + sketchKb + " KB</td></tr>"
        "<tr><td>Свободно для OTA</td><td>" + freeKb + " KB</td></tr>"
        "</table>"
        "<div id='drop' style='border:2px dashed #555;border-radius:12px;padding:32px;text-align:center;cursor:pointer;transition:border-color .2s'>"
        "&#x1F4C1; Перетащите .bin сюда или нажмите для выбора"
        "<input type='file' id='file' accept='.bin' style='display:none'>"
        "</div>"
        "<div id='prog' style='display:none;margin-top:16px'>"
        "<div style='background:#0f3460;border-radius:8px;overflow:hidden;height:24px'>"
        "<div id='bar' style='height:100%;background:#e94560;width:0%;transition:width .3s;display:flex;align-items:center;justify-content:center;font-size:.8rem'></div>"
        "</div>"
        "<p id='ptext' style='text-align:center;margin-top:8px;color:#aaa;font-size:.85rem'></p>"
        "</div>"
        "<div id='status' style='margin-top:16px;text-align:center;font-size:.95rem'></div>"
        "</div>"
        "<script>"
        "const drop=document.getElementById('drop');"
        "const file=document.getElementById('file');"
        "drop.onclick=()=>file.click();"
        "drop.ondragover=e=>{e.preventDefault();drop.style.borderColor='#e94560';};"
        "drop.ondragleave=()=>drop.style.borderColor='#555';"
        "drop.ondrop=e=>{e.preventDefault();drop.style.borderColor='#555';"
        "if(e.dataTransfer.files[0])upload(e.dataTransfer.files[0]);};"
        "file.onchange=()=>{if(file.files[0])upload(file.files[0]);};"
        "function upload(f){"
        "if(!f.name.endsWith('.bin')){document.getElementById('status').innerHTML='<span style=\"color:#e94560\">Нужен файл .bin</span>';return;}"
        "const prog=document.getElementById('prog');"
        "const bar=document.getElementById('bar');"
        "const pt=document.getElementById('ptext');"
        "const st=document.getElementById('status');"
        "prog.style.display='block';drop.style.display='none';"
        "const fd=new FormData();fd.append('firmware',f);"
        "const xhr=new XMLHttpRequest();"
        "xhr.upload.onprogress=e=>{"
        "if(e.lengthComputable){"
        "const p=Math.round(e.loaded/e.total*100);"
        "bar.style.width=p+'%';bar.textContent=p+'%';"
        "pt.textContent='Загружено: '+Math.round(e.loaded/1024)+'/'+ Math.round(e.total/1024)+' KB';}};"
        "xhr.onload=()=>{"
        "const d=JSON.parse(xhr.responseText);"
        "if(d.ok){bar.style.background='#4caf50';bar.textContent='✓ OK';"
        "st.innerHTML='<span style=\"color:#4caf50\">Прошивка загружена! Устройство перезагружается...</span>';"
        "setTimeout(()=>location.href='/',4000);"
        "}else{bar.style.background='#e94560';bar.textContent='✗';"
        "st.innerHTML='<span style=\"color:#e94560\">Ошибка: '+d.err+'</span>';drop.style.display='block';}};"
        "xhr.onerror=()=>{st.innerHTML='<span style=\"color:#e94560\">Ошибка соединения</span>';drop.style.display='block';};"
        "xhr.open('POST','/ota');xhr.send(fd);}"
        "</script>";

    _server.send(200, "text/html", _page("OTA", "ota", body));
}

void ConfigServer::_handleOtaUpload() {
    HTTPUpload& upload = _server.upload();

    if (upload.status == UPLOAD_FILE_START) {
        Serial.println("[OTA] Start: " + upload.filename + " (" + String(upload.totalSize / 1024) + " KB)");
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            Serial.println("[OTA] Begin error: " + String(Update.errorString()));
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            Serial.println("[OTA] Write error: " + String(Update.errorString()));
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) {
            Serial.println("[OTA] Done: " + String(upload.totalSize) + " bytes");
        } else {
            Serial.println("[OTA] End error: " + String(Update.errorString()));
        }
    }
}
