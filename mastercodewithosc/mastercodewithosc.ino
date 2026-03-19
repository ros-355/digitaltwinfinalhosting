#include <esp_now.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <LiquidCrystal_I2C.h>
#include <WiFiUdp.h>       
#include <OSCMessage.h>    
#include <esp_wifi.h>      



// --- Network Configuration ---

const char* ssid = "TP-LINK_E0EA";
const char* password = "1234567890";
const char* mqtt_broker = "broker.hivemq.com";

// --- OSC Configuration ---

const IPAddress pcIP(192, 168, 0, 108); 
const unsigned int pcPort = 8000; 
WiFiUDP Udp;

// --- Hardware Pins ---
const int resetBtn = 4;
LiquidCrystal_I2C lcd(0x27, 16, 2);
double totalKWh = 0;
unsigned long lastTime = 0;
int lcdPage = 0; 

typedef struct struct_message {

    float curSec1; float curSec2; float curSec3;
    float curPrim; float curLoad;
    float volPrim; float volSec;
    float wattsLoad;

} struct_message;

struct_message incoming;
WiFiClient espClient;
PubSubClient client(espClient);

// --- UPDATED OSC: Including S1, S2, S3 ---

void sendOSCData() {
    // 1. Primary Address
    OSCMessage msgPri("/transformer/primary");
    msgPri.add(incoming.volPrim);   // Index 0
    msgPri.add(incoming.curPrim);   // Index 1
    Udp.beginPacket(pcIP, pcPort);
    msgPri.send(Udp);
    Udp.endPacket();
    msgPri.empty();

    // 2. Secondary Address (Updated with Coils)

    OSCMessage msgSec("/transformer/secondary");
    msgSec.add(incoming.volSec);    // Index 0
    msgSec.add(incoming.curLoad);   // Index 1
    msgSec.add(incoming.wattsLoad); // Index 2
    msgSec.add((float)totalKWh);    // Index 3
    msgSec.add(incoming.curSec1);   // Index 4 (S1)
    msgSec.add(incoming.curSec2);   // Index 5 (S2)
    msgSec.add(incoming.curSec3);   // Index 6 (S3)
    Udp.beginPacket(pcIP, pcPort);
    msgSec.send(Udp);
    Udp.endPacket();
    msgSec.empty();

}
void OnDataRecv(const esp_now_recv_info * info, const uint8_t *incomingData, int len) {
  memcpy(&incoming, incomingData, sizeof(incoming));
  unsigned long currentTime = millis();

  if (lastTime > 0) {
    float seconds = (currentTime - lastTime) / 1000.0;
    double Wh = (incoming.wattsLoad * seconds) / 3600.0;
    totalKWh += (Wh / 1000.0);

  }
  lastTime = currentTime;
  sendOSCData(); 
  // --- ORIGINAL SERIAL PRINT ---
  Serial.println("\n============================================");
  Serial.println("         TRANSFORMER DATA RECEIVED           ");
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
  lcd.init(); lcd.backlight();
  lcd.print("System Starting");
  pinMode(resetBtn, INPUT_PULLUP);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(WiFi.channel(), WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);


  Udp.begin(pcPort);
  client.setServer(mqtt_broker, 1883);
  if (esp_now_init() != ESP_OK) return;
  esp_now_register_recv_cb(OnDataRecv);
  lcd.clear();

}
void loop() {
  if (!client.connected()) reconnectMQTT();
  client.loop();
  if (digitalRead(resetBtn) == LOW) {
    totalKWh = 0;
    lcd.clear(); lcd.print("RESETTING kWh...");
    delay(1000);
  }
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

    case 0: lcd.setCursor(0,0); lcd.print("Pwr: "); lcd.print(incoming.wattsLoad, 1); lcd.print(" W");

            lcd.setCursor(0,1); lcd.print("kWh: "); lcd.print(totalKWh, 5); break;

    case 1: lcd.setCursor(0,0); lcd.print("V-PRI: "); lcd.print(incoming.volPrim, 1);

            lcd.setCursor(0,1); lcd.print("V-SEC: "); lcd.print(incoming.volSec, 1); break;

    case 2: lcd.setCursor(0,0); lcd.print("I-PRI: "); lcd.print(incoming.curPrim, 2);

            lcd.setCursor(0,1); lcd.print("I-SEC: "); lcd.print(incoming.curLoad, 2); break;

    case 3: lcd.setCursor(0,0); lcd.print("S1:"); lcd.print(incoming.curSec1, 1); lcd.print(" S2:"); lcd.print(incoming.curSec2, 1);

            lcd.setCursor(0,1); lcd.print("S3:"); lcd.print(incoming.curSec3, 1); break;

  }

}



// --- UPDATED MQTT: Including S1, S2, S3 ---

void publishAllData() {

  if (client.connected()) {

    // --- KEEP YOUR ORIGINAL COMBINED PAYLOAD (Good for MATLAB string parsing) ---

    char payload[150];

    snprintf(payload, sizeof(payload), "%.2f,%.2f,%.2f,%.2f,%.2f,%.6f,%.2f,%.2f,%.2f", 

             incoming.volPrim, incoming.curPrim, incoming.volSec, 

             incoming.curLoad, incoming.wattsLoad, totalKWh,

             incoming.curSec1, incoming.curSec2, incoming.curSec3);

    client.publish("transformer/all_data", payload);



    // --- ADD THESE INDIVIDUAL TOPICS (Required for Dashboard Gauges & Report) ---

    client.publish("transformer/primary/voltage", String(incoming.volPrim).c_str());

    client.publish("transformer/primary/current", String(incoming.curPrim).c_str());

    client.publish("transformer/secondary/voltage", String(incoming.volSec).c_str());

    client.publish("transformer/secondary/total_amps", String(incoming.curLoad).c_str());

    client.publish("transformer/secondary/watts", String(incoming.wattsLoad).c_str());

    client.publish("transformer/energy/kwh", String(totalKWh, 6).c_str());

    client.publish("transformer/secondary/coil1", String(incoming.curSec1).c_str());

    client.publish("transformer/secondary/coil2", String(incoming.curSec2).c_str());

    client.publish("transformer/secondary/coil3", String(incoming.curSec3).c_str());

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

