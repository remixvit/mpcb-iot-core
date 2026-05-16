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
#if __has_include(<VL53L0X.h>)
  #include <VL53L0X.h>
  #define MPCB_HAS_VL53L0X
#endif
#if __has_include(<VL53L1X.h>)
  #include <VL53L1X.h>
  #define MPCB_HAS_VL53L1X
#endif
#if __has_include(<PCF8574.h>)
  #include <PCF8574.h>
  #define MPCB_HAS_PCF8574
#endif

// ─────────────────────────────────────────────────────────────────────────────

String PeriphManager::_sanitize(const String& s) {
    String out;
    out.reserve(s.length());
    for (char c : s) {
        if (isAlphaNumeric(c)) out += (char)tolower(c);
        // all other characters dropped — labels must be [a-z0-9] only
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────

void PeriphManager::begin(const String& deviceId, const String& deviceName, ConfigStorage& storage, ITransport& transport) {
    _deviceId   = deviceId;
    _deviceName = deviceName;
    _transport  = &transport;
    _storage    = &storage;
    _count    = 0;
    memset(_pcfObjs, 0, sizeof(_pcfObjs));

    String json = storage.loadPeripherals();
    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok || !doc.is<JsonArray>()) {
        Log.log("Periph", "No peripherals configured");
        return;
    }

    // ── I2C bus init (once, before _initPeriph) ─────────────────────────────
    JsonArray arrCheck = doc.as<JsonArray>();
    for (JsonObject obj : arrCheck) {
        String t = obj["type"].as<String>();
        if (t == "aht10" || t == "vl53" || t == "pcf_relay" || t == "pcf_button" || t == "ccs811") {
            // Bus recovery: toggle SCL 9× to release any stuck slave (e.g. VL53 mid-ranging)
            pinMode(19, OUTPUT); pinMode(18, OUTPUT);
            digitalWrite(19, HIGH);
            for (int i = 0; i < 9; i++) {
                digitalWrite(18, LOW); delayMicroseconds(5);
                digitalWrite(18, HIGH); delayMicroseconds(5);
            }
            digitalWrite(19, LOW); delayMicroseconds(5);  // STOP
            digitalWrite(19, HIGH); delayMicroseconds(5);
            Wire.end();
            delay(20);
            Wire.begin(19, 18);
            delay(50);
            Log.log("Periph", "I2C init SDA=19 SCL=18");
            break;
        }
    }

    JsonArray arr = doc.as<JsonArray>();
    for (JsonObject obj : arr) {
        if (_count >= MAX_PERIPHERALS) break;

        Peripheral& p = _list[_count];
        p.type    = obj["type"].as<String>();
        p.pin     = obj["pin"]     | 0;
        p.i2cAddr = obj["i2cAddr"] | 0;
        p.channel = obj["channel"] | 0;
        p.label   = obj["label"].as<String>();
        if (p.label.isEmpty()) p.label = p.type + String(p.pin);

        // Analog calibration fields
        p.calMode   = obj["calMode"]   | 0;
        p.calRawMin = obj["calRawMin"] | 0.0f;
        p.calRawMax = obj["calRawMax"] | 4095.0f;
        p.calValMin = obj["calValMin"] | 0.0f;
        p.calValMax = obj["calValMax"] | 100.0f;
        p.calRRef   = obj["calRRef"]   | 10000.0f;
        p.calBeta   = obj["calBeta"]   | 3950.0f;
        p.calR25    = obj["calR25"]    | 10000.0f;
        if (!obj["calUnit"].isNull()) p.calUnit = obj["calUnit"].as<String>();

        p.key        = _sanitize(p.label);
        p.topicState = "mpcb/devices/" + deviceId + "/" + p.key + "/state";
        p.topicSet   = "mpcb/devices/" + deviceId + "/" + p.key + "/set";

        if (p.type == "analog") p.calOffset = storage.loadCalOffset(p.key);

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

    } else if (p.type == "vl53") {
#if defined(MPCB_HAS_VL53L1X) || defined(MPCB_HAS_VL53L0X)
        {
            uint8_t addr = p.i2cAddr ? p.i2cAddr : 0x29;
            // Adafruit_AHTX0::begin() calls Wire.begin() without Wire.end() which on
            // ESP32-C6 misconfigures the I2C peripheral (timing/config mismatch) while
            // leaving GPIO mux intact. This silently breaks getSpadInfo() polling in
            // VL53L0X::init(). Re-init the bus here to restore correct peripheral state.
            Wire.end();
            delay(5);
            Wire.begin(19, 18);
            delay(20);

            // VL53L0X soft reset: after ESP.restart() device stays mid-measurement.
            // Reg 0xBF = SOFT_RESET_GO2_SOFT_RESET_N (safe NOP for L1X at 16-bit addr 0xBF00).
            Wire.beginTransmission(addr); Wire.write(0xBF); Wire.write(0x00); Wire.endTransmission();
            delay(5);
            Wire.beginTransmission(addr); Wire.write(0xBF); Wire.write(0x01); Wire.endTransmission();
            delay(100);

            // Auto-detect chip type by reading model ID register 0xC0:
            //   VL53L0X → 0xEE,  VL53L1X → 0xEA
            uint8_t modelId = 0x00;
            for (uint8_t r = 0; r < 5 && modelId == 0x00; r++) {
                if (r > 0) delay(20);
                Wire.beginTransmission(addr); Wire.write(0xC0); Wire.endTransmission(false);
                Wire.requestFrom(addr, (uint8_t)1);
                modelId = Wire.available() ? Wire.read() : 0x00;
            }
            Log.log("Periph", "vl53 detect: modelId=0x" + String(modelId, HEX));

#if defined(MPCB_HAS_VL53L1X)
            if (modelId == 0xEA) {
                // VL53L1X — soft reset via register 0x0000
                Wire.beginTransmission(addr); Wire.write(0x00); Wire.write(0x00); Wire.write(0x00); Wire.endTransmission();
                delay(2);
                Wire.beginTransmission(addr); Wire.write(0x00); Wire.write(0x00); Wire.write(0x01); Wire.endTransmission();
                delay(5);
                VL53L1X* l1x = new VL53L1X();
                l1x->setBus(&Wire);
                l1x->setTimeout(500);
                bool l1xOk = false;
                for (uint8_t r = 0; r < 10 && !l1xOk; r++) { delay(200); l1xOk = l1x->init(); }
                if (l1xOk) {
                    l1x->setDistanceMode(VL53L1X::Long);
                    l1x->setMeasurementTimingBudget(50000);
                    l1x->startContinuous(100);
                    p.sensorObj   = l1x;
                    p.floatState2 = 1.0f;
                    p.lastReadMs  = millis();
                    p.initialized = true;
                    Log.log("Periph", "vl53 L1X init OK");
                } else {
                    delete l1x;
                    Log.log("Periph", "vl53 L1X init failed");
                }
            }
#endif
#if defined(MPCB_HAS_VL53L0X)
            if (modelId == 0xEE) {
                VL53L0X* l0x = new VL53L0X();
                l0x->setBus(&Wire);
                l0x->setTimeout(500);
                bool l0xOk = l0x->init();
                if (l0xOk) {
                    l0x->setMeasurementTimingBudget(50000);
                    Log.log("Periph", "vl53 L0X init OK");
                } else {
                    // getSpadInfo() hangs after soft reset: 0xBF only resets Go2 CPU,
                    // NOT the SPAD/NVM analog hardware (stays stuck mid-measurement).
                    // DataInit DID run before getSpadInfo failed, so stop_variable is set.
                    // Complete StaticInit + RefCalibration via public writeReg/readReg API.
                    Log.log("Periph", "vl53 L0X warmStart");

                    // getSpadInfo() timed out at the poll loop (line 895 in VL53L0X.cpp)
                    // without executing its cleanup (lines 905-912). Restore register state:
                    // on page 0xFF=0x07: clear 0x81; switch to 0x06: clear bit2 of 0x83;
                    // switch to 0x01: set 0x00=0x01; back to page 0x00; clear 0x80.
                    l0x->writeReg(0x81, 0x00);
                    l0x->writeReg(0xFF, 0x06);
                    l0x->writeReg(0x83, l0x->readReg(0x83) & ~0x04);
                    l0x->writeReg(0xFF, 0x01);
                    l0x->writeReg(0x00, 0x01);
                    l0x->writeReg(0xFF, 0x00);
                    l0x->writeReg(0x80, 0x00);

                    // setReferenceSPADs (hardcode 12 non-aperture = typical VL53L0X)
                    uint8_t ref_spad_map[6];
                    l0x->readMulti(VL53L0X::GLOBAL_CONFIG_SPAD_ENABLES_REF_0, ref_spad_map, 6);
                    l0x->writeReg(0xFF, 0x01);
                    l0x->writeReg(VL53L0X::DYNAMIC_SPAD_REF_EN_START_OFFSET, 0x00);
                    l0x->writeReg(VL53L0X::DYNAMIC_SPAD_NUM_REQUESTED_REF_SPAD, 0x2C);
                    l0x->writeReg(0xFF, 0x00);
                    l0x->writeReg(VL53L0X::GLOBAL_CONFIG_REF_EN_START_SELECT, 0xB4);
                    uint8_t spads_enabled = 0;
                    for (uint8_t i = 0; i < 48; i++) {
                        if (spads_enabled == 12) { ref_spad_map[i/8] &= ~(1 << (i%8)); }
                        else if ((ref_spad_map[i/8] >> (i%8)) & 0x1) { spads_enabled++; }
                    }
                    l0x->writeMulti(VL53L0X::GLOBAL_CONFIG_SPAD_ENABLES_REF_0, ref_spad_map, 6);

                    // load tuning settings (DefaultTuningSettings from vl53l0x_tuning.h)
                    l0x->writeReg(0xFF,0x01); l0x->writeReg(0x00,0x00);
                    l0x->writeReg(0xFF,0x00); l0x->writeReg(0x09,0x00); l0x->writeReg(0x10,0x00); l0x->writeReg(0x11,0x00);
                    l0x->writeReg(0x24,0x01); l0x->writeReg(0x25,0xFF); l0x->writeReg(0x75,0x00);
                    l0x->writeReg(0xFF,0x01); l0x->writeReg(0x4E,0x2C); l0x->writeReg(0x48,0x00); l0x->writeReg(0x30,0x20);
                    l0x->writeReg(0xFF,0x00); l0x->writeReg(0x30,0x09); l0x->writeReg(0x54,0x00); l0x->writeReg(0x31,0x04);
                    l0x->writeReg(0x32,0x03); l0x->writeReg(0x40,0x83); l0x->writeReg(0x46,0x25); l0x->writeReg(0x60,0x00);
                    l0x->writeReg(0x27,0x00); l0x->writeReg(0x50,0x06); l0x->writeReg(0x51,0x00); l0x->writeReg(0x52,0x96);
                    l0x->writeReg(0x56,0x08); l0x->writeReg(0x57,0x30); l0x->writeReg(0x61,0x00); l0x->writeReg(0x62,0x00);
                    l0x->writeReg(0x64,0x00); l0x->writeReg(0x65,0x00); l0x->writeReg(0x66,0xA0);
                    l0x->writeReg(0xFF,0x01); l0x->writeReg(0x22,0x32); l0x->writeReg(0x47,0x14); l0x->writeReg(0x49,0xFF); l0x->writeReg(0x4A,0x00);
                    l0x->writeReg(0xFF,0x00); l0x->writeReg(0x7A,0x0A); l0x->writeReg(0x7B,0x00); l0x->writeReg(0x78,0x21);
                    l0x->writeReg(0xFF,0x01); l0x->writeReg(0x23,0x34); l0x->writeReg(0x42,0x00); l0x->writeReg(0x44,0xFF);
                    l0x->writeReg(0x45,0x26); l0x->writeReg(0x46,0x05); l0x->writeReg(0x40,0x40); l0x->writeReg(0x0E,0x06);
                    l0x->writeReg(0x20,0x1A); l0x->writeReg(0x43,0x40);
                    l0x->writeReg(0xFF,0x00); l0x->writeReg(0x34,0x03); l0x->writeReg(0x35,0x44);
                    l0x->writeReg(0xFF,0x01); l0x->writeReg(0x31,0x04); l0x->writeReg(0x4B,0x09); l0x->writeReg(0x4C,0x05); l0x->writeReg(0x4D,0x04);
                    l0x->writeReg(0xFF,0x00); l0x->writeReg(0x44,0x00); l0x->writeReg(0x45,0x20); l0x->writeReg(0x47,0x08);
                    l0x->writeReg(0x48,0x28); l0x->writeReg(0x67,0x00); l0x->writeReg(0x70,0x04); l0x->writeReg(0x71,0x01);
                    l0x->writeReg(0x72,0xFE); l0x->writeReg(0x76,0x00); l0x->writeReg(0x77,0x00);
                    l0x->writeReg(0xFF,0x01); l0x->writeReg(0x0D,0x01);
                    l0x->writeReg(0xFF,0x00); l0x->writeReg(0x80,0x01); l0x->writeReg(0x01,0xF8);
                    l0x->writeReg(0xFF,0x01); l0x->writeReg(0x8E,0x01); l0x->writeReg(0x00,0x01);
                    l0x->writeReg(0xFF,0x00); l0x->writeReg(0x80,0x00);

                    // GPIO config: interrupt on new sample ready, active low
                    l0x->writeReg(0x0A, 0x04);
                    l0x->writeReg(0x84, l0x->readReg(0x84) & ~0x10);
                    l0x->writeReg(0x0B, 0x01);

                    // disable MSRC/TCC, set timing budget
                    l0x->writeReg(VL53L0X::SYSTEM_SEQUENCE_CONFIG, 0xE8);
                    l0x->setMeasurementTimingBudget(50000);

                    // VHV calibration (performSingleRefCalibration(0x40))
                    l0x->writeReg(VL53L0X::SYSTEM_SEQUENCE_CONFIG, 0x01);
                    l0x->writeReg(VL53L0X::SYSRANGE_START, 0x41);
                    { uint32_t t = millis(); while ((l0x->readReg(VL53L0X::RESULT_INTERRUPT_STATUS) & 0x07) == 0 && millis()-t < 500); }
                    l0x->writeReg(VL53L0X::SYSTEM_INTERRUPT_CLEAR, 0x01);
                    l0x->writeReg(VL53L0X::SYSRANGE_START, 0x00);

                    // phase calibration (performSingleRefCalibration(0x00))
                    l0x->writeReg(VL53L0X::SYSTEM_SEQUENCE_CONFIG, 0x02);
                    l0x->writeReg(VL53L0X::SYSRANGE_START, 0x01);
                    { uint32_t t = millis(); while ((l0x->readReg(VL53L0X::RESULT_INTERRUPT_STATUS) & 0x07) == 0 && millis()-t < 500); }
                    l0x->writeReg(VL53L0X::SYSTEM_INTERRUPT_CLEAR, 0x01);
                    l0x->writeReg(VL53L0X::SYSRANGE_START, 0x00);

                    l0x->writeReg(VL53L0X::SYSTEM_SEQUENCE_CONFIG, 0xE8);
                    l0xOk = true;
                    Log.log("Periph", "vl53 L0X warmStart OK");
                }
                if (l0xOk) {
                    l0x->startContinuous(100);
                    p.sensorObj   = l0x;
                    p.floatState2 = 0.0f;
                    p.lastReadMs  = millis();
                    p.initialized = true;
                } else {
                    delete l0x;
                    Log.log("Periph", "vl53 L0X init failed");
                }
            }
#endif
            if (!p.initialized)
                Log.log("Periph", "vl53 not found (modelId=0x" + String(modelId, HEX) + ")");
        }
#else
        Log.log("Periph", "vl53: add Adafruit_VL53L0X or VL53L1X to lib_deps");
        p.initialized = false;
#endif

    } else if (p.type == "pcf_relay" || p.type == "pcf_button") {
#ifdef MPCB_HAS_PCF8574
        if (p.i2cAddr < 0x20 || p.i2cAddr > 0x27) {
            Log.log("Periph", p.type + ": bad addr 0x" + String(p.i2cAddr, HEX));
            return;
        }
        uint8_t idx = p.i2cAddr - 0x20;
        if (!_pcfObjs[idx]) {
            // Create shared PCF8574 object for this address
            PCF8574* pcf = new PCF8574(p.i2cAddr, &Wire);
            if (pcf->begin()) {
                _pcfObjs[idx] = pcf;
                Log.log("Periph", "PCF8574 init addr=0x" + String(p.i2cAddr, HEX));
            } else {
                delete pcf;
                Log.log("Periph", "PCF8574 not found at 0x" + String(p.i2cAddr, HEX));
                return;
            }
        }
        PCF8574* pcf = (PCF8574*)_pcfObjs[idx];
        if (p.type == "pcf_relay") {
            // HIGH = relay OFF (active-low relay modules use LOW=active)
            pcf->write(p.channel, HIGH);
            p.boolState = false;
        } else {
            // pcf_button: read initial state (LOW=pressed with pullup)
            p.boolState = !pcf->read(p.channel);
            p.prevBool  = p.boolState;
        }
        p.initialized = true;
        Log.log("Periph", p.type + " addr=0x" + String(p.i2cAddr, HEX) + " ch=" + p.channel);
#else
        Log.log("Periph", "PCF8574: add 'robtillaart/PCF8574' to lib_deps");
#endif

    } else if (p.type == "ccs811") {
        // Native I2C driver — no library, avoids Wire.begin() issues
        uint8_t addr = p.i2cAddr ? p.i2cAddr : 0x5A;
        Wire.beginTransmission(addr); Wire.write(0x20); Wire.endTransmission(false);
        Wire.requestFrom(addr, (uint8_t)1);
        uint8_t hwId = Wire.available() ? Wire.read() : 0;
        if (hwId != 0x81) {
            Log.log("Periph", "ccs811 not found (hwId=0x" + String(hwId, HEX) + ", expected 0x81)");
        } else {
            Wire.beginTransmission(addr); Wire.write(0xF4); Wire.endTransmission(); // APP_START
            delay(2);
            Wire.beginTransmission(addr); Wire.write(0x01); Wire.write(0x10); Wire.endTransmission(); // MEAS_MODE drive=1 (1s)
            delay(50);
            p.lastReadMs = millis();
            p.initialized = true;
            Log.log("Periph", "ccs811 init OK at 0x" + String(addr, HEX));
        }

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

    } else if (p.type == "pcf_relay") {
#ifdef MPCB_HAS_PCF8574
        if (p.pulseEndMs > 0 && now >= p.pulseEndMs) {
            p.pulseEndMs = 0;
            p.boolState  = false;
            uint8_t idx  = p.i2cAddr - 0x20;
            if (idx < 8 && _pcfObjs[idx])
                ((PCF8574*)_pcfObjs[idx])->write(p.channel, HIGH); // active-low: HIGH=OFF
            _publishState(p);
            Log.log("Rules", p.key + " pcf_relay pulse end → OFF");
        }
#endif

    } else if (p.type == "pcf_button") {
#ifdef MPCB_HAS_PCF8574
        uint8_t idx = p.i2cAddr - 0x20;
        if (idx >= 8 || !_pcfObjs[idx]) return;
        bool cur = !((PCF8574*)_pcfObjs[idx])->read(p.channel); // LOW=pressed
        if (cur != p.prevBool) {
            p.prevBool   = cur;
            p.pulseEndMs = now + 30;
        } else if (p.pulseEndMs && now >= p.pulseEndMs) {
            p.pulseEndMs = 0;
            if (cur != p.boolState) {
                p.boolState = cur;
                _publishState(p);
                _checkRules(p.key, cur ? "pressed" : "released");
                _checkRules(p.key, "any");
            }
        }
#endif

    } else if (p.type == "analog") {
        if (now - p.lastReadMs >= 10000) {
            p.lastReadMs = now;
            int raw      = analogRead(p.pin);
            p.intState   = raw;
            p.floatState = raw * (3.3f / 4095.0f);  // 12-bit ADC → voltage

            // Apply calibration
            if (p.calMode == 1) {  // linear interpolation
                float span  = p.calRawMax - p.calRawMin;
                p.converted = (span != 0.0f)
                    ? p.calValMin + (raw - p.calRawMin) / span * (p.calValMax - p.calValMin)
                    : 0.0f;
            } else if (p.calMode == 2) {  // thermistor NTC Beta equation
                if (raw > 0 && raw < 4095) {
                    float R = p.calRRef * (float)raw / (4095.0f - (float)raw);
                    p.converted = 1.0f / (1.0f / (p.calR25 + 273.15f) + log(R / p.calR25) / p.calBeta) - 273.15f;
                }
            } else {
                p.converted = (float)raw;
            }
            p.converted -= p.calOffset;

            _publishState(p);
            float ruleVal = (p.calMode != 0) ? p.converted : (float)p.intState;
            _checkRulesValue(p.key, ruleVal);
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
    } else if (p.type == "vl53") {
        if (now - p.lastReadMs >= 500) {
            p.lastReadMs = now;
            uint16_t mm = 0; bool ok = false;
#if defined(MPCB_HAS_VL53L1X)
            if (p.floatState2 >= 1.0f) {
                VL53L1X* s = (VL53L1X*)p.sensorObj;
                if (s->dataReady()) {
                    mm = s->read(false);
                    ok = (s->ranging_data.range_status == VL53L1X::RangeValid);
                }
            }
#endif
#if defined(MPCB_HAS_VL53L0X)
            if (p.floatState2 < 1.0f) {
                VL53L0X* s = (VL53L0X*)p.sensorObj;
                mm = s->readRangeContinuousMillimeters();
                ok = !s->timeoutOccurred() && (mm < 8190);
            }
#endif
            if (ok) {
                p.intState = mm;
                _publishState(p);
                _checkRulesValue(p.key, (float)mm);
                Log.log("Periph", p.key + " dist=" + String(mm) + "mm");
            }
        }
    } else if (p.type == "ccs811") {
        if (now - p.lastReadMs >= 10000) {
            p.lastReadMs = now;
            uint8_t addr = p.i2cAddr ? p.i2cAddr : 0x5A;
            Wire.beginTransmission(addr); Wire.write(0x00); Wire.endTransmission(false);
            Wire.requestFrom(addr, (uint8_t)1);
            uint8_t status = Wire.available() ? Wire.read() : 0;
            if (status & 0x08) { // DATA_RDY
                Wire.beginTransmission(addr); Wire.write(0x02); Wire.endTransmission(false);
                Wire.requestFrom(addr, (uint8_t)4);
                if (Wire.available() >= 4) {
                    uint16_t eco2 = ((uint16_t)Wire.read() << 8) | Wire.read();
                    uint16_t tvoc = ((uint16_t)Wire.read() << 8) | Wire.read();
                    p.intState   = eco2;
                    p.floatState = (float)tvoc;
                    _publishState(p);
                    _checkRulesValue(p.key, (float)eco2, (float)tvoc);
                    Log.log("Periph", p.key + " eco2=" + String(eco2) + "ppm tvoc=" + String(tvoc) + "ppb");
                }
            }
        }
    }
    // pwm / neopixel: event-driven only, nothing to poll
}

// ─────────────────────────────────────────────────────────────────────────────

void PeriphManager::onMqttConnected() {
    _transport->subscribe("mpcb/devices/" + _deviceId + "/+/set");
    _publishConfig();
    for (uint8_t i = 0; i < _count; i++) {
        if (!_list[i].initialized) continue;
        // Sensors publish from loop once they have a valid reading
        const String& t = _list[i].type;
        if (t == "dht22" || t == "ds18b20" || t == "aht10" || t == "vl53" || t == "ccs811") continue;
        _publishState(_list[i]);
    }
}

void PeriphManager::_publishConfig() {
    JsonDocument doc;
    doc["device_id"]   = _deviceId;
    doc["device_name"] = _deviceName;
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
    _transport->publish("mpcb/devices/" + _deviceId + "/config", out, true);
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

    } else if (p.type == "pcf_relay") {
#ifdef MPCB_HAS_PCF8574
        if (doc["pulse"].is<int>()) {
            _applyAction(p, "pulse", doc["pulse"].as<int>());
            return;
        }
        if (doc["on"].is<bool>()) {
            uint8_t idx = p.i2cAddr - 0x20;
            if (idx < 8 && _pcfObjs[idx]) {
                p.boolState = doc["on"].as<bool>();
                ((PCF8574*)_pcfObjs[idx])->write(p.channel, p.boolState ? LOW : HIGH);
                _publishState(p);
            }
        }
#endif

    } else if (p.type == "pwm") {
        if (doc["duty"].is<int>()) {
            p.intState = constrain(doc["duty"].as<int>(), 0, 255);
            ledcWrite(p.pin, (uint32_t)p.intState);
            _publishState(p);
        }

    } else if (p.type == "analog") {
        if (doc["tare"].is<bool>() && doc["tare"].as<bool>()) {
            // raw_converted = current reading before offset; new zero = capture it
            p.calOffset += p.converted;  // raw_conv = converted + old_offset
            p.converted  = 0.0f;
            if (_storage) _storage->saveCalOffset(p.key, p.calOffset);
            _publishState(p);
            Log.log("Periph", p.key + " tare → offset=" + String(p.calOffset, 3));
        } else if (doc["tare_reset"].is<bool>() && doc["tare_reset"].as<bool>()) {
            p.calOffset = 0.0f;
            if (_storage) _storage->saveCalOffset(p.key, 0.0f);
            Log.log("Periph", p.key + " tare reset");
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
                Log.log("Rules", triggerKey + " " + r.event + " → " + r.action + " " + r.targetKey);
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

    } else if (p.type == "pcf_relay") {
#ifdef MPCB_HAS_PCF8574
        uint8_t idx = p.i2cAddr - 0x20;
        if (idx >= 8 || !_pcfObjs[idx]) return;
        PCF8574* pcf = (PCF8574*)_pcfObjs[idx];
        if (action == "on")          p.boolState = true;
        else if (action == "off")    p.boolState = false;
        else if (action == "toggle") p.boolState = !p.boolState;
        else if (action == "pulse") {
            p.boolState  = true;
            p.pulseEndMs = millis() + (pulseMs > 0 ? pulseMs : 500);
            pcf->write(p.channel, LOW); // active-low: LOW=ON
            _publishState(p);
            Log.log("Rules", p.key + " pcf_relay pulse ON " + String(pulseMs) + "ms");
            return;
        } else return;
        p.pulseEndMs = 0;
        pcf->write(p.channel, p.boolState ? LOW : HIGH);
        _publishState(p);
        Log.log("Rules", p.key + " pcf_relay " + action);
#endif

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
    if (!_transport) return;
    String payload;

    if (p.type == "relay" || p.type == "pcf_relay") {
        payload = "{\"on\":" + String(p.boolState ? "true" : "false") + "}";

    } else if (p.type == "button" || p.type == "pcf_button") {
        payload = "{\"pressed\":" + String(p.boolState ? "true" : "false") + "}";

    } else if (p.type == "analog") {
        char buf[96];
        if (p.calMode != 0 && !p.calUnit.isEmpty()) {
            snprintf(buf, sizeof(buf), "{\"value\":%ld,\"voltage\":%.2f,\"converted\":%.2f,\"unit\":\"%s\"}",
                     (long)p.intState, p.floatState, p.converted, p.calUnit.c_str());
        } else if (p.calMode != 0) {
            snprintf(buf, sizeof(buf), "{\"value\":%ld,\"voltage\":%.2f,\"converted\":%.2f}",
                     (long)p.intState, p.floatState, p.converted);
        } else {
            snprintf(buf, sizeof(buf), "{\"value\":%ld,\"voltage\":%.2f}",
                     (long)p.intState, p.floatState);
        }
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

    } else if (p.type == "vl53") {
        payload = "{\"distance\":" + String(p.intState) + "}";

    } else if (p.type == "ccs811") {
        payload = "{\"eco2\":" + String(p.intState) + ",\"tvoc\":" + String((int)p.floatState) + "}";

    } else {
        return;
    }

    _transport->publish(p.topicState, payload, true);  // retain=true
}

// ─── Dashboard state JSON ─────────────────────────────────────────────────────

String PeriphManager::getStateJson() const {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (uint8_t i = 0; i < _count; i++) {
        const Peripheral& p = _list[i];
        JsonObject obj = arr.add<JsonObject>();
        obj["key"]   = p.key;
        obj["label"] = p.label;
        obj["type"]  = p.type;
        obj["ok"]    = p.initialized;
        if (p.type == "relay" || p.type == "pcf_relay") {
            obj["on"] = p.boolState;
        } else if (p.type == "button" || p.type == "pcf_button") {
            obj["pressed"] = p.boolState;
        } else if (p.type == "pwm") {
            obj["duty"] = p.intState;
        } else if (p.type == "analog") {
            obj["value"]   = p.intState;
            obj["voltage"] = p.floatState;
            if (p.calMode != 0) { obj["converted"] = p.converted; obj["unit"] = p.calUnit; }
        } else if (p.type == "dht22" || p.type == "aht10") {
            obj["temp"]     = p.floatState;
            obj["humidity"] = p.floatState2;
        } else if (p.type == "ds18b20") {
            obj["temp"] = p.floatState;
        } else if (p.type == "vl53") {
            obj["distance"] = p.intState;
        } else if (p.type == "ccs811") {
            obj["eco2"] = p.intState;
            obj["tvoc"] = (int)p.floatState;
        }
    }
    String result;
    serializeJson(doc, result);
    return result;
}

void PeriphManager::handleLocalCmd(const String& key, const String& payload) {
    for (uint8_t i = 0; i < _count; i++) {
        if (_list[i].key == key) {
            _applyCommand(_list[i], payload);
            _publishState(_list[i]);
            return;
        }
    }
}
