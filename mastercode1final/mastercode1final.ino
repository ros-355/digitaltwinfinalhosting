#include <esp_now.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <LiquidCrystal_I2C.h>

// --- Network Configuration ---
const char* ssid = "TP-LINK_E0EA";
const char* password = "1234567890";
const char* mqtt_broker = "broker.hivemq.com";

// --- Hardware Pins ---
const int resetBtn = 4;
LiquidCrystal_I2C lcd(0x27, 16, 2);

// --- Calculation Variables ---
double totalKWh = 0;
unsigned long lastTime = 0;
int lcdPage = 0; 

// --- Data Structure (Must match Transmitter exactly) ---
typedef struct struct_message {
    float curSec1; float curSec2; float curSec3;
    float curPrim; float curLoad;
    float volPrim; float volSec;
    float wattsLoad;
} struct_message;

struct_message incoming;
WiFiClient espClient;
PubSubClient client(espClient);

// --- ESP-NOW Callback Function ---
void OnDataRecv(const esp_now_recv_info * info, const uint8_t *incomingData, int len) {
  memcpy(&incoming, incomingData, sizeof(incoming));
  
  // Calculate Energy Consumption
  unsigned long currentTime = millis();
  if (lastTime > 0) {
    float seconds = (currentTime - lastTime) / 1000.0;
    double Wh = (incoming.wattsLoad * seconds) / 3600.0;
    totalKWh += (Wh / 1000.0);
  }
  lastTime = currentTime;

  // --- SERIAL PRINT ALL DATA ---
  Serial.println("\n============================================");
  Serial.println("         TRANSFORMER DATA RECEIVED          ");
  Serial.println("============================================");
  Serial.printf(" PRIMARY SIDE   | Voltage: %6.1f V | Current: %6.2f A\n", incoming.volPrim, incoming.curPrim);
  Serial.println("----------------+------------------+----------------");
  Serial.printf(" SECONDARY SIDE | Voltage: %6.1f V | Current: %6.2f A\n", incoming.volSec, incoming.curLoad);
  Serial.println("----------------+------------------+----------------");
  Serial.printf(" COIL BREAKDOWN | S1: %6.2f A | S2: %6.2f A | S3: %6.2f A\n", incoming.curSec1, incoming.curSec2, incoming.curSec3);
  Serial.println("----------------+------------------+----------------");
  Serial.printf(" CALCULATED     | Power: %8.1f W | Energy: %.6f kWh\n", incoming.wattsLoad, totalKWh);
  Serial.println("============================================\n");
}

void setup() {
  Serial.begin(115200);
  
  // LCD Initialization
  lcd.init(); 
  lcd.backlight();
  lcd.setCursor(0,0);
  lcd.print("System Starting");

  pinMode(resetBtn, INPUT_PULLUP);

  // WiFi Connectivity
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { 
    delay(500); 
    Serial.print("."); 
  }
  Serial.println("\nWiFi Connected");

  // MQTT Server Setup
  client.setServer(mqtt_broker, 1883);

  // ESP-NOW Initialization
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error starting ESP-NOW");
    return;
  }
  esp_now_register_recv_cb(OnDataRecv);
  
  lcd.clear();
}

void loop() {
  // Maintain MQTT connection
  if (!client.connected()) reconnectMQTT();
  client.loop();

  // Reset kWh Button Logic
  if (digitalRead(resetBtn) == LOW) {
    totalKWh = 0;
    lcd.clear();
    lcd.print("RESETTING kWh...");
    delay(1000);
  }

  // Timer: Switch LCD page and publish to MQTT every 3 seconds
  static unsigned long updateTimer = 0;
  if (millis() - updateTimer > 3000) {
    updateTimer = millis();
    lcdPage++;
    if(lcdPage > 3) lcdPage = 0; 
    
    updateInterface();
    publishAllData();
  }
}

void updateInterface() {
  lcd.clear();
  switch(lcdPage) {
    case 0: // Power Summary
      lcd.setCursor(0,0);
      lcd.print("Pwr: "); lcd.print(incoming.wattsLoad, 1); lcd.print(" W");
      lcd.setCursor(0,1);
      lcd.print("kWh: "); lcd.print(totalKWh, 5);
      break;

    case 1: // Voltage Comparison
      lcd.setCursor(0,0);
      lcd.print("V-PRI: "); lcd.print(incoming.volPrim, 1); lcd.print("V");
      lcd.setCursor(0,1);
      lcd.print("V-SEC: "); lcd.print(incoming.volSec, 1); lcd.print("V");
      break;

    case 2: // Main Currents
      lcd.setCursor(0,0);
      lcd.print("I-PRI: "); lcd.print(incoming.curPrim, 2); lcd.print("A");
      lcd.setCursor(0,1);
      lcd.print("I-SEC: "); lcd.print(incoming.curLoad, 2); lcd.print("A");
      break;

    case 3: // Secondary Coils
      lcd.setCursor(0,0);
      lcd.print("S1:"); lcd.print(incoming.curSec1, 1);
      lcd.print(" S2:"); lcd.print(incoming.curSec2, 1);
      lcd.setCursor(0,1);
      lcd.print("S3:"); lcd.print(incoming.curSec3, 1);
      lcd.print(" (Amps)");
      break;
  }
}

void publishAllData() {
  if (client.connected()) {
    char buf[12];
    
    // Primary
    dtostrf(incoming.volPrim, 1, 2, buf);
    client.publish("transformer/primary/voltage", buf);
    dtostrf(incoming.curPrim, 1, 2, buf);
    client.publish("transformer/primary/current", buf);

    // Secondary
    dtostrf(incoming.volSec, 1, 2, buf);
    client.publish("transformer/secondary/voltage", buf);
    dtostrf(incoming.curLoad, 1, 2, buf);
    client.publish("transformer/secondary/total_amps", buf);
    
    // Individual Coils
    dtostrf(incoming.curSec1, 1, 2, buf);
    client.publish("transformer/secondary/coil1", buf);
    dtostrf(incoming.curSec2, 1, 2, buf);
    client.publish("transformer/secondary/coil2", buf);
    dtostrf(incoming.curSec3, 1, 2, buf);
    client.publish("transformer/secondary/coil3", buf);

    // Energy & Power
    dtostrf(incoming.wattsLoad, 1, 2, buf);
    client.publish("transformer/secondary/watts", buf);
    dtostrf(totalKWh, 1, 6, buf);
    client.publish("transformer/energy/kwh", buf);
  }
}

void reconnectMQTT() {
  static unsigned long lastAttempt = 0;
  if (millis() - lastAttempt > 5000) {
    lastAttempt = millis();
    if (client.connect("ESP32_Receiver_Node")) {
      Serial.println("MQTT Connected");
    }
  }
}