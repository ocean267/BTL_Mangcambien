#include <WiFi.h>
#include <Wire.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "MAX30100_PulseOximeter.h"
#include <ArduinoJson.h>

// --- WiFi và MQTT cấu hình ---
const char* ssid = "Tầng 2";
const char* password = "lacaigivay";

const char* mqtt_server = "f8e9523a8a3b48ea957832a96e91caa0.s1.eu.hivemq.cloud";
const int mqtt_port = 8883;
const char* mqtt_username = "e-smart";
const char* mqtt_password = "Abc@1234";

// --- MQTT client ---
WiFiClientSecure espClient;
PubSubClient client(espClient);

// --- MAX30100 ---
PulseOximeter pox;
float heartRate = 0.0;
float spO2 = 0.0;
bool dataReady = false;

// --- LED output config ---
const int outPins[2] = {25, 33}; // LED điều khiển qua MQTT
bool updateState = false;

// --- Task handle ---
TaskHandle_t TaskSensorHandle = NULL;

void setup_wifi() {
  Serial.print("Kết nối WiFi: ");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi đã kết nối!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Kết nối MQTT...");
    String clientID = "ESP32Client-";
    clientID += String(random(0xffff), HEX);
    if (client.connect(clientID.c_str(), mqtt_username, mqtt_password)) {
      Serial.println("Đã kết nối MQTT");
      client.subscribe("esp32/client");
    } else {
      Serial.print("Thất bại, mã lỗi = ");
      Serial.print(client.state());
      Serial.println(". Thử lại sau 5s");
      delay(5000);
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println("MQTT nhận [" + String(topic) + "]: " + message);

  DynamicJsonDocument doc(256);
  if (deserializeJson(doc, message)) {
    Serial.println("Lỗi parse JSON");
    return;
  }

  if (doc.containsKey("out1")) {
    bool p = doc["out1"];
    digitalWrite(outPins[0], p);
    Serial.println("out1 = " + String(p));
  }
  if (doc.containsKey("out2")) {
    bool p = doc["out2"];
    digitalWrite(outPins[1], p);
    Serial.println("out2 = " + String(p));
  }

  updateState = true;
}

void publishMessage(const char* topic, const String& payload, bool retained) {
  if (client.publish(topic, payload.c_str(), retained)) {
    Serial.println("Đã gửi [" + String(topic) + "]: " + payload);
  } else {
    Serial.println("Gửi MQTT thất bại");
  }
}

// Task đọc MAX30100
void TaskSensor(void * parameter) {
  Serial.println("TaskSensor chạy trên core " + String(xPortGetCoreID()));
  for (;;) {
    pox.update();
    float hr = pox.getHeartRate();
    float spo2_val = pox.getSpO2();
    if (hr > 0 && spo2_val > 0) {
      heartRate = hr;
      spO2 = spo2_val;
      dataReady = true;
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void setup() {
  Serial.begin(115200);
  setup_wifi();

  espClient.setInsecure();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  // Cấu hình LED output
  for (int i = 0; i < 2; i++) {
    pinMode(outPins[i], OUTPUT);
    digitalWrite(outPins[i], LOW);
  }

  // I2C MAX30100 (SDA=26, SCL=32)
  Wire.begin(26, 32);
  Wire.setClock(400000);

  // Khởi tạo MAX30100
  Serial.print("Khởi tạo MAX30100...");
  if (!pox.begin()) {
    Serial.println("Lỗi MAX30100!");
    while (1) delay(1000);
  }
  pox.setIRLedCurrent(MAX30100_LED_CURR_50MA);
  Serial.println("MAX30100 OK");

  // Tạo task MAX30100
  xTaskCreatePinnedToCore(
    TaskSensor,
    "TaskSensor",
    10000,
    NULL,
    1,
    &TaskSensorHandle,
    0
  );
}

unsigned long lastSend = 0;

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  if (millis() - lastSend > 1000) {
    if (dataReady) {
      DynamicJsonDocument doc(128);
      doc["heart_rate"] = heartRate;
      doc["spo2"] = spO2;

      char buf[128];
      serializeJson(doc, buf);
      publishMessage("esp32/sensors", String(buf), false);

      Serial.printf("Gửi HR=%.1f bpm, SpO2=%.1f%%\n", heartRate, spO2);
      dataReady = false;
    }
    lastSend = millis();
  }

  if (updateState) {
    DynamicJsonDocument doc(128);
    doc["out1"] = (bool)digitalRead(outPins[0]);
    doc["out2"] = (bool)digitalRead(outPins[1]);
    char buf[128];
    serializeJson(doc, buf);
    publishMessage("esp32/out", String(buf), true);
    updateState = false;
  }
}
