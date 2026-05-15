#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "../ITransport.h"
#include "../storage/ConfigStorage.h"
#include "../log/RingLog.h"

// ─── Peripheral types ────────────────────────────────────────────────────────
// relay    — digital output, ON/OFF via MQTT {"on": bool}
// button   — digital input pullup, publishes {"pressed": bool} on edge
// analog   — ADC input, publishes {"value": int, "voltage": float} every 10s
// pwm      — PWM output, {"duty": 0-255} via MQTT
// neopixel — WS2812 LED, {"r":0,"g":255,"b":0} via MQTT (needs Adafruit NeoPixel)
// dht22    — temp+humidity sensor (needs DHT library), publishes every 30s
// ds18b20  — temp sensor (needs DallasTemperature), publishes every 30s
// ─────────────────────────────────────────────────────────────────────────────

static constexpr uint8_t MAX_PERIPHERALS = 24;
static constexpr uint8_t MAX_RULES       = 20;

// ─── Automation rule ─────────────────────────────────────────────────────────
// trigger: sanitized key of a button/sensor peripheral
// event:   "pressed" | "released" | "any"
// action:  "on" | "off" | "toggle"
// target:  sanitized key of a relay/pwm peripheral
struct Rule {
    String   triggerKey;
    String   event;
    float    threshold = 0.0f;  // for sensor threshold events (above/below/temp_above/etc.)
    String   targetKey;
    String   action;
    uint32_t pulseMs = 500;
    bool     armed   = true;   // re-arms when condition reverses (hysteresis latch)
};

struct Peripheral {
    String  type;
    uint8_t pin      = 0;
    uint8_t i2cAddr  = 0;    // I2C address (aht10, vl53, pcf_relay, pcf_button)
    uint8_t channel  = 0;    // PCF8574 channel (0–7)
    String  label;
    String  key;          // sanitized label → MQTT path component
    String  topicSet;     // subscribe (actuators)
    String  topicState;   // publish

    // Analog calibration
    uint8_t calMode   = 0;         // 0=raw, 1=linear, 2=thermistor NTC
    float   calRawMin = 0.0f;      // linear: ADC raw min
    float   calRawMax = 4095.0f;   // linear: ADC raw max
    float   calValMin = 0.0f;      // linear: output min
    float   calValMax = 100.0f;    // linear: output max
    float   calRRef   = 10000.0f;  // thermistor: reference resistor Ω
    float   calBeta   = 3950.0f;   // thermistor: Beta coefficient
    float   calR25    = 10000.0f;  // thermistor: R at 25°C
    String  calUnit;               // unit string (e.g. "°C", "%")
    float   calOffset = 0.0f;      // tare offset — subtracted from converted

    // Runtime state
    bool     boolState   = false;
    int32_t  intState    = 0;
    float    floatState  = 0.0f;
    float    floatState2 = 0.0f;  // second sensor value (humidity)
    float    converted   = 0.0f;  // calibrated analog value
    bool     prevBool    = false; // for edge detection (button)
    uint32_t lastReadMs  = 0;
    uint32_t pulseEndMs  = 0;    // relay: pulse timer; button: debounce timer
    bool     initialized = false;
    void*    sensorObj   = nullptr;  // DHT*, DallasTemperature*
    void*    sensorObj2  = nullptr;  // OneWire* (must outlive DallasTemperature)
};

class PeriphManager {
public:
    // Call when MQTT is connected (IotState::RUNNING)
    void begin(const String& deviceId, const String& deviceName, ConfigStorage& storage, ITransport& transport);

    // Call every loop()
    void loop();

    // Forward MQTT messages from iot.onMqttMessage() here
    // Returns true if the message was handled by a peripheral
    bool handleMessage(const String& topic, const String& payload);

    // Call from iot.onMqttConnected() — re-subscribe, publish config + all states
    void onMqttConnected();

    uint8_t count() const { return _count; }

    // Returns JSON array of all peripheral states (for dashboard)
    String getStateJson() const;

    // Apply command locally (from web UI) and publish state via MQTT
    void handleLocalCmd(const String& key, const String& payload);

private:
    void _initPeriph(Peripheral& p);
    void _loopPeriph(Peripheral& p);
    void _applyCommand(Peripheral& p, const String& payload);
    void _publishState(const Peripheral& p);
    void _publishConfig();
    void _checkRules(const String& triggerKey, const String& event);
    void _checkRulesValue(const String& triggerKey, float val, float val2 = 0.0f);
    void _applyAction(Peripheral& p, const String& action, uint32_t pulseMs = 0);

    static String _sanitize(const String& s);

    Peripheral    _list[MAX_PERIPHERALS];
    uint8_t       _count      = 0;
    Rule          _rules[MAX_RULES];
    uint8_t       _rulesCount = 0;
    ITransport*   _transport  = nullptr;
    String        _deviceId;
    String        _deviceName;
    ConfigStorage* _storage   = nullptr;

    // PCF8574 shared objects — indexed by (i2cAddr - 0x20), max 8 chips
    void*         _pcfObjs[8] = {};
};
