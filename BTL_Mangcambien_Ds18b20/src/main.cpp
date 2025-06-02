#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ArduinoJson.h>

// --- WiFi credentials ---
const char* ssid = "Tầng 2";
const char* password = "lacaigivay";

// --- MQTT TLS config ---
const char* mqtt_server = "55b6d157392a42a09ff842f477208194.s1.eu.hivemq.cloud";
const int mqtt_port = 8883;
const char* mqtt_username = "ocean";
const char* mqtt_password = "Hunghung68";

// --- MQTT topics ---
const char* mqtt_topic_temp = "esp32/ds18b20/temperature";
const char* mqtt_topic_control = "esp32/ds18b20/client";
const char* mqtt_topic_out = "esp32/ds18b20/out";

// --- DS18B20 config ---
const int oneWireBus = 27;
OneWire oneWire(oneWireBus);
DallasTemperature sensors(&oneWire);

// --- LED output config ---
const int outPins[1] = {25};  // Chân LED điều khiển qua MQTT (GPIO25)
bool updateState = false;

// --- WiFi & MQTT client ---
WiFiClientSecure espClient;
PubSubClient client(espClient);

unsigned long lastSend = 0;

// --- WiFi setup ---
void setup_wifi() {
  Serial.print("Kết nối WiFi: ");
  WiFi.begin(ssid, password);

  int count = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    count++;
    if (count > 60) { // sau 30 giây nếu chưa kết nối thì báo lỗi
      Serial.println("\nKhông thể kết nối WiFi. Thử lại...");
      count = 0;
      WiFi.disconnect();
      WiFi.begin(ssid, password);
    }
  }
  Serial.println("\nWiFi đã kết nối!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

// --- MQTT reconnect ---
void reconnect() {
  while (!client.connected()) {
    Serial.print("Kết nối MQTT...");
    String clientID = "ESP32Client-";
    clientID += String(random(0xffff), HEX);
    Serial.print("ClientID: ");
    Serial.println(clientID);

    if (client.connect(clientID.c_str(), mqtt_username, mqtt_password)) {
      Serial.println("Đã kết nối MQTT");
      client.subscribe(mqtt_topic_control);
      Serial.print("Đăng ký topic nhận lệnh: ");
      Serial.println(mqtt_topic_control);
    } else {
      Serial.print("Thất bại, mã lỗi = ");
      Serial.print(client.state());
      Serial.println(". Thử lại sau 5s");
      delay(5000);
    }
  }
}

// --- MQTT callback ---
void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println("MQTT nhận [" + String(topic) + "]: " + message);

  DynamicJsonDocument doc(256);
  DeserializationError error = deserializeJson(doc, message);
  if (error) {
    Serial.print("Lỗi parse JSON: ");
    Serial.println(error.c_str());
    return;
  }

  if (doc.containsKey("out3")) {
    bool p = doc["out3"];
    Serial.print("Giá trị out3 nhận được: ");
    Serial.println(p ? "true" : "false");
    digitalWrite(outPins[0], p);
    Serial.print("LED GPIO");
    Serial.print(outPins[0]);
    Serial.print(" đã được ");
    Serial.println(p ? "BẬT" : "TẮT");
    updateState = true;
  }
}

// --- MQTT publish helper ---
void publishMessage(const char* topic, const String& payload, bool retained) {
  if (client.publish(topic, payload.c_str(), retained)) {
    Serial.println("Đã gửi [" + String(topic) + "]: " + payload);
  } else {
    Serial.println("Gửi MQTT thất bại");
  }
}

void setup() {
  Serial.begin(115200);
  setup_wifi();

  espClient.setInsecure();  // Bỏ kiểm tra chứng chỉ để dễ kết nối HiveMQ
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  // Khởi tạo LED output
  for (int i = 0; i < 1; i++) {
    pinMode(outPins[i], OUTPUT);
    digitalWrite(outPins[i], LOW);
  }

  sensors.begin();
}

void loop() {
  if (!client.connected()) {
    Serial.println("MQTT chưa kết nối, thực hiện reconnect");
    reconnect();
  }
  client.loop();

  if (millis() - lastSend > 5000) {
    sensors.requestTemperatures();
    float temperatureC = sensors.getTempCByIndex(0);

    DynamicJsonDocument doc(64);
    doc["temperature"] = temperatureC;

    char payload[64];
    serializeJson(doc, payload);
    publishMessage(mqtt_topic_temp, String(payload), false);

    Serial.printf("Đã gửi nhiệt độ: %.2f°C\n", temperatureC);
    lastSend = millis();
  }

  if (updateState) {
    DynamicJsonDocument doc(64);
    doc["out3"] = (bool)digitalRead(outPins[0]);
    char buf[64];
    serializeJson(doc, buf);
    publishMessage(mqtt_topic_out, String(buf), true);
    updateState = false;
  }
}
