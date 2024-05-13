#define BLYNK_TEMPLATE_ID "TMPL3SLz34feC"
#define BLYNK_TEMPLATE_NAME "esp32"
#define BLYNK_AUTH_TOKEN "3lXKsv3mOe-MqJX8ML_3jJNpRsa42cwe"

#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include <string.h>
#include <sys/time.h>
#include <ezTime.h>
#include <ArduinoJson.h>
#include <BlynkSimpleEsp32.h>
#include <math.h>

// Obscured sensitive information
const char *ssid = "SamirK";
const char *password = "12345678998";
const char *auth = "3lXKsv3mOe-MqJX8ML_3jJNpRsO";
const char *tapWaterAPI = "https://ku1m.execute-api.ap-south-1.amazonaws.com/default/tapwater";
const char *startTimeAPI = "https://ku1m.execute-api.ap-south-1.amazonaws.com/default/tapwater/start";

volatile int trigger = 0;
const int LED_PIN = 14;
const int RELAY_PIN = 25;
const int RELAY_TURN_OFF_BUTTON = 26;
volatile bool shouldBlink = false;

QueueHandle_t queue;

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
    if (!shouldBlink)
    {
        digitalWrite(RELAY_PIN, HIGH);
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        digitalWrite(RELAY_PIN, LOW);
    }
    digitalWrite(LED_PIN, LOW);
    vTaskDelete(NULL);
}

void blynkTask(void *parameter)
{
    while (shouldBlink)
    {
        Blynk.run();
        vTaskDelay(50 / portTICK_PERIOD_MS); // Delay for a short while to allow other tasks to run
    }
    Serial.println("Releasing Blynk Task");
    vTaskDelete(NULL);
}

String currentISTTime()
{
    String timeFormat = "g:i A";
    Timezone India;
    India.setLocation(F("Asia/Kolkata"));
    String localTime = India.dateTime(timeFormat);
    Serial.printf("local time string: %s\n", localTime);
    return localTime;
}

String todaysISTDate()
{
    String dateFormat = "d-M-Y";
    Timezone India;
    India.setLocation(F("Asia/Kolkata"));
    String localDate = India.dateTime(dateFormat);
    Serial.printf("local date string: %s\n", localDate);
    return localDate;
}

String todaysISTFullDate()
{
    String dateFormat = "D, d-M-Y";
    Timezone India;
    India.setLocation(F("Asia/Kolkata"));
    String localDate = India.dateTime(dateFormat);
    Serial.print("local full date string: ");
    Serial.println(localDate);
    return localDate;
}

String todaysISTWeekday()
{
    String weekDayFormat = "l";
    Timezone India;
    India.setLocation(F("Asia/Kolkata"));
    String localWeekday = India.dateTime(weekDayFormat);
    Serial.printf("local weekday string: %s\n", localWeekday);
    return localWeekday;
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
    Serial.printf("JsonDataForTapwater:");
    Serial.println(jsonData);
    return jsonData;
}

String createJsonDataForStartTime(unsigned long startTime, const String &date)
{
    JsonDocument jsonDoc;
    jsonDoc["start_time"] = startTime;
    jsonDoc["date"] = date;

    String jsonString;
    serializeJson(jsonDoc, jsonString);
    Serial.printf("JsonDataForStartTime: ");
    Serial.println(jsonString);
    return jsonString;
}

int64_t parseResponse(String response)
{
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);

    if (error)
    {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        return 0;
    }

    Serial.printf("Parsed start time respose: ");
    Serial.println(response);

    int startTime = doc["start_time"];
    if (startTime != 0)
    {
        Serial.print("Start Time: ");
        Serial.println(startTime);
    }
    else
    {
        Serial.println("Start time not found in response");
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
        Serial.println("Response:");
        Serial.println(response);
    }
    else
    {
        Serial.print("Error request failed, code: ");
        Serial.println(httpResponseCode);
    }

    client.end();
    return parseResponse(response);
}

String makePOSTRequest(String jsonData, String apiUrl)
{
    HTTPClient http;
    String payload;

    Serial.println("Connecting to API endpoint...");
    http.begin(apiUrl);

    http.addHeader("Content-Type", "application/json");
    Serial.println("Sending HTTP POST request...");
    int httpResponseCode = http.POST(jsonData);

    if (httpResponseCode > 0)
    {
        Serial.print("HTTP Response code: ");
        Serial.println(httpResponseCode);
        payload = http.getString();
        Serial.println(payload);
    }
    else
    {
        Serial.print("Error code: ");
        Serial.println(httpResponseCode);
    }

    http.end();
    return payload;
}

String createTapwaterRecord(String jsonData, String tapWaterAPI)
{
    Serial.println("Sending API request...");
    String id = makePOSTRequest(jsonData, tapWaterAPI);
    return id;
}

void connectWifiAndStartBlynk()
{
    Blynk.begin(auth, ssid, password);
    Serial.println("Connecting to WiFi After Waking up...");
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    xTaskCreate(blynkTask, "BlynkTask", 2000, NULL, 1, NULL);
    Serial.println("Connected to WiFi");
}

unsigned long millisToLong(String &time_millis_str)
{
    int time_millis = atoi(time_millis_str.c_str());

    if (time_millis >= 0 && time_millis <= 4294967295)
    {
        return time_millis;
    }
    else
    {
        throw std::out_of_range("Time in milliseconds is outside the valid range for unsigned long (0 to 4294967295).");
    }
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

String millisToString(unsigned long value)
{
    char buffer[12];
    sprintf(buffer, "%lu", value);
    return String(buffer);
}

void triggerSound(void *parameter)
{
    if (shouldBlink == false)
    {
        digitalWrite(RELAY_PIN, HIGH);
        vTaskDelay(3000 / portTICK_PERIOD_MS);
        digitalWrite(RELAY_PIN, LOW);
    }
    else
    {
        digitalWrite(RELAY_PIN, HIGH);
    }
    vTaskDelete(NULL);
}

void IRAM_ATTR triggerSoundOff()
{
    gpio_set_level(gpio_num_t(RELAY_PIN), 0);
}

void processTrigger() {
    String start_time, end_time;
    unsigned long lowStartTime = 0;
    String startTime;
    String id;
    int value;

    while (digitalRead(GPIO_NUM_33) == HIGH)
    {
        if (trigger == 0)
        {
            Serial.println("starting wake up sequence");
            shouldBlink = true;
            xTaskCreate(triggerSound, "triggerSound", 1000, NULL, 1, NULL);
            xTaskCreate(blinkLed, "blinkLed", 1000, NULL, 1, NULL);
            unsigned long startTimeMillis = millis();

            connectWifiAndStartBlynk();
            waitForSync();

            start_time = currentISTTime();
            String date = todaysISTDate();
            int64_t startTimeStr = getTapWaterStartTime(date, startTimeAPI);

            if (startTimeStr == 0)
            {
                String jsonData = createJsonDataForTapwater(start_time, "", 0, "");
                createTapwaterRecord(jsonData, tapWaterAPI);
                String startTimeDBJson = createJsonDataForStartTime(startTimeMillis, date);
                makePOSTRequest(startTimeDBJson, startTimeAPI);
            }
        }
        trigger = 1;
    }

    // confirm that the sensor is low
    while (true) {
        if (digitalRead(GPIO_NUM_33) == LOW) {
            if (lowStartTime == 0) { // Start the timer when GPIO_NUM_33 first goes LOW
                lowStartTime = millis();
            } else if (millis() - lowStartTime >= 10000) { // Check if GPIO_NUM_33 has been LOW for 5 seconds
                Serial.println("sensor was LOW for last 10 seconds, ending wake up sequence");
                lowStartTime = 0;
                break;
            }
            delay(2000);
            Serial.println("sensor is LOW waiting for 10 seconds to end wake up sequence");
        } else {
            lowStartTime = 0; // Reset the timer when GPIO_NUM_33 goes HIGH
        }
    }

    if (digitalRead(GPIO_NUM_33) == LOW && trigger)
    {
        Serial.println("ending wake up sequence");
        shouldBlink = false;
        unsigned long endTimeMillis = millis();
        Serial.print("endTimeMillis: ");
        Serial.println(endTimeMillis);

        end_time = currentISTTime();
        String weekday = todaysISTWeekday();
        String date = todaysISTDate();

        int64_t startTimeLong = getTapWaterStartTime(date, startTimeAPI);

        Serial.print("startTimeLong: ");
        Serial.println(startTimeLong);
        int elapsedTime = calculateElapsedTime(startTimeLong, endTimeMillis);
        Serial.print("elapsedTime: ");
        Serial.println(elapsedTime);

        String jsonData = createJsonDataForTapwater(start_time, end_time, elapsedTime, todaysISTFullDate());
        createTapwaterRecord(jsonData, tapWaterAPI);

        trigger = 0;
    }
    xQueueReceive(queue, &value, 0);
    Serial.printf("Releasing queue value: %d\n", value);
}

BLYNK_WRITE(V0)
{
    int pinValue = param.asInt();
    if (pinValue == 0)
    {
        digitalWrite(RELAY_PIN, LOW);
    }
}

void sensor_woke_up()
{
  int value = 1;
  if(xQueueSend(queue, &value, pdMS_TO_TICKS(60000*10)) == pdPASS) {
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