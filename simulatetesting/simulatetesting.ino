#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// --- NETWORK CONFIG ---
const char* ssid = "Dinesh_DHInternet";
const char* password = "DHInternet@20948995";
const char* mqtt_server = "broker.emqx.io";
const char* mqtt_topic = "substation/33kv_11kv/final";

// --- DATA STRUCTURE ---
struct Transformer {
  float v_high;  // 33kV Side
  float i_high;  
  float v_low;   // 11kV Side
  float i_low;   
  float temp;    
  float kw;      
};

Transformer tx[3]; 
LiquidCrystal_I2C lcd(0x27, 20, 4);
WiFiClient espClient;
PubSubClient client(espClient);

unsigned long lastMillis = 0;

void setup() {
  Serial.begin(115200);
  lcd.init();
  lcd.backlight();

  WiFi.begin(ssid, password);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) { 
    delay(500); 
    Serial.print("."); 
  }
  Serial.println("\nConnected!");
  
  client.setServer(mqtt_server, 1883);
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Connecting to MQTT...");
    if (client.connect("ESP32_Substation_Client")) {
      Serial.println("Success!");
    } else {
      delay(5000);
    }
  }
}

void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  if (millis() - lastMillis > 5000) {
    lastMillis = millis();

    // 1. UNIQUE DATA SIMULATION FOR EACH TX
    for (int i = 0; i < 3; i++) {
      // Varying Voltage per Transformer
      tx[i].v_high = 33000.0 + random(-200, 200);
      tx[i].v_low = 11000.0 + random(-100, 100);

      // Unique Load Profiles
      if (i == 0) { // TX 1: Heavy Load
        tx[i].i_low = 120.0 + (random(0, 300) / 10.0); // 120-150A
        tx[i].temp = 55.0 + (random(0, 100) / 10.0);  // Higher Temp
      } 
      else if (i == 1) { // TX 2: Medium Load
        tx[i].i_low = 70.0 + (random(0, 200) / 10.0);  // 70-90A
        tx[i].temp = 45.0 + (random(0, 80) / 10.0);
      } 
      else { // TX 3: Light Load
        tx[i].i_low = 30.0 + (random(0, 150) / 10.0);  // 30-45A
        tx[i].temp = 38.0 + (random(0, 50) / 10.0);   // Cooler
      }

      // Calculate Secondary Values
      tx[i].i_high = (tx[i].i_low / 3.0) * 1.03; // Including 3% loss
      tx[i].kw = (tx[i].v_low * tx[i].i_low * 0.9) / 1000.0;
    }

    // 2. PRINT TO SERIAL MONITOR
    Serial.println("\n----------------------------------------------------------");
    Serial.println("TX_ID | 33kV(V/A)      | 11kV(V/A)      | Load(kW) | Temp");
    Serial.println("----------------------------------------------------------");
    for (int i = 0; i < 3; i++) {
      Serial.printf("TX#%d  | %5.0fV/%2.1fA | %5.0fV/%2.1fA | %4.1f kW | %2.1fC\n",
                    i+1, tx[i].v_high, tx[i].i_high, tx[i].v_low, tx[i].i_low, tx[i].kw, tx[i].temp);
    }
    Serial.println("----------------------------------------------------------");

    // 3. LCD UPDATE
    lcd.clear();
    lcd.setCursor(0,0); lcd.print("SUBSTATION MONITOR");
    for (int i = 0; i < 3; i++) {
      lcd.setCursor(0, i+1);
      lcd.printf("T%d: %4.1f kW %2.0fC", i+1, tx[i].kw, tx[i].temp);
    }

    // 4. JSON FOR MONGODB
    StaticJsonDocument<1024> doc;
    JsonArray root = doc.createNestedArray("transformers");

    for (int i = 0; i < 3; i++) {
      JsonObject unit = root.createNestedObject();
      unit["id"] = i + 1;
      unit["v_33kv"] = tx[i].v_high;
      unit["i_33kv"] = tx[i].i_high;
      unit["v_11kv"] = tx[i].v_low;
      unit["i_11kv"] = tx[i].i_low;
      unit["kw"] = tx[i].kw;
      unit["temp"] = tx[i].temp;
    }

    char buffer[1024];
    serializeJson(doc, buffer);
    client.publish(mqtt_topic, buffer);
    Serial.println(">> Data sent to MongoDB via MQTT Broker");
  }
}