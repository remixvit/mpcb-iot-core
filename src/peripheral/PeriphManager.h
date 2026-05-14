#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "../storage/ConfigStorage.h"
#include "../log/RingLog.h"

// Forward declaration
class MpcbIotCore;

// ─── Peripheral types ────────────────────────────────────────────────────────
// relay    — digital output, ON/OFF via MQTT {"on": bool}
// button   — digital input pullup, publishes {"pressed": bool} on edge
// analog   — ADC input, publishes {"value": int, "voltage": float} every 10s
// pwm      — PWM output, {"duty": 0-255} via MQTT
// neopixel — WS2812 LED, {"r":0,"g":255,"b":0} via MQTT (needs Adafruit NeoPixel)
// dht22    — temp+humidity sensor (needs DHT library), publishes every 30s
// ds18b20  — temp sensor (needs DallasTemperature), publishes every 30s
// ─────────────────────────────────────────────────────────────────────────────

static constexpr uint8_t MAX_PERIPHERALS = 12;
static constexpr uint8_t MAX_RULES       = 20;

// ─── Automation rule ─────────────────────────────────────────────────────────
// trigger: sanitized key of a button/sensor peripheral
// event:   "pressed" | "released" | "any"
// action:  "on" | "off" | "toggle"
// target:  sanitized key of a relay/pwm peripheral
struct Rule {
    String   triggerKey;
    String   event;
    String   targetKey;
    String   action;
    uint32_t pulseMs = 500;  // used when action == "pulse"
};

struct Peripheral {
    String  type;
    uint8_t pin      = 0;
    String  label;
    String  key;          // sanitized label → MQTT path component
    String  topicSet;     // subscribe (actuators)
    String  topicState;   // publish

    // Runtime state
    bool     boolState   = false;
    int32_t  intState    = 0;
    float    floatState  = 0.0f;
    float    floatState2 = 0.0f;  // second sensor value (humidity)
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
    void begin(const String& deviceId, ConfigStorage& storage, MpcbIotCore& iot);

    // Call every loop()
    void loop();

    // Forward MQTT messages from iot.onMqttMessage() here
    // Returns true if the message was handled by a peripheral
    bool handleMessage(const String& topic, const String& payload);

    // Call from iot.onMqttConnected() — re-subscribe, publish config + all states
    void onMqttConnected();

    uint8_t count() const { return _count; }

private:
    void _initPeriph(Peripheral& p);
    void _loopPeriph(Peripheral& p);
    void _applyCommand(Peripheral& p, const String& payload);
    void _publishState(const Peripheral& p);
    void _publishConfig();
    void _checkRules(const String& triggerKey, const String& event);
    void _applyAction(Peripheral& p, const String& action, uint32_t pulseMs = 0);

    static String _sanitize(const String& s);

    Peripheral   _list[MAX_PERIPHERALS];
    uint8_t      _count      = 0;
    Rule         _rules[MAX_RULES];
    uint8_t      _rulesCount = 0;
    MpcbIotCore* _iot        = nullptr;
    String       _deviceId;
};
