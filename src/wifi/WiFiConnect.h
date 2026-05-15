#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <functional>

class WiFiConnect {
public:
    using StatusCallback = std::function<void(bool connected)>;

    void begin(const String& ssid, const String& password);
    void beginSTA(const String& ssid, const String& password);  // AP+STA mode — keeps AP alive
    bool waitConnected(uint32_t timeoutMs = 10000);
    void loop();

    bool isConnected() const { return WiFi.status() == WL_CONNECTED; }
    String ip() const { return WiFi.localIP().toString(); }

    void onStatus(StatusCallback cb) { _onStatus = cb; }

private:
    String _ssid, _password;
    bool _wasConnected = false;
    StatusCallback _onStatus;
};
