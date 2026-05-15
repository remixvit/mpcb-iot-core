#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <functional>
#include "WiFiConnect.h"

class APPortal {
public:
    using ConnectCallback = std::function<void(const String& ssid, const String& password)>;
    using CloseCallback   = std::function<void()>;

    APPortal(const String& apName);

    void begin();
    void loop();
    void stop();

    void onConnect(ConnectCallback cb) { _onConnect = cb; }
    void onClose(CloseCallback cb)     { _onClose = cb; }

    bool isStaConnected() const { return _staConnected; }
    String staIP()        const { return _staIP; }

private:
    void _handleRoot();
    void _handleScan();
    void _handleConnect();
    void _handleStatus();
    void _handleCloseAP();
    void _handleNotFound();

    String _apName;
    WebServer _server{80};
    DNSServer _dns;
    WiFiConnect _sta;

    ConnectCallback _onConnect;
    CloseCallback   _onClose;

    bool   _staConnecting = false;
    bool   _staConnected  = false;
    String _staIP;
    String _cachedScan = "[]";
};
