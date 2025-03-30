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
const int mqtt_port = 8883;
const int WDT_TIMEOUT = 25;
const int BUTTON_CHECK_INTERVAL = 50;

const unsigned long CONNECTIVITY_CHECK_INTERVAL = 15000;
volatile bool g_startStatusSent = false;
volatile bool g_connectivityDaemonRunning = true;
bool g_connectivityEstablished = false;
bool g_wifi_connected = false;
#define DAILY_RESTART_INTERVAL_US (24ULL * 60 * 60 * 1000000)

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
    if (g_connectivityEstablished) {
        publishMessage(message.c_str());
    }
}

void notifyWaterGone() {
    printAndPublish(true, "water has gone at: %s", getDateTimeForFormat("g:i A").c_str());
    digitalWrite(RELAY_PIN, HIGH);
    delay(5000 / portTICK_PERIOD_MS);
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

    while (true) {
        if (g_connectivityEstablished) {
            Blynk.run();
            client.loop();
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        vTaskDelay(20 / portTICK_PERIOD_MS);
        esp_task_wdt_reset();
    }
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


void connectivityDaemonTask(void *parameter) {
    TaskHandle_t customLoopTask = nullptr;
    unsigned long lastConnectivityCheck = 0;
    bool wasConnectedPreviously = false;
    int reconnectAttempts = 0;
    const int MAX_RECONNECT_ATTEMPTS = 5;
    unsigned long connectionStartTime = millis();

    Serial.println("ConnectivityDaemon: Task started");

    // Create customLoop task once and let it handle its own waiting
    Serial.println("ConnectivityDaemon: Creating customLoop task...");
    xTaskCreate(
            customLoop,
            "customLoop",
            30000,
            nullptr,
            1,
            &customLoopTask
    );

    if (customLoopTask == nullptr) {
        Serial.println("ConnectivityDaemon: ERROR - Failed to create customLoop task!");
    } else {
        Serial.println("ConnectivityDaemon: customLoop task created successfully");
    }

    while (g_connectivityDaemonRunning) {
        unsigned long currentTime = millis();
        unsigned long uptime = (currentTime - connectionStartTime) / 1000; // in seconds

        if (currentTime - lastConnectivityCheck >= CONNECTIVITY_CHECK_INTERVAL) {
            lastConnectivityCheck = currentTime;

            Serial.printf("\nConnectivityDaemon: Status Check [Uptime: %lu seconds]\n", uptime);
            Serial.println("----------------------------------------");

            // Check WiFi
            bool isWiFiConnected = (WiFiClass::status() == WL_CONNECTED);
            Serial.printf("WiFi Status: %s (RSSI: %d dBm)\n",
                          isWiFiConnected ? "CONNECTED" : "DISCONNECTED",
                          isWiFiConnected ? WiFi.RSSI() : 0);

            if (!isWiFiConnected) {
                Serial.printf("WiFi Reconnect Attempt %d/%d\n",
                              ++reconnectAttempts, MAX_RECONNECT_ATTEMPTS);
                Blynk.begin(auth, ssid, password);
                // Wait briefly to check if reconnection was immediate
                delay(1000);
                isWiFiConnected = (WiFiClass::status() == WL_CONNECTED);
                Serial.printf("WiFi Reconnect Result: %s\n",
                              isWiFiConnected ? "SUCCESS" : "FAILED");
            }

            // Check Blynk
            bool isBlynkConnected = isWiFiConnected ? Blynk.connected() : false;
            Serial.printf("Blynk Status: %s\n",
                          isBlynkConnected ? "CONNECTED" : "DISCONNECTED");

            if (isWiFiConnected && !isBlynkConnected) {
                Serial.println("Attempting Blynk reconnection...");
                isBlynkConnected = Blynk.connected();
                Serial.printf("Blynk Reconnect Result: %s\n",
                              isBlynkConnected ? "SUCCESS" : "FAILED");
            }

            // Check MQTT
            bool isMqttConnected = isWiFiConnected ? client.connected() : false;
            Serial.printf("MQTT Status: %s\n",
                          isMqttConnected ? "CONNECTED" : "DISCONNECTED");

            if (isWiFiConnected && !isMqttConnected) {
                Serial.println("Attempting MQTT reconnection...");
                bool mqttReconnectResult = false;

                // Try MQTT reconnection with timeout
                unsigned long mqttStartTime = millis();
                while (!mqttReconnectResult && (millis() - mqttStartTime < 5000)) {
                    Serial.print("MQTT connecting...");
                    String clientId = "ESP32Client-" + String(random(0xffff), HEX);
                    mqttReconnectResult = client.connect(clientId.c_str(), mqtt_username, mqtt_password);
                    if (mqttReconnectResult) {
                        Serial.println("SUCCESS");
                        client.publish(mqtt_topic, "Reconnected successfully");
                        client.subscribe(mqtt_topic);
                    } else {
                        Serial.printf("FAILED (rc = %d)\n", client.state());
                        delay(1000);
                    }
                }
            }

            bool currentConnectivityStatus = (
                    isWiFiConnected &&
                    isBlynkConnected &&
                    isMqttConnected
            );

            if (currentConnectivityStatus != wasConnectedPreviously) {
                g_connectivityEstablished = currentConnectivityStatus;
                g_wifi_connected = isWiFiConnected;

                Serial.println("\nConnectivity State Change Detected!");
                Serial.printf("Previous State: %s\n",
                              wasConnectedPreviously ? "CONNECTED" : "DISCONNECTED");
                Serial.printf("Current State: %s\n",
                              currentConnectivityStatus ? "CONNECTED" : "DISCONNECTED");

                if (currentConnectivityStatus) {
                    reconnectAttempts = 0; // Reset counter on successful connection
                    printAndPublish(true, "All connections established");
                } else {
                    printAndPublish(false, "Connectivity lost");
                }
            }

            Serial.println("----------------------------------------");
            wasConnectedPreviously = currentConnectivityStatus;

            // Check if we've exceeded max reconnection attempts
            if (reconnectAttempts >= MAX_RECONNECT_ATTEMPTS && !currentConnectivityStatus) {
                Serial.println("WARNING: Max reconnection attempts reached!");
                Serial.println("Consider implementing recovery action here");
                // You might want to add recovery logic here
                reconnectAttempts = 0; // Reset for next round
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // Clean up customLoop if daemon is stopping
    if (customLoopTask != nullptr) {
        Serial.println("ConnectivityDaemon: Cleaning up customLoop task");
        vTaskDelete(customLoopTask);
    }

    Serial.println("ConnectivityDaemon: Task ending");
    vTaskDelete(nullptr);
}

void startWireless() {
    unsigned long startAttempt = millis();
    const unsigned long WIFI_TIMEOUT = 30000;

    printAndPublish(false, "Connecting to WiFi After Waking up...");
    WiFi.begin(ssid, password);

    while (WiFiClass::status() != WL_CONNECTED) {
        if (millis() - startAttempt > WIFI_TIMEOUT) {
            printAndPublish(false, "WiFi connection timeout");
            return;
        }
        esp_task_wdt_reset();
        delay(500);
    }

    // Only proceed with Blynk and MQTT if WiFi connected
    if (WiFiClass::status() == WL_CONNECTED) {
        Blynk.begin(auth, ssid, password);
        xTaskCreate(customLoop, "customLoop", 30000, nullptr, 1, nullptr);
        //        waitForSync();
        espClient.setCACert(root_ca);
        client.setServer(mqtt_server, mqtt_port);
    }
}

template<size_t N>
class RollingSensorState {
private:
    bool readings[N]{};
    size_t currentIndex = 0;
    size_t count = 0;
    const float threshold;
    const size_t minSamplesRequired;  // Minimum samples needed before making state decisions
    bool currentState = false;

public:
    explicit RollingSensorState(float thresholdPercentage = 0.7, size_t minimumSamples = 5)
            : threshold(thresholdPercentage),
              minSamplesRequired(std::min(minimumSamples, N)) {
        memset(readings, 0, sizeof(readings));
    }

    void addReading(bool value) {
        readings[currentIndex] = value;
        if (count < N) count++;
        currentIndex = (currentIndex + 1) % N;

        // Update state only if we have minimum required samples
        if (count >= minSamplesRequired) {
            float avg = getAverage();
            bool newState = (avg >= threshold);

            if (newState != currentState) {
                Serial.printf("RollingSensor: State transition %s → %s (avg: %.2f%% → %.2f%%)\n",
                              currentState ? "ACTIVE" : "INACTIVE",
                              newState ? "ACTIVE" : "INACTIVE",
                              currentState ? threshold * 100 : 0.0,
                              avg * 100);
                currentState = newState;
            }
        } else {
            // For first reading, set initial state
            if (count == 1 && value) {
                currentState = true;
                Serial.printf("RollingSensor: Initial state set to ACTIVE\n");
            }
        }

        // Debug output
        Serial.printf("RollingSensor [%zu/%zu] New: %d, Avg: %.2f%%, State: %s, Buffer: %s\n",
                      count, N, value, getAverage() * 100,
                      currentState ? "ACTIVE" : "INACTIVE",
                      getBufferString().c_str());
    }

    bool getCurrentState() {
        if (count == 0) {
            Serial.println("RollingSensor: Warning - Getting state with no readings");
            return false;
        }

        // If we don't have minimum samples, maintain current state
        if (count < minSamplesRequired) {
            return currentState;
        }

        return currentState;
    }

    float getAverage() {
        if (count == 0) {
            Serial.println("RollingSensor: Warning - Getting average with no readings");
            return 0.0f;
        }

        size_t trueCount = 0;
        for (size_t i = 0; i < count; i++) {
            if (readings[i]) trueCount++;
        }

        return (float) trueCount / count;
    }

    String getBufferString() {
        String bufferState = "[";
        for (size_t i = 0; i < count; i++) {
            bufferState += readings[i] ? "1" : "0";
            if (i < count - 1) bufferState += ",";
        }
        bufferState += "]";
        return bufferState;
    }

    void reset() {
        currentIndex = 0;
        count = 0;
        currentState = false;
        memset(readings, 0, sizeof(readings));
    }
};

void waitForSensorState(int desiredState, unsigned long duration, const char *message, bool isConnected) {
    // Configuration constants
    const struct Config {
        unsigned long TIMEOUT_DURATION = 120000;     // 2 minutes total timeout
        unsigned long PRINT_INTERVAL = 2000;         // Status update interval
        unsigned long SAMPLING_INTERVAL = 20;        // Time between sensor readings
        size_t BUFFER_SIZE = 50;                    // Number of readings to average (1 second worth at 20ms intervals)
        float STATE_THRESHOLD = 0.7;                // 70% of readings must agree
        TickType_t CHECK_INTERVAL = pdMS_TO_TICKS(20);
    } config;

    // State tracking structure
    struct SensorState {
        unsigned long accumulatedTime = 0;    // Time spent in desired state
        unsigned long totalWaitTime = 0;      // Total elapsed time
        unsigned long lastCheckTime;          // Last sensor check timestamp
        unsigned long lastPrintTime = 0;      // Last status print timestamp
    } sensorState;

    // Initialize rolling average calculator
    RollingSensorState<50> rollingState(config.STATE_THRESHOLD);

    // Initialize LED blink task
    TaskHandle_t blinkTask = nullptr;
    xTaskCreate(blinkLed, "blinkLed", 1000, nullptr, 1, &blinkTask);

    sensorState.lastCheckTime = millis();

    while (true) {
        unsigned long currentTime = millis();
        unsigned long elapsedTime = currentTime - sensorState.lastCheckTime;
        sensorState.lastCheckTime = currentTime;
        sensorState.totalWaitTime += elapsedTime;

        // Check for overall timeout
        if (sensorState.totalWaitTime >= config.TIMEOUT_DURATION) {
            printAndPublish(isConnected, "Timeout waiting for sensor state - going to sleep");
            vTaskDelete(blinkTask);
            return;
        }

        // Read current sensor state and add to rolling average
        bool rawSensorState = (digitalRead(GPIO_NUM_33) == desiredState);
        rollingState.addReading(rawSensorState);

        // Get the debounced state based on rolling average
        bool currentSensorState = rollingState.getCurrentState();

        // If we're in the desired state, accumulate time
        if (currentSensorState) {
            sensorState.accumulatedTime += elapsedTime;
        } else {
            // Optional: Could add partial credit for readings close to threshold
            float stateAverage = rollingState.getAverage();
            if (stateAverage > config.STATE_THRESHOLD * 0.8) {  // Close to threshold
                sensorState.accumulatedTime += elapsedTime / 2;  // Add partial credit
            }
        }

        // Print status updates
        if (currentTime - sensorState.lastPrintTime >= config.PRINT_INTERVAL) {
            int remainingTime = (duration - sensorState.accumulatedTime) / 1000;
            int timeoutRemaining = (config.TIMEOUT_DURATION - sensorState.totalWaitTime) / 1000;

            printAndPublish(isConnected,
                            "Sensor state: raw=%d, averaged=%d (%.2f%%), accumulated: %lu ms, need %d more seconds (timeout in %d seconds)",
                            rawSensorState,
                            currentSensorState,
                            rollingState.getAverage() * 100,
                            sensorState.accumulatedTime,
                            remainingTime,
                            timeoutRemaining);

            sensorState.lastPrintTime = currentTime;
        }

        // Check if we've reached the target duration
        if (sensorState.accumulatedTime >= duration) {
            printAndPublish(isConnected, message);
            vTaskDelete(blinkTask);
            break;
        }

        delay(config.CHECK_INTERVAL);
    }
}

void handleLowState() {
    if (digitalRead(GPIO_NUM_33) == LOW && trigger) {
        notifyWaterGone();
        printAndPublish(true, "ending wake up sequence");
        sendStatusToAPI("end");
        reset_alarm_state();
        trigger = 0;
        esp_restart();
    }
}

class SensorReader {
private:
    const uint8_t pin;
    const unsigned long debounceTime;
    unsigned long lastReadTime = 0;
    bool lastStableState = false;
    const unsigned int numSamples;
    const unsigned long sampleInterval;

public:
    SensorReader(uint8_t sensorPin,
                 unsigned long debounceMs = 50,
                 unsigned int samples = 5,
                 unsigned long intervalMs = 1)
            : pin(sensorPin), debounceTime(debounceMs), numSamples(samples), sampleInterval(intervalMs) {
    }

    bool read() {
        unsigned long currentTime = millis();

        // Enforce minimum time between readings
        if (currentTime - lastReadTime < debounceTime) {
            return lastStableState;
        }

        // Take multiple samples
        unsigned int highCount = 0;
        for (unsigned int i = 0; i < numSamples; i++) {
            if (digitalRead(pin) == HIGH) {
                highCount++;
            }
            if (sampleInterval > 0) {
                delay(sampleInterval);
            }
        }

        // Update state only if we have a clear majority
        bool newState = (highCount > numSamples / 2);

        if (newState != lastStableState) {
            // State changed - update timestamp
            lastReadTime = currentTime;
        }

        lastStableState = newState;
        return newState;
    }

    void calibrate() {
        // Initial calibration - take several readings to establish stable state
        bool initialState = read();
        delay(debounceTime);

        // Make sure we have consistent readings
        for (int i = 0; i < 3; i++) {
            if (read() != initialState) {
                // If inconsistent, reset and try again
                delay(debounceTime);
                i = -1;
                initialState = read();
            }
        }

        lastStableState = initialState;
    }
};

void processTrigger() {
    Serial.println("processTrigger called - waiting for stable state");

    const unsigned long STABILITY_CHECK_DURATION = 15000;  // 15 seconds total timeout
    const size_t STABILITY_BUFFER_SIZE = 10;              // Store 10 readings
    const float STABILITY_THRESHOLD = 0.9;                // 90% must be HIGH
    const unsigned long SAMPLING_INTERVAL = 1000;         // Take reading every second
    const size_t MINIMUM_SAMPLES_REQUIRED = 10;           // Need all 10 readings before decision

    RollingSensorState<STABILITY_BUFFER_SIZE> stabilityState(STABILITY_THRESHOLD, MINIMUM_SAMPLES_REQUIRED);

    unsigned long stabilityStartTime = millis();
    unsigned long lastSampleTime = 0;
    bool stabilityAchieved = false;
    int totalSamples = 0;

    Serial.println("Beginning stability check...");

    while (millis() - stabilityStartTime < STABILITY_CHECK_DURATION) {
        unsigned long currentTime = millis();

        // Only take readings at the specified interval
        if (currentTime - lastSampleTime >= SAMPLING_INTERVAL) {
            bool rawReading = (digitalRead(GPIO_NUM_33) == HIGH);
            stabilityState.addReading(rawReading);
            lastSampleTime = currentTime;
            totalSamples++;

            // Print status after each reading
            Serial.printf("Stability check: Reading %d/10: %s, Average: %.2f%% HIGH (need %.2f%%)\n",
                totalSamples,
                rawReading ? "HIGH" : "LOW",
                stabilityState.getAverage() * 100,
                STABILITY_THRESHOLD * 100);

            // Check for stability once we have all 10 samples
            if (totalSamples >= MINIMUM_SAMPLES_REQUIRED) {
                if (stabilityState.getCurrentState() && stabilityState.getAverage() >= STABILITY_THRESHOLD) {
                    stabilityAchieved = true;
                    Serial.printf("Stable HIGH state achieved after %d seconds!\n", totalSamples);
                    break;
                }
                // If we have all samples but didn't achieve stability, we can exit early
                if (stabilityState.getAverage() < STABILITY_THRESHOLD) {
                    Serial.println("Failed to achieve stability after all samples collected");
                    break;
                }
            }
        }

        delay(50); // Small delay to prevent tight looping
    }

    if (!stabilityAchieved) {
        Serial.println("Could not achieve stable HIGH state - aborting trigger");
        esp_task_wdt_delete(NULL); // Remove this task from watchdog monitoring
        return;
    }

    Serial.println("Proceeding with main trigger logic");

    int value;
    int loop = 0;
    const size_t BUFFER_SIZE = 20;           // Increased buffer size
    const float STATE_THRESHOLD = 0.7;

    static bool connectivityDaemonCreated = false;
    static bool ledDaemonCreated = false;

    // Variables for tracking LOW state duration
    static unsigned long lowStateStartTime = 0;
    static bool inLowState = false;
    esp_task_wdt_add(NULL); // Add current task to watchdog
    esp_task_wdt_reset();   // Reset the watchdog timer

    // Initialize sensor reader with GPIO 33
    SensorReader sensor(GPIO_NUM_33, 100, 10, 1);
    sensor.calibrate();

    RollingSensorState<BUFFER_SIZE> rollingState(STATE_THRESHOLD);

    g_startStatusSent = false;
    trigger = 0;

    while (true) {
        // Reset watchdog in the main loop
        esp_task_wdt_reset();

        if (!was_alarm_triggered()) {
            digitalWrite(RELAY_PIN, HIGH);
            mark_alarm_triggered();
        }

        if (g_wifi_connected && !g_startStatusSent) {
            sendStatusToAPI("start");
            g_startStatusSent = true;
        }

        bool rawSensorState = sensor.read();
        rollingState.addReading(rawSensorState);

        if (!rollingState.getCurrentState()) {
            if (!inLowState) {
                lowStateStartTime = millis();
                inLowState = true;
                Serial.println("Entered LOW state - starting 30 second timer");
            }

            // Only break if we've been in LOW state for at least 30 seconds
            if (millis() - lowStateStartTime >= 30000) {
                Serial.println("LOW state maintained for 30 seconds - breaking loop");
                break;
            }
        } else {
            if (inLowState) {
                Serial.println("HIGH state detected - resetting 30 second timer");
                inLowState = false;
            }
        }

        if (!ledDaemonCreated) {
            xTaskCreate(blinkLed, "BL", 1000, nullptr, 50, nullptr);
            ledDaemonCreated = true;
        }
        if (!connectivityDaemonCreated) {
            xTaskCreate(connectivityDaemonTask, "CD", 10000, nullptr, 1, nullptr);
            connectivityDaemonCreated = true;
        }

        char message[128];
        snprintf(message, sizeof(message),
                 "Loop iteration: %d, Raw: %d, Avg: %.2f%%",
                 loop, rawSensorState, rollingState.getAverage() * 100);
        printAndPublish(true, message);

        trigger = 1;
        loop++;
        delay(3000);
    }

    handleLowState();

    xQueueReceive(queue, &value, 0);
    printAndPublish(true, "restarting esp to clean up the resources");

    // Remove this task from watchdog monitoring before exiting
    esp_task_wdt_delete(NULL);
}

BLYNK_WRITE(V0) {
    int pinValue = param.asInt();
    if (pinValue == 0) {
        digitalWrite(RELAY_PIN, LOW);
    }
}

void esp_woke_up() {
    int value = 1;
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
        ESP.restart();
    }

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
        reset_reason != ESP_RST_PANIC && reset_reason != ESP_RST_BROWNOUT) {
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
    while(!Serial && millis() < 5000); 

    check_reset_reason();
    esp_task_wdt_init(WDT_TIMEOUT, true);

    espClient.setCACert(root_ca);
    client.setServer(mqtt_server, mqtt_port);

    pinMode(LED_PIN, OUTPUT);
    pinMode(GPIO_NUM_33, INPUT_PULLDOWN);
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);
    pinMode(RELAY_TURN_OFF_BUTTON, INPUT_PULLDOWN);

    // Only create the button monitor task if this wasn't an alarm trigger
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
    esp_sleep_enable_timer_wakeup(DAILY_RESTART_INTERVAL_US);
    digitalWrite(LED_PIN, HIGH);
    esp_woke_up();

    printAndPublish(false, "going to sleep now");
    esp_deep_sleep_start();
}

void loop() {
}
