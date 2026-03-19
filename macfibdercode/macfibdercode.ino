#include "WiFi.h"

void setup(){
  Serial.begin(115200);
  delay(1000); // Give the hardware a second to stabilize
  
  WiFi.mode(WIFI_STA);
  WiFi.STA.begin(); // Force the station interface to start
  
  Serial.println("");
  Serial.print("ESP32 Board MAC Address:  ");
  Serial.println(WiFi.macAddress());
}
 
void loop(){}