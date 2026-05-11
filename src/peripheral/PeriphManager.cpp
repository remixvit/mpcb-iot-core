#include "PeriphManager.h"
#include "../MpcbIotCore.h"

// ─────────────────────────────────────────────────────────────────────────────

String PeriphManager::_sanitize(const String& s) {
    String out;
    out.reserve(s.length());
    for (char c : s) {
        if (isAlphaNumeric(c)) out += (char)tolower(c);
        else if (c == ' ' || c == '-') out += '_';
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────

void PeriphManager::begin(const String& deviceId, ConfigStorage& storage, MpcbIotCore& iot) {
    _deviceId = deviceId;
    _iot      = &iot;
    _count    = 0;

    String json = storage.loadPeripherals();
    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok || !doc.is<JsonArray>()) {
        Log.log("Periph", "No peripherals configured");
        return;
    }

    JsonArray arr = doc.as<JsonArray>();
    for (JsonObject obj : arr) {
        if (_count >= MAX_PERIPHERALS) break;

        Peripheral& p = _list[_count];
        p.type  = obj["type"].as<String>();
        p.pin   = obj["pin"]  | 0;
        p.label = obj["label"].as<String>();
        if (p.label.isEmpty()) p.label = p.type + "_" + p.pin;

        p.key        = _sanitize(p.label);
        p.topicState = "mpcb/devices/" + deviceId + "/" + p.key + "/state";
        p.topicSet   = "mpcb/devices/" + deviceId + "/" + p.key + "/set";

        _initPeriph(p);
        _count++;

        Log.log("Periph", "[" + String(_count) + "] " + p.type +
                " pin=" + p.pin + " label='" + p.label + "' key=" + p.key);
    }

    Log.log("Periph", "Initialized " + String(_count) + " peripheral(s)");

    // ── Rules ────────────────────────────────────────────────────────────────
    _rulesCount = 0;
    String rulesJson = storage.loadRules();
    JsonDocument rdoc;
    if (deserializeJson(rdoc, rulesJson) == DeserializationError::Ok && rdoc.is<JsonArray>()) {
        for (JsonObject obj : rdoc.as<JsonArray>()) {
            if (_rulesCount >= MAX_RULES) break;
            Rule& r = _rules[_rulesCount];
            r.triggerKey = obj["trigger"].as<String>();
            r.event      = obj["event"].as<String>();
            r.targetKey  = obj["target"].as<String>();
            r.action     = obj["action"].as<String>();
            if (r.triggerKey.isEmpty() || r.targetKey.isEmpty()) continue;
            Log.log("Rules", r.triggerKey + " " + r.event + " → " + r.action + " " + r.targetKey);
            _rulesCount++;
        }
    }
    Log.log("Rules", "Loaded " + String(_rulesCount) + " rule(s)");
}

// ─────────────────────────────────────────────────────────────────────────────

void PeriphManager::_initPeriph(Peripheral& p) {
    if (p.type == "relay") {
        pinMode(p.pin, OUTPUT);
        digitalWrite(p.pin, LOW);
        p.boolState = false;
        _iot->subscribe(p.topicSet);
        _publishState(p);
        p.initialized = true;

    } else if (p.type == "button") {
        pinMode(p.pin, INPUT_PULLUP);
        p.boolState = !digitalRead(p.pin);
        p.prevBool  = p.boolState;
        _publishState(p);
        p.initialized = true;

    } else if (p.type == "analog") {
        // ADC — no init needed
        p.initialized = true;

    } else if (p.type == "pwm") {
        // arduino-esp32 3.x: ledcAttach(pin, freq, resolution)
        ledcAttach(p.pin, 1000, 8);  // 1kHz, 8-bit (0-255)
        ledcWrite(p.pin, 0);
        p.intState = 0;
        _iot->subscribe(p.topicSet);
        _publishState(p);
        p.initialized = true;

    } else if (p.type == "neopixel") {
        // NeoPixel init requires Adafruit_NeoPixel — user manages it externally.
        // PeriphManager subscribes to the topic; user handles the actual LED.
        _iot->subscribe(p.topicSet);
        Log.log("Periph", "neopixel: subscribe only — control your LED in onMessage()");
        p.initialized = true;

    } else if (p.type == "dht22") {
        // Stub — add adafruit/DHT sensor library to your project
        Log.log("Periph", "dht22: add 'adafruit/DHT sensor library' to lib_deps");
        p.initialized = false;

    } else if (p.type == "ds18b20") {
        // Stub — add milesburton/DallasTemperature to your project
        Log.log("Periph", "ds18b20: add 'milesburton/DallasTemperature' to lib_deps");
        p.initialized = false;

    } else {
        Log.log("Periph", "Unknown type: " + p.type);
    }
}

// ─────────────────────────────────────────────────────────────────────────────

void PeriphManager::loop() {
    for (uint8_t i = 0; i < _count; i++) {
        if (_list[i].initialized) _loopPeriph(_list[i]);
    }
}

void PeriphManager::_loopPeriph(Peripheral& p) {
    uint32_t now = millis();

    if (p.type == "button") {
        bool pressed = !digitalRead(p.pin);  // pullup: LOW = pressed
        if (pressed != p.prevBool) {
            p.boolState = pressed;
            p.prevBool  = pressed;
            _publishState(p);
            _checkRules(p.key, pressed ? "pressed" : "released");
            _checkRules(p.key, "any");
        }

    } else if (p.type == "analog") {
        if (now - p.lastReadMs >= 10000) {
            p.lastReadMs  = now;
            int raw       = analogRead(p.pin);
            p.intState    = raw;
            p.floatState  = raw * (3.3f / 4095.0f);  // 12-bit ADC → voltage
            _publishState(p);
        }
    }
    // relay / pwm / neopixel: event-driven only, nothing to poll
}

// ─────────────────────────────────────────────────────────────────────────────

bool PeriphManager::handleMessage(const String& topic, const String& payload) {
    for (uint8_t i = 0; i < _count; i++) {
        if (topic == _list[i].topicSet) {
            _applyCommand(_list[i], payload);
            return true;
        }
    }
    return false;
}

void PeriphManager::_applyCommand(Peripheral& p, const String& payload) {
    JsonDocument doc;
    if (deserializeJson(doc, payload) != DeserializationError::Ok) return;

    if (p.type == "relay") {
        if (doc["on"].is<bool>()) {
            p.boolState = doc["on"].as<bool>();
            digitalWrite(p.pin, p.boolState ? HIGH : LOW);
            _publishState(p);
        }

    } else if (p.type == "pwm") {
        if (doc["duty"].is<int>()) {
            p.intState = constrain(doc["duty"].as<int>(), 0, 255);
            ledcWrite(p.pin, (uint32_t)p.intState);
            _publishState(p);
        }

    } else if (p.type == "neopixel") {
        // Just publish ack — user handles actual LED in their onMqttMessage()
        if (doc["r"].is<int>()) {
            p.intState    = doc["r"].as<int>();
            p.floatState  = doc["g"] | 0;
            p.floatState2 = doc["b"] | 0;
            _publishState(p);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────────────────────

void PeriphManager::_checkRules(const String& triggerKey, const String& event) {
    for (uint8_t i = 0; i < _rulesCount; i++) {
        Rule& r = _rules[i];
        if (r.triggerKey != triggerKey) continue;
        if (r.event != event) continue;
        for (uint8_t j = 0; j < _count; j++) {
            if (_list[j].key == r.targetKey && _list[j].initialized) {
                _applyAction(_list[j], r.action);
                break;
            }
        }
    }
}

void PeriphManager::_applyAction(Peripheral& p, const String& action) {
    if (p.type == "relay") {
        if (action == "on")          p.boolState = true;
        else if (action == "off")    p.boolState = false;
        else if (action == "toggle") p.boolState = !p.boolState;
        else return;
        digitalWrite(p.pin, p.boolState ? HIGH : LOW);
        _publishState(p);
        Log.log("Rules", p.key + " relay " + action + " → " + (p.boolState ? "ON" : "OFF"));

    } else if (p.type == "pwm") {
        if (action == "on")          p.intState = 255;
        else if (action == "off")    p.intState = 0;
        else if (action == "toggle") p.intState = (p.intState > 0) ? 0 : 255;
        else return;
        ledcWrite(p.pin, (uint32_t)p.intState);
        _publishState(p);
        Log.log("Rules", p.key + " pwm " + action + " → " + String(p.intState));
    }
}

// ─────────────────────────────────────────────────────────────────────────────

void PeriphManager::_publishState(const Peripheral& p) {
    if (!_iot) return;
    String payload;

    if (p.type == "relay") {
        payload = "{\"on\":" + String(p.boolState ? "true" : "false") + "}";

    } else if (p.type == "button") {
        payload = "{\"pressed\":" + String(p.boolState ? "true" : "false") + "}";

    } else if (p.type == "analog") {
        char buf[64];
        snprintf(buf, sizeof(buf), "{\"value\":%ld,\"voltage\":%.2f}",
                 (long)p.intState, p.floatState);
        payload = buf;

    } else if (p.type == "pwm") {
        payload = "{\"duty\":" + String(p.intState) + "}";

    } else if (p.type == "neopixel") {
        payload = "{\"r\":" + String(p.intState) +
                  ",\"g\":" + String((int)p.floatState) +
                  ",\"b\":" + String((int)p.floatState2) + "}";

    } else if (p.type == "dht22") {
        char buf[64];
        snprintf(buf, sizeof(buf), "{\"temp\":%.1f,\"humidity\":%.1f}",
                 p.floatState, p.floatState2);
        payload = buf;

    } else if (p.type == "ds18b20") {
        char buf[32];
        snprintf(buf, sizeof(buf), "{\"temp\":%.1f}", p.floatState);
        payload = buf;

    } else {
        return;
    }

    _iot->publish(p.topicState, payload, false);
}
