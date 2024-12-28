#include "secrets.h"
#include <HTTPClient.h>
#include <string.h>
#include "ezTime.h"
#include "ArduinoJson.h"
#include "BlynkSimpleEsp32.h"
#include "PubSubClient.h"
#include <WiFiClientSecure.h>
#include <Preferences.h>

#include <utility>
#include "esp_err.h"

#include "esp_task_wdt.h"

volatile int trigger = 0;
const int LED_PIN = 14;
const int RELAY_PIN = 25;
const int RELAY_TURN_OFF_BUTTON = 26;
volatile bool shouldBlink = false;
const int mqtt_port = 8883;

WiFiClientSecure espClient;
PubSubClient client(espClient);
Timezone India;
Preferences preferences;

bool enableDebug = true; // TODO
volatile bool isConnected = false;
TaskHandle_t mqttTaskHandle = NULL;
volatile bool mqttConnected = false;

QueueHandle_t queue;

String getDateTimeForFormat(const String &format) {
    String dateTime = India.dateTime(format);
    return dateTime;
}

void reconnect() {
    Serial.print("Attempting MQTT connection… ");
    String clientId = "ESP32Client";
    if (client.connect(clientId.c_str(), mqtt_username, mqtt_password)) {
        Serial.println("connected!");
        client.publish(mqtt_topic, "Sent from the other world");
        mqttConnected = true;
        client.subscribe(mqtt_topic);
    } else {
        Serial.print("failed, rc = ");
        Serial.print(client.state());
        Serial.println(" try again in 5 seconds");
        delay(5000);
    }
}

void publishMessage(const char *message) {
    if (!client.connected()) {
        reconnect();
    }
    client.publish(mqtt_topic, message);
}

void printAndPublish(const char *format = "", ...) {
    // Get current time
    String currentTime = getDateTimeForFormat("g:i:s A");

    // Prepare the message
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    // Prepend timestamp to the message
    String message = "[" + currentTime + "] " + buffer;
    if (enableDebug) {
        Serial.println(message);
    }
    if (isConnected && mqttConnected) {
        publishMessage(message.c_str());
    }
}

void notifyWaterGone() {
    printAndPublish("water has gone at: %s", getDateTimeForFormat("g:i A").c_str());
    digitalWrite(RELAY_PIN, HIGH);
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    digitalWrite(RELAY_PIN, LOW);
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
    while (isConnected) {
        Blynk.run();
        vTaskDelay(20 / portTICK_PERIOD_MS); // Delay for a short while to allow other tasks to run
    }
    printAndPublish("Releasing Blynk and HiveMqtt Task");
    vTaskDelete(nullptr);
}

int calculateDurationInMinutes(const char *startTime, const char *endTime) {
    int hour1, minute1, hour2, minute2;

    // Parse the input times
    sscanf(startTime, "%d:%d", &hour1, &minute1);
    sscanf(endTime, "%d:%d", &hour2, &minute2);

    // Convert to minutes past midnight
    int startTimeInMinutes = hour1 * 60 + minute1;
    int endTimeInMinutes = hour2 * 60 + minute2;

    // Calculate the duration
    int duration = endTimeInMinutes - startTimeInMinutes;
    if (duration < 0)
        duration += 24 * 60; // If the duration is negative, add 24 hours to get the duration for the next day

    return duration;
}

String createJsonDataForTapwater(const String &startTime, const String &endTime, int duration, const String &date) {
    JsonDocument jsonDoc;

    if (!startTime.isEmpty()) jsonDoc["start_time"] = startTime;
    if (!endTime.isEmpty()) jsonDoc["end_time"] = endTime;
    if (duration > 0) jsonDoc["duration"] = duration;
    if (!date.isEmpty()) jsonDoc["date"] = date;

    String jsonData;
    serializeJson(jsonDoc, jsonData);

    if (jsonData.length() > 0) {
        printAndPublish("JsonDataForTapwater: %s", jsonData.c_str());
    } else {
        printAndPublish("Failed to serialize JSON for tap water data");
        return "";
    }

    return jsonData;
}

String createJsonDataForStartTime(const String &startTime, const String &date) {
    JsonDocument jsonDoc;
    jsonDoc["start_time"] = startTime;
    jsonDoc["date"] = date;

    String jsonString;
    serializeJson(jsonDoc, jsonString);
    printAndPublish("JsonDataForStartTime: %s", jsonString.c_str());
    return jsonString;
}

String parseResponse(String response) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);

    if (error) {
        printAndPublish("deserializeJson() failed: %s", error.c_str());
        return "";
    }

    printAndPublish("Parsed start time respose: %s", response.c_str());

    String startTime = doc["start_time"];
    printAndPublish("decoded start_time: %s", startTime.c_str());

    if (startTime != "") {
        printAndPublish("Start Time: %s", startTime.c_str());
    } else {
        printAndPublish("Start time not found in response");
        return "";
    }

    return startTime.c_str();
}

String getTapWaterStartTime(const String &date) {
    HTTPClient client;
    String response;

    String url = String(startTimeAPI) + "?date=" + date;
    printAndPublish("getTapWaterStartTime: %s", url.c_str());

    client.begin(url);

    client.addHeader("Content-Type", "application/json");

    int httpResponseCode = client.GET();

    if (httpResponseCode == 200) {
        response = client.getString();
        printAndPublish("Response: %s", response.c_str());
    } else {
        printAndPublish("Error request failed, code: %s", String(httpResponseCode).c_str());
        return "";
    }

    client.end();
    return parseResponse(response);
}

String makePOSTRequest(const String& jsonData, const String& apiUrl) {
    HTTPClient http;
    String payload;

    printAndPublish("Connecting to API endpoint...");
    if (http.begin(apiUrl)) { // Make sure begin() succeeded
        http.addHeader("Content-Type", "application/json");
        printAndPublish("Sending HTTP POST request...");
        int httpResponseCode = http.POST(jsonData);

        if (httpResponseCode >= 200 && httpResponseCode < 300) { // Handle all success codes
            printAndPublish("HTTP Response code: %d", httpResponseCode);
            payload = http.getString();
            printAndPublish("makePOSTRequest payload: %s", payload.c_str());
        } else {
            printAndPublish("Error code: %d", httpResponseCode);
            payload = ""; // Ensure payload is cleared in case of error
        }

        http.end();
    } else {
        printAndPublish("Failed to connect to API endpoint");
        payload = ""; // Ensure payload is cleared in case of connection failure
    }

    return payload;
}


String createTapwaterRecord(String jsonData) {
    printAndPublish("Sending API request...");
    String id = makePOSTRequest(jsonData, tapWaterAPI);
    return id;
}


void makeHttpCall() {
    // check if the start time is already present in the database
    printAndPublish("handleHighState: Checking for start time in database");
    String date = getDateTimeForFormat("d-M-Y"); // Get the current date
    printAndPublish("Current date: %s", date.c_str());
    String startTime = getTapWaterStartTime(date);
    printAndPublish("Start time from database: %s", startTime.c_str());
    printAndPublish("startTime: %d", startTime.isEmpty());

    if (startTime == "") {
        printAndPublish("handleHighState: Start time not found, creating new records");
        // If the start time is not present in the database, create a new tapwater record
        String timeNow = getDateTimeForFormat("g:i A"); // Get the current time in 12-hour format
        printAndPublish("Current time: %s", timeNow.c_str());
        String jsonData = createJsonDataForTapwater(timeNow, "", 0, getDateTimeForFormat("D, d-M-Y"));
        printAndPublish("Json data for createJsonDataForTapwater: %s", jsonData.c_str());
        createTapwaterRecord(jsonData);

        // Also, create a new start time record
        String startTime24hr = getDateTimeForFormat("H:i"); // 24-hour format
        printAndPublish("startTime24hr: %s", startTime24hr.c_str());
        String startTimeDBJson = createJsonDataForStartTime(startTime24hr, date);
        printAndPublish("Json data for createJsonDataForStartTime: %s", startTimeDBJson.c_str());
        makePOSTRequest(startTimeDBJson, startTimeAPI);
    }
}


void startWireless() {
    String clientId = "ESP32Client";
    Blynk.begin(auth, ssid, password);
    waitForSync();
    India.setLocation(F("Asia/Kolkata"));
    setInterval(300);
    xTaskCreate(customLoop, "customLoop", 8000, NULL, 1, NULL);
    printAndPublish("Connected to WiFi, Blynk and Mqtt client");
}

TaskHandle_t wifiTaskHandle = NULL;

void wifiTask(void *pvParameters) {
    for (;;) {
        if (isConnected) {
            Serial.println("Attempting to start wireless connection...");
            startWireless();
            makeHttpCall();
            break;
        } else if (!shouldBlink) {
            break;
        } else {
            WiFi.begin(ssid, password);
        }
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
    vTaskDelete(nullptr);
}

void onWiFiEvent(WiFiEvent_t event) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            isConnected = false;
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            printAndPublish("[onWiFiEvent] WiFi reconnect succeeded");
            isConnected = true;
            break;
        default:
            break;
    }
}

void triggerSound() {
    digitalWrite(RELAY_PIN, HIGH);
}

void IRAM_ATTR triggerSoundOff() {
    gpio_set_level((gpio_num_t) RELAY_PIN, 0);
}

void waitForSensorState(int state, unsigned long duration, const char *message) {
    unsigned long startTime = 0;
    while (true) {
        if (digitalRead(GPIO_NUM_33) == state) {
            if (startTime == 0) {
                startTime = millis();
            } else if (millis() - startTime >= duration) {
                printAndPublish(message);
                startTime = 0;
                break;
            }
            delay(2000);
            int remainingTime = (duration / 1000) - ((millis() - startTime) / 1000); // Calculate the remaining time
            printAndPublish("sensor is %s, waiting for %d seconds", state == HIGH ? "HIGH" : "LOW",
                            remainingTime);
        } else {
            startTime = 0; // Reset the timer when GPIO_NUM_33 changes state
        }
    }
}

void handleHighState() {
    if (trigger == 0) {
        shouldBlink = true;
        xTaskCreate(blinkLed, "blinkLed", 1000, NULL, 1, NULL);
        bool alreadyAlarmed = preferences.getBool("alreadyAlarmed", false);

        // trigger sound and start blink led
        if (!alreadyAlarmed) {
            preferences.putBool("alreadyAlarmed", true);
            triggerSound();
        }
    }
    trigger = 1;
    printAndPublish("sensor is in HIGH state");
    delay(5000);
}

// convert string formatted 24 hour time to 12-hour time format
String convert24To12(String time) {
    int hour, minute;
    sscanf(time.c_str(), "%d:%d", &hour, &minute);
    String suffix = hour >= 12 ? "PM" : "AM";
    hour = hour % 12;
    hour = hour ? hour : 12;
    return String(hour) + ":" + String(minute) + " " + suffix;
}

void handleLowState() {
    if (digitalRead(GPIO_NUM_33) == LOW && trigger) {
        preferences.putBool("alreadyAlarmed", false);
        shouldBlink = false;
        notifyWaterGone();
        printAndPublish("ending wake up sequence");
        String endTime = getDateTimeForFormat("g:i A");
        String weekday = getDateTimeForFormat("l");
        String date = getDateTimeForFormat("d-M-Y");

        String endTime24hr = getDateTimeForFormat("H:i");
        String startTime24hr = getTapWaterStartTime(date);

        int elapsedTime = calculateDurationInMinutes(startTime24hr.c_str(), endTime24hr.c_str());
        printAndPublish("elapsedTime: %d", elapsedTime);
        String jsonData = createJsonDataForTapwater(convert24To12(startTime24hr), endTime, elapsedTime,
                                                    getDateTimeForFormat("D, d-M-Y"));
        printAndPublish("Json data for createJsonDataForTapwater: %s", jsonData.c_str());
        createTapwaterRecord(jsonData);
        printAndPublish("preferences.putBool: removing flag from eeprom");
        trigger = 0;
    }
}

void killAllTasks() {
    printAndPublish("killing wifi and mqtt");

    // Delete known tasks first
    if (wifiTaskHandle != NULL) {
        vTaskDelete(wifiTaskHandle);
        wifiTaskHandle = NULL;
    }
    if (mqttTaskHandle != NULL) {
        vTaskDelete(mqttTaskHandle);
        mqttTaskHandle = NULL;
    }
}


void mqttTask(void *pvParameters) {
    const int MQTT_RECONNECT_DELAY = 10000; // 5 seconds delay between reconnection attempts
    String clientId = "espClient";
    espClient.setCACert(root_ca);
    client.setServer(mqtt_server, mqtt_port);

    for (;;) {
        if (!client.connected() && isConnected) {
            mqttConnected = false;
            if (client.connect(clientId.c_str(), mqtt_username, mqtt_password)) {
                mqttConnected = true;
            } else {
                Serial.println("Trying again in 5 seconds");
            }
        }

        if (!mqttConnected) {
            vTaskDelay(pdMS_TO_TICKS(MQTT_RECONNECT_DELAY));
        } else {
            client.loop();
            vTaskDelay(pdMS_TO_TICKS(100)); // Small delay to prevent task from hogging CPU
        }
    }
}

void processTrigger() {
    int value;
    // confirm that the sensor is high
    waitForSensorState(HIGH, 20000, "sensor was HIGH for last 10 seconds, triggering wake up sequence");
    esp_task_wdt_init(15, true);
    WiFi.begin(ssid, password);
    xTaskCreate(wifiTask, "WiFiTask", 40000, NULL, 1, &wifiTaskHandle);
    xTaskCreate(mqttTask, "MqttTask", 10000, NULL, 1, &mqttTaskHandle);

    while (digitalRead(GPIO_NUM_33) == HIGH) {
        handleHighState();
    }

    // confirm that the sensor is low
    waitForSensorState(LOW, 35000, "sensor was LOW for last 35 seconds, ending wake up sequence");
    handleLowState();

    xQueueReceive(queue, &value, 0);
    printAndPublish("releasing queue value: %d", value);
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
    if (xQueueSend(queue, &value, pdMS_TO_TICKS(60000)) == pdPASS) {
        if (digitalRead(GPIO_NUM_33) == HIGH) {
            printAndPublish("processTrigger called from sensor_woke_up");
            processTrigger();
        } else {
            printAndPublish("waiting for sensor to be HIGH");
            xQueueReceive(queue, &value, 0);
            printAndPublish("releasing queue value: %d", value);
        }
    } else {
        printAndPublish("waiting for queue to be empty");
    }
}

void setup() {
    Serial.begin(115200);
    esp_log_level_set("*", ESP_LOG_NONE);
    WiFi.onEvent(onWiFiEvent);

    pinMode(LED_PIN, OUTPUT); // pin 14 led
    digitalWrite(LED_PIN, LOW);

    pinMode(GPIO_NUM_33, INPUT_PULLDOWN); // pin 33 sensor

    pinMode(RELAY_PIN, OUTPUT); // pin 5 relay
    digitalWrite(RELAY_PIN, LOW);

    pinMode(RELAY_TURN_OFF_BUTTON, INPUT_PULLDOWN); // pin 26 button
    attachInterrupt(RELAY_TURN_OFF_BUTTON, triggerSoundOff, RISING);

    queue = xQueueCreate(1, sizeof(int));

    esp_sleep_enable_ext0_wakeup(GPIO_NUM_33, 1);
    sensor_woke_up();

    Serial.println("enabling deep-sleep mode");

    bool alreadyAlarmed = preferences.getBool("alreadyAlarmed", false);
    if (alreadyAlarmed) {
        printAndPublish("erasing sound flag");
        preferences.putBool("alreadyAlarmed", false);
    }
    killAllTasks();
    printAndPublish("going to sleep now");
    esp_task_wdt_deinit();
    esp_deep_sleep_start();
}

void loop() {}
