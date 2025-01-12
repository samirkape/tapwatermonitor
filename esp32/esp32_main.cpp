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
#include <WiFi.h>

// Constants
volatile int trigger = 0;
const int LED_PIN = 14;
const int RELAY_PIN = 25;
const int RELAY_TURN_OFF_BUTTON = 26;
volatile bool shouldBlink = false;
const int mqtt_port = 8883;
const int WDT_TIMEOUT = 25; // Watchdog timeout in seconds

// Global objects
WiFiClientSecure espClient;
PubSubClient client(espClient);
Timezone India;
bool enableDebug = true;
QueueHandle_t queue;

Preferences preferences;

const char* PREF_NAMESPACE = "wdt";
const char* ALARM_TRIGGERED_KEY = "alarm_triggered";
TaskHandle_t blinkTaskHandle = NULL;
TaskHandle_t wirelessTaskHandle = NULL;
TaskHandle_t customLoopTaskHandle = NULL;

bool isConnected() {
    return WiFiClass::status() == WL_CONNECTED;
}

// Forward declarations of functions used before their definition
void printAndPublish(bool isConnected = true, const char* format = "", ...);

String getDateTimeForFormat(const String& format) {
    return India.dateTime(format);
}

void cleanupTasks() {
    if (blinkTaskHandle != NULL) {
        esp_task_wdt_delete(NULL);
        vTaskDelete(blinkTaskHandle);
        blinkTaskHandle = NULL;
    }

    if (wirelessTaskHandle != NULL) {
        esp_task_wdt_delete(NULL);
        vTaskDelete(wirelessTaskHandle);
        wirelessTaskHandle = NULL;
    }

    if (customLoopTaskHandle != NULL) {
        esp_task_wdt_delete(NULL);
        vTaskDelete(customLoopTaskHandle);
        customLoopTaskHandle = NULL;
    }

    int value;
    xQueueReceive(queue, &value, 0);
    printAndPublish(isConnected(), "releasing queue value in cleanup: %d", value);

    shouldBlink = false;
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
    WiFiClass::status();
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

void publishMessage(const char* message) {
    if (!client.connected()) {
        reconnect();
    }
    client.publish(mqtt_topic, message);
}

void printAndPublish(bool isConnected, const char* format, ...) {
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
    printAndPublish(isConnected(), "water has gone at: %s", getDateTimeForFormat("g:i A").c_str());
    digitalWrite(RELAY_PIN, HIGH);
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    digitalWrite(RELAY_PIN, LOW);
}

void blinkLed(void *parameter) {
    if (esp_task_wdt_add(NULL) != ESP_OK) {
        Serial.println("Error adding blinkLed to WDT");
        vTaskDelete(NULL);
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
    if (esp_task_wdt_add(NULL) != ESP_OK) {
        Serial.println("Error adding customLoop to WDT");
        vTaskDelete(NULL);
        return;
    }

    while (shouldBlink) {
        Blynk.run();
        client.loop();
        vTaskDelay(20 / portTICK_PERIOD_MS);
        esp_task_wdt_reset();
    }

    printAndPublish(isConnected(), "Releasing Blynk and HiveMqtt Task");
    esp_task_wdt_delete(NULL);
    vTaskDelete(NULL);
}

String createStatusUpdate(const String& status, const String& date) {
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
        printAndPublish(isConnected(), "HTTP Response code: %s", String(httpResponseCode).c_str());
        payload = http.getString();
        printAndPublish(isConnected(), "makePOSTRequest payload: %s", payload.c_str());
    } else {
        printAndPublish(isConnected(), "Error code: %s", String(httpResponseCode).c_str());
    }

    http.end();
    return payload;
}

void sendStatusToAPI(const String& status) {
    String date = getDateTimeForFormat("d-M-Y");
    String jsonData = createStatusUpdate(status, date);
    makePOSTRequest(jsonData, tapWaterAPI);
}

//void startWireless() {
//    printAndPublish(false, "Connecting to WiFi After Waking up...");
//    Blynk.begin(auth, ssid, password);
//    xTaskCreate(
//            customLoop,
//            "customLoop",
//            30000,
//            NULL,
//            1,
//            NULL);
//    waitForSync();
//    espClient.setCACert(root_ca);
//    client.setServer(mqtt_server, mqtt_port);
//}


void wirelessTask(void *parameter) {
    if (esp_task_wdt_add(NULL) != ESP_OK) {
        Serial.println("Error adding wirelessTask to WDT");
        vTaskDelete(NULL);
        return;
    }

    printAndPublish(isConnected(), "Connecting to WiFi After Waking up...");
    Blynk.begin(auth, ssid, password);

    xTaskCreate(
            customLoop,
            "customLoop",
            30000,
            NULL,
            1,
            &customLoopTaskHandle);

    waitForSync();
    espClient.setCACert(root_ca);
    client.setServer(mqtt_server, mqtt_port);

    while(1) {
        esp_task_wdt_reset();
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

void startWireless() {
    xTaskCreate(
            wirelessTask,
            "wirelessTask",
            10000,
            NULL,
            1,
            &wirelessTaskHandle);
}

void blinkLed() {
    xTaskCreate(
            blinkLed,
            "blinkLed",
            1000,
            nullptr,
            1,
            &blinkTaskHandle);
}

void triggerSound(void *parameter) {
    if (!shouldBlink) {
        digitalWrite(RELAY_PIN, HIGH);
        vTaskDelay(3000 / portTICK_PERIOD_MS);
        digitalWrite(RELAY_PIN, LOW);
    } else {
        digitalWrite(RELAY_PIN, HIGH);
    }
    vTaskDelete(NULL);
}

void IRAM_ATTR triggerSoundOff() {
    gpio_set_level(gpio_num_t(RELAY_PIN), 0);
}

bool waitForSensorState(int state, unsigned long duration, const char* message) {
    unsigned long totalWaitStart = millis();
    const unsigned long TOTAL_TIMEOUT = 60000; // 1 minute timeout
    unsigned long startTime = 0;

    if ( state == HIGH ) {
        blinkLed();
        startWireless();
    }

    while (true) {
        if (millis() - totalWaitStart >= TOTAL_TIMEOUT) {
            printAndPublish(isConnected(), "Timeout waiting for sensor state");
            cleanupTasks();
            return false;
        }

        if (digitalRead(GPIO_NUM_33) == state) {
            if (startTime == 0) {
                startTime = millis();
            } else if (millis() - startTime >= duration) {
                printAndPublish(isConnected(), message);
                return true;
            }
            delay(2000);
            int remainingTime = (duration / 1000) - ((millis() - startTime) / 1000);
            printAndPublish(isConnected(), "sensor is %s, waiting for %d seconds",
                            state == HIGH ? "HIGH" : "LOW", remainingTime);
        } else {
            startTime = 0;
        }
    }
}

void handleHighState() {
    if (trigger == 0) {
        printAndPublish(isConnected(), "starting wake up sequence");

        if (!was_alarm_triggered()) {
            xTaskCreate(triggerSound, "triggerSound", 1000, NULL, 1, NULL);
            mark_alarm_triggered();
        }

        sendStatusToAPI("start");
        printAndPublish(isConnected(), "Established connectivity");
    }
    trigger = 1;
    printAndPublish(isConnected(), "1");
    vTaskDelay(5000 / portTICK_PERIOD_MS);
}

void handleLowState() {
    if (digitalRead(GPIO_NUM_33) == LOW && trigger) {
        notifyWaterGone();
        printAndPublish(isConnected(), "ending wake up sequence");
        sendStatusToAPI("end");
        reset_alarm_state();
        trigger = 0;
    }
}

void processTrigger() {
    if (!waitForSensorState(HIGH, 10000, "sensor was HIGH for last 10 seconds, triggering wake up sequence")) {
        esp_deep_sleep_start();
    }

    while (digitalRead(GPIO_NUM_33) == HIGH) {
        shouldBlink = true;
        handleHighState();
    }

    if (!waitForSensorState(LOW, 35000, "sensor was LOW for last 35 seconds, ending wake up sequence")) {
        cleanupTasks();
        esp_deep_sleep_start();
    }

    handleLowState();
    cleanupTasks();
    esp_deep_sleep_start();
}

BLYNK_WRITE(V0) {
    int pinValue = param.asInt();
    if (pinValue == 0) {
        digitalWrite(RELAY_PIN, LOW);
        printAndPublish(isConnected(),"received Blynk call to stop the alarm");
    }
}

void esp_woke_up() {
    int value = 1;
    if(xQueueSend(queue, &value, pdMS_TO_TICKS(60000)) == pdPASS) {
        if (digitalRead(GPIO_NUM_33) == HIGH ) {
            printAndPublish(isConnected(), "processTrigger called from esp_woke_up");
            processTrigger();
        } else {
            printAndPublish(isConnected(), "waiting for sensor to be HIGH");
            xQueueReceive(queue, &value, 0);
            printAndPublish(isConnected(), "releasing queue value: %d", value);
        }
    } else {
        printAndPublish(isConnected(), "waiting for queue to be empty");
    }
}

void check_reset_reason() {
    esp_reset_reason_t reset_reason = esp_reset_reason();

    preferences.begin(PREF_NAMESPACE, false);

    if (reset_reason != ESP_RST_TASK_WDT && reset_reason != ESP_RST_WDT && reset_reason != ESP_RST_POWERON) {
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

    // Initialize global watchdog timer
    esp_task_wdt_init(WDT_TIMEOUT, true);

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
    esp_woke_up();

    printAndPublish(isConnected(), "going to sleep now");
    esp_deep_sleep_start();
}

void loop() {
    // Empty as we're using deep sleep
}
