#include <Arduino.h>
#include <WiFiManager.h>    // https://github.com/tzapu/WiFiManager
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <SD.h>
#include <SPI.h>
#include <RTClib.h>
#include <ModbusMaster.h>

#include "mqtt_client.h"

TaskHandle_t taskSht;
ModbusMaster node;
#define DEFAULT_ADDRESS_SENSOR 0X01

#define DEFAULT_WIFI_SSID "SSID"
#define DEFAULT_WIFI_PASS "PASSWORD"

const char rebootTags[7] = "REBOOT";
const char mqttTags[5] = "MQTT";

const char *MQTT_HOST = "103.150.190.35";
const char *MQTT_USERNAME = "user";
const char *MQTT_PASSWORD = "12344321";

esp_mqtt_client_config_t mqttCfg;
esp_mqtt_client_handle_t mqttClient;

const char *topic = "TOPIC-MQTT";

bool mqttConnected = false;

// reboot esp32
void rebootEspWithReason(const char *reason) {
    ESP_LOGE(rebootTags, "%s", reason);
    ESP.restart();
}

boolean sendDataMqtt(const char* topicPublish, const char* dataPublish, uint16_t lenDataPublish) {
    uint8_t sendTries = 0;
    if (esp_mqtt_client_publish(mqttClient, topicPublish, dataPublish, lenDataPublish, 1, false) != -1) {
        return true;
    } else {
        ESP_LOGI(mqttTags, "MQTT publish failed, retrying (%d/3)", sendTries);
        for (int i = 0; i < 3; i++) {
            if (esp_mqtt_client_publish(mqttClient, topicPublish, dataPublish, lenDataPublish, 1, false) != -1) {
                return true;
            } else {
                ESP_LOGE(mqttTags, "MQTT publish failed, retrying (%d/3)");
            }
            sendTries++;
            delay(1000);
        }
    }
    return false;
}

void sendMqtt(char *formatMqtt, uint16_t lenFormatMqtt, char *topicMqtt) {
    if (WiFi.status() == WL_CONNECTED && mqttConnected) {
        if (sendDataMqtt(topicMqtt, formatMqtt, lenFormatMqtt)) {
            // ledState.clearBlink();
            // ledState.blink(normalPattern, 2);
            ESP_LOGI(mqttTags, "Topic: %s", topicMqtt);
            ESP_LOGI(mqttTags, "Data: %s", formatMqtt);
            ESP_LOGI(mqttTags, "Data Sent SUCCESSFULLY");
        } else {
            // ledState.clearBlink();
            // ledState.blink2();
            ESP_LOGE(mqttTags, "Data Sent FAILED");
        }
    } else {
        ESP_LOGE(mqttTags, "Data Sent FAILED, Store to SD Card");
        mqttConnected = false;
        // if (sdInitSuccess) {
            // sdCard.store(formatMqtt, lenFormatMqtt);  // store data to sd card
        // }
    }
}

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event) {
  if (event->event_id == MQTT_EVENT_CONNECTED) {
    mqttConnected = true;

    esp_mqtt_client_subscribe(mqttClient, topic, 0);

  } else if (event->event_id == MQTT_EVENT_DISCONNECTED) {
    ESP_LOGI("TEST", "MQTT event: %d. MQTT_EVENT_DISCONNECTED", event->event_id);
    mqttConnected = false;

  } else if (event->event_id == MQTT_EVENT_SUBSCRIBED) {
    ESP_LOGI("TEST", "MQTT msgid= %d event: %d. MQTT_EVENT_SUBSCRIBED", event->msg_id, event->event_id);
  } else if (event->event_id == MQTT_EVENT_UNSUBSCRIBED) {
    ESP_LOGI("TEST", "MQTT msgid= %d event: %d. MQTT_EVENT_UNSUBSCRIBED", event->msg_id, event->event_id);
  } else if (event->event_id == MQTT_EVENT_PUBLISHED) {
    ESP_LOGI("TEST", "MQTT event: %d. MQTT_EVENT_PUBLISHED", event->event_id);
  } else if (event->event_id == MQTT_EVENT_DATA) {
    ESP_LOGI("TEST", "MQTT msgid= %d event: %d. MQTT_EVENT_DATA", event->msg_id, event->event_id);
    ESP_LOGI("TEST", "Topic length %d. Data length %d", event->topic_len, event->data_len);
    ESP_LOGI("TEST", "Incoming data: %.*s %.*s\n", event->topic_len, event->topic, event->data_len, event->data);

  } else if (event->event_id == MQTT_EVENT_BEFORE_CONNECT) {
    ESP_LOGI("TEST", "MQTT event: %d. MQTT_EVENT_BEFORE_CONNECT", event->event_id);
  }
  return ESP_OK;
}

// reconnect to wifi
bool reconnectToWifi() {
    ESP_LOGI(wifiTags, "WiFi Disconnected. Trying to reconnect...");
    uint8_t MAX_WIFI_RECONNECT_TRIES = 2;
    for (uint8_t i = 0; i < MAX_WIFI_RECONNECT_TRIES; i++) {
        WiFi.disconnect();
        delay(5000);
        WiFi.reconnect();
        delay(5000);
        if (WiFi.status() == WL_CONNECTED) {
            return true;
        }
    }
    return false;
}

void pollSht30( void * pvParameters ) {
  uint8_t j, result;
  float data[6];
  float temp=0;
  float huma=0;
  for(;;) {
    vTaskDelay(100);
    result = node.readHoldingRegisters(0x00, 2);
    if (result == node.ku8MBSuccess) {
      for (j = 0; j < 2; j++)
      {
        data[j] = node.getResponseBuffer(j);
        if (j==0) {
          temp = data[j]/10.00;
        } else {
          huma = data[j]/10.00;
        }
      }
      Serial.printf("temp %.2f hum %.2f\n", temp, huma);
    } else {
      // errorCodePool = errorCodePool+ (pow(2,1));
      Serial.println("Not Coneccted Modbus RS485");
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("***INITIALIZATION STARTING***");

  Serial.println("---WIFI INITIALIZATION---");
  WiFi.mode(WIFI_STA);  // explicitly set mode, esp defaults to STA+AP
  // preferences.putBool(DEFAULTSSID_STATUS_ADDRESS, false);
  WiFi.begin(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  // Print local IP address and start web server
  Serial.println("");
  Serial.println("WiFi connected.");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  
  Serial.println("---END WIFI INITIALIZATION---");

  Serial2.begin(9600);
  node.begin(DEFAULT_ADDRESS_SENSOR, Serial2);

  Serial.println("---MQTT INITIALIZATION---");
  mqttCfg.host = MQTT_HOST;
  mqttCfg.port = 1883;
  mqttCfg.keepalive = 60;
  mqttCfg.username = MQTT_USERNAME;
  mqttCfg.password = MQTT_PASSWORD;
  mqttCfg.event_handle = mqtt_event_handler;
  mqttCfg.lwt_msg = "0";
  mqttCfg.lwt_msg_len = 1;
  mqttCfg.lwt_retain = 1;
  mqttCfg.transport = MQTT_TRANSPORT_OVER_TCP;

  Serial.println("   Connecting to MQTT Server....");
  mqttClient = esp_mqtt_client_init(&mqttCfg);
  esp_err_t err = esp_mqtt_client_start(mqttClient);
  ESP_LOGI("TEST", "Client connect. Error = %d %s", err, esp_err_to_name(err));
  if (err != ESP_OK) {
    Serial.println("[MQTT] Failed to connect to MQTT server at setup");
    // rebootEspWithReason("   Failed MQTT Connection");
  }
  Serial.println("---END MQTT INITIALIZATION---");

  /* pin task to core 0 */
  xTaskCreatePinnedToCore(
    pollSht30,      /* Task function. */
    "pollSht",    /* name of task. */
    1024*4,           /* Stack size of task */
    NULL,           /* parameter of the task */
    1,              /* priority of the task */
    &taskSht,     /* Task handle to keep track of created task */
    1
  );          /* pin task to core 1 */
}

void loop() {
}