#include "DHT.h"

#define DHT_PIN 4
#define DHT_TYPE DHT11

DHT dht(DHT_PIN, DHT_TYPE);

void setup() {
  Serial.begin(115200);
  dht.begin();

  delay(2000);
}

void loop() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();

  if (isnan(h) || isnan(t)) {
    Serial.println("FAILED");
  } else {
    Serial.print("Temperature: ");
    Serial.print(t);
    Serial.print(" C   Humidity: ");
    Serial.print(h);
    Serial.println(" %");
  }

  delay(2000);
}