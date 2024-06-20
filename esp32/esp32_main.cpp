#define BLYNK_TEMPLATE_ID "TMPL3SLz34feC"
#define BLYNK_TEMPLATE_NAME "esp32"
#define BLYNK_AUTH_TOKEN "3lXKsv3mOe-MqJX8ML_3jJNpRsOBpfxl"
#define DEBOUNCE_DELAY 25  // Debounce delay in milliseconds

#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include <string.h>
#include <sys/time.h>
#include "ezTime.h"
#include "ArduinoJson.h"
#include "BlynkSimpleEsp32.h"
#include <math.h>
#include <WiFi.h>
#include "PubSubClient.h"
#include <WiFiClientSecure.h>
#include "esp_err.h"
#include "soc/efuse_reg.h"


const char *ssid = "SamirK"; // TODO: Change to your WiFi SSID
const char *password = "12345678998";
const char *auth = "3lXKsv3mOe-MqJX8ML_3jJNpRsOBpfxl";
const char *tapWaterAPI = "https://ku1mhk4fw0.execute-api.ap-south-1.amazonaws.com/default/tapwater";
const char *startTimeAPI = "https://ku1mhk4fw0.execute-api.ap-south-1.amazonaws.com/default/tapwater/start";

volatile int trigger = 0;
const int LED_PIN = 14;
const int RELAY_PIN = 25;
const int RELAY_TURN_OFF_BUTTON = 26;
volatile bool shouldBlink = false;

const char* mqtt_server = "7fce715a29af4da092c7cba101b0954d.s1.eu.hivemq.cloud";
const char* mqtt_topic = "tapwater";
const char* mqtt_username = "samirkape31";
const char* mqtt_password = "@Vpceh31en";
const int mqtt_port = 8883;

WiFiClientSecure espClient;
PubSubClient client(espClient);

QueueHandle_t queue;

static const char *root_ca PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
)EOF";


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

String currentISTTime()
{
    String timeFormat = "g:i A";
    Timezone India;
    India.setLocation(F("Asia/Kolkata"));
    String localTime = India.dateTime(timeFormat);
    return localTime;
}


void publishMessage(const char* message) {
    if (!client.connected()) {
        reconnect();
    }
    client.publish(mqtt_topic, message);
}

void printAndPublish(const char* format, ...) {
    // Get current time
    String currentTime = currentISTTime();

    // Prepare the message
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    // Prepend timestamp to the message
    String message = "[" + currentTime + "] " + buffer;

    // Print and publish the message
    Serial.println(message);
    publishMessage(message.c_str());
}

void notfiyWaterGone()
{
    printAndPublish("Water is gone");
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
    vTaskDelete(NULL);
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


String getDateTimeForFormat(const String& format)
{
    Timezone India;
    India.setLocation(F("Asia/Kolkata"));
    String dateTime = India.dateTime(format);
    return dateTime;
}



String createJsonDataForTapwater(const String &startTime, const String &endTime, int duration, String date)
{
    JsonDocument jsonDoc;
    if (startTime != "")
    {
        jsonDoc["start_time"] = startTime;
    }
    if (endTime != "")
    {
        jsonDoc["end_time"] = endTime;
    }
    if (duration != 0)
    {
        jsonDoc["duration"] = duration;
    }
    if (date != "")
    {
        jsonDoc["date"] = date;
    }

    String jsonData;
    serializeJson(jsonDoc, jsonData);
    printAndPublish("JsonDataForTapwater: %s", jsonData.c_str());
    return jsonData;
}

String createJsonDataForStartTime(unsigned long startTime, const String &date)
{
    JsonDocument jsonDoc;
    jsonDoc["start_time"] = startTime;
    jsonDoc["date"] = date;

    String jsonString;
    serializeJson(jsonDoc, jsonString);
    printAndPublish("JsonDataForStartTime: %s",jsonString.c_str());
    return jsonString;
}

int64_t parseResponse(String response)
{
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);

    if (error)
    {
        printAndPublish("deserializeJson() failed: %s", error.c_str());
        return 0;
    }

    printAndPublish("Parsed start time respose: %s", response.c_str());

    int startTime = doc["start_time"];
    if (startTime != 0)
    {
        printAndPublish("Start Time: %d", startTime);
    }
    else
    {
        printAndPublish("Start time not found in response");
        return 0;
    }

    return startTime;
}

int64_t getTapWaterStartTime(String date, String startTimeAPI)
{
    HTTPClient client;
    String response;

    String url = String(startTimeAPI) + "?date=" + date;
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
    http.begin(apiUrl);

    http.addHeader("Content-Type", "application/json");
    printAndPublish("Sending HTTP POST request...");
    int httpResponseCode = http.POST(jsonData);

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
    String id = makePOSTRequest(jsonData, tapWaterAPI);
    return id;
}

void startWireless()
{
    delay(5000);
    Serial.println("Connecting to WiFi After Waking up...");
    Blynk.begin(auth, ssid, password);
    waitForSync();
    espClient.setCACert(root_ca);
    client.setServer(mqtt_server, mqtt_port);
    xTaskCreate(customLoop, "customLoop", 30000, NULL, 1, NULL);
    printAndPublish("Connected to WiFi");
}

int calculateElapsedTime(int64_t start_millis, unsigned long end_millis)
{
    const unsigned long max_unsigned_long = 4294967295;
    unsigned long elapsed_millis;
    if (end_millis < start_millis)
    {
        elapsed_millis = (max_unsigned_long - start_millis) + end_millis;
    }
    else
    {
        elapsed_millis = end_millis - start_millis;
    }
    double elapsed_minutes = (double)elapsed_millis / (double)(1000 * 60);
    return std::ceil(elapsed_minutes);
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

void processTrigger() {
    String start_time, end_time;
    unsigned long lowStartTime = 0;
    unsigned long highStartTime = 0;
    String startTime;
    String id;
    int value;

    // confirm that the sensor is low
    while (true) {
        if (digitalRead(GPIO_NUM_33) == HIGH) {
            if (highStartTime == 0) {
                highStartTime = millis();
            } else if (millis() - highStartTime >= 10000) {
                Serial.println("sensor was HIGH for last 10 seconds, triggering wake up sequence");
                highStartTime = 0;
                break;
            }
            delay(2000);
            int remainingTime = 10 - ((millis() - highStartTime) / 1000); // Calculate the remaining time
            Serial.printf("sensor is HIGH, waiting for %d seconds to trigger wake up sequence\n", remainingTime);
        } else {
            highStartTime = 0; // Reset the timer when GPIO_NUM_33 goes HIGH
        }
    }

    while (digitalRead(GPIO_NUM_33) == HIGH)
    {
        if (trigger == 0)
        {
            Serial.println("starting wake up sequence");
            shouldBlink = true;
            xTaskCreate(triggerSound, "triggerSound", 1000, NULL, 1, NULL);
            xTaskCreate(blinkLed, "blinkLed", 1000, NULL, 1, NULL);
            startWireless();
            unsigned long startTimeMillis = millis();
            start_time = getDateTimeForFormat("g:i A"); // Get the current time
            String date = getDateTimeForFormat("d-M-Y"); // Get the current date
            int64_t startTime = getTapWaterStartTime(date, startTimeAPI);

            if (startTime == 0)
            {
                String jsonData = createJsonDataForTapwater(start_time, "", 0, getDateTimeForFormat("D, d-M-Y"));
                createTapwaterRecord(jsonData, tapWaterAPI);
                String startTimeDBJson = createJsonDataForStartTime(startTimeMillis, date);
                makePOSTRequest(startTimeDBJson, startTimeAPI);
            }
        }
        trigger = 1;
        printAndPublish("sensor is HIGH");
        delay(2000);
    }

    // confirm that the sensor is low
    while (true) {
        if (digitalRead(GPIO_NUM_33) == LOW) {
            if (lowStartTime == 0) { // Start the timer when GPIO_NUM_33 first goes LOW
                lowStartTime = millis();
            } else if (millis() - lowStartTime >= 30000) { // Check if GPIO_NUM_33 has been LOW for 5 seconds
                printAndPublish("sensor was LOW for last 30 seconds, ending wake up sequence");
                lowStartTime = 0;
                break;
            }
            delay(2000);
            int remainingTime = 30 - ((millis() - lowStartTime) / 1000); // Calculate the remaining time
            printAndPublish("sensor is LOW, waiting for %d seconds to end wake up sequence", remainingTime);
        } else {
            lowStartTime = 0; // Reset the timer when GPIO_NUM_33 goes HIGH
        }
    }

    if (digitalRead(GPIO_NUM_33) == LOW && trigger)
    {
        notfiyWaterGone();
        printAndPublish("ending wake up sequence");
        unsigned long endTimeMillis = millis();

        end_time = getDateTimeForFormat("g:i A");
        String weekday = getDateTimeForFormat("l");
        String date = getDateTimeForFormat("d-M-Y");

        int64_t startTimeLong = getTapWaterStartTime(date, startTimeAPI);

        int elapsedTime = calculateElapsedTime(startTimeLong, endTimeMillis);
        printAndPublish("elapsedTime: %d", elapsedTime);
        String jsonData = createJsonDataForTapwater(start_time, end_time, elapsedTime, getDateTimeForFormat("D, d-M-Y"));
        createTapwaterRecord(jsonData, tapWaterAPI);

        trigger = 0;
    }
    xQueueReceive(queue, &value, 0);
    printAndPublish("Releasing queue value: %d\n", value);
    shouldBlink = false;
}

BLYNK_WRITE(V0)
{
    int pinValue = param.asInt();
    if (pinValue == 0)
    {
        printAndPublish("Received Blynk call to stop the alarm");
        digitalWrite(RELAY_PIN, LOW);
    }
}

void sensor_woke_up()
{
    int value = 1;
    if(xQueueSend(queue, &value, pdMS_TO_TICKS(60000)) == pdPASS) {
        if (digitalRead(GPIO_NUM_33) == HIGH ){
            Serial.println("processTrigger called from sensor_woke_up");
            processTrigger();
        } else {
            Serial.println("waiting for sensor to be HIGH");
            xQueueReceive(queue, &value, 0);
            Serial.printf("Releasing queue value: %d\n", value);
        }
    } else {
        Serial.println("waiting for queue to be empty");
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

    Serial.println("Going to sleep now");
    esp_deep_sleep_start();
}

void loop() {}