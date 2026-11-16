#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <HX711_ADC.h>
#include <Preferences.h>
#include <NewPing.h>
#include <WiFi.h>
#include <FirebaseESP32.h>
#include <math.h>

#define WIFI_SSID "wir"
#define WIFI_PASSWORD "kokbisasii"

#define FIREBASE_HOST "https://alat-pendeteksi-imt-default-rtdb.firebaseio.com/"
#define FIREBASE_AUTH "P1dJaDpAejcj9OpEVTrkM83unUu0gzlM4im5yogQ"
#define ID_PERANGKAT "ALAT_IMT_001"

FirebaseData fbData;
FirebaseConfig config;
FirebaseAuth auth;

// SENSOR ULTRASONIK
#define PIN_TRIG 16
#define PIN_ECHO 17

// SENSOR BERAT HX711
#define PIN_DT 19
#define PIN_SCK 18

// TOMBOL
#define TOMBOL_KALIBRASI_TINGGI 14
#define TOMBOL_KALIBRASI_BERAT 26
#define TOMBOL_MULAI_DETEKSI 27

// LCD I2C
#define PIN_SDA 21
#define PIN_SCL 22

#define ALAMAT_LCD 0x27
#define JARAK_MAKSIMUM 400

const float MASSA_REFERENSI = 0.25;
const float KAPASITAS_MAKSIMUM = 200.0;
const int JUMLAH_DATA_TINGGI = 15;
const int JUMLAH_DATA_BERAT = 20;

LiquidCrystal_I2C lcd(ALAMAT_LCD, 20, 4);
HX711_ADC sensorBerat(PIN_DT, PIN_SCK);
NewPing sensorUltrasonik(PIN_TRIG,PIN_ECHO,JARAK_MAKSIMUM);
Preferences memori;

float faktorKalibrasiBerat = 1.0;
float tinggiAcuanSensor = 0.0;
bool beratSudahDikalibrasi = false;
bool tinggiSudahDikalibrasi = false;
byte statusKalibrasiBerat = 0;

float beratBadan = 0.0;
float jarakKepala = 0.0;
float tinggiBadan = 0.0;
float nilaiIMT = 0.0;
float beratMinimum = 0.0;
float beratMaksimum = 0.0;
String kategoriIMT = "";

unsigned long waktuCobaWifi = 0;
const unsigned long INTERVAL_WIFI = 15000;
bool statusWifiSebelumnya = false;

struct StatusTombol {
  bool kondisiStabil = HIGH;
  bool kondisiSebelumnya = HIGH;
  unsigned long waktuPerubahan = 0;
};

StatusTombol tombolTinggi;
StatusTombol tombolBerat;
StatusTombol tombolMulai;

float pembulatan(float nilai, int angkaDesimal) {
  float pengali = pow(10.0, angkaDesimal);
  return roundf(nilai * pengali) / pengali;
}

void setup() {
  Serial.begin(115200);

  // PIN ULTRASONIK
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  digitalWrite(PIN_TRIG, LOW);

  // PIN TOMBOL
  pinMode(TOMBOL_KALIBRASI_TINGGI, INPUT_PULLUP);
  pinMode(TOMBOL_KALIBRASI_BERAT, INPUT_PULLUP);
  pinMode(TOMBOL_MULAI_DETEKSI, INPUT_PULLUP);

  // LCD
  Wire.begin(PIN_SDA, PIN_SCL);
  lcd.init();
  lcd.backlight();
  tampilkanBaris(0, "ALAT PENGUKUR IMT");
  tampilkanBaris(1, "ESP32 + FIREBASE");
  tampilkanBaris(2, "INISIALISASI...");
  tampilkanBaris(3, "MOHON TUNGGU");

  // HX711
  sensorBerat.begin();
  sensorBerat.start(2000, true);

  if (sensorBerat.getTareTimeoutFlag() || sensorBerat.getSignalTimeoutFlag()) {
    tampilkanBaris(0, "ERROR HX711");
    tampilkanBaris(1, "PERIKSA KABEL DT");
    tampilkanBaris(2, "PERIKSA KABEL SCK");
    tampilkanBaris(3, "SISTEM BERHENTI");
    while (true) {
      delay(100);
    }
  }

  // MEMUAT KALIBRASI
  muatDataKalibrasi();
  while (!sensorBerat.update()) {
    delay(1);
  }

  // WIFI
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  waktuCobaWifi = millis();

  // FIREBASE
  mulaiFirebase();

  // TAMPILAN STATUS SISTEM
  tampilkanBaris(0, "SISTEM SIAP");
  tampilkanBaris(1, beratSudahDikalibrasi ? "BERAT: SUDAH" : "BERAT: BELUM");
  tampilkanBaris(2, tinggiSudahDikalibrasi ? "TINGGI: SUDAH" : "TINGGI: BELUM" );
  tampilkanBaris(3, "MEMUAT MENU...");
  delay(2000);
  tampilkanNormal();
}

void loop() {

  // MEMPERBARUI SENSOR HX711
  sensorBerat.update();

  // MEMERIKSA WIFI
  periksaWifi();

  // TOMBOL KALIBRASI TINGGI
  if (tombolDitekan(TOMBOL_KALIBRASI_TINGGI, tombolTinggi)) {
    kalibrasiTinggi();
  }

  // TOMBOL KALIBRASI BERAT
  if (tombolDitekan(TOMBOL_KALIBRASI_BERAT, tombolBerat)) {
    if (statusKalibrasiBerat == 0) {
      mulaiKalibrasiBerat();
    }else if (statusKalibrasiBerat == 2) {
      hitungKalibrasiBerat();
    }
  }

  // MEMERIKSA PROSES TARE
  if (statusKalibrasiBerat == 1) {
    prosesTareBerat();
  }

  // TOMBOL DETEKSI IMT
  if (tombolDitekan(TOMBOL_MULAI_DETEKSI, tombolMulai )) {
    if (statusKalibrasiBerat == 0) {
      mulaiPengukuran();
    }
  }
}
