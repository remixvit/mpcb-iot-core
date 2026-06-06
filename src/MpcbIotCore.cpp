#include "MpcbIotCore.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ESPmDNS.h>

// ---------------------------------------------------------------------------

void MpcbIotCore::begin(const String& deviceName) {
    _storage.begin();

    WiFi.mode(WIFI_STA);  // needed before macAddress() returns valid data
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char suffix[5];
    snprintf(suffix, sizeof(suffix), "%02X%02X", mac[4], mac[5]);
    _apName = "mpcb-" + String(suffix);

    DeviceConfig dev = _storage.loadDevice();
    if (dev.deviceId.isEmpty()) {
        dev.deviceId = "esp32-" + String(suffix);
        if (!deviceName.isEmpty()) dev.deviceName = deviceName;
        _storage.saveDevice(dev);
    }

    Log.log("IoT", "Device: " + dev.deviceName + " (" + dev.deviceId + ")");
    Log.log("IoT", "MAC: " + WiFi.macAddress());

    // ── Pre-scan before BLE — avoids STA→AP mode-switch issues on C3 ─────────
    // When no WiFi is saved, we'll need the AP portal. Scanning now (before BLE
    // starts sharing the radio) ensures softAP() gets a clean mode transition.
    if (!_storage.hasWifi()) {
        delay(100);
        int n = WiFi.scanNetworks();
        String scanJson = "[";
        for (int i = 0; i < n; i++) {
            if (i) scanJson += ",";
            scanJson += "{\"ssid\":\"" + WiFi.SSID(i) + "\","
                       "\"rssi\":"    + WiFi.RSSI(i) + ","
                       "\"enc\":"     + (WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false") + "}";
        }
        scanJson += "]";
        WiFi.scanDelete();
        _preScanJson = scanJson;
        Log.log("IoT", "Pre-scan: " + String(n) + " networks");
    }

    // ── BLE — запускается всегда, независимо от WiFi ─────────────────────────
    _ble.begin(_apName.c_str(),
        [this](const String& json) { _handleBleCommand(json); },
        [this](NimBLEServer* server) { _bleOta.createService(server)->start(); }
    );
    _ble.updateSettings(_buildSettingsJson());

    _setState(IotState::BOOTING);

    if (_storage.hasWifi()) {
        WifiConfig wifi = _storage.loadWifi();
        Log.log("IoT", "Connecting to WiFi: " + wifi.ssid);
        _wifi.begin(wifi.ssid, wifi.password);
        _setState(IotState::CONNECTING);

        if (_wifi.waitConnected(12000)) {
            _startConfigServer();
        } else {
            Log.log("IoT", "WiFi failed, starting AP portal");
            _startAP();
        }
    } else {
        Log.log("IoT", "No WiFi saved, starting AP portal");
        _startAP();
    }
}

void MpcbIotCore::loop() {
    Log.tick();

    switch (_state) {
        case IotState::AP_PORTAL:
            if (_portal) _portal->loop();
            if (_closeApRequested) {
                _closeApRequested = false;
                _portal->stop();
                delete _portal;
                _portal = nullptr;
                WiFi.mode(WIFI_STA);
                _startConfigServer();
            }
            break;

        case IotState::CONFIG_SERVER:
            if (_configServer) _configServer->loop();
            _wifi.loop();
            _mqttLoop();
            break;

        case IotState::RUNNING:
            if (_configServer) _configServer->loop();
            _wifi.loop();
            _mqttLoop();
            break;

        default:
            break;
    }

    _bleLoop();
}

void MpcbIotCore::_bleLoop() {
    _bleOta.loop();
    if (!_ble.connected()) return;
    uint32_t now = millis();

    if (_wifiScanPending) {
        int n = WiFi.scanComplete();
        if (n == WIFI_SCAN_RUNNING) return;
        _wifiScanPending = false;
        if (n > 0) {
            JsonDocument doc;
            JsonArray arr = doc["wifi_networks"].to<JsonArray>();
            for (int i = 0; i < n; i++) {
                JsonObject net = arr.add<JsonObject>();
                net["ssid"] = WiFi.SSID(i);
                net["rssi"] = WiFi.RSSI(i);
            }
            String out;
            serializeJson(doc, out);
            _ble.updateStatus(out);
            WiFi.scanDelete();
            Log.log("BLE", "WiFi scan: " + String(n) + " networks");
        }
        if (_state == IotState::AP_PORTAL) WiFi.mode(WIFI_AP);
        return;
    }

    if (now - _bleStatusAt >= 1000) {
        _bleStatusAt = now;
        _ble.updateStatus(_buildStatusJson());
    }
}

// ---------------------------------------------------------------------------

void MpcbIotCore::_setState(IotState s) {
    _state = s;
    if (_onState) _onState(s);
}

void MpcbIotCore::_startAP() {
    _portal = new APPortal(_apName);
    if (!_preScanJson.isEmpty()) _portal->setCachedScan(_preScanJson);
    _portal->onConnect([this](const String& ssid, const String& pass) {
        _storage.saveWifi(ssid, pass);
        Log.log("IoT", "WiFi saved: " + ssid + ". Waiting for STA connection...");
        // Don't reboot — AP stays up, portal shows connection status
    });
    _portal->onClose([this]() {
        _closeAP();
    });
    _portal->begin();
    _setState(IotState::AP_PORTAL);
    Log.log("IoT", "AP portal: connect to \"" + _apName + "\" -> http://192.168.4.1");
}

void MpcbIotCore::_closeAP() {
    // Deferred — we're called from inside _portal->loop() (HTTP handler)
    // Actual cleanup happens after loop() returns
    _closeApRequested = true;
}

void MpcbIotCore::_startConfigServer() {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);  // Disable modem sleep — keeps TCP responsive for web server
    Log.log("IoT", "WiFi connected, IP: " + WiFi.localIP().toString());

    // mDNS: device accessible at http://mpcb-XXXX.local
    String mdnsName = _apName;  // "mpcb-XXXX"
    if (MDNS.begin(mdnsName.c_str())) {
        MDNS.addService("http", "tcp", 80);
        Log.log("IoT", "mDNS: http://" + mdnsName + ".local");
    }

    if (!_noConfigServer) {
        _configServer = new ConfigServer(_storage);
        if (_dashState) _configServer->onStateRequest(_dashState);
        if (_dashCmd)   _configServer->onCmd(_dashCmd);
        for (auto& r : _pendingRoutes) _configServer->addRoute(r.first, r.second);
        _configServer->begin();
        Log.log("IoT", "Config server: http://" + WiFi.localIP().toString());
    }
    _setState(IotState::CONFIG_SERVER);
    _connectMqtt();
}

// ---------------------------------------------------------------------------
// MQTT
// ---------------------------------------------------------------------------

void MpcbIotCore::_connectMqtt() {
    MqttConfig cfg = _storage.loadMqtt();
    if (cfg.host.isEmpty()) {
        Log.log("MQTT", "No broker configured — skipping");
        return;
    }

    if (!_wifiClient) {
        if (cfg.tls) {
            auto* c = new WiFiClientSecure();
            c->setInsecure();
            _wifiClient = c;
        } else {
            _wifiClient = new WiFiClient();
        }
    }

    if (!_mqttClient) {
        PubSubClient* mqtt;
        if (cfg.tls) {
            mqtt = new PubSubClient(*reinterpret_cast<WiFiClientSecure*>(_wifiClient));
        } else {
            mqtt = new PubSubClient(*reinterpret_cast<WiFiClient*>(_wifiClient));
        }
        mqtt->setBufferSize(1024);
        mqtt->setCallback([this](char* topic, byte* payload, unsigned int len) {
            String t(topic);
            String p;
            for (unsigned int i = 0; i < len; i++) p += (char)payload[i];
            Log.log("MQTT", "← " + t + ": " + p);
            if (_onMqtt) _onMqtt(t, p);
        });
        _mqttClient = mqtt;
    }

    DeviceConfig dev = _storage.loadDevice();
    PubSubClient* mqtt = reinterpret_cast<PubSubClient*>(_mqttClient);
    mqtt->setServer(cfg.host.c_str(), cfg.port);

    String lwtTopic   = "mpcb/devices/" + dev.deviceId + "/announce";
    String lwtPayload = "{\"online\":false}";

    Log.log("MQTT", "Connecting to " + cfg.host + ":" + String(cfg.port) + "...");
    bool ok = cfg.user.isEmpty()
        ? mqtt->connect(dev.deviceId.c_str(),
                        nullptr, nullptr,
                        lwtTopic.c_str(), 1, true, lwtPayload.c_str())
        : mqtt->connect(dev.deviceId.c_str(),
                        cfg.user.c_str(), cfg.password.c_str(),
                        lwtTopic.c_str(), 1, true, lwtPayload.c_str());

    if (ok) {
        Log.log("MQTT", "Connected OK");
        // Announce online
        String ann = "{\"online\":true,\"ip\":\"" + WiFi.localIP().toString() + "\"}";
        mqtt->publish(lwtTopic.c_str(), ann.c_str(), true);
        Log.log("MQTT", "Announced online");

        if (_state != IotState::RUNNING) {
            _setState(IotState::RUNNING);
        }
        if (_onConnected) _onConnected();
    } else {
        Log.log("MQTT", "Failed rc=" + String(mqtt->state()) + ", retry in 5s");
        _mqttReconnectAt = millis() + 5000;
    }
}

void MpcbIotCore::_mqttLoop() {
    if (!_mqttClient) return;
    PubSubClient* mqtt = reinterpret_cast<PubSubClient*>(_mqttClient);

    if (!mqtt->connected()) {
        if (millis() >= _mqttReconnectAt) {
            _connectMqtt();
        }
        return;
    }
    mqtt->loop();
}

bool MpcbIotCore::publish(const String& topic, const String& payload, bool retain) {
    if (!_mqttClient) return false;
    PubSubClient* mqtt = reinterpret_cast<PubSubClient*>(_mqttClient);
    if (!mqtt->connected()) return false;
    bool ok = mqtt->publish(topic.c_str(), payload.c_str(), retain);
    if (ok) Log.log("MQTT", "→ " + topic + ": " + payload);
    return ok;
}

// ---------------------------------------------------------------------------
// BLE helpers
// ---------------------------------------------------------------------------

String MpcbIotCore::_buildSettingsJson() {
    DeviceConfig dev  = _storage.loadDevice();
    MqttConfig   mqtt = _storage.loadMqtt();
    WifiConfig   wifi = _storage.loadWifi();

    JsonDocument doc;
    doc["device_id"]   = dev.deviceId;
    doc["device_name"] = dev.deviceName;
    doc["wifi_ssid"]   = wifi.ssid;
    doc["mqtt_host"]   = mqtt.host;
    doc["mqtt_port"]   = mqtt.port;
    doc["mqtt_user"]   = mqtt.user;
    doc["mqtt_tls"]    = mqtt.tls;
    doc["ip"]          = WiFi.localIP().toString();

    // Peripherals and rules as nested JSON
    JsonDocument pd, rd;
    if (deserializeJson(pd, _storage.loadPeripherals()) == DeserializationError::Ok)
        doc["peripherals"] = pd;
    if (deserializeJson(rd, _storage.loadRules()) == DeserializationError::Ok)
        doc["rules"] = rd;

    String out;
    serializeJson(doc, out);
    return out;
}

String MpcbIotCore::_buildStatusJson() {
    static const char* stateNames[] = {
        "BOOTING", "AP_PORTAL", "CONNECTING", "CONFIG_SERVER", "RUNNING"
    };
    JsonDocument doc;
    doc["state"]  = stateNames[(int)_state];
    doc["ip"]     = WiFi.localIP().toString();
    doc["rssi"]   = WiFi.RSSI();
    doc["mqtt"]   = (_state == IotState::RUNNING);
    doc["uptime"] = millis() / 1000;
    doc["heap"]   = ESP.getFreeHeap() / 1024;
    String out;
    serializeJson(doc, out);
    return out;
}

void MpcbIotCore::_handleBleCommand(const String& json) {
    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return;

    // Special commands
    if (doc["cmd"].is<const char*>()) {
        String cmd = doc["cmd"].as<String>();
        if (cmd == "reboot")     { Log.log("BLE", "reboot cmd"); delay(300); ESP.restart(); }
        if (cmd == "reset_wifi") { _storage.clearWifi(); delay(300); ESP.restart(); }
        if (cmd == "wifi_scan") {
            if (_state == IotState::AP_PORTAL) WiFi.mode(WIFI_AP_STA);
            WiFi.scanNetworks(true);
            _wifiScanPending = true;
            Log.log("BLE", "WiFi scan started");
        }
        return;
    }

    bool needReboot = false;

    if (doc["wifi_ssid"].is<const char*>()) {
        _storage.saveWifi(doc["wifi_ssid"].as<String>(),
                          doc["wifi_pass"] | String(""));
        needReboot = true;
        Log.log("BLE", "WiFi saved: " + doc["wifi_ssid"].as<String>());
    }
    if (doc["mqtt_host"].is<const char*>()) {
        MqttConfig cfg = _storage.loadMqtt();
        cfg.host     = doc["mqtt_host"].as<String>();
        cfg.port     = doc["mqtt_port"] | cfg.port;
        cfg.user     = doc["mqtt_user"] | cfg.user;
        if (doc["mqtt_pass"].is<const char*>()) cfg.password = doc["mqtt_pass"].as<String>();
        cfg.tls      = doc["mqtt_tls"] | cfg.tls;
        _storage.saveMqtt(cfg);
        Log.log("BLE", "MQTT saved: " + cfg.host);
    }
    if (doc["device_name"].is<const char*>()) {
        DeviceConfig dev = _storage.loadDevice();
        dev.deviceName = doc["device_name"].as<String>();
        _storage.saveDevice(dev);
    }
    if (doc["peripherals"].is<JsonArray>()) {
        String out;
        serializeJson(doc["peripherals"], out);
        _storage.savePeripherals(out);
        Log.log("BLE", "Peripherals saved");
    }
    if (doc["rules"].is<JsonArray>()) {
        // Normalise trigger/target to lowercase so keys match peripheral sanitized keys
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

bool MpcbIotCore::subscribe(const String& topic) {
    if (!_mqttClient) return false;
    PubSubClient* mqtt = reinterpret_cast<PubSubClient*>(_mqttClient);
    if (!mqtt->connected()) return false;
    bool ok = mqtt->subscribe(topic.c_str());
    if (ok) Log.log("MQTT", "subscribed: " + topic);
    return ok;
}
