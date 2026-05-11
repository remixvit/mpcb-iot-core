#include <MpcbIotCore.h>
#include <Adafruit_NeoPixel.h>

#define RGB_PIN 8
Adafruit_NeoPixel led(1, RGB_PIN, NEO_GRB + NEO_KHZ800);
MpcbIotCore iot;

void setup() {
    Serial.begin(115200);

    led.begin();
    led.setBrightness(40);
    led.setPixelColor(0, led.Color(0, 0, 30));  // синий — загрузка
    led.show();

    // Индикация состояний
    iot.onStateChange([](IotState s) {
        switch (s) {
            case IotState::AP_PORTAL:
                led.setPixelColor(0, led.Color(30, 15, 0)); // жёлтый — AP
                break;
            case IotState::CONNECTING:
                led.setPixelColor(0, led.Color(0, 0, 30));  // синий — подключение
                break;
            case IotState::CONFIG_SERVER:
                led.setPixelColor(0, led.Color(0, 15, 30)); // голубой — настройка
                break;
            case IotState::RUNNING:
                led.setPixelColor(0, led.Color(0, 0, 0));   // выкл — готов
                break;
            default: break;
        }
        led.show();
    });

    // Команды по MQTT
    iot.onMqttMessage([](const String& topic, const String& payload) {
        Serial.println("MQTT ← " + topic + ": " + payload);
        if (payload.indexOf("\"on\":true") >= 0) {
            led.setPixelColor(0, led.Color(0, 50, 0));  // зелёный
        } else {
            led.setPixelColor(0, led.Color(0, 0, 0));   // выкл
        }
        led.show();
    });

    iot.begin("Basic Lamp");

    // Подписываемся после того как MQTT подключился
    if (iot.state() == IotState::RUNNING) {
        MqttConfig cfg = iot.mqttConfig();
        DeviceConfig dev = iot.storage().loadDevice();
        iot.subscribe("mpcb/devices/" + dev.deviceId + "/command");
    }
}

void loop() {
    iot.loop();
}
