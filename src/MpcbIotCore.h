#pragma once
#include <Arduino.h>
#include <functional>
#include "ITransport.h"
#include "storage/ConfigStorage.h"
#include "wifi/APPortal.h"
#include "wifi/WiFiConnect.h"
#include "web/ConfigServer.h"
#include "log/RingLog.h"
#include "peripheral/PeriphManager.h"
#include "ble/BleConfig.h"
#include "ble/BleOta.h"

#define MPCB_FIRMWARE_VERSION "1.0.0"

enum class IotState {
    BOOTING,
    AP_PORTAL,
    CONNECTING,
    CONFIG_SERVER,
    RUNNING
};

class MpcbIotCore : public ITransport {
public:
    using StateCallback     = std::function<void(IotState state)>;
    using MqttCallback      = std::function<void(const String& topic, const String& payload)>;
    using ConnectedCallback = std::function<void()>;

    void begin(const String& deviceName = "");
    void loop();

    // Callbacks
    void onStateChange(StateCallback cb)    { _onState     = cb; }
    void onMqttMessage(MqttCallback cb)     { _onMqtt      = cb; }
    void onMqttConnected(ConnectedCallback cb) { _onConnected = cb; }

    // Dashboard wiring — call before begin() or after (applied on next configServer start)
    void onDashState(ConfigServer::StateProvider cb) { _dashState = cb; }
    void onDashCmd(ConfigServer::CmdHandler cb)      { _dashCmd   = cb; }

    // ITransport
    bool publish(const String& topic, const String& payload, bool retain = false) override;
    bool subscribe(const String& topic) override;

    // Accessors
    IotState     state()   const { return _state; }
    ConfigStorage& storage()     { return _storage; }
    MqttConfig   mqttConfig()    { return _storage.loadMqtt(); }

private:
    void _setState(IotState s);
    void _startAP();
    void _closeAP();
    void _startConfigServer();
    void _connectMqtt();
    void _mqttLoop();
    void _bleLoop();
    void _handleBleCommand(const String& json);
    String _buildSettingsJson();
    String _buildStatusJson();

    ConfigStorage _storage;
    WiFiConnect   _wifi;
    BleConfig     _ble;
    BleOta        _bleOta;

    APPortal*     _portal = nullptr;
    ConfigServer* _configServer = nullptr;

    // MQTT — lazy init based on config
    void*         _mqttClient = nullptr;
    void*         _wifiClient = nullptr;

    IotState      _state = IotState::BOOTING;
    String        _apName;
    String        _preScanJson;
    uint32_t      _mqttReconnectAt = 0;
    uint32_t      _bleStatusAt     = 0;
    bool          _wifiScanPending   = false;
    bool          _closeApRequested  = false;

    StateCallback          _onState;
    MqttCallback           _onMqtt;
    ConnectedCallback      _onConnected;
    ConfigServer::StateProvider _dashState;
    ConfigServer::CmdHandler    _dashCmd;
};
