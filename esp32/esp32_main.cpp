#include "secrets.h"
#include <HTTPClient.h>
#include <string.h>
#include "ezTime.h"
#include "ArduinoJson.h"
#include "BlynkSimpleEsp32.h"
#include "PubSubClient.h"
#include <WiFiClientSecure.h>
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_panic.h"
#include "Preferences.h"
#include <utility>
#include "esp_err.h"

volatile int trigger = 0;
const int LED_PIN = 14;
const int RELAY_PIN = 25;
const int RELAY_TURN_OFF_BUTTON = 26;
volatile bool shouldBlink = false;
const int mqtt_port = 8883;
const int WDT_TIMEOUT = 25;
const int BUTTON_CHECK_INTERVAL = 50;

// Global objects
WiFiClientSecure espClient;
PubSubClient client(espClient);
Timezone India;
bool enableDebug = true;
QueueHandle_t queue;
volatile bool alarmActive = true;

Preferences preferences;

const char *PREF_NAMESPACE = "wdt";
const char *ALARM_TRIGGERED_KEY = "alarm_triggered";

// Forward declarations of functions used before their definition
void printAndPublish(bool isConnected = true, const char *format = "", ...);

String getDateTimeForFormat(const String &format) {
    return India.dateTime(format);
}

void buttonMonitorTask(void *parameter) {
    const TickType_t xDelay = pdMS_TO_TICKS(BUTTON_CHECK_INTERVAL);
    while (true) {
        if (digitalRead(RELAY_TURN_OFF_BUTTON) == HIGH) {
            digitalWrite(RELAY_PIN, LOW);
            break;
        }
        vTaskDelay(xDelay);
    }
    vTaskDelete(nullptr);
}

bool was_alarm_triggered() {
    if (!preferences.begin(PREF_NAMESPACE, true)) {
        return false;
    }

    bool triggered = preferences.getBool(ALARM_TRIGGERED_KEY, false);
    preferences.end();

    Serial.printf("Checking alarm state: %s\n", triggered ? "triggered" : "not triggered");
    return triggered;
}

void mark_alarm_triggered() {
    if (!preferences.begin(PREF_NAMESPACE, false)) {
        return;
    }

    preferences.putBool(ALARM_TRIGGERED_KEY, true);
    preferences.end();

    Serial.println("Marked alarm as triggered");
}

void reset_alarm_state() {
    if (!preferences.begin(PREF_NAMESPACE, false)) {
        return;
    }

    preferences.putBool(ALARM_TRIGGERED_KEY, false);
    preferences.end();

    Serial.println("Reset alarm state for next session");
}

void reconnect() {
    while (!client.connected()) {
        Serial.print("Attempting MQTT connection... ");
        String clientId = "ESP32Client";
        if (client.connect(clientId.c_str(), mqtt_username, mqtt_password)) {
            Serial.println("connected!");
            client.publish(mqtt_topic, "Sent from the other world");
            client.subscribe(mqtt_topic);
        } else {
            Serial.print("failed, rc = ");
            Serial.print(client.state());
            Serial.println(" try again in 5 seconds");
            delay(5000);
        }
    }
}

void publishMessage(const char *message) {
    if (!client.connected()) {
        reconnect();
    }
    client.publish(mqtt_topic, message);
}

void printAndPublish(bool isConnected, const char *format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    String message = buffer;
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

void blinkLed(void *parameter) {
    if (esp_task_wdt_add(nullptr) != ESP_OK) {
        Serial.println("Error adding blinkLed to WDT");
        vTaskDelete(nullptr);
        return;
    }

    pinMode(LED_PIN, OUTPUT);
    while (1) {
        digitalWrite(LED_PIN, HIGH);
        vTaskDelay(500 / portTICK_PERIOD_MS);
        digitalWrite(LED_PIN, LOW);
        vTaskDelay(500 / portTICK_PERIOD_MS);
        esp_task_wdt_reset();
    }
}

void customLoop(void *parameter) {
    if (esp_task_wdt_add(nullptr) != ESP_OK) {
        Serial.println("Error adding customLoop to WDT");
        vTaskDelete(nullptr);
        return;
    }

    while (shouldBlink) {
        Blynk.run();
        client.loop();
        vTaskDelay(20 / portTICK_PERIOD_MS);
        esp_task_wdt_reset();
    }

    printAndPublish(true, "Releasing Blynk and HiveMqtt Task");
    esp_task_wdt_delete(nullptr);
    vTaskDelete(nullptr);
}

String createStatusUpdate(const String &status, const String &date) {
    JsonDocument jsonDoc;
    jsonDoc["status"] = status;
    jsonDoc["date"] = date;

    String jsonString;
    serializeJson(jsonDoc, jsonString);
    return jsonString;
}

String makePOSTRequest(String jsonData, String apiUrl) {
    HTTPClient http;
    String payload;

    http.begin(std::move(apiUrl));
    http.addHeader("Content-Type", "application/json");
    int httpResponseCode = http.POST(std::move(jsonData));

    if (httpResponseCode > 0) {
        printAndPublish(true, "HTTP Response code: %s", String(httpResponseCode).c_str());
        payload = http.getString();
        printAndPublish(true, "makePOSTRequest payload: %s", payload.c_str());
    } else {
        printAndPublish(true, "Error code: %s", String(httpResponseCode).c_str());
    }

    http.end();
    return payload;
}

void sendStatusToAPI(const String &status) {
    String date = getDateTimeForFormat("d-M-Y");
    String jsonData = createStatusUpdate(status, date);
    makePOSTRequest(jsonData, tapWaterAPI);
}

void startWireless() {
    printAndPublish(false, "Connecting to WiFi After Waking up...");
    Blynk.begin(auth, ssid, password);
    xTaskCreate(
            customLoop,
            "customLoop",
            30000,
            nullptr,
            1,
            nullptr);
    waitForSync();
    espClient.setCACert(root_ca);
    client.setServer(mqtt_server, mqtt_port);
}

void triggerSound(void *parameter) {
    digitalWrite(RELAY_PIN, HIGH);

    while (alarmActive) {
        vTaskDelay(pdMS_TO_TICKS(100));  // Small delay to prevent tight loop
        esp_task_wdt_reset();
    }

    digitalWrite(RELAY_PIN, LOW);
    vTaskDelete(nullptr);
}

void waitForSensorState(int state, unsigned long duration, const char *message, bool isConnected) {
    TaskHandle_t blinkTask = nullptr;
    xTaskCreate(blinkLed, "blinkLed", 1000, nullptr, 1, &blinkTask);

    const TickType_t checkInterval = pdMS_TO_TICKS(20);  // Check every 20ms for faster response
    const unsigned long TIMEOUT_DURATION = 120000;        // 2 minutes timeout
    unsigned long accumulatedTime = 0;                    // Total time in desired state
    unsigned long totalWaitTime = 0;                      // Total time spent waiting
    unsigned long lastCheckTime = millis();
    unsigned long lastPrintTime = 0;                      // For throttling status messages
    const unsigned long PRINT_INTERVAL = 2000;            // Print status every 2 seconds
    const unsigned long MAX_INTERRUPTION = 5000;          // 5 seconds maximum interruption


    while (true) {
        unsigned long currentTime = millis();
        esp_task_wdt_reset();

        unsigned long elapsedTime = currentTime - lastCheckTime;
        lastCheckTime = currentTime;
        totalWaitTime += elapsedTime;

        if (totalWaitTime >= TIMEOUT_DURATION) {
            printAndPublish(isConnected, "Timeout waiting for sensor state - going to sleep");
            if (blinkTask) {
                vTaskDelete(blinkTask);
            }
            esp_task_wdt_delete(nullptr);
            esp_deep_sleep_start();
        }

        if (digitalRead(GPIO_NUM_33) == state) {
            accumulatedTime += elapsedTime;

            if (currentTime - lastPrintTime >= PRINT_INTERVAL) {
                int remainingTime = (duration - accumulatedTime) / 1000;
                int timeoutRemaining = (TIMEOUT_DURATION - totalWaitTime) / 1000;
                printAndPublish(isConnected,
                                "sensor is %s, accumulated time: %lu ms, need %d more seconds (timeout in %d seconds)",
                                state == HIGH ? "HIGH" : "LOW",
                                accumulatedTime,
                                remainingTime,
                                timeoutRemaining);
                lastPrintTime = currentTime;
            }

            if (accumulatedTime >= duration) {
                printAndPublish(isConnected, message);
                break;
            }
        } else {
            if (elapsedTime >= MAX_INTERRUPTION) {
                if (accumulatedTime > 0) {
                    printAndPublish(isConnected, "Signal interrupted for too long, resetting accumulated time");
                    accumulatedTime = 0;
                }
            }
        }

        vTaskDelay(checkInterval);
    }
}

void handleHighState() {
    if (trigger == 0) {
        printAndPublish(false, "starting wake up sequence");

        if (!was_alarm_triggered()) {
            xTaskCreate(triggerSound, "triggerSound", 1000, nullptr, 1, nullptr);
            mark_alarm_triggered();
        }

        startWireless();
        sendStatusToAPI("start");
        printAndPublish(true, "Established connectivity");
    }
    trigger = 1;
    printAndPublish(true, "1");
    vTaskDelay(5000);
}

void handleLowState() {
    if (digitalRead(GPIO_NUM_33) == LOW && trigger) {
        notifyWaterGone();
        printAndPublish(true, "ending wake up sequence");
        sendStatusToAPI("end");
        reset_alarm_state();
        trigger = 0;
    }
}

void processTrigger() {
    int value;

    waitForSensorState(HIGH, 10000, "sensor was HIGH for last 10 seconds, triggering wake up sequence", false);

    shouldBlink = true;
    handleHighState();

    waitForSensorState(LOW, 35000, "sensor was LOW for last 35 seconds, ending wake up sequence", true);
    handleLowState();

    xQueueReceive(queue, &value, 0);
    printAndPublish(true, "releasing queue value: %d", value);
    shouldBlink = false;
    esp_deep_sleep_start();
}

BLYNK_WRITE(V0) {
    int pinValue = param.asInt();
    if (pinValue == 0) {
        digitalWrite(RELAY_PIN, LOW);
    }
}

void esp_woke_up() {
    int value = 1;
    if (xQueueSend(queue, &value, pdMS_TO_TICKS(60000)) == pdPASS) {
        if (digitalRead(GPIO_NUM_33) == HIGH) {
            printAndPublish(false, "processTrigger called from esp_woke_up");
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

void check_reset_reason() {
    esp_reset_reason_t reset_reason = esp_reset_reason();

    preferences.begin(PREF_NAMESPACE, false);

    if (reset_reason != ESP_RST_TASK_WDT && reset_reason != ESP_RST_WDT && reset_reason != ESP_RST_POWERON &&
        reset_reason != ESP_RST_SW && reset_reason != ESP_RST_PANIC && reset_reason != ESP_RST_BROWNOUT) {
        if (preferences.getBool(ALARM_TRIGGERED_KEY, false)) {
            preferences.putBool(ALARM_TRIGGERED_KEY, false);
            Serial.printf("Abnormal reset detected (reason: %d) - resetting alarm state\n", reset_reason);
        } else {
            Serial.printf("Abnormal reset detected (reason: %d) - alarm was not triggered\n", reset_reason);
        }
    } else {
        Serial.printf("Normal reset detected (reason: %d) - preserving alarm state\n", reset_reason);
    }

    preferences.end();
}

void setup() {
    Serial.begin(115200);

    check_reset_reason();
    esp_task_wdt_init(WDT_TIMEOUT, true);

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    pinMode(GPIO_NUM_33, INPUT_PULLDOWN);
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);
    pinMode(RELAY_TURN_OFF_BUTTON, INPUT_PULLDOWN);

    if (!was_alarm_triggered()) {
        xTaskCreate(
                buttonMonitorTask,
                "ButtonMonitor",
                2048,
                nullptr,
                50,
                nullptr
        );
    }

    queue = xQueueCreate(1, sizeof(int));

    esp_sleep_enable_ext0_wakeup(GPIO_NUM_33, 1);
    esp_woke_up();

    printAndPublish(false, "going to sleep now");
    esp_deep_sleep_start();
}

void loop() {
}
