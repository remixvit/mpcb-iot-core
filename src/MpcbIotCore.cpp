#include "MpcbIotCore.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

// ---------------------------------------------------------------------------

void MpcbIotCore::begin(const String& deviceName) {
    _storage.begin();

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
        Log.log("IoT", "WiFi saved: " + ssid + ". Rebooting...");
        delay(300);
        ESP.restart();
    });
    _portal->begin();
    _setState(IotState::AP_PORTAL);
    Log.log("IoT", "AP portal: connect to \"" + _apName + "\" → http://192.168.4.1");
}

void MpcbIotCore::_startConfigServer() {
    WiFi.mode(WIFI_STA);
    Log.log("IoT", "WiFi connected, IP: " + WiFi.localIP().toString());
    _configServer = new ConfigServer(_storage);
    _configServer->begin();
    Log.log("IoT", "Config server: http://" + WiFi.localIP().toString());
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
        mqtt->setBufferSize(512);
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

    Log.log("MQTT", "Connecting to " + cfg.host + ":" + String(cfg.port) + "...");
    bool ok = cfg.user.isEmpty()
        ? mqtt->connect(dev.deviceId.c_str())
        : mqtt->connect(dev.deviceId.c_str(), cfg.user.c_str(), cfg.password.c_str());

    if (ok) {
        Log.log("MQTT", "Connected OK");
        _setState(IotState::RUNNING);
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

bool MpcbIotCore::subscribe(const String& topic) {
    if (!_mqttClient) return false;
    PubSubClient* mqtt = reinterpret_cast<PubSubClient*>(_mqttClient);
    if (!mqtt->connected()) return false;
    bool ok = mqtt->subscribe(topic.c_str());
    if (ok) Log.log("MQTT", "subscribed: " + topic);
    return ok;
}
