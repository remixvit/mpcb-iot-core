#include "MpcbZigbeeCore.h"
#ifdef ZIGBEE_MODE_ED

#include <ArduinoJson.h>
#include <esp_mac.h>  // esp_read_mac — не требует WiFi стека

// ─── Статический диспетчер ────────────────────────────────────────────────────
// ZigbeeLight::onLightChange принимает void(*)(bool) — нельзя лямбду с захватом.
// Используем глобальный указатель на единственный экземпляр.
static MpcbZigbeeCore* _zbInst = nullptr;
static uint8_t _relayCount = 0;

// До 8 реле — по одной статической функции на слот
static void _zbR0(bool on){if(_zbInst)_zbInst->_onRelayCb(0,on);}
static void _zbR1(bool on){if(_zbInst)_zbInst->_onRelayCb(1,on);}
static void _zbR2(bool on){if(_zbInst)_zbInst->_onRelayCb(2,on);}
static void _zbR3(bool on){if(_zbInst)_zbInst->_onRelayCb(3,on);}
static void _zbR4(bool on){if(_zbInst)_zbInst->_onRelayCb(4,on);}
static void _zbR5(bool on){if(_zbInst)_zbInst->_onRelayCb(5,on);}
static void _zbR6(bool on){if(_zbInst)_zbInst->_onRelayCb(6,on);}
static void _zbR7(bool on){if(_zbInst)_zbInst->_onRelayCb(7,on);}
using RelayFn = void(*)(bool);
static const RelayFn _zbRelayFns[] = {_zbR0,_zbR1,_zbR2,_zbR3,_zbR4,_zbR5,_zbR6,_zbR7};
static const uint8_t _zbRelayFnsMax = sizeof(_zbRelayFns)/sizeof(_zbRelayFns[0]);

// ─────────────────────────────────────────────────────────────────────────────

void MpcbZigbeeCore::begin(const String& deviceName) {
    _zbInst = this;
    _relayCount = 0;
    _storage.begin();

    // ── Device ID из MAC (не инициализируем WiFi — читаем eFuse напрямую) ────
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_IEEE802154);
    char suffix[5];
    snprintf(suffix, sizeof(suffix), "%02X%02X", mac[4], mac[5]);
    _bleAdvName = "mpcb-" + String(suffix);

    DeviceConfig dev = _storage.loadDevice();
    if (dev.deviceId.isEmpty()) {
        dev.deviceId   = "esp32-" + String(suffix);
        dev.deviceName = deviceName.isEmpty() ? "mpcb-c6z" : deviceName;
        _storage.saveDevice(dev);
    }
    _deviceId = dev.deviceId;
    Log.log("ZB", "Device: " + dev.deviceName + " (" + _deviceId + ")");

    // ── Создаём Zigbee endpoints по конфигу из NVS ───────────────────────────
    _createEndpoints();

    // ── Старт Zigbee стека (erase_nvs=false — сохраняем сеть) ───────────────
    Zigbee.begin(ZIGBEE_END_DEVICE, false);
    Log.log("ZB", "Zigbee stack started");

    // ── BLE конфигуратор + OTA ───────────────────────────────────────────────
    _ble.begin(_bleAdvName.c_str(),
        [this](const String& json) { _handleBleCommand(json); },
        [this](NimBLEServer* srv)  { _bleOta.createService(srv)->start(); }
    );
    _ble.updateSettings(_buildSettingsJson());
    Log.log("ZB", "BLE started: " + _bleAdvName);
}

void MpcbZigbeeCore::loop() {
    Log.tick();
    _bleOta.loop();
    _bleLoop();

    if (!_readyFired && Zigbee.isStarted()) {
        _readyFired = true;
        Log.log("ZB", "Ready — firing onReady");
        if (_onReady) _onReady();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Endpoint factory
// ─────────────────────────────────────────────────────────────────────────────

void MpcbZigbeeCore::_createEndpoints() {
    String json = _storage.loadPeripherals();
    if (json.isEmpty() || json == "[]") {
        Log.log("ZB", "No peripherals saved — no endpoints created");
        return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) {
        Log.log("ZB", "Peripherals JSON parse error");
        return;
    }

    for (JsonObject p : doc.as<JsonArray>()) {
        String type = p["type"].as<String>();
        String key  = p["key"].as<String>();
        if (type.isEmpty() || key.isEmpty()) continue;

        if (type == "relay" || type == "pcf_relay") {
            if (_relayCount >= _zbRelayFnsMax) {
                Log.log("ZB", "Max relay endpoints reached, skipping: " + key);
                continue;
            }
            auto* ep = new ZigbeeLight(_nextEp++);
            ep->setManufacturerAndModel("mpcbstudio", "mpcb-relay");
            ep->onLightChange(_zbRelayFns[_relayCount]);
            Zigbee.addEndpoint(ep);
            _eps.push_back({key, type, ep});
            Log.log("ZB", "EP" + String(_nextEp - 1) + " relay[" + String(_relayCount) + "]: " + key);
            _relayCount++;
        }
        else if (type == "dht22" || type == "aht10" || type == "ds18b20") {
            auto* ep = new ZigbeeTempSensor(_nextEp++);
            ep->setManufacturerAndModel("mpcbstudio", "mpcb-temp");
            ep->setMinMaxValue(-40, 85);
            ep->setReporting(30, 300, 0.5f);
            Zigbee.addEndpoint(ep);
            _eps.push_back({key, type, ep});
            Log.log("ZB", "EP" + String(_nextEp - 1) + " temp: " + key);
        }
        // Остальные типы (button, analog, vl53 и т.д.) — работают через PeriphManager
        // для rules/BLE, но не имеют Zigbee кластеров в этой версии
    }
}

void MpcbZigbeeCore::_dispatchCmd(const String& key, const String& payload) {
    if (!_onMessage) return;
    _onMessage("mpcb/devices/" + _deviceId + "/" + key + "/set", payload);
}

// ─────────────────────────────────────────────────────────────────────────────
// ITransport
// ─────────────────────────────────────────────────────────────────────────────

bool MpcbZigbeeCore::publish(const String& topic, const String& payload, bool retain) {
    // Парсим ключ из топика: "mpcb/devices/{id}/{key}/state"
    int p2 = topic.lastIndexOf('/');
    if (p2 < 1) return false;
    int p1 = topic.lastIndexOf('/', p2 - 1);
    if (p1 < 0) return false;
    String key = topic.substring(p1 + 1, p2);

    for (auto& slot : _eps) {
        if (slot.key != key) continue;

        if (slot.type == "dht22" || slot.type == "aht10" || slot.type == "ds18b20") {
            JsonDocument doc;
            if (deserializeJson(doc, payload) == DeserializationError::Ok
                    && doc["temp"].is<float>()) {
                auto* ep = static_cast<ZigbeeTempSensor*>(slot.ep);
                ep->setTemperature(doc["temp"].as<float>());
                ep->reportTemperature();
            }
        }
        // relay: состояние управляется coordinator'ом — не нужно пушить обратно
        return true;
    }
    return true;  // Типы без Zigbee кластера — тихо игнорируем
}

bool MpcbZigbeeCore::subscribe(const String& /*topic*/) {
    return true;  // Zigbee использует callbacks эндпоинтов, не topic-подписки
}

// ─────────────────────────────────────────────────────────────────────────────
// BLE
// ─────────────────────────────────────────────────────────────────────────────

void MpcbZigbeeCore::_bleLoop() {
    if (!_ble.connected()) return;
    uint32_t now = millis();
    if (now - _bleStatusAt >= 1000) {
        _bleStatusAt = now;
        _ble.updateStatus(_buildStatusJson());
    }
}

String MpcbZigbeeCore::_buildSettingsJson() {
    DeviceConfig dev = _storage.loadDevice();
    JsonDocument doc;
    doc["device_id"]   = dev.deviceId;
    doc["device_name"] = dev.deviceName;
    // WiFi и MQTT — не нужны для Zigbee устройств

    JsonDocument pd, rd;
    if (deserializeJson(pd, _storage.loadPeripherals()) == DeserializationError::Ok)
        doc["peripherals"] = pd;
    if (deserializeJson(rd, _storage.loadRules()) == DeserializationError::Ok)
        doc["rules"] = rd;

    String out;
    serializeJson(doc, out);
    return out;
}

String MpcbZigbeeCore::_buildStatusJson() {
    JsonDocument doc;
    doc["zigbee"] = Zigbee.isStarted();
    doc["uptime"] = millis() / 1000;
    doc["heap"]   = ESP.getFreeHeap() / 1024;
    String out;
    serializeJson(doc, out);
    return out;
}

void MpcbZigbeeCore::_handleBleCommand(const String& json) {
    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return;

    if (doc["cmd"].is<const char*>()) {
        String cmd = doc["cmd"].as<String>();
        if (cmd == "reboot")   { delay(300); ESP.restart(); }
        if (cmd == "zb_reset") { Zigbee.factoryReset(); }
        return;
    }

    bool needReboot = false;

    if (doc["device_name"].is<const char*>()) {
        DeviceConfig dev = _storage.loadDevice();
        dev.deviceName = doc["device_name"].as<String>();
        _storage.saveDevice(dev);
        Log.log("BLE", "Name: " + dev.deviceName);
    }
    if (doc["peripherals"].is<JsonArray>()) {
        String out;
        serializeJson(doc["peripherals"], out);
        _storage.savePeripherals(out);
        needReboot = true;  // endpoints создаются при старте
        Log.log("BLE", "Peripherals saved — reboot needed");
    }
    if (doc["rules"].is<JsonArray>()) {
        for (JsonObject r : doc["rules"].as<JsonArray>()) {
            if (r["trigger"].is<const char*>()) { String s = r["trigger"]; s.toLowerCase(); r["trigger"] = s; }
            if (r["target"].is<const char*>())  { String s = r["target"];  s.toLowerCase(); r["target"]  = s; }
        }
        String out;
        serializeJson(doc["rules"], out);
        _storage.saveRules(out);
        Log.log("BLE", "Rules saved");
    }

    _ble.updateSettings(_buildSettingsJson());
    if (needReboot) { delay(500); ESP.restart(); }
}

// ─────────────────────────────────────────────────────────────────────────────

void MpcbZigbeeCore::_onRelayCb(uint8_t slot, bool on) {
    // Находим ключ реле по порядковому номеру слота среди relay-эндпоинтов
    uint8_t idx = 0;
    for (auto& ep : _eps) {
        if (ep.type != "relay" && ep.type != "pcf_relay") continue;
        if (idx == slot) {
            _dispatchCmd(ep.key, on ? "{\"on\":true}" : "{\"on\":false}");
            return;
        }
        idx++;
    }
}

#endif  // ZIGBEE_MODE_ED
