#pragma once
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <ArduinoJson.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

// ─── BLE OTA Service ─────────────────────────────────────────────────────────
// Service:  BLE_OTA_SVC_UUID
// Chars:
//   OTA_CTRL   [WRITE]    — {"cmd":"start","size":N} / {"cmd":"end"/"abort"}
//   OTA_DATA   [WRITE_NR] — raw binary chunks (≤509 bytes)
//   OTA_STATUS [NOTIFY]   — {"state":"ready|progress|done|error","written":N,"total":N}
//
// Flow: start → ready → data chunks → end → done → reboot 1.5s
// ─────────────────────────────────────────────────────────────────────────────

#define BLE_OTA_SVC_UUID    "FB1E4001-54AE-4A28-9F74-DFCCB248601D"
#define BLE_OTA_CTRL_UUID   "FB1E4002-54AE-4A28-9F74-DFCCB248601D"
#define BLE_OTA_DATA_UUID   "FB1E4003-54AE-4A28-9F74-DFCCB248601D"
#define BLE_OTA_STATUS_UUID "FB1E4004-54AE-4A28-9F74-DFCCB248601D"

class BleOta {
public:
    BleOta() { _ctrlCb.parent = this; _dataCb.parent = this; }

    NimBLEService* createService(NimBLEServer* server) {
        NimBLEService* svc = server->createService(BLE_OTA_SVC_UUID);

        _ctrlChar = svc->createCharacteristic(
            BLE_OTA_CTRL_UUID, NIMBLE_PROPERTY::WRITE);
        _ctrlChar->setCallbacks(&_ctrlCb);

        _dataChar = svc->createCharacteristic(
            BLE_OTA_DATA_UUID, NIMBLE_PROPERTY::WRITE_NR);
        _dataChar->setCallbacks(&_dataCb);

        _statusChar = svc->createCharacteristic(
            BLE_OTA_STATUS_UUID, NIMBLE_PROPERTY::NOTIFY);

        return svc;
    }

    // Call from loop() — flash operations are safe here
    void loop() {
        if (_pendingStart)  { _pendingStart  = false; _doBegin();  }
        if (_pendingFinish) { _pendingFinish = false; _doFinish(); }
        if (_rebootAt && millis() >= _rebootAt) esp_restart();
    }

    bool isActive() const { return _active; }

private:
    NimBLECharacteristic* _ctrlChar   = nullptr;
    NimBLECharacteristic* _dataChar   = nullptr;
    NimBLECharacteristic* _statusChar = nullptr;

    esp_ota_handle_t       _otaHandle    = 0;
    const esp_partition_t* _otaPartition = nullptr;
    bool     _active        = false;
    bool     _pendingStart  = false;
    bool     _pendingFinish = false;
    uint32_t _total         = 0;
    uint32_t _written       = 0;
    uint32_t _rebootAt      = 0;

    void _notify(const char* state, const char* msg = nullptr) {
        if (!_statusChar) return;
        JsonDocument doc;
        doc["state"]   = state;
        doc["written"] = _written;
        doc["total"]   = _total;
        if (msg) doc["msg"] = msg;
        String json;
        serializeJson(doc, json);
        _statusChar->setValue(json.c_str());
        _statusChar->notify();
        Serial.printf("[OTA] %s  %u/%u\n", state, _written, _total);
    }

    void _onCtrl(const String& json) {
        JsonDocument doc;
        if (deserializeJson(doc, json) != DeserializationError::Ok) {
            _notify("error", "bad json"); return;
        }
        const char* cmd = doc["cmd"];
        if (!cmd) return;

        if (strcmp(cmd, "start") == 0) {
            if (_active) { esp_ota_abort(_otaHandle); _active = false; }
            _total        = doc["size"].as<uint32_t>();
            _written      = 0;
            _pendingStart = true;
        } else if (strcmp(cmd, "end") == 0) {
            _pendingFinish = true;
        } else if (strcmp(cmd, "abort") == 0) {
            _abort("aborted");
        }
    }

    void _doBegin() {
        if (_total == 0) { _notify("error", "size=0"); return; }
        _otaPartition = esp_ota_get_next_update_partition(nullptr);
        if (!_otaPartition) { _notify("error", "no ota partition"); return; }
        esp_err_t err = esp_ota_begin(_otaPartition, OTA_SIZE_UNKNOWN, &_otaHandle);
        if (err != ESP_OK) {
            char buf[48]; snprintf(buf, sizeof(buf), "ota_begin: %d", err);
            _notify("error", buf); return;
        }
        _active = true;
        _notify("ready");
    }

    void _doFinish() {
        if (!_active) { _notify("error", "not started"); return; }
        esp_err_t err = esp_ota_end(_otaHandle);
        if (err != ESP_OK) {
            char buf[48]; snprintf(buf, sizeof(buf), "ota_end: %d", err);
            _abort(buf); return;
        }
        err = esp_ota_set_boot_partition(_otaPartition);
        if (err != ESP_OK) {
            char buf[48]; snprintf(buf, sizeof(buf), "set_boot: %d", err);
            _abort(buf); return;
        }
        _active   = false;
        _rebootAt = millis() + 1500;
        _notify("done");
        Serial.println("[OTA] BLE firmware OK, rebooting...");
    }

    void _onData(const uint8_t* data, size_t len) {
        if (!_active) return;
        esp_err_t err = esp_ota_write(_otaHandle, data, len);
        if (err != ESP_OK) {
            char buf[48]; snprintf(buf, sizeof(buf), "ota_write: %d", err);
            _abort(buf); return;
        }
        _written += len;
        if ((_written % (8 * 1024)) < (uint32_t)len || _written == _total)
            _notify("progress");
    }

    void _abort(const char* reason) {
        if (_active) esp_ota_abort(_otaHandle);
        _active = false; _written = 0; _total = 0;
        _notify("error", reason);
    }

    struct CtrlCb : public NimBLECharacteristicCallbacks {
        BleOta* parent = nullptr;
        void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
            if (parent) parent->_onCtrl(c->getValue().c_str());
        }
    } _ctrlCb;

    struct DataCb : public NimBLECharacteristicCallbacks {
        BleOta* parent = nullptr;
        void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
            if (!parent) return;
            auto val = c->getValue();
            parent->_onData((const uint8_t*)val.data(), val.length());
        }
    } _dataCb;
};
