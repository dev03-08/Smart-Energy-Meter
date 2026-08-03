/************ SMART ENERGY METER (ESP32 + ACS712 + LCD + BLYNK) ************/
#define BLYNK_TEMPLATE_ID   ""
#define BLYNK_TEMPLATE_NAME "Smart IOT Energy Meter"
#define BLYNK_AUTH_TOKEN    ""

#define BLYNK_PRINT Serial

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

/* -------------------- WIFI / BLYNK -------------------- */
char auth[] = BLYNK_AUTH_TOKEN;
char ssid[] = "";  /*hotspot name*/
char pass[] = ""; /*hotspot pass*/

/* -------------------- PINS -------------------- */
#define RELAY_PIN 25          // Relay control pin (2N2222 driver)
#define ACS_PIN   34          // ACS712 OUT pin to GPIO34 (ADC)

/* Relay ON/OFF logic (change if opposite behaviour) */
const int RELAY_ON_LEVEL  = HIGH;
const int RELAY_OFF_LEVEL = LOW;

/* -------------------- LCD -------------------- */
LiquidCrystal_I2C lcd(0x27, 16, 2);

/* -------------------- METER CONSTANTS -------------------- */
const float MAINS_VOLTAGE = 230.0;     // fixed because no voltage sensor
const float UNIT_RATE_RS  = 7.5;       // ₹ per kWh
const float ACS712_SENSITIVITY = 0.100f; // 20A version = 100mV/A

/* RMS sampling window */
const uint16_t SAMPLE_WINDOW_MS = 600;

/* -------------------- RUNTIME VARIABLES -------------------- */
float Irms = 0.0f, PowerW = 0.0f, EnergyKWh = 0.0f, CostRs = 0.0f;
unsigned long lastUpdateMs = 0;

/* -------------------- BLYNK RELAY CONTROL (V5) -------------------- */
BLYNK_WRITE(V5) {
  int btn = param.asInt();
  digitalWrite(RELAY_PIN, btn ? RELAY_ON_LEVEL : RELAY_OFF_LEVEL);

  Blynk.virtualWrite(V5, btn);

  lcd.setCursor(0, 1);
  lcd.print("Relay:");
  lcd.print(btn ? "ON " : "OFF");
}

BLYNK_CONNECTED() {
  Blynk.syncVirtual(V5);
}

/* -------------------- Vpp Measurement -------------------- */
float measureVpp() {
  int rawMin = 4095;
  int rawMax = 0;

  unsigned long start = millis();
  while (millis() - start < SAMPLE_WINDOW_MS) {
    int v = analogRead(ACS_PIN);
    if (v < rawMin) rawMin = v;
    if (v > rawMax) rawMax = v;
  }

  float Vmin = (rawMin * 3.3f) / 4095.0f;
  float Vmax = (rawMax * 3.3f) / 4095.0f;
  return (Vmax - Vmin);
}

/* -------------------- Measurement & Calculation -------------------- */
void readAndCompute() {
  float Vpp = measureVpp();

  // RMS formula: Irms = Vpp / (2√2 × sensitivity)
  const float denom = 2.828f * ACS712_SENSITIVITY;
  float Irms_raw = Vpp / denom;

  /* ✅ CLEAN CURRENT FIX  
     If relay is OFF → force current to zero  
     This removes the 0.5–0.8 A noise when OFF */
  int relayState = digitalRead(RELAY_PIN);
  if (relayState == RELAY_OFF_LEVEL) {
    Irms_raw = 0.0f;
  }

  // Extra noise filtering
  if (Irms_raw < 0.10f) Irms_raw = 0.0f;

  // Smooth display
  Irms = 0.7f * Irms + 0.3f * Irms_raw;

  PowerW = MAINS_VOLTAGE * Irms;

  static unsigned long lastMs = millis();
  unsigned long now = millis();
  float dt_hours = (now - lastMs) / 3600000.0f;
  lastMs = now;

  EnergyKWh += (PowerW / 1000.0f) * dt_hours;
  CostRs     = EnergyKWh * UNIT_RATE_RS;
}

/* -------------------- LCD + Blynk Output -------------------- */
void pushOutputs() {
  lcd.setCursor(0, 0);
  lcd.print("V:"); lcd.print((int)MAINS_VOLTAGE);
  lcd.print(" I:"); lcd.print(Irms, 2);
  lcd.print(" ");

  lcd.setCursor(0, 1);
  lcd.print("P:"); lcd.print(PowerW, 0);
  lcd.print(" E:"); lcd.print(EnergyKWh, 3);

  Blynk.virtualWrite(V0, MAINS_VOLTAGE);
  Blynk.virtualWrite(V1, Irms);
  Blynk.virtualWrite(V2, PowerW);
  Blynk.virtualWrite(V3, EnergyKWh);
  Blynk.virtualWrite(V4, CostRs);
}

/* -------------------- SETUP -------------------- */
void setup() {
  Serial.begin(115200);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_OFF_LEVEL);
  pinMode(ACS_PIN, INPUT);

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Smart Energy");
  lcd.setCursor(0, 1); lcd.print("Booting...");
  delay(1200);
  lcd.clear();

  WiFi.begin(ssid, pass);
  Serial.print("WiFi:");
  while (WiFi.status() != WL_CONNECTED) { delay(400); Serial.print("."); }
  Serial.println("\nWiFi OK");

  Blynk.begin(auth, ssid, pass);

  lcd.setCursor(0, 0); lcd.print("WiFi Connected");
  delay(700);
  lcd.clear();
}

/* -------------------- LOOP -------------------- */
void loop() {
  Blynk.run();
  readAndCompute();

  if (millis() - lastUpdateMs > 1500) {
    pushOutputs();
    lastUpdateMs = millis();
  }
}