#include <WiFi.h>
#include <ModbusMaster.h>
#include <HardwareSerial.h>
#include <PubSubClient.h>

// RS485 povezava (uporablja Serial2)
#define RXD2 16
#define TXD2 17

HardwareSerial RS485Serial(2);
ModbusMaster node;

// WiFi podatki
const char* ssid = "IME_WIFI";
const char* password = "GESLO_WIFI";

// MQTT strežnik
const char* mqtt_server = "192.168.1.100";
WiFiClient espClient;
PubSubClient client(espClient);

// Časovna kontrola
unsigned long lastRead = 0;
const unsigned long interval = 5000;

// --- Globalne spremenljivke za histerezo ---
#define POWER_LIMIT_HIGH 5800  // če presežemo to vrednost, omejimo
#define POWER_LIMIT_LOW  5500  // ko pademo pod to, vrnemo na 100%
bool powerLimited = false;     // stanje omejitve moči

void callback(char* topic, byte* payload, unsigned int length) {
  String messageTemp;
  for (unsigned int i = 0; i < length; i++) {
    messageTemp += (char)payload[i];
  }

  if (String(topic) == "qb/inverter/cmd") {
    if (messageTemp.indexOf("limit_power") > -1) {
      int value = messageTemp.substring(messageTemp.indexOf(":") + 1).toInt();
      node.writeSingleRegister(37113, value); // limit power
    }
    if (messageTemp.indexOf("set_mode") > -1) {
      int mode = messageTemp.substring(messageTemp.indexOf(":") + 1).toInt();
      node.writeSingleRegister(37009, mode); // set work mode
    }
  }
}

void setup_wifi() {
  delay(10);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
}

void reconnect() {
  while (!client.connected()) {
    if (client.connect("ESP32Client")) {
      client.subscribe("qb/inverter/cmd");
    } else {
      delay(5000);
    }
  }
}

void setup() {
  RS485Serial.begin(9600, SERIAL_8N1, RXD2, TXD2);
  node.begin(1, RS485Serial);

  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  unsigned long now = millis();
  if (now - lastRead > interval) {
    lastRead = now;

    uint8_t result = node.readInputRegisters(35105, 2);
    if (result == node.ku8MBSuccess) {
      long acPower = ((uint32_t)node.getResponseBuffer(0) << 16) | node.getResponseBuffer(1);
      char msg[100];
      sprintf(msg, "{\"ac_power\": %ld}", acPower);
      client.publish("qb/inverter/status", msg);

      // --- Logika za omejevanje izhodne moči inverterja ---
      if (!powerLimited && acPower > POWER_LIMIT_HIGH) {
        node.writeSingleRegister(37113, 50);  // omeji moč na 50 %
        powerLimited = true;
      }
      else if (powerLimited && acPower < POWER_LIMIT_LOW) {
        node.writeSingleRegister(37113, 100); // vrni moč na 100 %
        powerLimited = false;
      }
    }
  }
}
