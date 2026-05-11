# mpcb-iot-core

ESP32 IoT starter library — WiFi provisioning, MQTT, web configurator and GPIO constructor.

## What it does

1. **No WiFi saved** → starts AP `mpcb-XXXX`, captive portal for network selection
2. **WiFi connected** → web config server at `http://<device-ip>`
3. **All configured** → connects to MQTT, fires your callbacks

## Quick start

```cpp
#include <MpcbIotCore.h>

MpcbIotCore iot;

void setup() {
    Serial.begin(115200);

    iot.onStateChange([](IotState s) {
        // e.g. update LED indicator
    });

    iot.onMqttMessage([](const String& topic, const String& payload) {
        Serial.println(topic + " → " + payload);
    });

    iot.begin("My Device");
}

void loop() {
    iot.loop();
}
```

## Web interface

| URL | Description |
|-----|-------------|
| `/` | Device name, status |
| `/wifi` | Change WiFi network |
| `/mqtt` | MQTT broker settings |
| `/gpio` | GPIO peripheral constructor |

## GPIO constructor

Assign peripherals to pins via the web UI — no reflashing needed:

- Relay, Button, DHT22, DS18B20, NeoPixel, Analog input, PWM output

Config is stored in NVS and survives reboots.

## PlatformIO install

```ini
lib_deps =
    https://github.com/remixvit/mpcb-iot-core
```

## State machine

```
BOOTING → CONNECTING → CONFIG_SERVER → RUNNING
       ↘ AP_PORTAL ↗
```

- `AP_PORTAL` — no WiFi credentials, captive portal active
- `CONFIG_SERVER` — WiFi connected, web server running
- `RUNNING` — MQTT connected, ready

## Supported platforms

ESP32, ESP32-C6, ESP32-S2, ESP32-S3 (any Arduino-framework ESP32)

## License

MIT
