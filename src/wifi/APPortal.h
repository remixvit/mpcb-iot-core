#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <functional>

class APPortal {
public:
    using ConnectCallback = std::function<void(const String& ssid, const String& password)>;

    APPortal(const String& apName);

    void begin();
    void loop();
    void stop();

    void onConnect(ConnectCallback cb) { _onConnect = cb; }

private:
    void _handleRoot();
    void _handleScan();
    void _handleConnect();
    void _handleNotFound();

    String _apName;
    WebServer _server{80};
    DNSServer _dns;
    ConnectCallback _onConnect;
};
