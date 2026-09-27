#include <DHT.h>
#include <LiquidCrystal.h>

// -------------------- DHT11 --------------------
#define DHT_PIN 4
#define DHT_TYPE DHT11

DHT dht(DHT_PIN, DHT_TYPE);

// -------------------- LCD --------------------
// LiquidCrystal(RS, EN, D4, D5, D6, D7)
LiquidCrystal lcd(13, 14, 27, 26, 25, 33);

void setup() {
  Serial.begin(115200);

  // Initialize DHT11
  dht.begin();

  // Initialize LCD
  lcd.begin(16, 2);

  // Startup message
  lcd.setCursor(0, 0);
  lcd.print("DHT11 Sensor");
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");

  delay(2000);
  lcd.clear();
}

void loop() {
  // Read humidity and temperature
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature(); // Celsius

  // Check whether reading failed
  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("Failed to read from DHT11 sensor!");

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("DHT11 Error!");
    lcd.setCursor(0, 1);
    lcd.print("Check wiring");

    delay(2000);
    return;
  }

  // -------------------- Serial Monitor --------------------
  Serial.print("Temperature: ");
  Serial.print(temperature);
  Serial.println(" C");

  Serial.print("Humidity: ");
  Serial.print(humidity);
  Serial.println(" %");

  // -------------------- LCD --------------------
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("Temp: ");
  lcd.print(temperature, 1);
  lcd.write(223); // Degree symbol
  lcd.print("C");

  lcd.setCursor(0, 1);
  lcd.print("Humidity: ");
  lcd.print(humidity, 1);
  lcd.print("%");

  // DHT11 should not be read too frequently
  delay(2000);
}