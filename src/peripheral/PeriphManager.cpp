#include "PeriphManager.h"
#include "../MpcbIotCore.h"
#include <WiFi.h>
#include <Wire.h>

#if __has_include(<DHT.h>)
  #include <DHT.h>
  #define MPCB_HAS_DHT
#endif
#if __has_include(<DallasTemperature.h>)
  #include <OneWire.h>
  #include <DallasTemperature.h>
  #define MPCB_HAS_DS18B20
#endif
#if __has_include(<Adafruit_AHTX0.h>)
  #include <Adafruit_AHTX0.h>
  #define MPCB_HAS_AHT
#endif

// ─────────────────────────────────────────────────────────────────────────────

String PeriphManager::_sanitize(const String& s) {
    String out;
    out.reserve(s.length());
    for (char c : s) {
        if (isAlphaNumeric(c)) out += (char)tolower(c);
        else if (c == ' ' || c == '-' || c == '_') out += '_';
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
        p.type    = obj["type"].as<String>();
        p.pin     = obj["pin"]     | 0;
        p.i2cAddr = obj["i2cAddr"] | 0;
        p.label   = obj["label"].as<String>();
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

    // ── I2C bus init (once) ──────────────────────────────────────────────────
    for (uint8_t i = 0; i < _count; i++) {
        const String& t = _list[i].type;
        if (t == "aht10" || t == "vl53" || t == "pcf8574") {
            Wire.begin(22, 23);
            Log.log("Periph", "I2C init SDA=22 SCL=23");
            break;
        }
    }

    // ── Rules ────────────────────────────────────────────────────────────────
    _rulesCount = 0;
    String rulesJson = storage.loadRules();
    JsonDocument rdoc;
    if (deserializeJson(rdoc, rulesJson) == DeserializationError::Ok && rdoc.is<JsonArray>()) {
        for (JsonObject obj : rdoc.as<JsonArray>()) {
            if (_rulesCount >= MAX_RULES) break;
            Rule& r = _rules[_rulesCount];
            r.triggerKey = _sanitize(obj["trigger"].as<String>());
            r.event      = obj["event"].as<String>();
            r.threshold  = obj["threshold"] | 0.0f;
            r.targetKey  = _sanitize(obj["target"].as<String>());
            r.action     = obj["action"].as<String>();
            r.pulseMs    = obj["pulseMs"] | 500;
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
        p.initialized = true;

    } else if (p.type == "button") {
        pinMode(p.pin, INPUT_PULLUP);
        p.boolState = !digitalRead(p.pin);
        p.prevBool  = p.boolState;
        p.initialized = true;

    } else if (p.type == "analog") {
        // ADC — no init needed
        p.initialized = true;

    } else if (p.type == "pwm") {
        // arduino-esp32 3.x: ledcAttach(pin, freq, resolution)
        ledcAttach(p.pin, 1000, 8);  // 1kHz, 8-bit (0-255)
        ledcWrite(p.pin, 0);
        p.intState = 0;
        p.initialized = true;

    } else if (p.type == "neopixel") {
        Log.log("Periph", "neopixel: subscribe only — control your LED in onMessage()");
        p.initialized = true;

    } else if (p.type == "dht22") {
#ifdef MPCB_HAS_DHT
        DHT* dht = new DHT(p.pin, DHT22);
        dht->begin();
        p.sensorObj   = dht;
        p.lastReadMs  = millis() - 29000;  // first read after ~1s
        p.initialized = true;
        Log.log("Periph", "dht22 init pin=" + String(p.pin));
#else
        Log.log("Periph", "dht22: add 'adafruit/DHT sensor library' to lib_deps");
        p.initialized = false;
#endif

    } else if (p.type == "ds18b20") {
#ifdef MPCB_HAS_DS18B20
        OneWire*          ow = new OneWire(p.pin);
        DallasTemperature* dt = new DallasTemperature(ow);
        dt->begin();
        p.sensorObj   = dt;
        p.sensorObj2  = ow;
        p.lastReadMs  = millis() - 29000;  // first read after ~1s
        p.initialized = true;
        Log.log("Periph", "ds18b20 init pin=" + String(p.pin));
#else
        Log.log("Periph", "ds18b20: add 'milesburton/DallasTemperature' to lib_deps");
        p.initialized = false;
#endif

    } else if (p.type == "aht10") {
#ifdef MPCB_HAS_AHT
        uint8_t addr = p.i2cAddr ? p.i2cAddr : 0x38;
        Adafruit_AHTX0* aht = new Adafruit_AHTX0();
        if (aht->begin(&Wire, 0, addr)) {
            p.sensorObj  = aht;
            p.lastReadMs = millis() - 29000;  // first read after ~1s
            p.initialized = true;
            Log.log("Periph", "aht10 init addr=0x" + String(addr, HEX));
        } else {
            delete aht;
            Log.log("Periph", "aht10 not found at 0x" + String(addr, HEX));
        }
#else
        Log.log("Periph", "aht10: add 'adafruit/Adafruit AHTX0' to lib_deps");
        p.initialized = false;
#endif

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
        if (now - p.lastReadMs >= 3000) {
            p.lastReadMs = now;
            Log.log("Periph", p.key + " pin=" + p.pin + " raw=" + digitalRead(p.pin));
        }
        bool cur = !digitalRead(p.pin);  // pullup: LOW = pressed
        if (cur != p.prevBool) {
            p.prevBool   = cur;
            p.pulseEndMs = now + 30;  // restart 30ms debounce window on any bounce
        } else if (p.pulseEndMs && now >= p.pulseEndMs) {
            p.pulseEndMs = 0;
            if (cur != p.boolState) {
                p.boolState = cur;
                _publishState(p);
                _checkRules(p.key, cur ? "pressed" : "released");
                _checkRules(p.key, "any");
            }
        }

    } else if (p.type == "relay") {
        if (p.pulseEndMs > 0 && now >= p.pulseEndMs) {
            p.pulseEndMs = 0;
            p.boolState  = false;
            digitalWrite(p.pin, LOW);
            _publishState(p);
            Log.log("Rules", p.key + " pulse end → OFF");
        }

    } else if (p.type == "analog") {
        if (now - p.lastReadMs >= 10000) {
            p.lastReadMs  = now;
            int raw       = analogRead(p.pin);
            p.intState    = raw;
            p.floatState  = raw * (3.3f / 4095.0f);  // 12-bit ADC → voltage
            _publishState(p);
            _checkRulesValue(p.key, (float)p.intState);
        }
    } else if (p.type == "dht22") {
#ifdef MPCB_HAS_DHT
        if (now - p.lastReadMs >= 30000) {
            p.lastReadMs = now;
            float t = ((DHT*)p.sensorObj)->readTemperature();
            float h = ((DHT*)p.sensorObj)->readHumidity();
            if (!isnan(t) && !isnan(h)) {
                p.floatState  = t;
                p.floatState2 = h;
                _publishState(p);
                _checkRulesValue(p.key, p.floatState, p.floatState2);
                Log.log("Periph", p.key + " t=" + String(t, 1) + " h=" + String(h, 1));
            } else {
                Log.log("Periph", p.key + " read failed");
            }
        }
#endif

    } else if (p.type == "ds18b20") {
#ifdef MPCB_HAS_DS18B20
        if (now - p.lastReadMs >= 30000) {
            p.lastReadMs = now;
            DallasTemperature* dt = (DallasTemperature*)p.sensorObj;
            dt->requestTemperatures();
            float t = dt->getTempCByIndex(0);
            if (t != DEVICE_DISCONNECTED_C) {
                p.floatState = t;
                _publishState(p);
                _checkRulesValue(p.key, p.floatState);
                Log.log("Periph", p.key + " t=" + String(t, 1));
            } else {
                Log.log("Periph", p.key + " not found on bus");
            }
        }
#endif
    } else if (p.type == "aht10") {
#ifdef MPCB_HAS_AHT
        if (now - p.lastReadMs >= 30000) {
            p.lastReadMs = now;
            sensors_event_t hum, temp;
            ((Adafruit_AHTX0*)p.sensorObj)->getEvent(&hum, &temp);
            p.floatState  = temp.temperature;
            p.floatState2 = hum.relative_humidity;
            _publishState(p);
            _checkRulesValue(p.key, p.floatState, p.floatState2);
            Log.log("Periph", p.key + " t=" + String(temp.temperature, 1) + " h=" + String(hum.relative_humidity, 1));
        }
#endif
    }
    // pwm / neopixel: event-driven only, nothing to poll
}

// ─────────────────────────────────────────────────────────────────────────────

void PeriphManager::onMqttConnected() {
    _iot->subscribe("mpcb/devices/" + _deviceId + "/+/set");
    _publishConfig();
    for (uint8_t i = 0; i < _count; i++) {
        if (!_list[i].initialized) continue;
        // Sensors publish from loop once they have a valid reading
        const String& t = _list[i].type;
        if (t == "dht22" || t == "ds18b20" || t == "aht10" || t == "vl53") continue;
        _publishState(_list[i]);
    }
}

void PeriphManager::_publishConfig() {
    JsonDocument doc;
    doc["device_id"]   = _deviceId;
    doc["device_name"] = _iot ? _iot->storage().loadDevice().deviceName : "";
    doc["firmware"]    = "MpcbIotCore";
    doc["version"]     = MPCB_FIRMWARE_VERSION;
    doc["ip"]          = WiFi.localIP().toString();
    JsonArray arr = doc["peripherals"].to<JsonArray>();
    for (uint8_t i = 0; i < _count; i++) {
        JsonObject obj = arr.add<JsonObject>();
        obj["key"]   = _list[i].key;
        obj["type"]  = _list[i].type;
        obj["label"] = _list[i].label;
    }
    String out;
    serializeJson(doc, out);
    _iot->publish("mpcb/devices/" + _deviceId + "/config", out, true);
    Log.log("MQTT", "Config published: " + String(_count) + " peripheral(s)");
}

bool PeriphManager::handleMessage(const String& topic, const String& payload) {
    for (uint8_t i = 0; i < _count; i++) {
        if (topic == _list[i].topicSet) {
            Log.log("Periph", "cmd " + _list[i].key + " ← " + payload);
            _applyCommand(_list[i], payload);
            return true;
        }
    }
    Log.log("Periph", "unhandled: " + topic);
    return false;
}

void PeriphManager::_applyCommand(Peripheral& p, const String& payload) {
    JsonDocument doc;
    if (deserializeJson(doc, payload) != DeserializationError::Ok) return;

    if (p.type == "relay") {
        if (doc["pulse"].is<int>()) {
            _applyAction(p, "pulse", doc["pulse"].as<int>());
            return;
        }
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

void PeriphManager::_checkRulesValue(const String& triggerKey, float val, float val2) {
    for (uint8_t i = 0; i < _rulesCount; i++) {
        Rule& r = _rules[i];
        if (r.triggerKey != triggerKey) continue;
        bool fire = false;
        if      (r.event == "above"     || r.event == "temp_above") fire = (val  > r.threshold);
        else if (r.event == "below"     || r.event == "temp_below") fire = (val  < r.threshold);
        else if (r.event == "hum_above")                             fire = (val2 > r.threshold);
        else if (r.event == "hum_below")                             fire = (val2 < r.threshold);
        else continue;

        if (fire && r.armed) {
            r.armed = false;
            for (uint8_t j = 0; j < _count; j++) {
                if (_list[j].key == r.targetKey && _list[j].initialized) {
                    _applyAction(_list[j], r.action, r.pulseMs);
                    Log.log("Rules", triggerKey + " " + r.event + " " + String(r.threshold) +
                            " → " + r.action + " " + r.targetKey);
                    break;
                }
            }
        } else if (!fire) {
            r.armed = true;
        }
    }
}

void PeriphManager::_checkRules(const String& triggerKey, const String& event) {
    for (uint8_t i = 0; i < _rulesCount; i++) {
        Rule& r = _rules[i];
        if (r.triggerKey != triggerKey) continue;
        if (r.event != event) continue;
        for (uint8_t j = 0; j < _count; j++) {
            if (_list[j].key == r.targetKey && _list[j].initialized) {
                _applyAction(_list[j], r.action, r.pulseMs);
                break;
            }
        }
    }
}

void PeriphManager::_applyAction(Peripheral& p, const String& action, uint32_t pulseMs) {
    if (p.type == "relay") {
        if (action == "on")          p.boolState = true;
        else if (action == "off")    p.boolState = false;
        else if (action == "toggle") p.boolState = !p.boolState;
        else if (action == "pulse") {
            p.boolState  = true;
            p.pulseEndMs = millis() + (pulseMs > 0 ? pulseMs : 500);
            digitalWrite(p.pin, HIGH);
            _publishState(p);
            Log.log("Rules", p.key + " pulse ON for " + String(pulseMs) + "ms");
            return;
        } else return;
        p.pulseEndMs = 0;  // cancel any active pulse if explicit on/off/toggle
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

    } else if (p.type == "dht22" || p.type == "aht10") {
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

    _iot->publish(p.topicState, payload, true);  // retain=true
}
