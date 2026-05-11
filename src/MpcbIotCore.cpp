#include "MpcbIotCore.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

// ---------------------------------------------------------------------------

static MpcbIotCore* _instance = nullptr;

static void _mqttCb(char* topic, byte* payload, unsigned int length) {
    if (!_instance) return;
    String t(topic);
    String p;
    for (unsigned int i = 0; i < length; i++) p += (char)payload[i];
    // forward via stored callback — accessed through instance
    // (we expose it via friend or static trampoline below)
}

// ---------------------------------------------------------------------------

void MpcbIotCore::begin(const String& deviceName) {
    _instance = this;
    _storage.begin();

    // Build AP name: "mpcb-XXXX" from last 2 bytes of MAC
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char suffix[5];
    snprintf(suffix, sizeof(suffix), "%02X%02X", mac[4], mac[5]);
    _apName = "mpcb-" + String(suffix);

    // Auto-fill deviceId if empty
    DeviceConfig dev = _storage.loadDevice();
    if (dev.deviceId.isEmpty()) {
        dev.deviceId = "esp32-" + String(suffix);
        if (!deviceName.isEmpty()) dev.deviceName = deviceName;
        _storage.saveDevice(dev);
    }

    Serial.println("[IoT] Device: " + dev.deviceName + " (" + dev.deviceId + ")");

    _setState(IotState::BOOTING);

    if (_storage.hasWifi()) {
        WifiConfig wifi = _storage.loadWifi();
        _wifi.begin(wifi.ssid, wifi.password);
        _setState(IotState::CONNECTING);

        if (_wifi.waitConnected(12000)) {
            _startConfigServer();
        } else {
            Serial.println("[IoT] WiFi failed, starting AP");
            _startAP();
        }
    } else {
        _startAP();
    }
}

void MpcbIotCore::loop() {
    switch (_state) {
        case IotState::AP_PORTAL:
            if (_portal) _portal->loop();
            break;

        case IotState::CONFIG_SERVER:
            if (_configServer) _configServer->loop();
            _wifi.loop();
            _mqttLoop();
            break;

        case IotState::RUNNING:
            _wifi.loop();
            _mqttLoop();
            break;

        default:
            break;
    }
}

// ---------------------------------------------------------------------------

void MpcbIotCore::_setState(IotState s) {
    _state = s;
    if (_onState) _onState(s);
}

void MpcbIotCore::_startAP() {
    WiFi.mode(WIFI_AP);
    _portal = new APPortal(_apName);
    _portal->onConnect([this](const String& ssid, const String& pass) {
        _storage.saveWifi(ssid, pass);
        delete _portal;
        _portal = nullptr;
        _startConfigServer();
    });
    _portal->begin();
    _setState(IotState::AP_PORTAL);
    Serial.println("[IoT] AP portal: connect to \"" + _apName + "\" → http://192.168.4.1");
}

void MpcbIotCore::_startConfigServer() {
    WiFi.mode(WIFI_STA);
    _configServer = new ConfigServer(_storage);
    _configServer->begin();
    _setState(IotState::CONFIG_SERVER);
    _connectMqtt();
}

// ---------------------------------------------------------------------------
// MQTT (lazy init)
// ---------------------------------------------------------------------------

void MpcbIotCore::_connectMqtt() {
    MqttConfig cfg = _storage.loadMqtt();
    if (cfg.host.isEmpty()) return;

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
        mqtt->setBufferSize(512);
        mqtt->setCallback([this](char* topic, byte* payload, unsigned int len) {
            String t(topic);
            String p;
            for (unsigned int i = 0; i < len; i++) p += (char)payload[i];
            if (_onMqtt) _onMqtt(t, p);
        });
        _mqttClient = mqtt;
    }

    DeviceConfig dev = _storage.loadDevice();
    PubSubClient* mqtt = reinterpret_cast<PubSubClient*>(_mqttClient);
    mqtt->setServer(cfg.host.c_str(), cfg.port);

    Serial.print("[MQTT] Connecting to " + cfg.host + ":" + cfg.port + "...");
    bool ok;
    if (cfg.user.isEmpty()) {
        ok = mqtt->connect(dev.deviceId.c_str());
    } else {
        ok = mqtt->connect(dev.deviceId.c_str(), cfg.user.c_str(), cfg.password.c_str());
    }

    if (ok) {
        Serial.println(" OK");
        _setState(IotState::RUNNING);
    } else {
        Serial.println(" failed rc=" + String(mqtt->state()));
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
    return mqtt->publish(topic.c_str(), payload.c_str(), retain);
}

bool MpcbIotCore::subscribe(const String& topic) {
    if (!_mqttClient) return false;
    PubSubClient* mqtt = reinterpret_cast<PubSubClient*>(_mqttClient);
    if (!mqtt->connected()) return false;
    return mqtt->subscribe(topic.c_str());
}
