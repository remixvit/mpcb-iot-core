#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include "../storage/ConfigStorage.h"
#include <functional>

class ConfigServer {
public:
    using SaveCallback = std::function<void()>;

    ConfigServer(ConfigStorage& storage);

    void begin();
    void loop();
    void stop();

    void onSave(SaveCallback cb) { _onSave = cb; }

private:
    void _handleRoot();
    void _handleWifi();
    void _handleMqtt();
    void _handleGpio();
    void _handleOta();
    void _handleLogs();
    void _handleSaveWifi();
    void _handleSaveMqtt();
    void _handleSaveGpio();
    void _handleSaveRules();
    void _handleSaveDevice();
    void _handleReset();
    void _handleStatus();
    void _handleOtaUpload();

    ConfigStorage& _storage;
    WebServer      _server{80};
    SaveCallback   _onSave;
};
