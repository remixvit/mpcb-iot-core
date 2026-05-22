#pragma once
#ifdef ZIGBEE_MODE_ED

#include <Arduino.h>
#include <functional>
#include <vector>
#include <ZigbeeCore.h>
#include <ep/ZigbeeLight.h>
#include <ep/ZigbeeTempSensor.h>
#include "../ITransport.h"
#include "../ble/BleConfig.h"
#include "../ble/BleOta.h"
#include "../storage/ConfigStorage.h"
#include "../log/RingLog.h"

class MpcbZigbeeCore : public ITransport {
public:
    using MessageCb   = std::function<void(const String& topic, const String& payload)>;
    using ConnectedCb = std::function<void()>;

    void begin(const String& deviceName = "");
    void loop();

    // Callbacks — аналогично MpcbIotCore
    void onMessage(MessageCb cb)   { _onMessage = cb; }
    void onReady(ConnectedCb cb)   { _onReady   = cb; }

    // ITransport
    bool publish(const String& topic, const String& payload, bool retain = false) override;
    bool subscribe(const String& topic) override;

    ConfigStorage& storage()       { return _storage; }
    const String&  deviceId() const { return _deviceId; }

    // Вызывается статическим диспетчером (ZigbeeLight принимает только void(*)(bool))
    void _onRelayCb(uint8_t slot, bool on);

private:
    void   _createEndpoints();
    void   _dispatchCmd(const String& key, const String& payload);
    void   _handleBleCommand(const String& json);
    String _buildSettingsJson();
    String _buildStatusJson();
    void   _bleLoop();

    ConfigStorage _storage;
    BleConfig     _ble;
    BleOta        _bleOta;

    String _deviceId;
    String _bleAdvName;

    struct ZbEp {
        String    key;
        String    type;
        ZigbeeEP* ep;
    };
    std::vector<ZbEp> _eps;
    uint8_t           _nextEp = 1;

    MessageCb   _onMessage;
    ConnectedCb _onReady;
    uint32_t    _bleStatusAt = 0;
    bool        _readyFired  = false;
};

#endif  // ZIGBEE_MODE_ED
