#include "WiFiConnect.h"

void WiFiConnect::begin(const String& ssid, const String& password) {
    _ssid     = ssid;
    _password = password;
    WiFi.mode(WIFI_STA);
    WiFi.begin(_ssid.c_str(), _password.c_str());
    Serial.println("[WiFi] Connecting to: " + _ssid);
}

bool WiFiConnect::waitConnected(uint32_t timeoutMs) {
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > timeoutMs) {
            Serial.println("[WiFi] Timeout");
            return false;
        }
        delay(200);
    }
    Serial.println("[WiFi] Connected, IP: " + ip());
    _wasConnected = true;
    if (_onStatus) _onStatus(true);
    return true;
}

void WiFiConnect::loop() {
    bool connected = isConnected();
    if (_wasConnected && !connected) {
        Serial.println("[WiFi] Lost connection, reconnecting...");
        WiFi.reconnect();
        if (_onStatus) _onStatus(false);
        _wasConnected = false;
    } else if (!_wasConnected && connected) {
        Serial.println("[WiFi] Reconnected, IP: " + ip());
        _wasConnected = true;
        if (_onStatus) _onStatus(true);
    }
}
