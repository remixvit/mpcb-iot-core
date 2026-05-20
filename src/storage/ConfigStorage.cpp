#include "ConfigStorage.h"

void ConfigStorage::begin() {
    // nothing to init, Preferences opens per-call
}

bool ConfigStorage::hasWifi() {
    _prefs.begin("wifi", true);
    bool has = _prefs.isKey("ssid");
    _prefs.end();
    return has;
}

WifiConfig ConfigStorage::loadWifi() {
    _prefs.begin("wifi", true);
    WifiConfig cfg;
    cfg.ssid     = _prefs.getString("ssid", "");
    cfg.password = _prefs.getString("pass", "");
    _prefs.end();
    return cfg;
}

void ConfigStorage::saveWifi(const String& ssid, const String& password) {
    _prefs.begin("wifi", false);
    _prefs.putString("ssid", ssid);
    _prefs.putString("pass", password);
    _prefs.end();
}

void ConfigStorage::clearWifi() {
    _prefs.begin("wifi", false);
    _prefs.clear();
    _prefs.end();
}

MqttConfig ConfigStorage::loadMqtt() {
    _prefs.begin("mqtt", true);
    MqttConfig cfg;
    cfg.host     = _prefs.getString("host", "");
    cfg.port     = _prefs.getUShort("port", 1883);
    cfg.user     = _prefs.getString("user", "");
    cfg.password = _prefs.getString("pass", "");
    cfg.tls      = _prefs.getBool("tls", false);
    _prefs.end();
    return cfg;
}

void ConfigStorage::saveMqtt(const MqttConfig& cfg) {
    _prefs.begin("mqtt", false);
    _prefs.putString("host", cfg.host);
    _prefs.putUShort("port", cfg.port);
    _prefs.putString("user", cfg.user);
    _prefs.putString("pass", cfg.password);
    _prefs.putBool("tls",   cfg.tls);
    _prefs.end();
}

DeviceConfig ConfigStorage::loadDevice() {
    _prefs.begin("device", true);
    DeviceConfig cfg;
    cfg.deviceId   = _prefs.getString("id",   "");
    cfg.deviceName = _prefs.getString("name", "ESP32 Device");
    _prefs.end();
    return cfg;
}

void ConfigStorage::saveDevice(const DeviceConfig& cfg) {
    _prefs.begin("device", false);
    _prefs.putString("id",   cfg.deviceId);
    _prefs.putString("name", cfg.deviceName);
    _prefs.end();
}

String ConfigStorage::loadPeripherals() {
    _prefs.begin("periph", false);
    String json = _prefs.getString("cfg", "[]");
    _prefs.end();
    return json;
}

void ConfigStorage::savePeripherals(const String& json) {
    _prefs.begin("periph", false);
    _prefs.putString("cfg", json);
    _prefs.end();
}

String ConfigStorage::loadRules() {
    _prefs.begin("rules", false);
    String json = _prefs.getString("cfg", "[]");
    _prefs.end();
    return json;
}

void ConfigStorage::saveRules(const String& json) {
    _prefs.begin("rules", false);
    _prefs.putString("cfg", json);
    _prefs.end();
}

float ConfigStorage::loadCalOffset(const String& key) {
    _prefs.begin("poffsets", false);
    float v = _prefs.getFloat(key.substring(0, 14).c_str(), 0.0f);
    _prefs.end();
    return v;
}

void ConfigStorage::saveCalOffset(const String& key, float offset) {
    _prefs.begin("poffsets", false);
    _prefs.putFloat(key.substring(0, 14).c_str(), offset);
    _prefs.end();
}
