#include <esp_now.h>
#include <WiFi.h>

uint8_t broadcastAddress[] = {0x88, 0x57, 0x21, 0x2E, 0x88, 0x5C};

typedef struct struct_message {
    float curSec1; float curSec2; float curSec3;
    float curPrim; float curLoad;
    float volPrim; float volSec;
    float wattsLoad;
} struct_message;

struct_message myData;
esp_now_peer_info_t peerInfo;

// --- CALIBRATION ---
const float CAL_V_PRI = 0.45;    
const float CAL_V_SEC = 0.45; 
const float CAL_I_PRI = 0.008;  
const float CAL_I_SEC = 0.008; 

// Diagnostic P2P Variables
long p2pVP, p2pVS, p2pIP, p2pIS1, p2pIS2, p2pIS3;

float getSensorData(int pin, float cal, long &p2p_out) {
  long sumSq = 0, avgSum = 0;
  int minVal = 4095, maxVal = 0;
  const int samples = 400;
  int raw[400];

  for(int i=0; i<samples; i++) {
    raw[i] = analogRead(pin);
    avgSum += raw[i];
    if(raw[i] < minVal) minVal = raw[i];
    if(raw[i] > maxVal) maxVal = raw[i];
    delayMicroseconds(100);
  }
  
  float midpoint = (float)avgSum / samples;
  p2p_out = maxVal - minVal;

  // --- NOISE GATE ---
  // If Peak-to-Peak is less than 20, consider it 0 (Dead Air/Noise)
  if (p2p_out < 20) return 0.0;

  for(int i=0; i<samples; i++) {
    float dev = (float)raw[i] - midpoint;
    sumSq += (dev * dev);
  }
  
  return sqrt(sumSq / samples) * cal;
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  analogReadResolution(12);

  if (esp_now_init() != ESP_OK) return;
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;  
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);
}

void loop() {
  // 1. Capture All Raw Readings
  myData.volPrim = getSensorData(39, CAL_V_PRI, p2pVP);
  myData.volSec  = getSensorData(36, CAL_V_SEC, p2pVS);
  myData.curPrim = getSensorData(35, CAL_I_PRI, p2pIP);
  myData.curSec1 = getSensorData(32, CAL_I_SEC, p2pIS1);
  myData.curSec2 = getSensorData(33, CAL_I_SEC, p2pIS2);
  myData.curSec3 = getSensorData(34, CAL_I_SEC, p2pIS3);

  // --- STEP 2: OFF-STATE ENFORCEMENT ---
  
  // If Primary Voltage is negligible, the whole system is OFF
  if (myData.volPrim < 10.0) { 
      myData.volPrim = 0.0;
      myData.volSec  = 0.0;
      myData.curPrim = 0.0;
      myData.curSec1 = 0.0;
      myData.curSec2 = 0.0;
      myData.curSec3 = 0.0;
      myData.curLoad = 0.0;
      myData.wattsLoad = 0.0;
  } 
  else {
      // If Primary is ON, check if Secondary has meaningful voltage
      if (myData.volSec < 2.0) {
          myData.volSec = 0.0;
          myData.curSec1 = 0.0;
          myData.curSec2 = 0.0;
          myData.curSec3 = 0.0;
          myData.curLoad = 0.0;
      } else {
          // System is fully operational
          myData.curLoad = myData.curSec1 + myData.curSec2 + myData.curSec3;
      }
      // Calculate real-time wattage
      myData.wattsLoad = myData.volSec * myData.curLoad;
  }

  // 3. DIAGNOSTIC OUTPUT
  Serial.println("\n--- [ TRANSFORMER LIVE MONITOR ] ---");
  Serial.printf("VOLT PRI | %6.1f V | P2P: %ld\n", myData.volPrim, p2pVP);
  Serial.printf("VOLT SEC | %6.1f V | P2P: %ld\n", myData.volSec, p2pVS);
  Serial.println("----------------------------------------------");
  Serial.printf("CURR PRI | %6.2f A | P2P: %ld\n", myData.curPrim, p2pIP);
  Serial.printf("COIL S1  | %6.2f A | P2P: %ld\n", myData.curSec1, p2pIS1);
  Serial.printf("COIL S2  | %6.2f A | P2P: %ld\n", myData.curSec2, p2pIS2);
  Serial.printf("COIL S3  | %6.2f A | P2P: %ld\n", myData.curSec3, p2pIS3);
  Serial.println("----------------------------------------------");
  Serial.printf("TOT POWER| %8.1f W\n", myData.wattsLoad);
  Serial.println("==============================================\n");

  // 4. Transmit cleaned data
  esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));
  
  delay(500); 
}