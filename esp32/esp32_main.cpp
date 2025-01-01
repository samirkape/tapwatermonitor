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

// Constants
volatile int trigger = 0;
const int LED_PIN = 14;
const int RELAY_PIN = 25;
const int RELAY_TURN_OFF_BUTTON = 26;
volatile bool shouldBlink = false;
const int mqtt_port = 8883;
const int WDT_TIMEOUT = 15; // Watchdog timeout in seconds

// Global objects
WiFiClientSecure espClient;
PubSubClient client(espClient);
Timezone India;
bool enableDebug = true;
QueueHandle_t queue;

Preferences preferences;

const char* PREF_NAMESPACE = "wdt";
const char* WATCHDOG_RESET_KEY = "reset_count";
const char* ALARM_TRIGGERED_KEY = "alarm_triggered";

// Forward declarations of functions used before their definition
void printAndPublish(bool isConnected = true, const char* format = "", ...);

String getDateTimeForFormat(const String& format) {
    return India.dateTime(format);
}

void shutdown_handler() {
    esp_reset_reason_t reset_reason = esp_reset_reason();

    // Only preserve alarm state for watchdog resets
    if (reset_reason != ESP_RST_TASK_WDT && reset_reason != ESP_RST_WDT) {
        preferences.begin(PREF_NAMESPACE, false);
        preferences.putBool(ALARM_TRIGGERED_KEY, false);
        preferences.end();
        Serial.println("Non-watchdog reset detected - resetting alarm state");
    } else {
        Serial.println("Watchdog reset detected - preserving alarm state");
    }
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
        Serial.print("Attempting MQTT connection… ");
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
    printAndPublish(true, "water has gone at: %s", getDateTimeForFormat("g:i A").c_str());
    digitalWrite(RELAY_PIN, HIGH);
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    digitalWrite(RELAY_PIN, LOW);
}

void blinkLed(void *parameter) {
    esp_task_wdt_init(WDT_TIMEOUT, true);
    esp_task_wdt_add(NULL);

    pinMode(LED_PIN, OUTPUT);
    while (shouldBlink) {
        digitalWrite(LED_PIN, HIGH);
        vTaskDelay(500 / portTICK_PERIOD_MS);
        digitalWrite(LED_PIN, LOW);
        vTaskDelay(500 / portTICK_PERIOD_MS);
        esp_task_wdt_reset(); // Reset watchdog timer
    }
    digitalWrite(LED_PIN, LOW);
    esp_task_wdt_delete(NULL); // Remove thread from WDT watch
    vTaskDelete(nullptr);
}

void customLoop(void *parameter) {
    while (shouldBlink) {
        Blynk.run();
        client.loop();
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
    printAndPublish("Releasing Blynk and HiveMqtt Task");
    vTaskDelete(nullptr);
}

// Simplified JSON creation for status updates
String createStatusUpdate(const String& status, const String& date) {
    JsonDocument jsonDoc;
    jsonDoc["status"] = status;
    jsonDoc["date"] = date;

    String jsonString;
    serializeJson(jsonDoc, jsonString);
//    printAndPublish(true, "Status update: %s", jsonString.c_str());
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

void sendStatusToAPI(const String& status) {
    String date = getDateTimeForFormat("d-M-Y");
    String jsonData = createStatusUpdate(status, date);
    makePOSTRequest(jsonData, tapWaterAPI);
}

void startWireless() {
    printAndPublish(false, "Connecting to WiFi After Waking up...");
    Blynk.begin(auth, ssid, password);
    waitForSync();
    espClient.setCACert(root_ca);
    client.setServer(mqtt_server, mqtt_port);
    xTaskCreate(customLoop, "customLoop", 30000, NULL, 1, NULL);
}

void triggerSound(void *parameter) {
    if (!shouldBlink) {
        digitalWrite(RELAY_PIN, HIGH);
        vTaskDelay(3000 / portTICK_PERIOD_MS);
        digitalWrite(RELAY_PIN, LOW);
    } else {
        digitalWrite(RELAY_PIN, HIGH);
    }
    vTaskDelete(nullptr);
}

void IRAM_ATTR triggerSoundOff() {
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
        shouldBlink = true;
        printAndPublish(false, "starting wake up sequence");
        xTaskCreate(blinkLed, "blinkLed", 1000, NULL, 1, NULL);

        // Only trigger sound if it hasn't been triggered in this session
        if (!was_alarm_triggered()) {
            xTaskCreate(triggerSound, "triggerSound", 1000, NULL, 1, NULL);
            mark_alarm_triggered();  // Mark that we've triggered the alarm
        }

        startWireless();
        sendStatusToAPI("start");
        printAndPublish(true, "Established connectivity");
    }
    trigger = 1;
    printAndPublish(true, "1");
    delay(5000);
}

void handleLowState() {
    if (digitalRead(GPIO_NUM_33) == LOW && trigger) {
        notifyWaterGone();
        printAndPublish(true, "ending wake up sequence");
        sendStatusToAPI("end");
        reset_alarm_state();  // Reset the alarm state for next water session
        trigger = 0;
    }
}

void processTrigger() {
    int value;

    // confirm that the sensor is high
    waitForSensorState(HIGH, 10000, "sensor was HIGH for last 10 seconds, triggering wake up sequence", false);

    while (digitalRead(GPIO_NUM_33) == HIGH) {
        handleHighState();
    }

    // confirm that the sensor is low
    waitForSensorState(LOW, 35000, "sensor was LOW for last 35 seconds, ending wake up sequence", true);

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

void esp_woke_up() {
    int value = 1;
    if(xQueueSend(queue, &value, pdMS_TO_TICKS(60000)) == pdPASS) {
        if (digitalRead(GPIO_NUM_33) == HIGH ) {
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

    if (reset_reason != ESP_RST_TASK_WDT && reset_reason != ESP_RST_WDT) {
        preferences.putBool(ALARM_TRIGGERED_KEY, false);
        Serial.printf("Abnormal reset detected (reason: %d) - resetting alarm state\n", reset_reason);
    } else {
        Serial.println("Watchdog reset detected - preserving alarm state");
    }

    preferences.end();
}

void setup() {
    Serial.begin(115200);

    ESP_ERROR_CHECK(esp_register_shutdown_handler(shutdown_handler));
    check_reset_reason();

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

    printAndPublish(false, "going to sleep now");
    esp_deep_sleep_start();
}

void loop() {
    // Empty as we're using deep sleep
}
