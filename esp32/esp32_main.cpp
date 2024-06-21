#include "secrets.h"
#include <HTTPClient.h>
#include <string.h>
#include "ezTime.h"
#include "ArduinoJson.h"
#include "BlynkSimpleEsp32.h"
#include "PubSubClient.h"
#include <WiFiClientSecure.h>

#include <utility>
#include "esp_err.h"

volatile int trigger = 0;
const int LED_PIN = 14;
const int RELAY_PIN = 25;
const int RELAY_TURN_OFF_BUTTON = 26;
volatile bool shouldBlink = false;
const int mqtt_port = 8883;

WiFiClientSecure espClient;
PubSubClient client(espClient);
Timezone India;

bool enableDebug = false;

QueueHandle_t queue;

String getDateTimeForFormat(const String& format)
{
    String dateTime = India.dateTime(format);
    return dateTime;
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

void printAndPublish(bool isConnected = true, const char* format = "", ...) {
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
    if (isConnected) {
        publishMessage(message.c_str());
    }
}

void notifyWaterGone()
{
    printAndPublish("water has gone at: ", getDateTimeForFormat("g:i A").c_str());
    digitalWrite(RELAY_PIN, HIGH);
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    digitalWrite(RELAY_PIN, LOW);
}

void blinkLed(void *parameter)
{
    pinMode(LED_PIN, OUTPUT);
    while (shouldBlink)
    {
        digitalWrite(LED_PIN, HIGH);
        vTaskDelay(500 / portTICK_PERIOD_MS);
        digitalWrite(LED_PIN, LOW);
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
    digitalWrite(LED_PIN, LOW);
    vTaskDelete(nullptr);
}

void customLoop(void *parameter)
{
    while (shouldBlink)
    {
        Blynk.run();
        client.loop();
        vTaskDelay(20 / portTICK_PERIOD_MS); // Delay for a short while to allow other tasks to run
    }
    printAndPublish("Releasing Blynk and HiveMqtt Task");
    vTaskDelete(nullptr);
}

int calculateDurationInMinutes(const char* startTime, const char* endTime) {
    int hour1, minute1, hour2, minute2;

    // Parse the input times
    sscanf(startTime, "%d:%d", &hour1, &minute1);
    sscanf(endTime, "%d:%d", &hour2, &minute2);

    // Convert to minutes past midnight
    int startTimeInMinutes = hour1 * 60 + minute1;
    int endTimeInMinutes = hour2 * 60 + minute2;

    // Calculate the duration
    int duration = endTimeInMinutes - startTimeInMinutes;
    if (duration < 0) duration += 24 * 60; // If the duration is negative, add 24 hours to get the duration for the next day

    return duration;
}

String createJsonDataForTapwater(const String &startTime, const String &endTime, int duration, const String &date)
{
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
    }

    return jsonData;
}

String createJsonDataForStartTime(const String& startTime, const String &date)
{
    JsonDocument jsonDoc;
    jsonDoc["start_time"] = startTime;
    jsonDoc["date"] = date;

    String jsonString;
    serializeJson(jsonDoc, jsonString);
    printAndPublish("JsonDataForStartTime: %s",jsonString.c_str());
    return jsonString;
}

String parseResponse(String response)
{
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);

    if (error)
    {
        printAndPublish("deserializeJson() failed: %s", error.c_str());
        return "";
    }

    printAndPublish("Parsed start time respose: %s", response.c_str());

    String startTime = doc["start_time"];
    if (startTime != "")
    {
        printAndPublish("Start Time: %s", startTime.c_str());
    }
    else
    {
        printAndPublish("Start time not found in response");
        return "";
    }

    return startTime;
}

String getTapWaterStartTime(const String& date, String startTimeAPI)
{
    HTTPClient client;
    String response;

    String url = String(std::move(startTimeAPI)) + "?date=" + date;
    client.begin(url);

    client.addHeader("Content-Type", "application/json");

    int httpResponseCode = client.GET();

    if (httpResponseCode > 0)
    {
        response = client.getString();
        printAndPublish("Response: %s", response.c_str());
    }
    else
    {
        printAndPublish("Error request failed, code: %s", String(httpResponseCode).c_str());
    }

    client.end();
    return parseResponse(response);
}

String makePOSTRequest(String jsonData, String apiUrl)
{
    HTTPClient http;
    String payload;

    printAndPublish("Connecting to API endpoint...");
    http.begin(std::move(apiUrl));

    http.addHeader("Content-Type", "application/json");
    printAndPublish("Sending HTTP POST request...");
    int httpResponseCode = http.POST(std::move(jsonData));

    if (httpResponseCode > 0)
    {
        printAndPublish("HTTP Response code: %s", String(httpResponseCode).c_str());
        payload = http.getString();
        printAndPublish(payload.c_str());
    }
    else
    {
        printAndPublish("Error code: %s", String(httpResponseCode).c_str());
    }

    http.end();
    return payload;
}

String createTapwaterRecord(String jsonData, String tapWaterAPI)
{
    printAndPublish("Sending API request...");
    String id = makePOSTRequest(std::move(jsonData), std::move(tapWaterAPI));
    return id;
}

void startWireless()
{
    delay(5000);
    Serial.println("Connecting to WiFi After Waking up...");
    Blynk.begin(auth, ssid, password);
    waitForSync();
    India.setLocation(F("Asia/Kolkata"));
    espClient.setCACert(root_ca);
    client.setServer(mqtt_server, mqtt_port);
    xTaskCreate(customLoop, "customLoop", 30000, NULL, 1, NULL);
    printAndPublish("Connected to WiFi");
}

void triggerSound(void *parameter)
{
    if (!shouldBlink)
    {
        digitalWrite(RELAY_PIN, HIGH);
        vTaskDelay(3000 / portTICK_PERIOD_MS);
        digitalWrite(RELAY_PIN, LOW);
    }
    else
    {
        digitalWrite(RELAY_PIN, HIGH);
    }
    vTaskDelete(nullptr);
}

void IRAM_ATTR triggerSoundOff()
{
    static unsigned long lastTriggerTime = 0;  // Last time the function was triggered
    unsigned long now = millis();

    // If not enough time has passed since the last trigger, return immediately
    if (now - lastTriggerTime < DEBOUNCE_DELAY) {
        return;
    }

    // Update the last trigger time
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
            int remainingTime = (duration / 1000) - ((millis() - startTime) / 1000); // Calculate the remaining time
            printAndPublish(isConnected,"sensor is %s, waiting for %d seconds\n", state == HIGH ? "HIGH" : "LOW", remainingTime);
        } else {
            startTime = 0; // Reset the timer when GPIO_NUM_33 changes state
        }
    }
}

void handleHighState() {
    if (trigger == 0)
    {
        shouldBlink = true;
        Serial.println("starting wake up sequence");

        // trigger sound and start blink led
        xTaskCreate(triggerSound, "triggerSound", 1000, NULL, 1, NULL);
        xTaskCreate(blinkLed, "blinkLed", 1000, NULL, 1, NULL);

        // start wireless connection
        startWireless();

        // check if the start time is already present in the database
        String date = getDateTimeForFormat("d-M-Y"); // Get the current date
        String startTime = getTapWaterStartTime(date, startTimeAPI);

        if (startTime == "")
        {
            // If the start time is not present in the database, create a new tapwater record
            String timeNow = getDateTimeForFormat("g:i A"); // Get the current time
            String jsonData = createJsonDataForTapwater(timeNow, "", 0, getDateTimeForFormat("D, d-M-Y"));
            createTapwaterRecord(jsonData, tapWaterAPI);

            // Also, create a new start time record
            String startTime24hr = getDateTimeForFormat("H:i"); // 24-hour format
            String startTimeDBJson = createJsonDataForStartTime(startTime24hr, date);
            makePOSTRequest(startTimeDBJson, startTimeAPI);
        }
    }
    trigger = 1;
    printAndPublish("sensor is HIGH");
    delay(2000);
}

void handleLowState() {
    if (digitalRead(GPIO_NUM_33) == LOW && trigger)
    {
        notifyWaterGone();
        printAndPublish(true,"ending wake up sequence");
        String endTime = getDateTimeForFormat("g:i A");
        String weekday = getDateTimeForFormat("l");
        String date = getDateTimeForFormat("d-M-Y");

        String endTime24hr = getDateTimeForFormat("H:i");
        String startTime24hr = getTapWaterStartTime(date, startTimeAPI);

        int elapsedTime = calculateDurationInMinutes(startTime24hr.c_str(), endTime24hr.c_str());
        printAndPublish(true, "elapsedTime: %d", elapsedTime);
        String jsonData = createJsonDataForTapwater("", endTime, elapsedTime, getDateTimeForFormat("D, d-M-Y"));
        createTapwaterRecord(jsonData, tapWaterAPI);

        trigger = 0;
    }
}

void processTrigger() {
    int value;

    // confirm that the sensor is high
    waitForSensorState(HIGH, 10000, "sensor was HIGH for last 10 seconds, triggering wake up sequence", false);

    while (digitalRead(GPIO_NUM_33) == HIGH)
    {
        handleHighState();
    }

    // confirm that the sensor is low
    waitForSensorState(LOW, 50000, "sensor was LOW for last 50 seconds, ending wake up sequence", true);

    handleLowState();

    xQueueReceive(queue, &value, 0);
    printAndPublish(true, "releasing queue value: %d\n", value);
    shouldBlink = false;
}

BLYNK_WRITE(V0)
{
    int pinValue = param.asInt();
    if (pinValue == 0)
    {
        printAndPublish("received Blynk call to stop the alarm");
        digitalWrite(RELAY_PIN, LOW);
    }
}

void sensor_woke_up()
{
    int value = 1;
    if(xQueueSend(queue, &value, pdMS_TO_TICKS(60000)) == pdPASS) {
        if (digitalRead(GPIO_NUM_33) == HIGH ){
            printAndPublish(false, "processTrigger called from sensor_woke_up");
            processTrigger();
        } else {
            printAndPublish(false,"waiting for sensor to be HIGH");
            xQueueReceive(queue, &value, 0);
            printAndPublish(false,"releasing queue value: %d\n", value);
        }
    } else {
        printAndPublish(false,"waiting for queue to be empty");
    }
}

void setup()
{
    Serial.begin(115200);

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

    printAndPublish(false,"going to sleep now");
    esp_deep_sleep_start();
}

void loop() {}