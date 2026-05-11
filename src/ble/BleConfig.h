#pragma once
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <ArduinoJson.h>

// ─── BLE Configuration Service ───────────────────────────────────────────────
// Service UUID:  "181A" (Environmental Sensing)
// Characteristics:
//   status   [NOTIFY]  — JSON device state, 1/sec when client connected
//   settings [READ]    — JSON all settings
//   command  [WRITE]   — JSON command / settings update
// ─────────────────────────────────────────────────────────────────────────────

#define BLE_CFG_SVC_UUID      "181A"
#define BLE_CFG_STATUS_UUID   "2A6E"
#define BLE_CFG_SETTINGS_UUID "2A6F"
#define BLE_CFG_COMMAND_UUID  "2A70"

using BleCommandCb = std::function<void(const String& json)>;

class BleConfig {
public:
    using ExtraSetupCb = std::function<void(NimBLEServer*)>;

    BleConfig() { _cmdCb.parent = this; }

    void begin(const char* deviceName, BleCommandCb commandCb,
               ExtraSetupCb extraSetup = nullptr) {
        _commandCb = commandCb;

        NimBLEDevice::init(deviceName);
        NimBLEDevice::setPower(ESP_PWR_LVL_P3);
        NimBLEDevice::setMTU(512);

        _server = NimBLEDevice::createServer();
        _server->setCallbacks(&_serverCb);

        NimBLEService* svc = _server->createService(BLE_CFG_SVC_UUID);

        _statusChar = svc->createCharacteristic(
            BLE_CFG_STATUS_UUID, NIMBLE_PROPERTY::NOTIFY);

        _settingsChar = svc->createCharacteristic(
            BLE_CFG_SETTINGS_UUID, NIMBLE_PROPERTY::READ);

        _commandChar = svc->createCharacteristic(
            BLE_CFG_COMMAND_UUID,
            NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
        _commandChar->setCallbacks(&_cmdCb);

        svc->start();

        if (extraSetup) extraSetup(_server);

        _startAdvertising(deviceName);
        Serial.printf("[BLE] Started: '%s'\n", deviceName);
    }

    void updateStatus(const String& json) {
        if (!_server || !_server->getConnectedCount()) return;
        _statusChar->setValue(json.c_str());
        _statusChar->notify();
    }

    void updateSettings(const String& json) {
        if (_settingsChar) _settingsChar->setValue(json.c_str());
    }

    bool connected() const {
        return _server && _server->getConnectedCount() > 0;
    }

    void setName(const char* name) {
        NimBLEDevice::getAdvertising()->stop();
        NimBLEDevice::setDeviceName(name);
        _startAdvertising(name);
        Serial.printf("[BLE] Name changed: '%s'\n", name);
    }

    BleCommandCb _commandCb;

private:
    NimBLEServer*         _server       = nullptr;
    NimBLECharacteristic* _statusChar   = nullptr;
    NimBLECharacteristic* _settingsChar = nullptr;
    NimBLECharacteristic* _commandChar  = nullptr;

    void _startAdvertising(const char* name) {
        // Manufacturer marker: company=0xFFFF (test), 'M','C' = mpcb
        uint8_t mfr[4] = {0xFF, 0xFF, 'M', 'C'};
        NimBLEAdvertisementData advData;
        advData.addServiceUUID(BLE_CFG_SVC_UUID);
        advData.setManufacturerData(std::string((char*)mfr, sizeof(mfr)));

        NimBLEAdvertisementData scanResp;
        scanResp.setName(name);

        NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
        adv->setAdvertisementData(advData);
        adv->setScanResponseData(scanResp);
        adv->enableScanResponse(true);
        adv->start();
    }

    struct ServerCb : public NimBLEServerCallbacks {
        void onConnect(NimBLEServer* s, NimBLEConnInfo&) override {
            Serial.printf("[BLE] Client connected. Total: %d\n",
                          s->getConnectedCount());
            NimBLEDevice::getAdvertising()->start();
        }
        void onDisconnect(NimBLEServer* s, NimBLEConnInfo&, int) override {
            Serial.printf("[BLE] Client disconnected. Total: %d\n",
                          s->getConnectedCount());
        }
    } _serverCb;

    struct CommandCb : public NimBLECharacteristicCallbacks {
        BleConfig* parent = nullptr;
        void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
            if (!parent || !parent->_commandCb) return;
            String val = c->getValue().c_str();
            if (val.length()) parent->_commandCb(val);
        }
    } _cmdCb;
};
