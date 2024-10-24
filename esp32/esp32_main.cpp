#include "secrets.h"
#include <HTTPClient.h>
#include <string.h>
#include "ezTime.h"
#include "ArduinoJson.h"
#include "BlynkSimpleEsp32.h"
#include "PubSubClient.h"
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include "esp_system.h"
#include "esp_task_wdt.h"

#include <utility>
#include "esp_err.h"

// Constants
volatile int trigger = 0;
const int LED_PIN = 14;
const int RELAY_PIN = 25;
const int RELAY_TURN_OFF_BUTTON = 26;
volatile bool shouldBlink = false;
const int mqtt_port = 8883;
const int WDT_TIMEOUT = 10;  // Watchdog timeout in seconds

// Global objects
WiFiClientSecure espClient;
PubSubClient client(espClient);
Timezone India;
Preferences preferences;
bool enableDebug = true;
QueueHandle_t queue;

// WiFi event handler
void onWiFiEvent(WiFiEvent_t event) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            Serial.print("[onWiFiEvent] WiFi begin failed ");
            WiFi.begin(ssid, password);
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            printAndPublish(true, "[onWiFiEvent] WiFi reconnect succeeded");
            break;
        default:
            break;
    }
}

String getDateTimeForFormat(const String& format) {
    return India.dateTime(format);
}

void reconnect() {
    for (int i = 0; i < 2; i++) {
        Serial.print("Attempting MQTT connection… ");
        String clientId = "ESP32Client";
        if (!client.connected()) {
            if (client.connect(clientId.c_str(), mqtt_username, mqtt_password)) {
                Serial.println("connected!");
                client.publish(mqtt_topic, "Successfully re-established mqtt connection");
                client.subscribe(mqtt_topic);
                break;
            } else {
                Serial.print("failed, rc = ");
                Serial.print(client.state());
                Serial.println(" try again in 5 seconds");
                delay(1000);
            }
        }
    }
}

void publishMessage(const char* message) {
    if (!client.connected()) {
        reconnect();
    }
    client.publish(mqtt_topic, message);
}

void printAndPublish(bool isConnected = true, const char* format = "", ...) {
    String currentTime = getDateTimeForFormat("g:i:s A");
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    String message = "[" + currentTime + "] " + buffer;
    if (enableDebug) {
        Serial.println(message);
    }
    if (isConnected) {
        publishMessage(message.c_str());
    }
}

void notifyWaterGone() {
    printAndPublish(true, "water has gone at: %s", getDateTimeForFormat("g:i A").c_str());
    digitalWrite(RELAY_PIN, HIGH);
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    digitalWrite(RELAY_PIN, LOW);
}

void triggerSound() {
    digitalWrite(RELAY_PIN, HIGH);
    while (true) {
        if (digitalRead(RELAY_PIN) == LOW) {
            break;
        }
        delay(1000);
    }
}

void blinkLed(void *parameter) {
    esp_task_wdt_add(nullptr);
    pinMode(LED_PIN, OUTPUT);
    while (shouldBlink) {
        esp_task_wdt_reset();
        digitalWrite(LED_PIN, HIGH);
        vTaskDelay(500 / portTICK_PERIOD_MS);
        digitalWrite(LED_PIN, LOW);
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
    digitalWrite(LED_PIN, LOW);
    vTaskDelete(nullptr);
}

void customLoop(void *parameter) {
    while (shouldBlink) {
        Blynk.run();
        client.loop();
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
    printAndPublish(true, "Releasing Blynk and HiveMqtt Task");
    vTaskDelete(nullptr);
}

// Simplified JSON creation for status updates
String createStatusUpdate(const String& status, const String& date) {
    JsonDocument jsonDoc;
    jsonDoc["status"] = status;
    jsonDoc["date"] = date;

    String jsonString;
    serializeJson(jsonDoc, jsonString);
    printAndPublish(true, "Status update: %s", jsonString.c_str());
    return jsonString;
}

String makePOSTRequest(String jsonData, String apiUrl) {
    HTTPClient http;
    String payload;

    printAndPublish(true, "Connecting to API endpoint...");
    http.begin(std::move(apiUrl));

    http.addHeader("Content-Type", "application/json");
    printAndPublish(true, "Sending HTTP POST request...");
    int httpResponseCode = http.POST(std::move(jsonData));

    if (httpResponseCode == 201) {
        printAndPublish(true, "HTTP Response code: %s", String(httpResponseCode).c_str());
        payload = http.getString();
        printAndPublish(true, "makePOSTRequest payload: %s", payload.c_str());
    } else {
        printAndPublish(true, "Error code: %s", String(httpResponseCode).c_str());
    }

    http.end();
    return payload;
}

void sendStatusToAPI(const String& status) {
    String date = getDateTimeForFormat("d-M-Y");
    String jsonData = createStatusUpdate(status, date);
    makePOSTRequest(jsonData, tapWaterAPI);
}

void startWireless() {
    printAndPublish(false, "Connecting to WiFi After Waking up...");
    Blynk.begin(auth, ssid, password);
    waitForSync();
    India.setLocation(F("Asia/Kolkata"));
    espClient.setCACert(root_ca);
    client.setServer(mqtt_server, mqtt_port);
    xTaskCreate(customLoop, "customLoop", 30000, NULL, 1, NULL);
    printAndPublish(true, "Connected to WiFi, Blynk and Mqtt client");
}

void IRAM_ATTR triggerSoundOff() {
    static unsigned long lastTriggerTime = 0;
    unsigned long now = millis();

    if (now - lastTriggerTime < DEBOUNCE_DELAY) {
        return;
    }

    lastTriggerTime = now;
    gpio_set_level(gpio_num_t(RELAY_PIN), 0);
}

void waitForSensorState(int state, unsigned long duration, const char* message, bool isConnected) {
    unsigned long startTime = 0;
    while (true) {
        if (digitalRead(GPIO_NUM_33) == state) {
            if (startTime == 0) {
                startTime = millis();
            } else if (millis() - startTime >= duration) {
                printAndPublish(isConnected, message);
                startTime = 0;
                break;
            }
            delay(2000);
            int remainingTime = (duration / 1000) - ((millis() - startTime) / 1000);
            printAndPublish(isConnected, "sensor is %s, waiting for %d seconds",
                          state == HIGH ? "HIGH" : "LOW", remainingTime);
        } else {
            startTime = 0;
        }
    }
}

void handleHighState() {
    if (trigger == 0) {
        esp_task_wdt_add(nullptr);
        shouldBlink = true;
        bool alreadyAlarmed = preferences.getBool("alreadyAlarmed", false);
        printAndPublish(false, "alreadyAlarmed: %s", alreadyAlarmed ? "true" : "false");

        // trigger sound and start blink led
        xTaskCreate(blinkLed, "blinkLed", 1000, NULL, 1, NULL);
        if (!alreadyAlarmed) {
            triggerSound();
            preferences.putBool("alreadyAlarmed", true);
        }

        // start wireless connection
        startWireless();

        // Send start signal to API
        sendStatusToAPI("start");
    }
    trigger = 1;
    esp_task_wdt_reset();
    printAndPublish(true, "sensor is in HIGH state");
    delay(5000);
}

void handleLowState() {
    if (digitalRead(GPIO_NUM_33) == LOW && trigger) {
        notifyWaterGone();
        printAndPublish(true, "ending wake up sequence");

        // Send end signal to API
        sendStatusToAPI("end");

        trigger = 0;
    }
}

void processTrigger() {
    int value;

    // confirm that the sensor is high
    waitForSensorState(HIGH, 10000, "sensor was HIGH for last 10 seconds, triggering wake up sequence", false);
    esp_task_wdt_init(WDT_TIMEOUT, true);

    while (digitalRead(GPIO_NUM_33) == HIGH) {
        handleHighState();
    }

    // confirm that the sensor is low
    waitForSensorState(LOW, 35000, "sensor was LOW for last 35 seconds, ending wake up sequence", true);
    preferences.putBool("alreadyAlarmed", false);
    handleLowState();

    xQueueReceive(queue, &value, 0);
    printAndPublish(true, "releasing queue value: %d", value);
    shouldBlink = false;
}

BLYNK_WRITE(V0) {
    int pinValue = param.asInt();
    if (pinValue == 0) {
        printAndPublish("received Blynk call to stop the alarm");
        digitalWrite(RELAY_PIN, LOW);
    }
}

void sensor_woke_up() {
    int value = 1;
    if(xQueueSend(queue, &value, pdMS_TO_TICKS(60000)) == pdPASS) {
        if (digitalRead(GPIO_NUM_33) == HIGH ) {
            printAndPublish(false, "processTrigger called from sensor_woke_up");
            processTrigger();
        } else {
            printAndPublish(false, "waiting for sensor to be HIGH");
            xQueueReceive(queue, &value, 0);
            printAndPublish(false, "releasing queue value: %d", value);
        }
    } else {
        printAndPublish(false, "waiting for queue to be empty");
    }
}

void setup() {
    Serial.begin(115200);
    WiFi.onEvent(onWiFiEvent);
    preferences.begin("tw", false);

    // Initialize pins
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    pinMode(GPIO_NUM_33, INPUT_PULLDOWN);

    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);

    pinMode(RELAY_TURN_OFF_BUTTON, INPUT_PULLDOWN);
    attachInterrupt(RELAY_TURN_OFF_BUTTON, triggerSoundOff, RISING);

    // Initialize queue
    queue = xQueueCreate(1, sizeof(int));

    // Configure deep sleep wakeup
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_33, 1);
    sensor_woke_up();

    printAndPublish(false, "going to sleep now");
    esp_task_wdt_deinit();
    esp_deep_sleep_start();
}

void loop() {}