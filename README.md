# mpcb-iot-core

ESP32 IoT библиотека — WiFi provisioning, MQTT (mpcbstudio protocol), BLE config, web configurator, GPIO constructor с automation rules.

## Что делает

1. **Нет WiFi** → AP `mpcb-XXXX`, captive portal для выбора сети
2. **WiFi есть** → веб-сервер `http://<ip>`, подключение к MQTT
3. **MQTT подключён** → состояние `RUNNING`, запускает колбэки, управляет GPIO

## Быстрый старт

```cpp
#include <MpcbIotCore.h>

MpcbIotCore   iot;
PeriphManager pm;

void setup() {
    iot.onStateChange([](IotState s) {
        if (s == IotState::RUNNING) {
            String deviceId = iot.storage().loadDevice().deviceId;
            pm.begin(deviceId, iot.storage(), iot);
        }
    });

    iot.onMqttConnected([]() {
        pm.onMqttConnected();  // subscribe +/set, publish config + states
    });

    iot.onMqttMessage([](const String& topic, const String& payload) {
        pm.handleMessage(topic, payload);
    });

    iot.begin("My Device");
}

void loop() {
    iot.loop();
    pm.loop();
}
```

## Веб-интерфейс

| URL | Описание |
|-----|----------|
| `/` | Имя устройства, статус |
| `/wifi` | Смена WiFi сети |
| `/mqtt` | Настройки MQTT брокера |
| `/gpio` | GPIO конструктор + сценарии |
| `/logs` | Лог устройства (кольцевой буфер) |
| `/ota` | OTA обновление прошивки |
| `/api/log-text` | Лог в виде plain text |
| `/api/status` | JSON статус |

## GPIO конструктор

Периферия настраивается через веб-интерфейс или BLE — без перепрошивки.

| Тип | Описание | MQTT команда | MQTT состояние |
|-----|----------|--------------|----------------|
| `relay` | Цифровой выход | `{"on":true}` / `{"pulse":500}` | `{"on":bool}` |
| `button` | Кнопка INPUT_PULLUP | — | `{"pressed":bool}` |
| `analog` | АЦП вход (10 с) | — | `{"value":int,"voltage":float}` |
| `pwm` | ШИМ выход | `{"duty":0-255}` | `{"duty":int}` |
| `neopixel` | WS2812 (внешний) | `{"r":0,"g":255,"b":0}` | то же |
| `dht22` | Темп+влажность (stub) | — | `{"temp":float,"humidity":float}` |
| `ds18b20` | Температура (stub) | — | `{"temp":float}` |

Конфиг хранится в NVS, переживает перезагрузки и обновления прошивки.

## Automation Rules (сценарии)

Настраиваются через `/gpio` → Сценарии:

```
button [pressed|released|any] → relay [on|off|toggle|pulse]
```

Поддерживается `pulseMs` — автовыключение реле через N мс.

## MQTT протокол (mpcbstudio v1.0)

Полная спецификация: [FIRMWARE_SPEC.md](https://github.com/remixvit/mpcbstudio-api/blob/main/FIRMWARE_SPEC.md)

**Последовательность при каждом подключении:**
```
1. connect с LWT  mpcb/devices/{id}/announce  {"online":false}  retain QoS1
2. publish        mpcb/devices/{id}/announce  {"online":true,"ip":"..."}  retain
3. subscribe      mpcb/devices/{id}/+/set  (wildcard, одна подписка)
4. publish        mpcb/devices/{id}/config  {device_id, firmware, version, ip, peripherals[]}  retain
5. publish        mpcb/devices/{id}/{key}/state  для каждой периферии  retain
```

**key** = `sanitize(label)`: нижний регистр, пробел/дефис/подчёркивание → `_`.

## BLE конфигурация

Устройство всегда доступно по BLE (NimBLE-Arduino).  
GATT сервис `181A`, характеристики:
- `2A6E` NOTIFY — статус JSON (обновляется каждую секунду)
- `2A6F` READ — настройки JSON (wifi, mqtt, peripherals, rules)
- `2A70` WRITE — команды JSON

BLE команды:
```json
{"cmd": "reboot"}
{"cmd": "reset_wifi"}
{"cmd": "wifi_scan"}
{"wifi_ssid": "...", "wifi_pass": "..."}
{"mqtt_host": "...", "mqtt_port": 8883, "mqtt_user": "...", "mqtt_pass": "...", "mqtt_tls": true}
{"peripherals": [...]}
{"rules": [...]}
```

OTA сервис `FB1E4001` — прошивка по BLE.

## PlatformIO

```ini
lib_deps =
    https://github.com/remixvit/mpcb-iot-core
    knolleary/PubSubClient @ ^2.8
    bblanchon/ArduinoJson @ ^7.0
    h2zero/NimBLE-Arduino @ ^2.0
```

## FSM состояния

```
BOOTING → CONNECTING → CONFIG_SERVER → RUNNING
       ↘ AP_PORTAL ↗
```

- `BOOTING` — инициализация
- `AP_PORTAL` — нет WiFi, AP активен
- `CONNECTING` — идёт подключение к WiFi
- `CONFIG_SERVER` — WiFi подключён, сервер работает, MQTT соединяется
- `RUNNING` — MQTT подключён, всё работает

## Поддерживаемые платформы

ESP32, ESP32-C6, ESP32-C3, ESP32-S2, ESP32-S3 (Arduino framework)

## License

MIT
