# mpcb-iot Platform

IoT платформа на базе ESP32 семейства. Единая библиотека `mpcb-iot-core`,
отдельный проект на каждый тип чипа.

## Репозитории

| Репо | Чип | Транспорт | Статус |
|------|-----|-----------|--------|
| [mpcb-iot-core](https://github.com/remixvit/mpcb-iot-core) | — | библиотека | ✅ активно |
| [esp32-mpcb](https://github.com/remixvit/esp32-mpcb) (→ mpcb-c6-wifi) | C6 | WiFi+MQTT+BLE | ✅ работает |
| mpcb-c6-zigbee | C6 | Zigbee+BLE | 🔲 план |
| mpcb-c3 | C3 | WiFi+MQTT+BLE | 🔲 план |
| mpcb-h2 | H2 | Zigbee+BLE | 🔲 план |
| mpcb-app | — | Flutter iOS+Android | 🔲 в работе |

## Локальная структура (e:\Projects\)

```
mpcb-iot-core\      ← библиотека (общая для всех проектов)
esp32-mpcb\         ← рабочий C6-WiFi девайс (reference impl)
mpcb-c6-wifi\       ← будущий чистый C6-WiFi проект
mpcb-c6-zigbee\     ← C6 Zigbee+BLE
mpcb-c3\            ← C3 WiFi+BLE+deep sleep
mpcb-h2\            ← H2 Zigbee+BLE only
mpcb-app\           ← Flutter приложение
```

## Железо

Все устройства в форм-факторе **Super Mini / Super Micro**.

| Чип | GPIO | RAM | Flash | WiFi | BT | Zigbee | Цена |
|-----|------|-----|-------|------|----|--------|------|
| ESP32-C3 | 12 | 400KB | 4MB | WiFi 4 | BT5 | — | дёшево |
| ESP32-C6 | 22 | 512KB | 4MB | WiFi 6 | BT5 | ✓ | средне |
| ESP32-S3 | 45 | 512KB+PSRAM | 4MB+ | WiFi 4 | BT5 | — | дороже |
| ESP32-H2 | 20 | 320KB | 4MB | — | BT5 | ✓ | дёшево |

## mpcb-iot-core — структура библиотеки

```
src/
  MpcbIotCore.h/.cpp        ← FSM: BOOTING→CONNECTING→CONFIG_SERVER→RUNNING
  storage/ConfigStorage      ← NVS (Preferences): wifi/mqtt/device/periph/rules
  wifi/APPortal              ← AP + captive portal (fallback)
  wifi/WiFiConnect           ← STA подключение
  web/ConfigServer           ← Web UI: /, /wifi, /mqtt, /gpio, /logs, /ota
  log/RingLog                ← Кольцевой лог 60×120 байт (PROGMEM)
  peripheral/PeriphManager   ← GPIO конструктор: relay/button/analog/pwm/neopixel
  ble/BleConfig.h            ← GATT сервис 181A (status/settings/command)
  ble/BleOta.h               ← BLE OTA сервис FB1E4001
```

## BLE протокол

**Сервис конфигурации** `181A`:
- `2A6E` NOTIFY — статус JSON каждую секунду (state/ip/rssi/mqtt/uptime/heap)
- `2A6F` READ   — настройки JSON (device/wifi/mqtt/peripherals/rules)
- `2A70` WRITE  — команда JSON (обновление настроек или {"cmd":"reboot"})

**Сервис OTA** `FB1E4001-54AE-4A28-9F74-DFCCB248601D`:
- `FB1E4002` WRITE    — ctrl: `{"cmd":"start","size":N}` / `{"cmd":"end"}`
- `FB1E4003` WRITE_NR — бинарные чанки ≤509 байт
- `FB1E4004` NOTIFY   — прогресс: `{"state":"ready|progress|done|error"}`

**Идентификация устройства**: Manufacturer Data marker `'M','C'` (0xFF 0xFF 0x4D 0x43)

## Отладка BLE

**nRF Connect for Mobile** (Google Play / App Store) — стандартный инструмент.
1. Открыть приложение → Scan
2. Найти устройство `mpcb-XXXX`
3. Connect → видны сервисы 181A и FB1E4001
4. Подписаться на 2A6E → видеть статус в реальном времени
5. Читать 2A6F → текущие настройки
6. Писать в 2A70 → `{"cmd":"reboot"}` или `{"wifi_ssid":"MyNet","wifi_pass":"pass"}`

## MQTT топики

```
mpcb/devices/{deviceId}/{label}/state  ← публикует устройство
mpcb/devices/{deviceId}/{label}/set    ← управление устройством
```

## Правила автоматизации (локальные)

Хранятся в NVS, выполняются без MQTT/интернета:
```json
{"trigger":"knopka1","event":"pressed","action":"toggle","target":"rele1"}
{"trigger":"knopka1","event":"pressed","action":"pulse","target":"vorota","pulseMs":1000}
```
События: `pressed` | `released` | `any`
Действия: `on` | `off` | `toggle` | `pulse`
