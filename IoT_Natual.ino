#include <SoftwareSerial.h>
#include "WiFiEsp.h"
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>

// === Wi-Fi 설정 ===
SoftwareSerial EspSerial(10, 11); // Mega 10=RX, 11=TX
char ssid[] = "IoTsocket";
char pass[] = "12345678";
char server[] = "192.168.0.101";
int port = 5000;
WiFiEspClient client;

// === LCD 설정 ===
LiquidCrystal_I2C lcd(0x27, 16, 2);

// === 센서 핀 ===
#define DHTTYPE DHT11
int pinGnd = 4;         // GND 공급핀
int pinVcc = 3;         // VCC 공급핀
int pinDht = 2;         // DATA
DHT dht1(pinDht, DHTTYPE);

#define IR_PIN 5
#define TRIG 7
#define ECHO 6
#define BUZZER 8
#define JOY_X A2

// === 조도 / 수위 (가상값 또는 연결 시 변경)
float readLux()   { return 300.0; }
float readLevel() { return 10.0; }

float readDistanceCM() {
  digitalWrite(TRIG, LOW); delayMicroseconds(2);
  digitalWrite(TRIG, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG, LOW);
  long duration = pulseIn(ECHO, HIGH, 30000);
  if (duration == 0) return -1;
  return duration / 58.2;
}

// === 조이스틱 기반 LCD 페이지 전환 ===
int lcdPage = 0;
int lastX = 512;
unsigned long lastMove = 0;
bool pageChanged = false;

void updateJoystick() {
  int xVal = analogRead(JOY_X);
  unsigned long now = millis();
  if (abs(xVal - lastX) > 100 && now - lastMove > 300) {
    if (xVal < 400) lcdPage = max(0, lcdPage - 1);
    else if (xVal > 600) lcdPage = min(1, lcdPage + 1);
    lastMove = now;
    lastX = xVal;
    pageChanged = true;
  }
}

void updateLCDByPage(float t, float h, float lux, float lvl, int ir, float dist) {
  lcd.clear();
  if (lcdPage == 0) {
    lcd.setCursor(0, 0);
    lcd.print("Temp: "); lcd.print(t, 1); lcd.print("C");
    lcd.setCursor(0, 1);
    lcd.print("Humi: "); lcd.print(h, 1); lcd.print("%");
  } else if (lcdPage == 1) {
    lcd.setCursor(0, 0);
    lcd.print("Lux:"); lcd.print((int)lux);
    lcd.print(" Lv:"); lcd.print((int)lvl);
    lcd.setCursor(0, 1);
    lcd.print("IR:"); lcd.print(ir == 1 ? "Y" : "N");
    lcd.print(" D:");
    if (dist > 0) lcd.print((int)dist);
    else lcd.print("--");
  }
}

void setup() {
  Serial.begin(9600);
  EspSerial.begin(9600);

  pinMode(pinVcc, OUTPUT);
  pinMode(pinGnd, OUTPUT);
  digitalWrite(pinVcc, HIGH);
  digitalWrite(pinGnd, LOW);
  delay(1000);
  dht1.begin();

  pinMode(IR_PIN, INPUT);
  pinMode(TRIG, OUTPUT);
  pinMode(ECHO, INPUT);
  pinMode(BUZZER, OUTPUT);

  lcd.begin();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("WiFi Connecting...");

  WiFi.init(&EspSerial);
  while (WiFi.begin(ssid, pass) != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi Connected");
  delay(1000);
}

void loop() {
  float t1 = dht1.readTemperature();
  float h1 = dht1.readHumidity();
  float lux = readLux();
  float lvl = readLevel();
  float dist = readDistanceCM();
  int ir = 0;

  if (dist > 0 && dist < 20) {
    ir = 1;
    tone(BUZZER, 1000);
  } else {
    ir = 0;
    noTone(BUZZER);
  }

  if (isnan(t1)) t1 = 0.0;
  if (isnan(h1)) h1 = 0.0;

  Serial.print("IR: "); Serial.println(ir);

  updateJoystick();
  if (pageChanged) {
    updateLCDByPage(t1, h1, lux, lvl, ir, dist);
    pageChanged = false;
  }

  if (client.connect(server, port)) {
    String data = String(t1, 1) + "," + h1 + "," + lux + "," + lvl + "," + ir + "," + dist;
    client.println(data);
    client.flush();
    delay(200);
    client.stop();
    Serial.print("[Sent]: ");
    Serial.println(data);
  } else {
    Serial.println("[Connect Failed]");
  }

  delay(1000);
}
