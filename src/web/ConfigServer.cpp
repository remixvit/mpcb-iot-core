#include "ConfigServer.h"
#include <WiFi.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <Update.h>
#include "../log/RingLog.h"

#ifndef I2C_SDA
#  define I2C_SDA 19
#  define I2C_SCL 18
#endif
#define _MPCB_STR(x) #x
#define MPCB_STR(x)  _MPCB_STR(x)

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
        "<a href='/dash' " + String(activeTab=="dash"?"class='active'":"") + ">&#x1F4CA; Статус</a>"
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
    _server.on("/api/i2c-scan", [this](){ _handleI2cScan(); });
    _server.on("/dash",         [this](){ _handleDash(); });
    _server.on("/api/state",    [this](){ _handleApiState(); });
    _server.on("/api/cmd",      HTTP_POST, [this](){ _handleApiCmd(); });
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
        "<button class='sec' onclick='reboot()' style='margin-left:8px'>&#x21BA; Перезагрузить</button>"
        "<p id='reboot_hint' style='display:none;margin-top:10px;font-size:.82rem;"
        "color:#f4a261;background:rgba(244,162,97,.1);padding:8px 12px;border-radius:8px'>"
        "&#x26A0; Изменения сохранены — перезагрузите устройство для применения</p>"
        "<hr style='border:none;border-top:1px solid #333;margin:16px 0'>"
        "<h2 style='margin-bottom:8px'>&#x1F50D; I2C сканер</h2>"
        "<button class='sec' onclick='scanI2c()' style='margin-top:0'>Сканировать шину</button>"
        "<div id='i2c_result' style='margin-top:10px;font-size:.85rem;color:#8e8e93'></div>"
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
        "if(/[a-z0-9]/.test(c))o+=c;}return o;}"
        "const TYPES=["
        "{v:'relay',     l:'Реле',       max:8},"
        "{v:'button',    l:'Кнопка',     max:8},"
        "{v:'analog',    l:'Аналог',     max:4},"
        "{v:'pwm',       l:'PWM',        max:4},"
        "{v:'neopixel',  l:'NeoPixel',   max:2},"
        "{v:'dht22',     l:'DHT22',      max:2},"
        "{v:'ds18b20',   l:'DS18B20',    max:2},"
        "{v:'aht10',     l:'AHT10',      max:2,i2c:1,addrs:[{n:0x38,l:'0x38'},{n:0x39,l:'0x39'}]},"
        "{v:'vl53l0',    l:'VL53L0X ToF',max:1,i2c:1,addrs:[{n:0x29,l:'0x29'}]},"
        "{v:'vl53l1',    l:'VL53L1X ToF',max:1,i2c:1,addrs:[{n:0x29,l:'0x29'}]},"
        "{v:'ccs811',    l:'CCS811 TVOC',max:2,i2c:1,addrs:[{n:0x5A,l:'0x5A (ADDR=GND)'},{n:0x5B,l:'0x5B (ADDR=VCC)'}]},"
        "{v:'pcf_relay', l:'PCF Реле',   max:16,i2c:1,pcf:1,addrs:["
        "{n:0x20,l:'0x20'},{n:0x21,l:'0x21'},{n:0x22,l:'0x22'},{n:0x23,l:'0x23'},"
        "{n:0x24,l:'0x24'},{n:0x25,l:'0x25'},{n:0x26,l:'0x26'},{n:0x27,l:'0x27'}]},"
        "{v:'pcf_button',l:'PCF Кнопка', max:16,i2c:1,pcf:1,addrs:["
        "{n:0x20,l:'0x20'},{n:0x21,l:'0x21'},{n:0x22,l:'0x22'},{n:0x23,l:'0x23'},"
        "{n:0x24,l:'0x24'},{n:0x25,l:'0x25'},{n:0x26,l:'0x26'},{n:0x27,l:'0x27'}]}"
        "];"
        // f=forbidden, w=warn (JTAG/strapping/onboard HW)
#ifdef CONFIG_IDF_TARGET_ESP32C3
        // ESP32-C3 Super Mini — GPIO8=LED(active-low), I2C=4/5
        "const PINS=["
        "{n:0,l:'GPIO0 (ADC)'},{n:1,l:'GPIO1 (ADC)'},{n:3,l:'GPIO3 (ADC)'},"
        "{n:10,l:'GPIO10'},"
        "{n:2,l:'GPIO2',w:1},"
        "{n:4,l:'GPIO4 (SDA)',w:1},{n:5,l:'GPIO5 (SCL)',w:1},"
        "{n:6,l:'GPIO6',w:1},{n:7,l:'GPIO7',w:1},"
        "{n:8,l:'GPIO8 (LED)',w:1},{n:9,l:'GPIO9 (BOOT)',w:1},"
        "{n:20,l:'GPIO20 (RX)',w:1},{n:21,l:'GPIO21 (TX)',w:1}"
        "];"
#else
        // ESP32-C6 Super Mini (FH4 — embedded flash, GPIO18/19 free)
        "const PINS=["
        "{n:0,l:'GPIO0 (ADC)'},{n:1,l:'GPIO1 (ADC)'},{n:2,l:'GPIO2 (ADC)'},{n:3,l:'GPIO3 (ADC)'},"
        "{n:4,l:'GPIO4 (ADC)',w:1},{n:5,l:'GPIO5',w:1},"
        "{n:6,l:'GPIO6',w:1},{n:7,l:'GPIO7',w:1},"
        "{n:8,l:'GPIO8 (WS2812)',w:1},{n:9,l:'GPIO9 (BOOT)',w:1},"
        "{n:12,l:'GPIO12 (USB-)',f:1},{n:13,l:'GPIO13 (USB+)',f:1},"
        "{n:14,l:'GPIO14'},"
        "{n:15,l:'GPIO15 (LED)',w:1},"
        "{n:18,l:'GPIO18 (SCL)'},{n:19,l:'GPIO19 (SDA)'},"
        "{n:20,l:'GPIO20 (RX)'},{n:21,l:'GPIO21 (TX)'},"
        "{n:22,l:'GPIO22 (SDA alt)',w:1},{n:23,l:'GPIO23 (SCL alt)',w:1}"
        "];"
#endif
        "function isPcf(v){const t=TYPES.find(t=>t.v===v);return !!(t&&t.pcf);}"
        "function isI2C(v){const t=TYPES.find(t=>t.v===v);return !!(t&&t.i2c&&!t.pcf);}"
        "function addrsFor(type){const t=TYPES.find(x=>x.v===type);return(t&&t.addrs)||[];}"
        // PCF8574 address selector — disables full chips and enforces max 2 distinct addresses
        "function pcfI2cOpts(idx){"
        "const oth=items.filter((it,j)=>j!==idx&&isPcf(it.type));"
        "const addrMap={};oth.forEach(it=>{"
        "if(!addrMap[it.i2cAddr])addrMap[it.i2cAddr]=new Set();"
        "addrMap[it.i2cAddr].add(it.channel);});"
        "const usedAddrs=Object.keys(addrMap).map(Number);"
        "return addrsFor(items[idx].type).map(a=>{"
        "const isNew=!usedAddrs.includes(a.n);"
        "const full=addrMap[a.n]&&addrMap[a.n].size>=8;"
        "const tooMany=isNew&&usedAddrs.length>=2;"
        "const dis=(full||tooMany)?' disabled':'';"
        "const sel=items[idx].i2cAddr===a.n?' selected':'';"
        "let lbl=a.l;"
        "if(full)lbl+=' (полный)';else if(tooMany)lbl+=' (лимит)';"
        "return`<option value='${a.n}'${sel}${dis}>${lbl}</option>`;}).join('');}"
        // PCF8574 channel selector (0–7), disables channels already taken at same address
        "function pcfChannelOpts(idx){"
        "const usedCh=new Set(items.filter((it,j)=>j!==idx&&isPcf(it.type)&&it.i2cAddr===items[idx].i2cAddr).map(x=>x.channel));"
        "return[0,1,2,3,4,5,6,7].map(ch=>{"
        "const busy=usedCh.has(ch);"
        "const sel=items[idx].channel===ch?' selected':'';"
        "const dis=busy?' disabled':'';"
        "return`<option value='${ch}'${sel}${dis}>CH${ch}${busy?' (занят)':''}</option>`;}).join('');}"
        "function i2cOpts(idx){"
        "const myAddrs=addrsFor(items[idx].type);"
        "const used=items.filter((it,j)=>j!==idx&&isI2C(it.type)).map(x=>x.i2cAddr);"
        "return myAddrs.map(a=>{"
        "const busy=used.includes(a.n);"
        "const sel=items[idx].i2cAddr===a.n?' selected':'';"
        "return`<option value='${a.n}'${sel}${busy?' disabled':''}>${a.l}${busy?' (занят)':''}</option>`;}).join('');}"
        "function pinOpts(idx){"
        "const used=items.filter((it,j)=>j!==idx&&!isI2C(it.type)&&!isPcf(it.type)).map(it=>it.pin);"
        "return PINS.map(p=>{"
        "const busy=used.includes(p.n);"
        "const dis=(p.f||busy)?' disabled':'';"
        "let lbl=p.l;"
        "if(p.f)lbl+=' — запрещён';"
        "else if(busy)lbl+=' (занят)';"
        "else if(p.w)lbl+=' ⚠';"
        "const sel=!isI2C(items[idx].type)&&items[idx].pin===p.n?' selected':'';"
        "return`<option value='${p.n}'${sel}${dis}>${lbl}</option>`;}).join('');}"
        "let items=" + periphJson + ";"
        "items=items.map(it=>Object.assign({pin:0,i2cAddr:0x38,channel:0,"
        "calMode:0,calRawMin:0,calRawMax:4095,calValMin:0,calValMax:100,"
        "calRRef:10000,calBeta:3950,calR25:10000,calUnit:'',zoneSet:0,zoneHyst:0},it));"
        "function typeOpts(idx){"
        "return TYPES.map(t=>{"
        "const oth=items.filter((x,j)=>j!==idx&&x.type===t.v).length;"
        "const dis=oth>=t.max?' disabled':'';"
        "const lbl=t.max<=4?t.l+' ('+Math.max(0,t.max-oth)+' ост.)':t.l;"
        "const sel=items[idx].type===t.v?' selected':'';"
        "return`<option value='${t.v}'${sel}${dis}>${lbl}</option>`;}).join('');}"
        "function onTypeChange(idx,v){"
        "const wasPcf=isPcf(items[idx].type);const wasI2C=isI2C(items[idx].type);"
        "const nowPcf=isPcf(v);const nowI2C=isI2C(v);"
        "items[idx].type=v;"
        "if(nowPcf){"
        // Find free addr+channel for pcf_ type
        "const oth=items.filter((it,j)=>j!==idx&&isPcf(it.type));"
        "const am={};oth.forEach(it=>{if(!am[it.i2cAddr])am[it.i2cAddr]=new Set();am[it.i2cAddr].add(it.channel);});"
        "const ua=Object.keys(am).map(Number);"
        "let fa=items[idx].i2cAddr||0x20,fc=0;"
        "const cc=am[fa]||new Set();"
        "const frCh=[0,1,2,3,4,5,6,7].find(c=>!cc.has(c));"
        "if(frCh!==undefined){fc=frCh;}else{"
        "let found=false;"
        "for(const a of addrsFor(v)){"
        "if(!am[a.n]){if(ua.length<2){fa=a.n;fc=0;found=true;break;}}"
        "else if(am[a.n].size<8){fa=a.n;fc=[0,1,2,3,4,5,6,7].find(c=>!am[a.n].has(c));found=true;break;}}"
        "if(!found){fa=addrsFor(v)[0].n;fc=0;}}"
        "items[idx].i2cAddr=fa;items[idx].channel=fc;"
        "if(!wasI2C&&!wasPcf)items[idx].pin=0;"
        "}else if(nowI2C){"
        "const used=items.filter((it,j)=>j!==idx&&isI2C(it.type)).map(x=>x.i2cAddr);"
        "const myAddrs=addrsFor(v);"
        "const fa=myAddrs.find(a=>!used.includes(a.n));"
        "items[idx].i2cAddr=fa?fa.n:(myAddrs[0]?myAddrs[0].n:0x38);"
        "if(!wasI2C&&!wasPcf)items[idx].pin=0;"
        "}else if((wasI2C||wasPcf)&&!nowI2C&&!nowPcf){"
        "const gpi=items.filter((it,j)=>j!==idx&&!isI2C(it.type)&&!isPcf(it.type));"
        "const fp=PINS.find(p=>!p.f&&!p.w&&!gpi.find(x=>x.pin===p.n))||PINS.find(p=>!p.f&&!gpi.find(x=>x.pin===p.n));"
        "items[idx].pin=fp?fp.n:0;items[idx].i2cAddr=0;items[idx].channel=0;"
        "}render();}"
        "function render(){"
        "const l=document.getElementById('list');l.innerHTML='';"
        "items.forEach((it,i)=>{"
        "const d=document.createElement('div');d.className='periph-item';"
        "const pcf=isPcf(it.type);const i2c=isI2C(it.type);"
        "let pctrl;"
        "if(pcf){"
        "pctrl=`<select style='min-width:90px' onchange='items[${i}].i2cAddr=+this.value;render()'>${pcfI2cOpts(i)}</select>`"
        "+`<select style='min-width:80px' onchange='items[${i}].channel=+this.value;render()'>${pcfChannelOpts(i)}</select>`;"
        "}else if(i2c){"
        "pctrl=`<select style='min-width:155px' onchange='items[${i}].i2cAddr=+this.value'>${i2cOpts(i)}</select>`"
        "+`<span style='font-size:.75rem;color:#8e8e93;white-space:nowrap;background:#1c1c1e;padding:3px 8px;border-radius:6px'>SDA&#x2192;" MPCB_STR(I2C_SDA) " &nbsp; SCL&#x2192;" MPCB_STR(I2C_SCL) "</span>`;"
        "}else{"
        "pctrl=`<select style='min-width:155px' onchange='items[${i}].pin=+this.value;render()'>${pinOpts(i)}</select>`;"
        "}"
        // Analog calibration sub-row
        "let calPart='';"
        "if(it.type==='analog'){"
        "const cm=it.calMode||0;"
        "calPart=`<div style='flex-basis:100%;margin-top:2px;display:flex;flex-wrap:wrap;gap:6px;align-items:center'>`"
        "+`<span class='rlbl'>КАЛ</span>`"
        "+`<select onchange='items[${i}].calMode=+this.value;render()'>`"
        "+`<option value='0'${cm===0?' selected':''}>raw</option>`"
        "+`<option value='1'${cm===1?' selected':''}>linear</option>`"
        "+`<option value='2'${cm===2?' selected':''}>thermistor</option>`"
        "+`</select>`;"
        "if(cm===1){"
        "calPart+=`<input type='number' style='width:68px' placeholder='raw↓' title='ADC min' value='${it.calRawMin||0}' onchange='items[${i}].calRawMin=+this.value'>`"
        "+`<input type='number' style='width:68px' placeholder='raw↑' title='ADC max' value='${it.calRawMax||4095}' onchange='items[${i}].calRawMax=+this.value'>`"
        "+`<input type='number' style='width:68px' placeholder='знач↓' title='val min' value='${it.calValMin||0}' onchange='items[${i}].calValMin=+this.value'>`"
        "+`<input type='number' style='width:68px' placeholder='знач↑' title='val max' value='${it.calValMax||100}' onchange='items[${i}].calValMax=+this.value'>`"
        "+`<input style='width:60px' placeholder='ед.' value='${it.calUnit||''}' onchange='items[${i}].calUnit=this.value'>`;"
        "}else if(cm===2){"
        "calPart+=`<input type='number' style='width:80px' placeholder='R_ref Ω' title='Reference resistor Ω' value='${it.calRRef||10000}' onchange='items[${i}].calRRef=+this.value'>`"
        "+`<input type='number' style='width:68px' placeholder='Beta' value='${it.calBeta||3950}' onchange='items[${i}].calBeta=+this.value'>`"
        "+`<input type='number' style='width:80px' placeholder='R25 Ω' title='Thermistor R at 25°C' value='${it.calR25||10000}' onchange='items[${i}].calR25=+this.value'>`"
        "+`<input style='width:60px' placeholder='ед.' value='${it.calUnit||''}' onchange='items[${i}].calUnit=this.value'>`;"
        "}"
        "calPart+=`</div>`;"
        "}"
        "let zonePart='';"
        "if(it.type==='vl53l0'||it.type==='vl53l1'){"
        "zonePart=`<div style='flex-basis:100%;margin-top:2px;display:flex;flex-wrap:wrap;gap:6px;align-items:center'>`"
        "+`<span class='rlbl'>ЗОНА</span>`"
        "+`<input type='number' style='width:82px' placeholder='уст. мм' title='Setpoint мм (0=выкл)' value='${it.zoneSet||0}' onchange='items[${i}].zoneSet=+this.value'>`"
        "+`<span style='font-size:.75rem;color:#8e8e93'>±</span>`"
        "+`<input type='number' style='width:70px' placeholder='гист. мм' title='Гистерезис мм' value='${it.zoneHyst||0}' onchange='items[${i}].zoneHyst=+this.value'>`"
        "+`<span style='font-size:.72rem;color:#555'>мм &nbsp; (0=ближе, 1=зона, 2=дальше)</span>`"
        "+`</div>`;"
        "}"
        "d.innerHTML=`<select onchange='onTypeChange(${i},this.value)'>${typeOpts(i)}</select>`"
        "+pctrl"
        "+`<input value='${it.label||''}' placeholder='Название' onchange='items[${i}].label=this.value'>`"
        "+`<button onclick='items.splice(${i},1);render();renderRules()'>&#x2715;</button>`"
        "+calPart+zonePart;"
        "l.appendChild(d);});"
        "const gp=items.filter(it=>!isI2C(it.type)&&!isPcf(it.type));"
        "const freePins=PINS.filter(p=>!p.f&&!gp.find(x=>x.pin===p.n)).length;"
        "const usedI2CAddrs=items.filter(it=>isI2C(it.type)).map(x=>x.i2cAddr);"
        "const freeI2C=TYPES.filter(t=>t.i2c&&!t.pcf).reduce((s,t)=>{"
        "const u=items.filter(x=>x.type===t.v).length;"
        "const fa=t.addrs.filter(a=>!usedI2CAddrs.includes(a.n)).length;"
        "return s+Math.min(Math.max(0,t.max-u),fa);},0);"
        // PCF free slots: existing chips free channels + possible new chips
        "const pcfItems=items.filter(it=>isPcf(it.type));"
        "const pcfAddrMap={};pcfItems.forEach(it=>{"
        "if(!pcfAddrMap[it.i2cAddr])pcfAddrMap[it.i2cAddr]=new Set();"
        "pcfAddrMap[it.i2cAddr].add(it.channel);});"
        "const usedPcfAddrs=Object.keys(pcfAddrMap).map(Number);"
        "let freePcf=usedPcfAddrs.reduce((s,a)=>s+(8-pcfAddrMap[a].size),0);"
        "if(usedPcfAddrs.length<2)freePcf+=(2-usedPcfAddrs.length)*8;"
        "freePcf=Math.min(freePcf,Math.max(0,16-items.filter(x=>x.type==='pcf_relay').length)"
        "+Math.max(0,16-items.filter(x=>x.type==='pcf_button').length));"
        "const freeGPIOType=TYPES.filter(t=>!t.i2c&&!t.pcf).some(t=>gp.filter(x=>x.type===t.v).length<t.max);"
        "const canAdd=items.length<24&&((freePins>0&&freeGPIOType)||freeI2C>0||freePcf>0);"
        "document.getElementById('slots_info').innerHTML="
        "`<b>${items.length}/24</b> слотов &nbsp;&bull;&nbsp; <b>${freePins}</b> GPIO &nbsp;&bull;&nbsp; <b>${freeI2C}</b> I2C &nbsp;&bull;&nbsp; <b>${freePcf}</b> PCF`;"
        "const ba=document.getElementById('btn_add');"
        "ba.disabled=!canAdd;ba.style.opacity=canAdd?'1':'0.4';}"
        "function addItem(){"
        "if(items.length>=24)return;"
        "const gpi=items.filter(it=>!isI2C(it.type)&&!isPcf(it.type));"
        "const fp=PINS.find(p=>!p.f&&!p.w&&!gpi.find(x=>x.pin===p.n))||PINS.find(p=>!p.f&&!gpi.find(x=>x.pin===p.n));"
        "const fgt=fp?TYPES.find(t=>!t.i2c&&!t.pcf&&items.filter(x=>x.type===t.v).length<t.max):null;"
        "if(fgt){items.push({type:fgt.v,pin:fp.n,i2cAddr:0,channel:0,label:''});render();return;}"
        "const usedA=items.filter(it=>isI2C(it.type)).map(x=>x.i2cAddr);"
        "for(const t of TYPES.filter(t=>t.i2c&&!t.pcf)){"
        "if(items.filter(x=>x.type===t.v).length>=t.max)continue;"
        "const fa=t.addrs.find(a=>!usedA.includes(a.n));"
        "if(fa){items.push({type:t.v,pin:0,i2cAddr:fa.n,channel:0,label:''});render();return;}}"
        // PCF8574: find free addr+channel
        "const pcfI=items.filter(it=>isPcf(it.type));"
        "const pcfAM={};pcfI.forEach(it=>{if(!pcfAM[it.i2cAddr])pcfAM[it.i2cAddr]=new Set();pcfAM[it.i2cAddr].add(it.channel);});"
        "const uPA=Object.keys(pcfAM).map(Number);"
        "for(const t of TYPES.filter(t=>t.pcf)){"
        "if(items.filter(x=>x.type===t.v).length>=t.max)continue;"
        "for(const a of t.addrs){"
        "const chs=pcfAM[a.n]||new Set();"
        "if(!pcfAM[a.n]&&uPA.length>=2)continue;"
        "const fc=[0,1,2,3,4,5,6,7].find(c=>!chs.has(c));"
        "if(fc!==undefined){items.push({type:t.v,pin:0,i2cAddr:a.n,channel:fc,label:''});render();return;}}}"
        "}"
        "function savePeriph(){post('/api/gpio',{peripherals:items},d=>{"
        "toast(d.ok?'Сохранено':d.err||'Ошибка',d.ok);"
        "if(d.ok){const h=document.getElementById('reboot_hint');if(h)h.style.display='block';}}); }"
        "function reboot(){if(confirm('Перезагрузить устройство?'))post('/api/reboot',{},()=>{});}"
        "const I2C_NAMES={0x29:'VL53L0X/L1X',0x38:'AHT10/AHT20',0x39:'AHT10/AHT20',"
        "0x5A:'CCS811 TVOC',0x5B:'CCS811 TVOC',"
        "0x3C:'SSD1306 OLED',0x3D:'SSD1306 OLED',0x48:'ADS1115',0x49:'ADS1115',"
        "0x4A:'ADS1115',0x4B:'ADS1115',0x68:'MPU6050/DS3231',0x69:'MPU6050',"
        "0x76:'BME/BMP280',0x77:'BME/BMP280',0x20:'PCF8574',0x21:'PCF8574',"
        "0x22:'PCF8574',0x23:'PCF8574',0x24:'PCF8574',0x25:'PCF8574',"
        "0x26:'PCF8574',0x27:'PCF8574'};"
        "const WIRE_ERR=['OK','data too long','NACK on addr','NACK on data','other error','timeout'];"
        "function scanI2c(){"
        "const el=document.getElementById('i2c_result');"
        "el.innerHTML='<span style=\"color:#888\">Сканирование...</span>';"
        "fetch('/api/i2c-scan').then(r=>r.json()).then(d=>{"
        "let h='<div style=\"font-size:.78rem;color:#8e8e93;margin-bottom:8px\">SDA→GPIO'+d.sda+'  SCL→GPIO'+d.scl"
        "+'  |  probe 0x38: <code style=\"color:'+(d.probe_0x38===0?'#34c759':'#ff453a')+'\">'+(WIRE_ERR[d.probe_0x38]||d.probe_0x38)+'</code></div>';"
        "if(!d.devices||d.devices.length===0){"
        "h+='<span style=\"color:#ff453a\">Устройства не найдены.</span> ';"
        "if(d.probe_0x38!==0)h+='<span style=\"color:#f4a261\">Возможно: нет подтяжек, неверные пины, нет питания.</span>';"
        "el.innerHTML=h;return;}"
        "h+='<table style=\"border-collapse:collapse;width:100%\">';"
        "h+='<tr><th style=\"text-align:left;padding:4px 8px;color:#8e8e93\">Адрес</th>"
        "<th style=\"text-align:left;padding:4px 8px;color:#8e8e93\">Устройство</th></tr>';"
        "d.devices.forEach(dev=>{"
        "const name=I2C_NAMES[dev.addr]||'Неизвестно';"
        "h+='<tr><td style=\"padding:4px 8px;font-family:monospace;color:#a855f7\">'+dev.hex+'</td>"
        "<td style=\"padding:4px 8px\">'+name+'</td></tr>';"
        "});"
        "h+='</table>';"
        "el.innerHTML=h;"
        "}).catch(()=>{el.innerHTML='<span style=\"color:#ff453a\">Ошибка запроса</span>';});}"
        "render();"

        // rules
        "const ACTIONS=[{v:'toggle',l:'переключить'},{v:'on',l:'включить'}"
        ",{v:'off',l:'выключить'},{v:'pulse',l:'импульс'}];"
        "const TRIGGER_TYPES=['button','pcf_button','analog','dht22','ds18b20','aht10','vl53l0','vl53l1','ccs811'];"
        "const TARGET_TYPES=['relay','pcf_relay','pwm','neopixel'];"
        "function trigType(key){const it=items.find(x=>(san(x.label)||x.type+'_'+x.pin)===san(key));return it?it.type:'';}"
        "function eventsFor(type){"
        "if(['dht22','aht10'].includes(type))return["
        "{v:'temp_above',l:'темп >'},{v:'temp_below',l:'темп <'},"
        "{v:'hum_above',l:'влажн >'},{v:'hum_below',l:'влажн <'}];"
        "if(['ds18b20','analog'].includes(type))return["
        "{v:'above',l:'значение >'},{v:'below',l:'значение <'}];"
        "if(['vl53l0','vl53l1'].includes(type))return["
        "{v:'above',l:'дистанция >'},{v:'below',l:'дистанция <'},"
        "{v:'zone_eq',l:'зона ='}];"
        "return[{v:'pressed',l:'нажата'},{v:'released',l:'отпущена'},{v:'any',l:'любое'}];}"
        "let rules=" + rulesJson + ";"
        "rules=rules.map(r=>Object.assign({threshold:0,pulseMs:500},r));"

        "function periphOpts(sel,filter){"
        "const selN=san(sel||'');"  // normalise: handles BLE-saved keys with uppercase
        "const src=filter?items.filter(it=>filter.includes(it.type)):items;"
        "if(!src.length)return`<option value=''>—нет—</option>`;"
        "return src.map(it=>{"
        "const k=san(it.label)||it.type+'_'+it.pin;"
        "const l=it.label||it.type+'_'+it.pin;"
        "return`<option value='${k}'${k===selN?' selected':''}>${l}</option>`;}).join('');}"

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
        "const tt=trigType(r.trigger);"
        "const evts=eventsFor(tt);"
        "const isSensor=['dht22','aht10','ds18b20','analog','vl53l0','vl53l1'].includes(tt);"
        "if(!evts.find(e=>e.v===r.event))r.event=evts[0].v;"
        "const pmsVis=r.action==='pulse'?'':'display:none';"
        "const thrVis=isSensor?'':'display:none';"
        "d.innerHTML="
        "`<span class='rlbl'>ЕСЛИ</span>`"
        "+`<select onchange='rules[${i}].trigger=this.value;renderRules()'>${periphOpts(r.trigger,TRIGGER_TYPES)}</select>`"
        "+`<select onchange='rules[${i}].event=this.value'>`"
        "+evts.map(e=>`<option value='${e.v}'${r.event===e.v?' selected':''}>${e.l}</option>`).join('')"
        "+`</select>`"
        "+`<input type='number' step='0.5' style='width:72px;flex:0 0 72px;${thrVis}'`"
        "+` value='${r.threshold||0}' onchange='rules[${i}].threshold=+this.value' placeholder='порог'>`"
        "+`<span class='rarrow'>&#x2794;</span>`"
        "+`<span class='rlbl'>ТО</span>`"
        "+`<select onchange='onActChange(${i},this.value)'>`"
        "+ACTIONS.map(a=>`<option value='${a.v}'${r.action===a.v?' selected':''}>${a.l}</option>`).join('')"
        "+`</select>`"
        "+`<input id='pms${i}' type='number' min='100' max='60000' step='100'`"
        "+` value='${r.pulseMs||500}' onchange='rules[${i}].pulseMs=+this.value'`"
        "+` style='${pmsVis}' placeholder='мс' title='Длительность импульса, мс'>`"
        "+`<select onchange='rules[${i}].target=this.value'>${periphOpts(r.target,TARGET_TYPES)}</select>`"
        "+`<button onclick='rules.splice(${i},1);renderRules()'>&#x2715;</button>`;"
        "l.appendChild(d);});}"

        "function addRule(){"
        "const tr=items.find(it=>TRIGGER_TYPES.includes(it.type));"
        "const tg=items.find(it=>TARGET_TYPES.includes(it.type));"
        "const tk=tr?san(tr.label)||tr.type+tr.pin:'';"
        "const gk=tg?san(tg.label)||tg.type+tg.pin:'';"
        "const defEvt=eventsFor(tr?tr.type:'')[0].v;"
        "rules.push({trigger:tk,event:defEvt,threshold:0,action:'toggle',target:gk,pulseMs:500});"
        "renderRules();}"

        "function saveRules(){post('/api/rules',{rules:rules},d=>toast(d.ok?'Сохранено':'Ошибка',d.ok));}"
        "renderRules();"
        "</script>";

    // Stream response with chunked transfer — never assemble a full ~23 KB page
    // string. Sending header/body/footer separately keeps peak heap ~17 KB.
    _server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    _server.send(200, "text/html", "");

    String hdr = "<!DOCTYPE html><html lang='ru'><head>"
        "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>mpcb-iot \xe2\x80\x94 GPIO</title><style>" + String(FPSTR(CONFIG_CSS)) + "</style></head>"
        "<body><div class='wrap'><h1>&#x2699; mpcb-iot Config</h1>"
        "<nav>"
        "<a href='/'>&#x2302; \xd0\xa3\xd1\x81\xd1\x82\xd1\x80\xd0\xbe\xd0\xb9\xd1\x81\xd1\x82\xd0\xb2\xd0\xbe</a>"
        "<a href='/wifi'>&#x1F4F6; WiFi</a>"
        "<a href='/mqtt'>&#x1F4E1; MQTT</a>"
        "<a href='/dash'>&#x1F4CA; \xd0\xa1\xd1\x82\xd0\xb0\xd1\x82\xd1\x83\xd1\x81</a>"
        "<a href='/gpio' class='active'>&#x26A1; GPIO</a>"
        "<a href='/logs'>&#x1F4CB; \xd0\x9b\xd0\xbe\xd0\xb3\xd0\xb8</a>"
        "<a href='/ota'>&#x1F4E6; OTA</a>"
        "</nav>";
    _server.sendContent(hdr);
    hdr = String();

    const char* ptr = body.c_str();
    size_t rem = body.length();
    while (rem > 0) {
        size_t chunk = rem < 2048 ? rem : 2048;
        _server.sendContent(ptr, chunk);
        ptr += chunk;
        rem -= chunk;
    }

    _server.sendContent("</div><div id='toast'></div><script>" +
                        String(FPSTR(CONFIG_JS)) + "</script></body></html>");
    _server.sendContent("", 0);
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
    if (!arr || arr.size() > 24) {
        _server.send(400, "application/json", "{\"ok\":false,\"err\":\"max 24 peripherals\"}");
        return;
    }

    static const char*   tNames[]  = {"relay","button","analog","pwm","neopixel","dht22","ds18b20","aht10","vl53l0","vl53l1","ccs811","pcf_relay","pcf_button"};
    static const uint8_t tLimits[] = {     8,       8,       4,    4,         2,      2,        2,      2,       1,       1,       1,        16,         16};
    static const uint8_t tCount    = 13;
    static const char*   i2cTypes[] = {"aht10","vl53l0","vl53l1","pcf_relay","pcf_button","ccs811"};
    static const uint8_t i2cCount   = 6;
#ifdef CONFIG_IDF_TARGET_ESP32C3
    static const uint8_t forbidden[] = {};
    const bool hasForbidden = false;
#else
    static const uint8_t forbidden[] = {12, 13};
    const bool hasForbidden = true;
#endif

    uint8_t cnt[tCount] = {};
    bool    usedPin[24] = {};

    // PCF8574 channel dedup state
    struct PcfChan { uint8_t addr; uint8_t ch; };
    PcfChan pcfChans[32];
    uint8_t pcfChanCnt  = 0;
    uint8_t pcfAddrs[8] = {};
    uint8_t pcfAddrCnt  = 0;

    for (JsonObject obj : arr) {
        String type = obj["type"].as<String>();
        bool isI2C  = false;
        bool isPcf  = (type == "pcf_relay" || type == "pcf_button");
        for (uint8_t k = 0; k < i2cCount; k++) if (type == i2cTypes[k]) { isI2C = true; break; }

        if (!isI2C) {
            uint8_t pin = obj["pin"] | 255;
            if (hasForbidden) {
                for (uint8_t fp : forbidden) {
                    if (pin == fp) {
                        _server.send(400, "application/json",
                            "{\"ok\":false,\"err\":\"GPIO" + String(pin) + " запрещён\"}");
                        return;
                    }
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
        }

        if (isPcf) {
            uint8_t addr = obj["i2cAddr"] | 0;
            uint8_t ch   = obj["channel"] | 0;
            if (addr < 0x20 || addr > 0x27) {
                _server.send(400, "application/json",
                    "{\"ok\":false,\"err\":\"PCF8574 недопустимый адрес\"}");
                return;
            }
            if (ch > 7) {
                _server.send(400, "application/json",
                    "{\"ok\":false,\"err\":\"PCF8574 канал 0–7\"}");
                return;
            }
            for (uint8_t k = 0; k < pcfChanCnt; k++) {
                if (pcfChans[k].addr == addr && pcfChans[k].ch == ch) {
                    _server.send(400, "application/json",
                        "{\"ok\":false,\"err\":\"PCF 0x" + String(addr, HEX) + " ch" + String(ch) + " занят\"}");
                    return;
                }
            }
            pcfChans[pcfChanCnt++] = {addr, ch};
            bool found = false;
            for (uint8_t k = 0; k < pcfAddrCnt; k++) if (pcfAddrs[k] == addr) { found = true; break; }
            if (!found) {
                if (pcfAddrCnt >= 2) {
                    _server.send(400, "application/json",
                        "{\"ok\":false,\"err\":\"макс. 2 PCF8574 чипа\"}");
                    return;
                }
                pcfAddrs[pcfAddrCnt++] = addr;
            }
        }

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
// Dashboard
// ---------------------------------------------------------------------------

void ConfigServer::_handleDash() {
    String body =
        "<div class='card'>"
        "<h2>&#x1F4CA; Состояние периферии</h2>"
        "<div id='dash' style='display:grid;grid-template-columns:repeat(auto-fill,minmax(180px,1fr));gap:10px;margin-top:4px'>"
        "<span style='color:#8e8e93;font-size:.85rem'>Загрузка...</span>"
        "</div>"
        "<p style='font-size:.75rem;color:#555;margin-top:12px'>Обновление каждые 2с</p>"
        "</div>"

        "<script>"
        "function icon(t){"
        "return{relay:'&#x1F50C;',pcf_relay:'&#x1F50C;',button:'&#x1F518;',pcf_button:'&#x1F518;',"
        "analog:'&#x1F4CA;',pwm:'&#x3030;',neopixel:'&#x1F7E3;',"
        "dht22:'&#x1F321;',ds18b20:'&#x1F321;',aht10:'&#x1F321;',vl53l0:'&#x1F4CF;',vl53l1:'&#x1F4CF;'}[t]||'&#x26AA;';}"
        "function val(p){"
        "if(!p.ok)return '<span style=\"color:#ff453a\">нет связи</span>';"
        "if(p.type==='relay'||p.type==='pcf_relay')"
        "return '<span style=\"color:'+(p.on?'#34c759':'#8e8e93')+'\">'+(p.on?'ВКЛ':'выкл')+'</span>';"
        "if(p.type==='button'||p.type==='pcf_button')"
        "return '<span style=\"color:'+(p.pressed?'#ff453a':'#8e8e93')+'\">'+(p.pressed?'&#x25CF; НАЖАТА':'&#x25CB; отпущена')+'</span>';"
        "if(p.type==='aht10'||p.type==='dht22')"
        "return p.temp.toFixed(1)+'&#176;C &nbsp; '+p.humidity.toFixed(1)+'%';"
        "if(p.type==='ds18b20')return p.temp.toFixed(1)+'&#176;C';"
        "if(p.type==='vl53l0'||p.type==='vl53l1'){"
        "const zl=['ближе','зона','дальше'];"
        "return p.distance+' мм'+(p.zone!==undefined?' <span style=\"color:#a855f7;font-size:.82rem\">['+zl[p.zone]+']</span>':'');}"
        "if(p.type==='ccs811')return 'CO&#8322; '+p.eco2+' ppm &nbsp; TVOC '+p.tvoc+' ppb';"
        "if(p.type==='analog')return p.converted!==undefined?p.converted.toFixed(2)+' '+(p.unit||''):p.value;"
        "if(p.type==='pwm')return 'duty '+p.duty;"
        "return '?';}"
        "function btn(p){"
        "if((p.type==='relay'||p.type==='pcf_relay')&&p.ok)"
        "return '<button onclick=\"cmd(\\'' + p.key + '\\',{on:'+(!p.on)+'})\" "
        "style=\"margin-top:8px;padding:4px 14px;font-size:.78rem;background:'+(p.on?'#3b1a1a':'#1a3b1a')+"
        "';color:'+(p.on?'#ff453a':'#34c759')+';border-radius:7px;border:none;cursor:pointer\">'+(p.on?'Выкл':'Вкл')+'</button>';"
        "return '';}"
        "function render(data){"
        "document.getElementById('dash').innerHTML=data.map(p=>"
        "'<div style=\"background:#2a2a2d;border-radius:14px;padding:14px\">'"
        "+'<div style=\"font-size:.72rem;color:#8e8e93;margin-bottom:3px\">'+icon(p.type)+' '+p.type+'</div>'"
        "+'<div style=\"font-size:.9rem;font-weight:600;margin-bottom:6px\">'+p.label+'</div>'"
        "+'<div style=\"font-size:1rem\">'+val(p)+'</div>'"
        "+btn(p)"
        "+'</div>').join('');}"
        "function cmd(key,data){"
        "fetch('/api/cmd',{method:'POST',headers:{'Content-Type':'application/json'},"
        "body:JSON.stringify({key:key,payload:JSON.stringify(data)})});"
        "setTimeout(refresh,200);}"
        "function refresh(){"
        "fetch('/api/state').then(r=>r.json()).then(render)"
        ".catch(()=>{});}"
        "refresh();setInterval(refresh,2000);"
        "</script>";

    _server.send(200, "text/html", _page("Статус", "dash", body));
}

void ConfigServer::_handleApiState() {
    String json = _stateProvider ? _stateProvider() : "[]";
    _server.send(200, "application/json", json);
}

void ConfigServer::_handleApiCmd() {
    if (!_server.hasArg("plain")) { _server.send(400); return; }
    JsonDocument doc;
    if (deserializeJson(doc, _server.arg("plain")) != DeserializationError::Ok) {
        _server.send(400, "application/json", "{\"ok\":false}");
        return;
    }
    String key     = doc["key"].as<String>();
    String payload = doc["payload"].as<String>();
    if (_cmdHandler && key.length()) {
        _cmdHandler(key, payload);
        _server.send(200, "application/json", "{\"ok\":true}");
    } else {
        _server.send(400, "application/json", "{\"ok\":false}");
    }
}

// ---------------------------------------------------------------------------
// I2C scanner
// ---------------------------------------------------------------------------

void ConfigServer::_handleI2cScan() {
    int sda = _server.hasArg("sda") ? _server.arg("sda").toInt() : I2C_SDA;
    int scl = _server.hasArg("scl") ? _server.arg("scl").toInt() : I2C_SCL;

    if (sda != I2C_SDA || scl != I2C_SCL) {
        Wire.end();
        delay(20);
        Wire.begin(sda, scl);
        Wire.setClock(400000);
        delay(20);
    }

    Wire.beginTransmission(0x38);
    uint8_t probe = Wire.endTransmission();

    String json = "{\"sda\":" + String(sda) + ",\"scl\":" + String(scl) +
                  ",\"probe_0x38\":" + String(probe) + ",\"devices\":[";
    bool first = true;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        uint8_t err = Wire.endTransmission();
        if (err == 0) {
            if (!first) json += ",";
            char buf[8];
            snprintf(buf, sizeof(buf), "0x%02X", addr);
            json += "{\"addr\":" + String(addr) + ",\"hex\":\"" + buf + "\"}";
            first = false;
        }
    }
    json += "]}";
    Log.log("I2C scan sda=" + String(sda) + " scl=" + String(scl) +
            " probe_0x38=" + String(probe) + (first ? " no devices" : " found devices"));

    if (sda != I2C_SDA || scl != I2C_SCL) {
        Wire.end();
        delay(20);
        Wire.begin(I2C_SDA, I2C_SCL);
        Wire.setClock(400000);
    }

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
