#pragma once
#include <Preferences.h>
#include <Arduino.h>

struct WifiConfig {
    String ssid;
    String password;
};

struct MqttConfig {
    String host;
    uint16_t port = 1883;
    String user;
    String password;
    bool tls = false;
};

struct DeviceConfig {
    String deviceId;
    String deviceName;
};

class ConfigStorage {
public:
    void begin();

    bool hasWifi();
    WifiConfig loadWifi();
    void saveWifi(const String& ssid, const String& password);
    void clearWifi();

    MqttConfig loadMqtt();
    void saveMqtt(const MqttConfig& cfg);

    DeviceConfig loadDevice();
    void saveDevice(const DeviceConfig& cfg);

    String loadPeripherals();
    void savePeripherals(const String& json);

    String loadRules();
    void saveRules(const String& json);

    // Tare offsets — stored independently so web UI saves don't erase them
    float loadCalOffset(const String& key);
    void saveCalOffset(const String& key, float offset);

private:
    Preferences _prefs;
};
