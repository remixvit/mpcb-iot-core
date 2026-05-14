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
body{font-family:-apple-system,'Segoe UI',sans-serif;background:#000;color:#fff;padding:16px;-webkit-font-smoothing:antialiased}
.wrap{max-width:960px;margin:0 auto}
h1{font-size:1.3rem;font-weight:700;letter-spacing:-.3px;background:linear-gradient(135deg,#a855f7,#7b2feb);-webkit-background-clip:text;-webkit-text-fill-color:transparent;background-clip:text;margin-bottom:4px}
nav{display:flex;gap:6px;margin:14px 0;flex-wrap:wrap}
nav a{padding:8px 16px;background:#1c1c1e;border-radius:10px;color:#8e8e93;text-decoration:none;font-size:.85rem;font-weight:500;transition:all .15s}
nav a.active,nav a:hover{background:#7b2feb;color:#fff}
.card{background:#1c1c1e;border-radius:20px;padding:22px;margin-bottom:12px}
h2{font-size:1rem;font-weight:600;margin-bottom:14px;color:#8e8e93;text-transform:uppercase;letter-spacing:.6px}
label{display:block;font-size:.8rem;color:#8e8e93;margin-bottom:5px;margin-top:14px;font-weight:500}
input,select{width:100%;padding:10px 13px;border-radius:12px;border:none;background:#2a2a2d;color:#fff;font-size:.95rem;outline:none;transition:background .15s}
input:focus,select:focus{background:#3a3a3e;box-shadow:0 0 0 2px rgba(123,47,235,.3)}
select option{background:#2a2a2d}
.row{display:flex;gap:8px;align-items:flex-end}
.row input{flex:1}
button{padding:10px 22px;background:#7b2feb;color:#fff;border:none;border-radius:10px;font-size:.9rem;font-weight:600;cursor:pointer;margin-top:18px;transition:opacity .15s,transform .1s}
button:hover{opacity:.88}
button:active{transform:scale(.97)}
button.sec{background:#2a2a2d;color:#8e8e93}
.badge{display:inline-block;padding:3px 10px;border-radius:20px;font-size:.75rem;background:#2a2a2d}
.badge.ok{background:rgba(52,199,89,.15);color:#34c759}
.badge.err{background:rgba(255,69,58,.15);color:#ff453a}
#toast{position:fixed;bottom:24px;right:24px;padding:12px 20px;border-radius:12px;background:#34c759;color:#fff;font-size:.9rem;font-weight:500;opacity:0;transition:opacity .3s;pointer-events:none}
#toast.show{opacity:1}
.periph-list{margin-top:10px}
.periph-item{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin-bottom:6px;background:#2a2a2d;border-radius:12px;padding:10px 12px}
.periph-item select{flex:1;min-width:110px;margin:0}
.periph-item input{flex:1;margin:0}
.periph-item button{margin:0;padding:6px 12px;background:rgba(255,69,58,.15);color:#ff453a;font-size:.8rem;flex-shrink:0;border-radius:8px}
.rule-item{display:flex;gap:6px;align-items:center;flex-wrap:wrap;margin-bottom:6px;background:#2a2a2d;border-radius:12px;padding:10px 12px}
.rule-item select{flex:1;min-width:120px;margin:0}
.rule-item input[type=number]{width:82px;flex:0 0 82px;margin:0}
.rule-item button{margin:0;padding:6px 12px;background:rgba(255,69,58,.15);color:#ff453a;font-size:.8rem;flex-shrink:0;border-radius:8px}
.rlbl{font-size:.72rem;color:#8e8e93;white-space:nowrap;flex-shrink:0;background:#1c1c1e;padding:3px 8px;border-radius:6px}
.rarrow{color:#7b2feb;font-size:1.1rem;flex-shrink:0;padding:0 2px}
@media(max-width:600px){
  .wrap{padding:0}
  body{padding:10px}
  .card{padding:16px;border-radius:16px}
  nav a{padding:7px 12px;font-size:.8rem}
  .rule-item select{min-width:100px}
}
)css";

static const char CONFIG_JS[] PROGMEM = R"js(
function toast(msg,ok=true){
  const t=document.getElementById('toast');
  t.textContent=msg;t.style.background=ok?'#34c759':'#ff453a';
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
    _server.on("/api/rules",   HTTP_POST, [this](){ _handleSaveRules(); });
    _server.on("/api/device",  HTTP_POST, [this](){ _handleSaveDevice(); });
    _server.on("/api/reset",   HTTP_POST, [this](){ _handleReset(); });
    _server.on("/api/reboot",  HTTP_POST, [this](){
        _server.send(200, "application/json", "{\"ok\":true}");
        delay(300);
        ESP.restart();
    });
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
    String rulesJson  = _storage.loadRules();

    String body =
        // ── Peripherals card ────────────────────────────────────────────────
        "<div class='card'>"
        "<h2>Периферия (GPIO)</h2>"
        "<p id='slots_info' style='font-size:.85rem;color:#888;margin-bottom:16px'>Загрузка...</p>"
        "<div id='list' class='periph-list'></div>"
        "<button id='btn_add' class='sec' onclick='addItem()'>+ Добавить</button>"
        "<button onclick='savePeriph()' style='margin-left:8px'>Сохранить</button>"
        "</div>"

        // ── Rules card ──────────────────────────────────────────────────────
        "<div class='card'>"
        "<h2>&#x26A1; Автоматизация (локальные правила)</h2>"
        "<p style='font-size:.85rem;color:#888;margin-bottom:16px'>"
        "Правила выполняются на устройстве без сервера. "
        "Сначала сохраните периферию, затем задайте правила.</p>"
        "<div id='rlist' class='periph-list'></div>"
        "<button class='sec' onclick='addRule()'>+ Добавить правило</button>"
        "<button onclick='saveRules()' style='margin-left:8px'>Сохранить правила</button>"
        "</div>"

        "<script>"
        "function san(s){let o='';for(let c of (s||'').toLowerCase()){"
        "if(/[a-z0-9]/.test(c))o+=c;else if(c===' '||c==='-')o+='_';}return o;}"
        "const TYPES=["
        "{v:'relay',   l:'Реле',    max:8},"
        "{v:'button',  l:'Кнопка',  max:8},"
        "{v:'analog',  l:'Аналог',  max:4},"
        "{v:'pwm',     l:'PWM',     max:4},"
        "{v:'neopixel',l:'NeoPixel',max:2},"
        "{v:'dht22',   l:'DHT22',   max:2},"
        "{v:'ds18b20', l:'DS18B20', max:2},"
        "{v:'vl53',    l:'VL53 ToF',max:1},"
        "{v:'pcf8574', l:'PCF8574', max:2}"
        "];"
        // ESP32-C6 Super Mini: f=forbidden (USB/Flash), w=warn (JTAG/strapping/onboard HW)
        "const PINS=["
        "{n:0,l:'GPIO0 (ADC)'},{n:1,l:'GPIO1 (ADC)'},{n:2,l:'GPIO2 (ADC)'},{n:3,l:'GPIO3 (ADC)'},"
        "{n:4,l:'GPIO4 (ADC/SCK)',w:1},{n:5,l:'GPIO5 (MISO)',w:1},"
        "{n:6,l:'GPIO6 (MOSI)',w:1},{n:7,l:'GPIO7 (SS)',w:1},"
        "{n:8,l:'GPIO8 (WS2812)',w:1},{n:9,l:'GPIO9 (BOOT)',w:1},"
        "{n:12,l:'GPIO12 (USB-)',f:1},{n:13,l:'GPIO13 (USB+)',f:1},"
        "{n:14,l:'GPIO14'},"
        "{n:15,l:'GPIO15 (LED)',w:1},"
        "{n:18,l:'GPIO18 (Flash)',f:1},{n:19,l:'GPIO19 (Flash)',f:1},"
        "{n:20,l:'GPIO20 (RX)'},{n:21,l:'GPIO21 (TX)'},"
        "{n:22,l:'GPIO22 (SDA)'},{n:23,l:'GPIO23 (SCL)'}"
        "];"
        "function pinOpts(idx){"
        "const used=items.map((it,j)=>j!==idx?it.pin:-1).filter(p=>p>=0);"
        "return PINS.map(p=>{"
        "const busy=used.includes(p.n);"
        "const dis=(p.f||busy)?' disabled':'';"
        "let lbl=p.l;"
        "if(p.f)lbl+=' — запрещён';"
        "else if(busy)lbl+=' (занят)';"
        "else if(p.w)lbl+=' ⚠';"
        "const sel=items[idx].pin===p.n?' selected':'';"
        "return`<option value='${p.n}'${sel}${dis}>${lbl}</option>`;}).join('');}"
        "let items=" + periphJson + ";"
        "function typeOpts(idx){"
        "return TYPES.map(t=>{"
        "const oth=items.filter((x,j)=>j!==idx&&x.type===t.v).length;"
        "const dis=oth>=t.max?' disabled':'';"
        "const lbl=t.max<=4?t.l+' ('+Math.max(0,t.max-oth)+' ост.)':t.l;"
        "const sel=items[idx].type===t.v?' selected':'';"
        "return`<option value='${t.v}'${sel}${dis}>${lbl}</option>`;}).join('');}"
        "function render(){"
        "const l=document.getElementById('list');l.innerHTML='';"
        "items.forEach((it,i)=>{"
        "const d=document.createElement('div');d.className='periph-item';"
        "d.innerHTML=`<select onchange='items[${i}].type=this.value;render()'>${typeOpts(i)}</select>"
        "<select style='min-width:155px' onchange='items[${i}].pin=+this.value;render()'>${pinOpts(i)}</select>"
        "<input value='${it.label||''}' placeholder='Название' onchange='items[${i}].label=this.value'>"
        "<button onclick='items.splice(${i},1);render();renderRules()'>&#x2715;</button>`;"
        "l.appendChild(d);});"
        "const freePins=PINS.filter(p=>!p.f&&!items.find(x=>x.pin===p.n)).length;"
        "const freeType=TYPES.some(t=>items.filter(x=>x.type===t.v).length<t.max);"
        "const canAdd=items.length<12&&freePins>0&&freeType;"
        "document.getElementById('slots_info').innerHTML="
        "`<b>${items.length}/12</b> слотов &nbsp;&bull;&nbsp; <b>${freePins}</b> пинов свободно`;"
        "const ba=document.getElementById('btn_add');"
        "ba.disabled=!canAdd;ba.style.opacity=canAdd?'1':'0.4';}"
        "function addItem(){"
        "if(items.length>=12)return;"
        "const fp=PINS.find(p=>!p.f&&!p.w&&!items.find(x=>x.pin===p.n))||PINS.find(p=>!p.f&&!items.find(x=>x.pin===p.n));"
        "if(!fp)return;"
        "const ft=TYPES.find(t=>items.filter(x=>x.type===t.v).length<t.max);"
        "if(!ft)return;"
        "items.push({type:ft.v,pin:fp.n,label:''});render();}"
        "function savePeriph(){post('/api/gpio',{peripherals:items},d=>toast(d.ok?'Сохранено':d.err||'Ошибка',d.ok));}"
        "render();"

        // rules
        "const EVENTS=[{v:'pressed',l:'нажата'},{v:'released',l:'отпущена'},{v:'any',l:'любое'}];"
        "const ACTIONS=[{v:'toggle',l:'переключить'},{v:'on',l:'включить'}"
        ",{v:'off',l:'выключить'},{v:'pulse',l:'импульс'}];"
        "let rules=" + rulesJson + ";"

        "function periphOpts(sel){"
        "return items.map(it=>{"
        "const k=san(it.label)||it.type+'_'+it.pin;"
        "const l=it.label||it.type+'_'+it.pin;"
        "return`<option value='${k}'${k===sel?' selected':''}>${l}</option>`;}).join('');}"

        // helper called by onchange on the action select
        "function onActChange(i,v){"
        "rules[i].action=v;"
        "var p=document.getElementById('pms'+i);"
        "if(p)p.style.display=v==='pulse'?'':'none';}"

        "function renderRules(){"
        "const l=document.getElementById('rlist');l.innerHTML='';"
        "if(!rules.length){l.innerHTML='<p style=\"color:#666;font-size:.85rem\">Нет правил</p>';return;}"
        "rules.forEach((r,i)=>{"
        "const d=document.createElement('div');d.className='rule-item';"
        "const pmsVis=r.action==='pulse'?'':'display:none';"
        "d.innerHTML="
        "`<span class='rlbl'>ЕСЛИ</span>`"
        "+`<select onchange='rules[${i}].trigger=this.value'>${periphOpts(r.trigger)}</select>`"
        "+`<select onchange='rules[${i}].event=this.value'>`"
        "+EVENTS.map(e=>`<option value='${e.v}'${r.event===e.v?' selected':''}>${e.l}</option>`).join('')"
        "+`</select>`"
        "+`<span class='rarrow'>&#x2794;</span>`"
        "+`<span class='rlbl'>ТО</span>`"
        "+`<select onchange='onActChange(${i},this.value)'>`"
        "+ACTIONS.map(a=>`<option value='${a.v}'${r.action===a.v?' selected':''}>${a.l}</option>`).join('')"
        "+`</select>`"
        "+`<input id='pms${i}' type='number' min='100' max='60000' step='100'`"
        "+` value='${r.pulseMs||500}' onchange='rules[${i}].pulseMs=+this.value'`"
        "+` style='${pmsVis}' placeholder='мс' title='Длительность импульса, мс'>`"
        "+`<select onchange='rules[${i}].target=this.value'>${periphOpts(r.target)}</select>`"
        "+`<button onclick='rules.splice(${i},1);renderRules()'>&#x2715;</button>`;"
        "l.appendChild(d);});}"

        "function addRule(){"
        "const k=items.length?san(items[0].label)||items[0].type+'_'+items[0].pin:'';"
        "rules.push({trigger:k,event:'pressed',action:'toggle',target:k,pulseMs:500});"
        "renderRules();}"

        "function saveRules(){post('/api/rules',{rules:rules},d=>toast(d.ok?'Сохранено':'Ошибка',d.ok));}"
        "renderRules();"
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
        _server.send(400, "application/json", "{\"ok\":false,\"err\":\"JSON\"}");
        return;
    }
    JsonArray arr = doc["peripherals"].as<JsonArray>();
    if (!arr || arr.size() > 12) {
        _server.send(400, "application/json", "{\"ok\":false,\"err\":\"max 12 peripherals\"}");
        return;
    }

    static const char*   tNames[]  = {"relay","button","analog","pwm","neopixel","dht22","ds18b20","vl53","pcf8574"};
    static const uint8_t tLimits[] = {     8,       8,       4,    4,         2,      2,        2,     1,        2};
    static const uint8_t tCount    = 9;
    static const uint8_t forbidden[] = {12, 13, 18, 19};

    uint8_t cnt[tCount] = {};
    bool    usedPin[24] = {};

    for (JsonObject obj : arr) {
        uint8_t pin = obj["pin"] | 255;
        for (uint8_t fp : forbidden) {
            if (pin == fp) {
                _server.send(400, "application/json",
                    "{\"ok\":false,\"err\":\"GPIO" + String(pin) + " запрещён\"}");
                return;
            }
        }
        if (pin < 24) {
            if (usedPin[pin]) {
                _server.send(400, "application/json",
                    "{\"ok\":false,\"err\":\"GPIO" + String(pin) + " занят\"}");
                return;
            }
            usedPin[pin] = true;
        }
        String type = obj["type"].as<String>();
        for (uint8_t i = 0; i < tCount; i++) {
            if (type == tNames[i]) {
                if (++cnt[i] > tLimits[i]) {
                    _server.send(400, "application/json",
                        "{\"ok\":false,\"err\":\"" + type + ": макс. " + String(tLimits[i]) + "\"}");
                    return;
                }
                break;
            }
        }
    }

    String out;
    serializeJson(doc["peripherals"], out);
    _storage.savePeripherals(out);
    _server.send(200, "application/json", "{\"ok\":true}");
    if (_onSave) _onSave();
}

void ConfigServer::_handleSaveRules() {
    JsonDocument doc;
    if (deserializeJson(doc, _server.arg("plain")) != DeserializationError::Ok) {
        _server.send(400, "application/json", "{\"ok\":false}");
        return;
    }
    String out;
    serializeJson(doc["rules"], out);
    _storage.saveRules(out);
    _server.send(200, "application/json", "{\"ok\":true}");
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
        "const el=document.getElementById('log');"
        "el.textContent=t.trim()||'-- пусто --';"
        "el.scrollTop=el.scrollHeight;"
        "});}"
        "function clearLogs(){"
        "fetch('/api/logs/clear',{method:'POST'}).then(()=>{"
        "document.getElementById('log').textContent='-- очищен --';"
        "document.getElementById('cnt').textContent='0';"
        "});}"
        "loadLogs();"
        "setInterval(loadLogs,5000);"
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
