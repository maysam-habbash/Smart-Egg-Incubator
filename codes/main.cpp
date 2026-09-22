#define BLYNK_TEMPLATE_ID "TMPLxxxxxxx"
#define BLYNK_TEMPLATE_NAME "SmartIncubator"
#define BLYNK_AUTH_TOKEN "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <DHT.h>

// ---- Wi-Fi / Blynk ----
char ssid[] = "YOUR_SSID";
char pass[] = "YOUR_PASSWORD";

// ---- Pins ----
#define DHT_PIN 4
#define HEATER_PIN 16
#define HUMID_PIN 17
#define SERVO_PIN 18
#define BUZZER_PIN 19
DHT dht(DHT_PIN, DHT22);

// ---- Shared data & RTOS objects ----
struct SensorData_t { float temperature; float humidity; uint32_t ts; bool valid; };
QueueHandle_t sensorQueue; // holds the latest SensorData_t (length 1, overwrite)
SemaphoreHandle_t displayMutex; // guards the I2C bus / LCD
SemaphoreHandle_t motorMutex; // guards the turning motor
SemaphoreHandle_t alarmSemaphore; // given by watchdog, taken by alarm/notify path
TimerHandle_t turnTimer; // periodic egg-turn trigger
volatile int incubationDay = 0;
volatile bool lockdown = false;
volatile bool manualMode = false;

// ---- Task: read sensor, publish latest value ----
void SensorReadTask(void *pv) {
  for (;;) {
    SensorData_t d;
    d.temperature = dht.readTemperature();
    d.humidity = dht.readHumidity();
    d.ts = millis();
    d.valid = !isnan(d.temperature) && !isnan(d.humidity);
    xQueueOverwrite(sensorQueue, &d); // always keep only the latest reading
    vTaskDelay(pdMS_TO_TICKS(1000)); // NFR1: 1 s period
  }
}

// ---- Task: heater bang-bang/PID control ----
void HeaterCtrlTask(void *pv) {
  SensorData_t d;
  for (;;) {
    if (xQueuePeek(sensorQueue, &d, 0) == pdTRUE && d.valid) {
    bool heaterOn = d.temperature < 37.2; // TODO: replace with PID / hysteresis band
    digitalWrite(HEATER_PIN, heaterOn ? HIGH : LOW);
    Blynk.virtualWrite(V2, heaterOn);
  } else {
    digitalWrite(HEATER_PIN, LOW); // fail-safe: no valid reading -> heater OFF
  }
  vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
// ---- Task: humidity control (setting vs. lockdown target) ----
void HumidityCtrlTask(void *pv) {
  SensorData_t d;
  for (;;) {
  if (xQueuePeek(sensorQueue, &d, 0) == pdTRUE && d.valid) {
    float target = lockdown ? 67.0 : 52.0; // TODO: tune per Section 3 setpoints
    bool humidOn = d.humidity < target;
    digitalWrite(HUMID_PIN, humidOn ? HIGH : LOW);
    Blynk.virtualWrite(V3, humidOn);
  }
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

// ---- Software timer callback: trigger a turn every N hours ----
void turnTimerCallback(TimerHandle_t xTimer) {
  if (!lockdown) xTaskNotifyGive(eggTurnTaskHandle); // wake EggTurnTask
}

// ---- Task: perform the physical egg-turn ----
TaskHandle_t eggTurnTaskHandle;
void EggTurnTask(void *pv) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY); // wait for timer/manual trigger
    if (xSemaphoreTake(motorMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
      // TODO: drive servo/motor to tilt tray, confirm with limit switch, NFR7: <=10 s
      Blynk.virtualWrite(V4, 1);
      vTaskDelay(pdMS_TO_TICKS(3000)); // placeholder for actual turn motion
      Blynk.virtualWrite(V4, 0);
      xSemaphoreGive(motorMutex);
    }
  }
}

// ---- Task: highest-priority safety watchdog ----
void SafetyWatchdogTask(void *pv) {
  SensorData_t d;
  for (;;) {
    bool fault = (xQueuePeek(sensorQueue, &d, 0) != pdTRUE) || !d.valid
    || d.temperature < 30 || d.temperature > 42
    || d.humidity < 20 || d.humidity > 90;
    if (fault) {
      digitalWrite(HEATER_PIN, LOW); // fail-safe
      digitalWrite(BUZZER_PIN, HIGH);
      xSemaphoreGive(alarmSemaphore); // let the notify path handle Blynk alert
    } else {
      digitalWrite(BUZZER_PIN, LOW);
    }
    vTaskDelay(pdMS_TO_TICKS(500)); // NFR2: <=500 ms
  }
}

// ---- Task: local LCD/OLED status ----
void DisplayTask(void *pv) {
  SensorData_t d;
  for (;;) {
    if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
      xQueuePeek(sensorQueue, &d, 0);
      // TODO: lcd.print(...) temperature, humidity, day counter
      xSemaphoreGive(displayMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// ---- Task: Blynk connection + periodic publish ----
void BlynkSyncTask(void *pv) {
  SensorData_t d;
  uint32_t lastPublish = 0;
  for (;;) {
    Blynk.run();
    if (millis() - lastPublish > 2000) { // FR6: every <=2 s
      if (xQueuePeek(sensorQueue, &d, 0) == pdTRUE && d.valid) {
        Blynk.virtualWrite(V0, d.temperature);
        Blynk.virtualWrite(V1, d.humidity);
        Blynk.virtualWrite(V5, incubationDay);
      }
      lastPublish = millis();
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ---- Blynk virtual-pin handlers (manual/test mode) ----
BLYNK_WRITE(V6) { manualMode = param.asInt(); }
BLYNK_WRITE(V8) { if (manualMode) xTaskNotifyGive(eggTurnTaskHandle); }

void setup() {
  Serial.begin(115200);

  pinMode(HEATER_PIN, OUTPUT); pinMode(HUMID_PIN, OUTPUT); pinMode(BUZZER_PIN, OUTPUT);
  dht.begin();
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
  sensorQueue = xQueueCreate(1, sizeof(SensorData_t));
  displayMutex = xSemaphoreCreateMutex();
  motorMutex = xSemaphoreCreateMutex();
  alarmSemaphore = xSemaphoreCreateBinary();
  xTaskCreatePinnedToCore(SensorReadTask, "Sensor", 4096, NULL, 3, NULL, 1);
  xTaskCreatePinnedToCore(HeaterCtrlTask, "Heater", 2048, NULL, 3, NULL, 1);
  xTaskCreatePinnedToCore(HumidityCtrlTask, "Humid", 2048, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(EggTurnTask, "Turn", 2048, NULL, 2, &eggTurnTaskHandle, 1);
  xTaskCreatePinnedToCore(SafetyWatchdogTask, "Watchdog", 2048, NULL, 4, NULL, 1);
  xTaskCreatePinnedToCore(DisplayTask, "Display",2048, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(BlynkSyncTask, "Blynk", 4096, NULL, 2, NULL, 0);
  turnTimer = xTimerCreate("TurnTimer", pdMS_TO_TICKS(4UL * 60 * 60 * 1000),
  pdTRUE, NULL, turnTimerCallback); // every 4 h, TODO tune
  xTimerStart(turnTimer, 0);
}

void loop() {
  // Intentionally empty: all work happens in FreeRTOS tasks above.
  vTaskDelay(pdMS_TO_TICKS(1000));
}