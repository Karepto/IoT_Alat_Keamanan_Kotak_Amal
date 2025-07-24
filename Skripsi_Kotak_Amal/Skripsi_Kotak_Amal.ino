#include <WiFi.h>e
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <HardwareSerial.h>
#include <TinyGPS++.h>
#include <Adafruit_Fingerprint.h>
#include <EEPROM.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2); 

#define RELAY_PIN 4

// Hardware serial untuk sensor fingerprint dengan pin yang sama
#define FINGERPRINT_RX 13
#define FINGERPRINT_TX 12
HardwareSerial fingerprintSerial(2); // Menggunakan UART #2 untuk fingerprint
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&fingerprintSerial);

#define MAX_FINGERPRINT_USERS 50
#define MAX_NAME_LENGTH 20

#define EEPROM_FINGERPRINT_NAMES_START 200

#define RX_ESP32CAM 18 
#define TX_ESP32CAM 19  
#define ESP32CAM_TRIGGER_PIN 5 

#define TRIG_PIN 14
#define ECHO_PIN 27
#define JARAK_AMBANG 5
#define DURASI_PENUH 3000
unsigned long waktuMulai = 0;
bool penuh = false;
bool notifikasiPenuhSudahDikirim = false;

TinyGPSPlus gps;
#define RXD2 16
#define TXD2 17
// Variabel untuk status dan diagnostik GPS
unsigned long lastGPSUpdate = 0;
unsigned long gpsReadingsCount = 0;
unsigned long gpsValidReadings = 0;
int gpsMaxSatellites = 0;

#define VIBRATION_SENSOR_PIN 34 
#define BUZZER_PIN 26           
#define AMBANG_BATAS 4000

const char* ssid = "Xiaomi 13T renjak";
const char* password = "12345678";

#define BOT_TOKEN ""

WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

String masterPassword = "admin123";
const int eepromAddrPassword = 0;
const int passwordMaxLength = 20;

String adminChatID = "";
const int eepromAddrChatID = 100;
const int chatIDMaxLength = 15;

unsigned long botLastTime = 0;
const unsigned long botRequestDelay = 1000;

unsigned long lastSyncTime = 0;
const unsigned long syncInterval = 3600000;
bool chatIDSyncRequired = true;

bool esp32CamConnected = false;
unsigned long lastESP32CAMStatusCheck = 0;
const unsigned long esp32CamCheckInterval = 30000; 

bool addNewFingerprint = false;
int newFingerprintId = 0;
String newFingerprintName = "";
bool waitingForFingerprintName = false;
bool waitingForDeleteConfirmation = false;
int fingerprintToDelete = -1;
bool waitingForNewPassword = false;
bool waitingForPassword = false;

unsigned long lastLCDUpdate = 0;
const unsigned long lcdUpdateInterval = 500;
boolean lcdBacklight = true;

// Variabel untuk mencegah foto saat fingerprint
bool fingerprintModeActive = false;
unsigned long fingerModeTimeoutStart = 0;
#define FINGERPRINT_TIMEOUT 10000 // 10 detik timeout setelah verifikasi sidik jari

// Variabel untuk melacak tindakan solenoid
bool solenoidActive = false;
unsigned long solenoidActivationTime = 0;
#define SOLENOID_SAFE_PERIOD 8000 // Periode aman 8 detik setelah solenoid aktif

// Variabel untuk status sensor (aktif/non-aktif)
bool sensorActive = true; // Default: sensor aktif
unsigned long sensorDeactivationTime = 0;
#define SENSOR_AUTO_REACTIVATION_TIME 15 * 60 * 1000 // 15 menit auto-reaktivasi

void displayOnLCD(String line1, String line2, bool clearFirst = true) {
  if (clearFirst) {
    lcd.clear();
  }
  lcd.setCursor(0, 0);

  if (line1.length() > 16) {
    line1 = line1.substring(0, 16);
  }
  if (line2.length() > 16) {
    line2 = line2.substring(0, 16);
  }
  
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
}

void blinkMessage(String line1, String line2, int times = 3) {
  for (int i = 0; i < times; i++) {
    displayOnLCD(line1, line2);
    delay(500);
    lcd.clear();
    delay(500);
  }
}

void scrollText(String text, int row, int delayTime = 300) {
  int len = text.length();
  if (len <= 16) {
    lcd.setCursor(0, row);
    lcd.print(text);
    return;
  }
  
  for (int i = 0; i <= len - 16; i++) {
    lcd.setCursor(0, row);
    lcd.print(text.substring(i, i + 16));
    delay(delayTime);
  }
}

// Fungsi prototypes
void saveNameToEEPROM(int id, String name);
String getNameFromEEPROM(int id);
void clearNameFromEEPROM(int id);
bool deleteFingerprint(int id);
String readPasswordFromEEPROM();
void savePasswordToEEPROM(String newPassword);
String readChatIDFromEEPROM();
void saveChatIDToEEPROM(String newChatID);
String createKeyboard();
void handleNewMessages(int numNewMessages);
int findAvailableFingerprintID();
int enrollFingerprint(int id);
float bacaJarak();
void kirimNotifikasiKotakPenuh();
void sendGPSData();
void sendGPSDataToTelegram(bool isNormalAccess);
void sendTakePhotoRequest();
void sendChatIDToESP32CAM();
bool checkESP32CAMConnection();
void clearESP32SerialBuffer();
int cariSidikJari();
void buzzerError(int durasi = 1000);
void bukaSolenoid();
void resetAdminCompletely();
void triggerESP32CAMHardware();
void handleESP32CAMResponse();
void updateLCDDisplay();
bool isValidGetaran();
void activateSensors();
void deactivateSensors();
void checkSensorAutoReactivation();

// Fungsi untuk menghidupkan sensor
void activateSensors() {
  sensorActive = true;
  Serial.println("SENSOR DIAKTIFKAN: Semua sensor sekarang aktif!");
  displayOnLCD("SENSOR AKTIF", "Monitoring ON");
  delay(2000);
}

// Fungsi untuk mematikan sensor
void deactivateSensors() {
  sensorActive = false;
  sensorDeactivationTime = millis();
  Serial.println("SENSOR DINONAKTIFKAN: Sensor getaran, jarak, dan GPS tidak akan mengirim notifikasi!");
  displayOnLCD("SENSOR NONAKTIF", "Maintenance Mode");
  delay(2000);
}

// Fungsi untuk mengecek apakah sensor perlu diaktifkan kembali secara otomatis
void checkSensorAutoReactivation() {
  if (!sensorActive && (millis() - sensorDeactivationTime > SENSOR_AUTO_REACTIVATION_TIME)) {
    activateSensors();
    if (adminChatID != "") {
      bot.sendMessage(adminChatID, "⚠️ PERHATIAN: Sensor telah diaktifkan kembali secara otomatis setelah 15 menit nonaktif.");
    }
  }
}

// Fungsi untuk menentukan apakah getaran valid
// Getaran tidak valid jika terjadi selama periode solenoid aktif ATAU sensor dinonaktifkan
bool isValidGetaran() {
  // Cek apakah sensor nonaktif
  if (!sensorActive) {
    Serial.println("Getaran terdeteksi, tapi diabaikan karena sensor dalam mode nonaktif.");
    return false;
  }
  
  // Cek apakah dalam periode aman solenoid
  if (solenoidActive || (millis() - solenoidActivationTime < SOLENOID_SAFE_PERIOD)) {
    Serial.println("Getaran terdeteksi, tapi diabaikan karena solenoid baru saja aktif.");
    return false;
  }
  
  return true;
}

void updateLCDDisplay() {
  static int displayMode = 0;
  static unsigned long lastModeChange = 0;
  const unsigned long modeChangeInterval = 3000;
  
  if (millis() - lastModeChange > modeChangeInterval) {
    displayMode = (displayMode + 1) % 6;  // Sekarang ada 6 mode display
    lastModeChange = millis();
  }
  
  switch (displayMode) {
    case 0: 
      displayOnLCD("Kotak Amal", "Aktif...");
      break;
      
    case 1: 
      {
        String wifiStatus = WiFi.status() == WL_CONNECTED ? "WiFi: OK" : "WiFi: Error";
        String adminStatus = adminChatID != "" ? "Admin: Ada" : "Admin: Belum";
        displayOnLCD(wifiStatus, adminStatus);
      }
      break;
      
    case 2:
      {
        float jarak = bacaJarak();
        String jarakStr = "Jarak: " + String(jarak, 1) + "cm";
        String statusKotak = jarak <= JARAK_AMBANG ? "Status: PENUH" : "Status: Normal";
        displayOnLCD(jarakStr, statusKotak);
      }
      break;
      
    case 3:
      {
        // Tampilan GPS yang lebih informatif
        String gpsStatus;
        if (gps.location.isValid()) {
          gpsStatus = "GPS: OK";
        } else if (gps.satellites.isValid() && gps.satellites.value() > 0) {
          gpsStatus = "GPS: " + String(gps.satellites.value()) + " sat";
        } else {
          gpsStatus = "GPS: Searching";
        }
        
        String location = "";
        if (gps.location.isValid()) {
          location = String(gps.location.lat(), 4);
        } else {
          // Tampilkan waktu sejak update terakhir
          unsigned long timeSinceLastUpdate = (millis() - lastGPSUpdate) / 1000;
          location = "Wait " + String(timeSinceLastUpdate) + "s";
        }
        displayOnLCD(gpsStatus, location);
      }
      break;
      
    case 4:
      {
        String camStatus = esp32CamConnected ? "Camera: OK" : "Camera: Error";
        String fingerprintStatus = finger.verifyPassword() ? "Fingerp: OK" : "Fingerp: Error";
        displayOnLCD(camStatus, fingerprintStatus);
      }
      break;
      
    case 5:
      {
        // Tambah mode display untuk status sensor
        String sensorStatus = sensorActive ? "Sensor: AKTIF" : "Sensor: NONAKTIF";
        String infoText = sensorActive ? "Mode Normal" : "Mode Maintenance";
        displayOnLCD(sensorStatus, infoText);
      }
      break;
  }
}

void resetAdminCompletely() {
  adminChatID = "";
  
  for (int i = 0; i < chatIDMaxLength; i++) {
    EEPROM.write(eepromAddrChatID + i, 0);
  }
  EEPROM.commit();
  
  Serial.println("Admin telah direset sepenuhnya.");
  displayOnLCD("Admin Reset", "Berhasil!");
  delay(2000);
  
  chatIDSyncRequired = true;
}

String readPasswordFromEEPROM() {
  String storedPassword = "";
  for (int i = 0; i < passwordMaxLength; i++) {
    char c = EEPROM.read(eepromAddrPassword + i);
    if (c == 0) break;
    storedPassword += c;
  }
  return storedPassword;
}

void savePasswordToEEPROM(String newPassword) {
  for (int i = 0; i < passwordMaxLength; i++) {
    if (i < newPassword.length()) {
      EEPROM.write(eepromAddrPassword + i, newPassword[i]);
    } else {
      EEPROM.write(eepromAddrPassword + i, 0);
    }
  }
  EEPROM.commit();
}

String readChatIDFromEEPROM() {
  String storedChatID = "";
  for (int i = 0; i < chatIDMaxLength; i++) {
    char c = EEPROM.read(eepromAddrChatID + i);
    if (c == 0) break;
    storedChatID += c;
  }
  return storedChatID;
}

void saveChatIDToEEPROM(String newChatID) {
  for (int i = 0; i < chatIDMaxLength; i++) {
    if (i < newChatID.length()) {
      EEPROM.write(eepromAddrChatID + i, newChatID[i]);
    } else {
      EEPROM.write(eepromAddrChatID + i, 0);
    }
  }
  EEPROM.commit();
}

void saveNameToEEPROM(int id, String name) {
  int baseAddr = EEPROM_FINGERPRINT_NAMES_START + (id * MAX_NAME_LENGTH);
  
  if (name.length() >= MAX_NAME_LENGTH) {
    name = name.substring(0, MAX_NAME_LENGTH - 1);
  }
  
  for (int i = 0; i < MAX_NAME_LENGTH; i++) {
    if (i < name.length()) {
      EEPROM.write(baseAddr + i, name[i]);
    } else {
      EEPROM.write(baseAddr + i, 0); // Null terminator atau padding
    }
  }
  EEPROM.commit();
}

String getNameFromEEPROM(int id) {
  int baseAddr = EEPROM_FINGERPRINT_NAMES_START + (id * MAX_NAME_LENGTH);
  String name = "";
  
  for (int i = 0; i < MAX_NAME_LENGTH; i++) {
    char c = EEPROM.read(baseAddr + i);
    if (c == 0) break; // Berhenti jika menemui null terminator
    name += c;
  }
  
  return name;
}

void clearNameFromEEPROM(int id) {
  int baseAddr = EEPROM_FINGERPRINT_NAMES_START + (id * MAX_NAME_LENGTH);
  
  for (int i = 0; i < MAX_NAME_LENGTH; i++) {
    EEPROM.write(baseAddr + i, 0);
  }
  EEPROM.commit();
}

bool deleteFingerprint(int id) {
  Serial.println("Mencoba menghapus sidik jari dengan ID #" + String(id));
  displayOnLCD("Hapus Sidik", "ID #" + String(id));
  
  uint8_t p = finger.deleteModel(id);
  
  if (p == FINGERPRINT_OK) {
    clearNameFromEEPROM(id);
    Serial.println("Sidik jari berhasil dihapus!");
    displayOnLCD("Hapus Berhasil", "ID #" + String(id));
    delay(2000);
    return true;
  } else {
    Serial.println("Gagal menghapus sidik jari. Kode error: " + String(p));
    displayOnLCD("Hapus Gagal", "Error: " + String(p));
    delay(2000);
    return false;
  }
}

String createKeyboard() {
  String keyboard = "[[\"Buka Kotak Amal\"],";
  keyboard += "[\"Tambah Sidik Jari\"],";
  keyboard += "[\"Hapus Sidik Jari\"],";
  keyboard += "[\"Update Password\"],";
  keyboard += "[\"Status Sistem\"],";
  keyboard += "[\"Daftar Sidik Jari\"],";
  keyboard += "[\"Matikan Sensor\"],";
  keyboard += "[\"Hidupkan Sensor\"],";
  keyboard += "[\"Reset Admin\"],";
  keyboard += "[\"Reset Notifikasi\"],";
  keyboard += "[\"Ambil Foto\"]],";
  keyboard += "[\"Kirim Lokasi\"]]";
  return keyboard;
}

// Fungsi untuk buzzer berbunyi ketika sidik jari salah
void buzzerError(int durasi) {
  digitalWrite(BUZZER_PIN, LOW);  // Buzzer ON (active LOW)
  
  // Tambahan pesan serial dan LCD saat sidik jari salah
  Serial.println("❌ AKSES DITOLAK! Sidik jari tidak dikenal atau tidak sah!");
  displayOnLCD("AKSES DITOLAK!", "SIDIK JARI SALAH");
  
  delay(durasi);
  digitalWrite(BUZZER_PIN, HIGH); // Buzzer OFF
  
  // Tambahan pesan setelah buzzer mati
  Serial.println("Silakan coba dengan sidik jari yang terdaftar.");
}

float bacaJarak() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long durasi = pulseIn(ECHO_PIN, HIGH);
  
  float jarak = durasi * 0.0343 / 2;
  
  return jarak;
}

void kirimNotifikasiKotakPenuh() {
  // Cek apakah sensor nonaktif
  if (!sensorActive) {
    Serial.println("Kotak terdeteksi penuh, tapi notifikasi diabaikan karena sensor nonaktif.");
    return;
  }
  
  if (adminChatID == "") {
    Serial.println("Tidak dapat mengirim notifikasi: Admin belum diatur");
    displayOnLCD("Notif Gagal", "No Admin");
    return;
  }
  
  Serial.println("Mengirim notifikasi kotak amal penuh ke Telegram...");
  displayOnLCD("Kirim Notif", "Kotak Penuh");
  
  String message = "📢 INFORMASI: Kotak Amal PENUH! 📢\n\n";
  message += "Kotak amal telah penuh dan perlu dikosongkan.\n";
  
  if (gps.location.isValid()) {
    message += "\nLokasi Kotak Amal:\n";
    message += "Latitude: " + String(gps.location.lat(), 6) + "\n";
    message += "Longitude: " + String(gps.location.lng(), 6) + "\n";
    
    bot.sendMessage(adminChatID, message);
    String mapsUrl = "https://maps.google.com/maps?q=" + 
                     String(gps.location.lat(), 6) + "," + 
                     String(gps.location.lng(), 6);
    bot.sendMessage(adminChatID, "Lokasi di Google Maps: " + mapsUrl);
  } else {
    bot.sendMessage(adminChatID, message);
  }
  
  notifikasiPenuhSudahDikirim = true;
  displayOnLCD("Notif Terkirim", "Kotak Penuh");
  delay(2000);
}

// Fungsi yang dimodifikasi untuk mengirimkan pesan berbeda berdasarkan jenis akses
void sendGPSDataToTelegram(bool isNormalAccess) {
  if (adminChatID == "") {
    Serial.println("Tidak dapat mengirim notifikasi: Admin belum diatur");
    return;
  }
  
  if (isNormalAccess) {
    displayOnLCD("Kirim Info", "Akses Sah");
  } else {
    displayOnLCD("! PERHATIAN !", "Getaran Kuat");
  }
  
  if (gps.location.isValid()) {
    String message;
    
    if (isNormalAccess) {
      message = "🔐 INFORMASI: Kotak Amal dibuka dengan cara sah 🔐\n\n";
    } else {
      message = "⚠️ PERINGATAN: Terdeteksi aktivitas mencurigakan! ⚠️\n\n";
    }
    
    message += "Lokasi Kotak Amal:\n";
    message += "Latitude: " + String(gps.location.lat(), 6) + "\n";
    message += "Longitude: " + String(gps.location.lng(), 6) + "\n\n";
    
    // message += "Waktu Kejadian: ";
    // if (gps.date.isValid()) {
    //   message += String(gps.date.day()) + "/" + String(gps.date.month()) + "/" + String(gps.date.year()) + " ";
    // }
    
    // if (gps.time.isValid()) {
    //   int jam = gps.time.hour();
    //   int menit = gps.time.minute();
    //   int detik = gps.time.second();
      
    //   message += String(jam < 10 ? "0" : "") + String(jam) + ":";
    //   message += String(menit < 10 ? "0" : "") + String(menit) + ":";
    //   message += String(detik < 10 ? "0" : "") + String(detik) + " UTC";
    // }
    
    bot.sendMessage(adminChatID, message);
    
    if (!isNormalAccess) {
      String mapsUrl = "https://maps.google.com/maps?q=" + 
                       String(gps.location.lat(), 6) + "," + 
                       String(gps.location.lng(), 6);
      bot.sendMessage(adminChatID, "Lokasi di Google Maps: " + mapsUrl);
    }
  } else {
    if (isNormalAccess) {
      bot.sendMessage(adminChatID, "🔐 INFORMASI: Kotak Amal dibuka dengan cara sah. Lokasi GPS belum tersedia.");
    } else {
      bot.sendMessage(adminChatID, "⚠️ PERINGATAN: Terdeteksi aktivitas mencurigakan! Lokasi GPS belum tersedia.");
    }
  }
}

// FUNGSI GPS YANG SUDAH DIOPTIMASI
void sendGPSData() {
  // Beri waktu lebih lama untuk membaca data GPS
  unsigned long startTime = millis();
  bool dataRead = false;
  
  // Lebih banyak informasi diagnostik
  gpsReadingsCount++;
  
  // Cek selama 300ms untuk data GPS
  while (millis() - startTime < 300) {
    while (Serial2.available() > 0) {
      char c = Serial2.read();
      dataRead = true;
      
      // Proses data GPS
      if (gps.encode(c)) {
        // Update waktu terakhir data GPS valid
        lastGPSUpdate = millis();
        
        // Catat jika lokasi valid
        if (gps.location.isValid()) {
          gpsValidReadings++;
        }
        
        // Catat jumlah satelit maksimum
        if (gps.satellites.isValid() && gps.satellites.value() > gpsMaxSatellites) {
          gpsMaxSatellites = gps.satellites.value();
        }
      }
    }
    
    // Beri waktu lebih sedikit antara pembacaan untuk mencegah CPU terlalu sibuk
    delay(5);
  }
  
  // Jika data berhasil dibaca, cetak informasi debug
  if (dataRead) {
    Serial.print("GPS - ");
    
    if (gps.location.isValid()) {
      Serial.print("Lokasi valid: ");
      Serial.print(gps.location.lat(), 6);
      Serial.print(", ");
      Serial.println(gps.location.lng(), 6);
    } else {
      Serial.print("Lokasi belum valid. ");
      
      if (gps.satellites.isValid()) {
        Serial.print("Satelit: ");
        Serial.print(gps.satellites.value());
        Serial.print(" (max: ");
        Serial.print(gpsMaxSatellites);
        Serial.print(") | ");
      }
      
      Serial.print("Valid/Total: ");
      Serial.print(gpsValidReadings);
      Serial.print("/");
      Serial.println(gpsReadingsCount);
    }
  }
}

void clearESP32SerialBuffer() {
  while (Serial1.available()) {
    Serial1.read();
  }
}

bool checkESP32CAMConnection() {
  clearESP32SerialBuffer();
  
  Serial1.println("PING");
  
  unsigned long startTime = millis();
  String response = "";
  
  while (millis() - startTime < 2000) {
    if (Serial1.available()) {
      char c = Serial1.read();
      response += c;
      
      if (response.indexOf("PONG") >= 0) {
        return true;
      }
    }
    delay(10);
  }
  
  return false;
}

void sendTakePhotoRequest() {
  displayOnLCD("Ambil Foto", "Processing...");
  
  clearESP32SerialBuffer();
  
  Serial1.println("TAKE_PHOTO");
  
  if (!esp32CamConnected) {
    triggerESP32CAMHardware();
  }
}

void sendChatIDToESP32CAM() {
  if (adminChatID == "") {
    return;
  }
  
  clearESP32SerialBuffer();
  
  Serial1.println("CHATID:" + adminChatID);
  
  unsigned long startTime = millis();
  String response = "";
  bool confirmed = false;
  
  while (millis() - startTime < 2000) {
    if (Serial1.available()) {
      char c = Serial1.read();
      response += c;
      
      if (response.indexOf("CHATID_CONFIRMED") >= 0) {
        confirmed = true;
        break;
      }
    }
    delay(10);
  }
  
  chatIDSyncRequired = !confirmed;
}

void triggerESP32CAMHardware() {
  digitalWrite(ESP32CAM_TRIGGER_PIN, LOW);
  delay(100);  // Hold LOW for 100 ms
  digitalWrite(ESP32CAM_TRIGGER_PIN, HIGH);
}

// Fungsi untuk memeriksa sidik jari - dioptimalkan untuk kecepatan
int cariSidikJari() {
  int result = finger.getImage();
  if (result != FINGERPRINT_OK) return -1;

  // Tandai mode fingerprint aktif untuk mencegah pengiriman foto
  fingerprintModeActive = true;
  fingerModeTimeoutStart = millis();

  result = finger.image2Tz();
  if (result != FINGERPRINT_OK) return -1;

  result = finger.fingerFastSearch();
  
  if (result == FINGERPRINT_OK) {
    // Tambahan pesan serial saat sidik jari ditemukan
    Serial.println("✅ SIDIK JARI TERDETEKSI DAN DIKENALI! ID: " + String(finger.fingerID));
    return finger.fingerID;
  } else if (result == FINGERPRINT_NOTFOUND) {
    // Sidik jari terdeteksi tapi tidak cocok dengan database
    // Aktifkan buzzer selama 1 detik
    buzzerError(1000);
    return -2; // Kode khusus untuk sidik jari tidak dikenal
  }
  
  return -1;
}

void bukaSolenoid() {
  // Tampilkan pesan lebih detil di LCD dan Serial
  Serial.println("🔓 AKSES DIBERIKAN! Membuka solenoid lock...");
  displayOnLCD("AKSES DIBERIKAN", "Membuka Kotak...");
  
  // Set flag solenoid aktif
  solenoidActive = true;
  solenoidActivationTime = millis();
  
  // Segera aktivasi selenoid tanpa delay tambahan
  digitalWrite(RELAY_PIN, HIGH);
  
  // Notifikasi bahwa kotak sedang terbuka
  Serial.println("Kotak Amal terbuka, harap menunggu 5 detik...");
  
  // Hitung mundur pada LCD
  for (int i = 5; i >= 1; i--) {
    displayOnLCD("Kotak Terbuka", "Tunggu " + String(i) + " detik");
    delay(1000);
  }
  
  digitalWrite(RELAY_PIN, LOW);
  Serial.println("⚠️ PERHATIAN: Solenoid terkunci kembali.");
  displayOnLCD("Kotak Terkunci", "Terima Kasih");
  delay(1500);
  
  // Reset flag solenoid
  solenoidActive = false;
}

int findAvailableFingerprintID() {
  Serial.println("Mencari ID sidik jari yang tersedia...");
  displayOnLCD("Cari ID", "Tunggu...");
  
  for (int id = 1; id <= 127; id++) {
    uint8_t p = finger.loadModel(id);
    
    if (p == FINGERPRINT_PACKETRECIEVEERR) {
      delay(50); 
      continue;
    }
    
    if (p == FINGERPRINT_BADLOCATION || p != FINGERPRINT_OK) {
      Serial.println("Slot kosong ditemukan: ID #" + String(id));
      displayOnLCD("ID Tersedia", "ID #" + String(id));
      delay(1000);
      return id;
    }
  }
  
  Serial.println("Semua slot sidik jari terpakai atau error!");
  displayOnLCD("Memori Penuh", "Hapus dulu!");
  delay(2000);
  return -1;
}

int enrollFingerprint(int id) {
  int p;
  
  if (id < 1 || id > 127) {
    bot.sendMessage(adminChatID, "Error: ID harus antara 1 - 127!");
    displayOnLCD("Error ID", "Range: 1-127");
    delay(2000);
    return false;
  }
  
  p = finger.loadModel(id);
  if (p == FINGERPRINT_OK) {
    bot.sendMessage(adminChatID, "Error: ID #" + String(id) + " sudah digunakan!");
    displayOnLCD("ID Terpakai", "ID #" + String(id));
    delay(2000);
    return false;
  }
  
  // Tandai mode fingerprint aktif
  fingerprintModeActive = true;
  
  bot.sendMessage(adminChatID, "Letakkan jari di sensor...");
  Serial.println("Letakkan jari di sensor...");
  displayOnLCD("Letakkan Jari", "Pada Sensor");
  
  while ((p = finger.getImage()) != FINGERPRINT_OK) {
    yield(); // Allow ESP32 to process WiFi/BT tasks
  }
  
  bot.sendMessage(adminChatID, "Sidik jari terdeteksi, mengonversi...");
  displayOnLCD("Jari Terdeteksi", "Konversi...");
  
  p = finger.image2Tz(1);
  if (p != FINGERPRINT_OK) {
    bot.sendMessage(adminChatID, "Error saat mengonversi gambar pertama!");
    displayOnLCD("Error Konversi", "Gambar 1");
    delay(2000);
    return false;
  }
  
  bot.sendMessage(adminChatID, "Angkat jari, lalu tempel lagi...");
  displayOnLCD("Angkat Jari", "Lalu Tempel Lagi");
  delay(2000);
  
  while ((p = finger.getImage()) != FINGERPRINT_NOFINGER) {
    yield();
  }
  
  bot.sendMessage(adminChatID, "Sekarang letakkan kembali jari di sensor...");
  displayOnLCD("Letakkan Lagi", "Jari Anda");
  
  while ((p = finger.getImage()) != FINGERPRINT_OK) {
    yield();
  }
  
  bot.sendMessage(adminChatID, "Sidik jari kedua terdeteksi, mengonversi...");
  displayOnLCD("Jari Terdeteksi", "Konversi 2...");
  
  p = finger.image2Tz(2);
  if (p != FINGERPRINT_OK) {
    bot.sendMessage(adminChatID, "Error saat mengonversi gambar kedua!");
    displayOnLCD("Error Konversi", "Gambar 2");
    delay(2000);
    return false;
  }
  
  bot.sendMessage(adminChatID, "Menyimpan sidik jari...");
  displayOnLCD("Menyimpan", "Sidik Jari...");
  
  p = finger.createModel();
  if (p != FINGERPRINT_OK) {
    bot.sendMessage(adminChatID, "Error: Sidik jari tidak cocok!");
    displayOnLCD("Error!", "Tidak Cocok");
    delay(2000);
    return false;
  }
  
  p = finger.storeModel(id);
  if (p != FINGERPRINT_OK) {
    bot.sendMessage(adminChatID, "Error saat menyimpan sidik jari!");
    displayOnLCD("Error Simpan", "Coba Lagi");
    delay(2000);
    return false;
  }
  
  saveNameToEEPROM(id, newFingerprintName);
  
  String successMsg = "✅ Sukses mendaftarkan sidik jari dengan ID #" + String(id);
  successMsg += " untuk " + newFingerprintName;
  bot.sendMessage(adminChatID, successMsg);
  
  displayOnLCD("Berhasil!", newFingerprintName);
  delay(2000);
  displayOnLCD("ID #" + String(id), "Tersimpan");
  delay(2000);
  
  return true;
}

void handleNewMessages(int numNewMessages) {
  Serial.println("Jumlah pesan baru: " + String(numNewMessages));
  
  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = bot.messages[i].chat_id;
    String text = bot.messages[i].text;
    String from_name = bot.messages[i].from_name;
    
    Serial.println("Chat ID: " + chat_id + ", Pesan: " + text);
    
    if (chat_id == "") {
      Serial.println("WARNING: chat_id kosong!");
      continue;
    }
    
    if (text == "/admin") {
      if (adminChatID == "") {
        Serial.println("Mendaftarkan admin baru: " + chat_id);
        adminChatID = chat_id;
        saveChatIDToEEPROM(adminChatID);
        
        displayOnLCD("Admin Baru", from_name);
        delay(2000);
        
        String message = "Selamat! Anda sekarang admin untuk kotak amal ini.\n";
        message += "Admin Chat ID: " + adminChatID + " telah disimpan ke sistem.";
        bot.sendMessage(chat_id, message);
        
        chatIDSyncRequired = true;
        
        String welcome = "Selamat datang, Admin " + from_name + "!\n";
        welcome += "Gunakan perintah berikut untuk mengontrol kotak amal:\n\n";
        welcome += "/buka - Masukkan password untuk membuka kotak\n";
        welcome += "/tambah_sidik - Tambah sidik jari baru (perlu password)\n";
        welcome += "/hapus_sidik - Hapus sidik jari (perlu password)\n";
        welcome += "/daftar_sidik - Lihat daftar pengguna sidik jari\n";
        welcome += "/update_password - Update password (perlu verifikasi sidik jari)\n";
        welcome += "/status - Cek status sistem\n";
        welcome += "/matikan_sensor - Matikan sensor untuk maintenance\n";
        welcome += "/hidup_sensor - Hidupkan kembali sensor\n";
        welcome += "/reset_admin - Reset admin (perlu password)\n";
        welcome += "/reset_notifikasi - Reset status notifikasi kotak penuh\n";
        welcome += "/ambil_foto - Minta ESP32-CAM mengambil foto\n";
        welcome += "/cek_gps - Diagnostik status GPS\n";
        welcome += "/kirim_lokasi - Untuk Mengirimkan Lokasi Kotak Amal\n";
        
        bot.sendMessageWithReplyKeyboard(chat_id, welcome, "", createKeyboard(), true);
      } else if (chat_id == adminChatID) {
        bot.sendMessage(chat_id, "Anda sudah terdaftar sebagai admin!");
      } else {
        bot.sendMessage(chat_id, "Maaf, kotak amal ini sudah memiliki admin. Hubungi admin untuk informasi lebih lanjut.");
      }
      return;
    }
     
    if (text == "/debug_reset_admin" && chat_id != "") {
      resetAdminCompletely();
      bot.sendMessage(chat_id, "Admin telah direset secara paksa. Kirim /admin untuk mendaftar sebagai admin baru.");
      return;
    }

    // Check for /kirim lokasi command
    if ((text == "/kirim_lokasi" || text == "Kirim Lokasi") && chat_id == adminChatID) {
      if (gps.location.isValid()) {
            String mapsUrl = "https://maps.google.com/maps?q=" + 
                     String(gps.location.lat(), 6) + "," + 
                     String(gps.location.lng(), 6);
    bot.sendMessage(adminChatID, "Lokasi di Google Maps: " + mapsUrl);
      } else {
        bot.sendMessage(adminChatID, "⚠️ GPS tidak valid. Coba lagi nanti.");
      }
      return;
    }
    
    // Perintah baru untuk diagnostik GPS
    if ((text == "/cek_gps" || text == "Cek GPS") && chat_id == adminChatID) {
      String gpsStatus = "📡 Diagnostik GPS:\n\n";
      
      // Baca data GPS lebih banyak sebelum menampilkan status
      for (int i = 0; i < 5; i++) {
        sendGPSData();
        delay(200);
      }
      
      if (gps.location.isValid()) {
        gpsStatus += "✅ Status: LOKASI VALID\n";
        gpsStatus += "Latitude: " + String(gps.location.lat(), 6) + "\n";
        gpsStatus += "Longitude: " + String(gps.location.lng(), 6) + "\n";
      } else {
        gpsStatus += "⚠️ Status: LOKASI BELUM VALID\n";
      }
      
      if (gps.satellites.isValid()) {
        gpsStatus += "Jumlah satelit saat ini: " + String(gps.satellites.value()) + "\n";
        gpsStatus += "Jumlah satelit maksimum: " + String(gpsMaxSatellites) + "\n";
      } else {
        gpsStatus += "Informasi satelit belum tersedia\n";
      }
      
      gpsStatus += "Waktu sejak update terakhir: " + String((millis() - lastGPSUpdate) / 1000) + " detik\n";
      gpsStatus += "Rasio pembacaan valid: " + String(gpsValidReadings) + "/" + String(gpsReadingsCount) + "\n";
      
      // if (gps.time.isValid() && gps.date.isValid()) {
      //   gpsStatus += "\nWaktu GPS: ";
      //   gpsStatus += String(gps.date.day()) + "/" + String(gps.date.month()) + "/" + String(gps.date.year()) + " ";
      //   gpsStatus += String(gps.time.hour()) + ":" + String(gps.time.minute()) + ":" + String(gps.time.second()) + " UTC\n";
      // }
      
      gpsStatus += "\nTips jika GPS tidak mendapatkan lokasi:\n";
      gpsStatus += "1. Bawa alat ke luar ruangan dengan pandangan langsung ke langit\n";
      gpsStatus += "2. Tunggu 5-10 menit untuk mendapatkan fix pertama\n";
      gpsStatus += "3. Periksa koneksi antara GPS dan ESP32 (RX/TX dan power)\n";
      gpsStatus += "4. Pastikan antena GPS tidak terhalang benda logam\n";
      
      bot.sendMessage(adminChatID, gpsStatus);
      displayOnLCD("Diagn. GPS", "Check Telegram");
      return;
    }
    
    // Handle perintah matikan sensor
    if ((text == "/matikan_sensor" || text == "Matikan Sensor") && chat_id == adminChatID) {
      deactivateSensors();
      bot.sendMessage(chat_id, "✅ Semua sensor berhasil dinonaktifkan! Anda dapat mengambil uang kotak amal tanpa memicu alarm. Sensor akan otomatis aktif kembali setelah 15 menit, atau gunakan /hidup_sensor untuk mengaktifkan manual.");
      return;
    }
    
    // Handle perintah hidupkan sensor
    if ((text == "/hidup_sensor" || text == "Hidupkan Sensor") && chat_id == adminChatID) {
      activateSensors();
      bot.sendMessage(chat_id, "✅ Semua sensor berhasil diaktifkan kembali! Sistem sekarang kembali ke mode normal.");
      return;
    }
    
    if (waitingForFingerprintName && chat_id == adminChatID) {
      if (text.length() > 0) {
        newFingerprintName = text;
        waitingForFingerprintName = false;
        
        bot.sendMessage(adminChatID, "Nama '" + newFingerprintName + "' akan ditetapkan untuk sidik jari ID #" + String(newFingerprintId));
        displayOnLCD("Nama Diterima", newFingerprintName);
        delay(2000);
        addNewFingerprint = true;
        
        return;
      } else {
        bot.sendMessage(adminChatID, "Nama tidak boleh kosong. Silakan masukkan nama pengguna:");
        return;
      }
    }
    
    if (waitingForDeleteConfirmation && chat_id == adminChatID) {
      int id = -1;
      
      if (text.startsWith("#")) {
        id = text.substring(1).toInt();
      } else {
        id = text.toInt();
      }
      
      if (text == "Ya" && fingerprintToDelete > 0) {
        bot.sendMessage(chat_id, "Menghapus sidik jari dengan ID #" + String(fingerprintToDelete) + "...");
        
        if (deleteFingerprint(fingerprintToDelete)) {
          bot.sendMessage(chat_id, "✅ Sidik jari dengan ID #" + String(fingerprintToDelete) + " berhasil dihapus!");
        } else {
          bot.sendMessage(chat_id, "❌ Gagal menghapus sidik jari. Pastikan ID valid dan sensor berfungsi.");
        }
        
        waitingForDeleteConfirmation = false;
        fingerprintToDelete = -1;
        return;
      } else if (text == "Tidak") {
        bot.sendMessage(chat_id, "Penghapusan sidik jari dibatalkan.");
        displayOnLCD("Hapus Dibatal", "");
        delay(2000);
        waitingForDeleteConfirmation = false;
        fingerprintToDelete = -1;
        return;
      }
      
      if (id > 0 && id <= 127) {
        fingerprintToDelete = id;
        
        uint8_t p = finger.loadModel(id);
        if (p != FINGERPRINT_OK) {
          bot.sendMessage(chat_id, "❌ Error: ID #" + String(id) + " tidak ditemukan dalam database!");
          displayOnLCD("ID Tidak Ada", "ID #" + String(id));
          delay(2000);
          waitingForDeleteConfirmation = false;
          return;
        }
        
        String nama = getNameFromEEPROM(id);
        String confirmMessage = "Anda yakin ingin menghapus sidik jari dengan ID #" + String(id);
        
        if (nama.length() > 0) {
          confirmMessage += " (" + nama + ")";
          displayOnLCD("Hapus " + nama + "?", "Ya/Tidak");
        } else {
          displayOnLCD("Hapus ID#" + String(id) + "?", "Ya/Tidak");
        }
        
        confirmMessage += "?\nKirim 'Ya' untuk mengkonfirmasi atau 'Tidak' untuk membatalkan.";
        bot.sendMessage(chat_id, confirmMessage);
      } else {
        bot.sendMessage(chat_id, "ID tidak valid. Silakan masukkan ID sidik jari yang valid (1-127) atau ketik 'Tidak' untuk membatalkan.");
      }
      
      return;
    }
    
    if (text == "/reset_admin" || text == "Reset Admin") {
      bot.sendMessage(chat_id, "Masukkan password untuk reset admin dengan format: reset:PASSWORD");
      displayOnLCD("Perlu Password", "reset:PASSWORD");
      return;
    }
    
    if (text.startsWith("reset:") && text.substring(6) == masterPassword) {
      resetAdminCompletely();
      bot.sendMessage(chat_id, "Admin telah direset. Gunakan /admin untuk menjadi admin baru.");
      
      chatIDSyncRequired = true;
      return;
    }
    
    if ((text == "/reset_notifikasi" || text == "Reset Notifikasi") && chat_id == adminChatID) {
      notifikasiPenuhSudahDikirim = false;
      bot.sendMessage(chat_id, "Notifikasi kotak penuh telah direset. Akan ada notifikasi baru jika kotak penuh lagi.");
      displayOnLCD("Notif Reset", "Berhasil");
      delay(2000);
      return;
    }
    
    if ((text == "/ambil_foto" || text == "Ambil Foto") && chat_id == adminChatID) {
      bot.sendMessage(chat_id, "Mengambil foto dengan ESP32-CAM...");
      sendTakePhotoRequest();
      return;
    }
    
    if (adminChatID == "") {
      bot.sendMessage(chat_id, "Belum ada admin yang terdaftar. Kirim '/admin' untuk mendaftar sebagai admin pertama.");
      displayOnLCD("No Admin", "Kirim /admin");
      return;
    }
    
    bool isAdmin = (chat_id == adminChatID);
    if (!isAdmin) {
      bot.sendMessage(chat_id, "Maaf, hanya admin yang bisa mengontrol kotak amal ini.");
      displayOnLCD("Bukan Admin", "Akses Ditolak");
      delay(2000);
      return;
    }
    
    if (text == "/start") {
      String welcome = "Selamat datang, " + from_name + "!\n";
      
      if (isAdmin) {
        welcome += "Anda adalah Admin kotak amal.\n\n";
        welcome += "Gunakan perintah berikut untuk mengontrol kotak amal:\n";
        welcome += "/buka - Masukkan password untuk membuka kotak\n";
        welcome += "/tambah_sidik - Tambah sidik jari baru (perlu password)\n";
        welcome += "/hapus_sidik - Hapus sidik jari (perlu password)\n";
        welcome += "/daftar_sidik - Lihat daftar pengguna sidik jari\n";
        welcome += "/update_password - Update password (perlu verifikasi sidik jari)\n";
        welcome += "/status - Cek status sistem\n";
        welcome += "/matikan_sensor - Matikan sensor untuk maintenance\n";
        welcome += "/hidup_sensor - Hidupkan kembali sensor\n";
        welcome += "/reset_admin - Reset admin (perlu password)\n";
        welcome += "/reset_notifikasi - Reset status notifikasi kotak penuh\n";
        welcome += "/ambil_foto - Minta ESP32-CAM mengambil foto\n";
        welcome += "/cek_gps - Diagnostik status GPS\n";
        
        bot.sendMessageWithReplyKeyboard(chat_id, welcome, "", createKeyboard(), true);
      } else {
        welcome += "Untuk mengakses fitur kotak amal, hubungi admin.\n";
        bot.sendMessage(chat_id, welcome);
      }
    }
    
    if (text == "/buka" || text == "Buka Kotak Amal") {
      bot.sendMessage(chat_id, "Masukkan password untuk membuka kotak amal:");
      displayOnLCD("Perlu Password", "Untuk Buka");
      waitingForPassword = true;
      return;
    }
    
    if (waitingForPassword && text == masterPassword) {
      bot.sendMessage(chat_id, "Password benar! Membuka kotak amal...");
      displayOnLCD("Password OK", "Membuka...");
      bukaSolenoid();
      waitingForPassword = false;
      
      // Kirim notifikasi akses normal (tanpa foto) ke Telegram
      sendGPSDataToTelegram(true);
      return;
    }
    else if (waitingForPassword && text != masterPassword && !text.startsWith("/")) {
      bot.sendMessage(chat_id, "❌ Password salah! Silakan coba lagi atau gunakan perintah lain.");
      displayOnLCD("Password Salah", "Coba Lagi");
      delay(2000);
      return;
    }
    
    if (text == "/tambah_sidik" || text == "Tambah Sidik Jari") {
      bot.sendMessage(chat_id, "Masukkan password untuk menambahkan sidik jari baru dengan format: sidik:PASSWORD");
      displayOnLCD("Format:", "sidik:PASSWORD");
      return;
    }
    
    if (text == "/hapus_sidik" || text == "Hapus Sidik Jari") {
      bot.sendMessage(chat_id, "Masukkan password untuk menghapus sidik jari dengan format: hapus:PASSWORD");
      displayOnLCD("Format:", "hapus:PASSWORD");
      return;
    }
    
    if (text.startsWith("hapus:") && text.substring(6) == masterPassword) {
      String daftar = "Daftar Pengguna Sidik Jari:\n\n";
      bool adaPengguna = false;
      
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Daftar Sidik:");
      
      for (int id = 1; id <= 127; id++) {
        uint8_t p = finger.loadModel(id);
        
        if (p == FINGERPRINT_OK) {
          String nama = getNameFromEEPROM(id);
          daftar += "ID #" + String(id) + ": " + (nama.length() > 0 ? nama : "Tidak bernama") + "\n";
          adaPengguna = true;
        }
      }
      
      if (!adaPengguna) {
        daftar += "Belum ada pengguna sidik jari yang terdaftar.";
        bot.sendMessage(chat_id, daftar);
        displayOnLCD("Daftar Kosong", "Belum ada user");
        delay(2000);
      } else {
        daftar += "\nMasukkan ID sidik jari yang ingin dihapus:";
        bot.sendMessage(chat_id, daftar);
        displayOnLCD("Pilih ID", "Untuk Hapus");
        waitingForDeleteConfirmation = true;
      }
      return;
    }
    
    if (text.startsWith("sidik:") && text.substring(6) == masterPassword) {
      newFingerprintId = findAvailableFingerprintID();
      
      if (newFingerprintId == -1) {
        bot.sendMessage(chat_id, "Error: Database sidik jari penuh!");
        displayOnLCD("Database Penuh", "Hapus dulu!");
        delay(2000);
      } else {
        String message = "ID yang akan digunakan: " + String(newFingerprintId) + "\n";
        message += "Silakan masukkan nama pengguna sidik jari:";
        bot.sendMessage(chat_id, message);
        
        displayOnLCD("ID: " + String(newFingerprintId), "Masukkan Nama");
        
        waitingForFingerprintName = true;
      }
      return;
    }
    
    if (text == "/daftar_sidik" || text == "Daftar Sidik Jari") {
      String daftar = "Daftar Pengguna Sidik Jari:\n\n";
      bool adaPengguna = false;
      
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Daftar User:");
      int count = 0;
      
      for (int id = 1; id <= 127; id++) {
        uint8_t p = finger.loadModel(id);
        
        if (p == FINGERPRINT_OK) {
          String nama = getNameFromEEPROM(id);
          daftar += "ID #" + String(id) + ": " + (nama.length() > 0 ? nama : "Tidak bernama") + "\n";
          adaPengguna = true;
          count++;
        }
      }
      
      if (!adaPengguna) {
        daftar += "Belum ada pengguna sidik jari yang terdaftar.";
        displayOnLCD("Daftar Kosong", "0 User");
      } else {
        displayOnLCD("Total User:", String(count) + " orang");
      }
      
      bot.sendMessage(chat_id, daftar);
      delay(3000);
      return;
    }
    
    if (text == "/update_password" || text == "Update Password") {
      bot.sendMessage(chat_id, "Silakan letakkan jari di sensor untuk verifikasi sebelum update password.");
      displayOnLCD("Verifikasi Dulu", "Letakkan Jari");
      
      waitingForNewPassword = true;
      return;
    }
    
    if (waitingForNewPassword && text.length() >= 6) {
      masterPassword = text;
      savePasswordToEEPROM(masterPassword);
      
      bot.sendMessage(chat_id, "Password berhasil diperbarui menjadi: " + masterPassword);
      displayOnLCD("Password Baru", "Tersimpan!");
      delay(2000);
      waitingForNewPassword = false;
      return;
    }
    
    if (text == "/status" || text == "Status Sistem") {
      String status = "📊 Status Sistem Keamanan Kotak Amal 📊\n\n";
      status += "- WiFi: Terhubung\n";
      status += "- IP: " + WiFi.localIP().toString() + "\n";
      status += "- Sensor Sidik Jari: " + String(finger.verifyPassword() ? "✅ OK" : "❌ Error") + "\n";
      status += "- Admin Chat ID: " + adminChatID + "\n";
      
      displayOnLCD("Cek Status", "Tunggu...");
      
      esp32CamConnected = checkESP32CAMConnection();
      status += "- ESP32-CAM: " + String(esp32CamConnected ? "✅ Terhubung" : "❌ Tidak Terhubung") + "\n";
      status += "- Mode Fingerprint: " + String(fingerprintModeActive ? "Aktif" : "Tidak Aktif") + "\n";
      status += "- Solenoid Status: " + String(solenoidActive ? "Aktif" : "Tidak Aktif") + "\n";
      
      // Tambah status sensor
      status += "- Status Sensor: " + String(sensorActive ? "✅ AKTIF" : "❌ NONAKTIF") + "\n";
      if (!sensorActive) {
        unsigned long timeLeft = (SENSOR_AUTO_REACTIVATION_TIME - (millis() - sensorDeactivationTime)) / 60000;
        status += "  (Aktif otomatis dalam ~" + String(timeLeft) + " menit)\n";
      }
      
      float jarakTerkini = bacaJarak();
      status += "- Sensor Ultrasonik: " + String(jarakTerkini) + " cm";
      if (jarakTerkini <= JARAK_AMBANG) {
        if (notifikasiPenuhSudahDikirim) {
          status += " (PENUH - Notifikasi sudah dikirim)";
        } else {
          status += " (PENUH - Menunggu konfirmasi)";
        }
      } else {
        status += " (Normal)";
      }
      status += "\n";
      
      // Informasi GPS yang lebih lengkap
      if (gps.location.isValid()) {
        status += "- GPS: ✅ OK (Lokasi valid)\n";
        status += "  Lokasi: " + String(gps.location.lat(), 6) + ", " + String(gps.location.lng(), 6) + "\n";
        
        if (gps.satellites.isValid()) {
          status += "  Satelit: " + String(gps.satellites.value()) + "\n";
        }
      } else if (gps.satellites.isValid() && gps.satellites.value() > 0) {
        status += "- GPS: 🔄 Terhubung (Satelit: " + String(gps.satellites.value()) + ")\n";
        status += "  Menunggu lokasi valid, sudah " + String(gpsReadingsCount) + " pembacaan\n";
      } else {
        status += "- GPS: 🔄 Searching\n";
        status += "  Gunakan /cek_gps untuk diagnostik\n";
      }
      
      status += "- Getaran Terakhir: " + String(analogRead(VIBRATION_SENSOR_PIN)) + " (Ambang: " + String(AMBANG_BATAS) + ")\n";
      status += "- Password Saat Ini: " + masterPassword + "\n";
      
      bot.sendMessage(chat_id, status);
      displayOnLCD("Status Sent", "Check Telegram");
      delay(2000);
      return;
    }
  }
}

void handleESP32CAMResponse() {
  if (Serial1.available()) {
    String response = "";
    unsigned long startTime = millis();
    
    while (Serial1.available() || (millis() - startTime < 100)) {
      if (Serial1.available()) {
        char c = Serial1.read();
        response += c;
        startTime = millis();
      }
      delay(1);
    }
    
    if (response.length() > 2) {
      if (response.indexOf("PHOTO_SENT") >= 0) {
        displayOnLCD("Foto Terkirim", "Via Telegram");
        delay(2000);
      } else if (response.indexOf("PONG") >= 0) {
        esp32CamConnected = true;
      } else if (response.indexOf("CHATID_CONFIRMED") >= 0) {
        chatIDSyncRequired = false;
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  
  // Inisialisasi LCD
  Wire.begin();
  lcd.init();
  lcd.backlight();
  displayOnLCD("Sistem Start", "Mohon Tunggu...");
  
  // Inisialisasi EEPROM dengan ukuran cukup untuk data sidik jari
  EEPROM.begin(1500);
  
  // Baca password dari EEPROM
  String storedPassword = readPasswordFromEEPROM();
  if (storedPassword.length() > 0) {
    masterPassword = storedPassword;
  } else {
    // Simpan password default jika belum ada
    savePasswordToEEPROM(masterPassword);
  }
  
  // Baca Chat ID admin dari EEPROM
  String storedChatID = readChatIDFromEEPROM();
  bool validChatID = true;
  if (storedChatID.length() > 0) {
    for (int i = 0; i < storedChatID.length(); i++) {
      if (!isDigit(storedChatID.charAt(i))) {
        validChatID = false;
        break;
      }
    }
    
    if (validChatID) {
      adminChatID = storedChatID;
      Serial.println("Admin Chat ID diambil dari EEPROM: " + adminChatID);
      displayOnLCD("Admin Loaded", adminChatID);
      delay(2000);
    } else {
      resetAdminCompletely();
      Serial.println("Admin Chat ID tidak valid! ID direset.");
    }
  } else {
    Serial.println("Admin Chat ID belum diatur. Tunggu pesan dari admin.");
    displayOnLCD("No Admin", "Send /admin");
    delay(2000);
  }
  
  // Inisialisasi sensor ultrasonik
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  
  // Inisialisasi sensor getaran dan buzzer
  pinMode(VIBRATION_SENSOR_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, HIGH); // Buzzer OFF (active LOW)
  
  // Inisialisasi pin untuk ESP32-CAM
  pinMode(ESP32CAM_TRIGGER_PIN, OUTPUT);
  digitalWrite(ESP32CAM_TRIGGER_PIN, HIGH); // Inactive HIGH
  
  // Koneksi WiFi
  displayOnLCD("Connecting WiFi", ssid);
  WiFi.begin(ssid, password);
  Serial.print("Menghubungkan ke WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    lcd.setCursor(15, 1);
    lcd.print(".");
  }
  
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: " + WiFi.localIP().toString());
  displayOnLCD("WiFi Connected", WiFi.localIP().toString());
  delay(2000);
  
  // Skip certificate verification
  secured_client.setCACert(NULL);
  secured_client.setInsecure();
  
  // Inisialisasi relay solenoid
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  // Setup untuk GPS menggunakan hardware Serial2
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
  displayOnLCD("Inisialisasi GPS", "Harap Tunggu...");
  delay(1000);
  
  // Baca data GPS beberapa kali untuk inisialisasi
  Serial.println("Menunggu koneksi GPS awal...");
  for (int i = 0; i < 10; i++) {
    displayOnLCD("GPS Init: " + String(i*10) + "%", "Tunggu...");
    // Proses GPS lebih lama untuk inisialisasi
    for (int j = 0; j < 5; j++) {
      sendGPSData();
      delay(100);
    }
  }
  
  if (gps.satellites.isValid()) {
    Serial.println("GPS terdeteksi, satelit: " + String(gps.satellites.value()));
    displayOnLCD("GPS OK", "Satelit: " + String(gps.satellites.value()));
  } else {
    Serial.println("GPS belum mendapatkan satelit, masih searching...");
    displayOnLCD("GPS Searching", "Tunggu Koneksi");
  }
  delay(2000);
  
  // Inisialisasi hardware serial untuk ESP32-CAM
  Serial1.begin(115200, SERIAL_8N1, RX_ESP32CAM, TX_ESP32CAM);
  
  // Inisialisasi hardware serial untuk fingerprint sensor - menggunakan baudrate original
  fingerprintSerial.begin(57600, SERIAL_8N1, FINGERPRINT_RX, FINGERPRINT_TX);
  
  Serial.println("Mencari sensor fingerprint...");
  displayOnLCD("Cek Fingerprint", "Sensor...");
  
  if (finger.verifyPassword()) {
    Serial.println("Sensor fingerprint terdeteksi! ✅");
    displayOnLCD("Fingerprint OK", "Sensor Ready");
    delay(2000);
    
    if (adminChatID != "") {
      bot.sendMessage(adminChatID, "Sistem Keamanan Kotak Amal Online! Sensor fingerprint terdeteksi.");
    } else {
      Serial.println("Admin belum diatur. Silakan kirim /admin ke bot untuk mendaftar!");
    }
  } else {
    Serial.println("Sensor fingerprint TIDAK terdeteksi! ❌ Periksa koneksi.");
    displayOnLCD("Fingerprint", "ERROR!");
    delay(2000);
    
    if (adminChatID != "") {
      bot.sendMessage(adminChatID, "⚠️ PERINGATAN: Sensor fingerprint TIDAK terdeteksi! Periksa koneksi.");
    }
  }
  
  // Cek koneksi ESP32-CAM
  displayOnLCD("Cek ESP32-CAM", "Mohon Tunggu");
  delay(2000);
  
  esp32CamConnected = checkESP32CAMConnection();
  
  if (esp32CamConnected) {
    displayOnLCD("ESP32-CAM OK", "Connected");
    delay(2000);
    
    if (adminChatID != "") {
      sendChatIDToESP32CAM();
    }
  } else {
    displayOnLCD("ESP32-CAM", "Not Found!");
    delay(2000);
  }
  
  // Tes buzzer sebentar
  digitalWrite(BUZZER_PIN, LOW);  // Buzzer ON
  displayOnLCD("Test Buzzer", "OK");
  delay(200);
  digitalWrite(BUZZER_PIN, HIGH); // Buzzer OFF
  
  displayOnLCD("Sistem Aktif", "Ver 1.8");
  delay(2000);
}

void loop() {
  // Cek apakah sensor perlu diaktifkan kembali secara otomatis
  checkSensorAutoReactivation();
  
  // Prioritaskan pembacaan sidik jari
  int id = cariSidikJari();
  if (id > 0) {
    Serial.print("Sidik jari dikenali! ID: ");
    Serial.println(id);
    
    String nama = getNameFromEEPROM(id);
    String displayName = nama.length() > 0 ? nama : "Pengguna Tidak Dikenal";
    
    displayOnLCD("Selamat Datang", displayName);
    // delay(500); // Mengurangi delay sebelum buka selenoid
    
    // Segera buka selenoid tanpa delay tambahan
    bukaSolenoid();
    
    // Kirim pesan akses normal via Telegram tanpa ambil foto
    if (adminChatID != "") {
      String message = "🔓 Kotak amal dibuka dengan sidik jari 🔓\n";
      message += "ID: " + String(id) + "\n";
      message += "Nama: " + displayName;
      
      // if (gps.time.isValid() && gps.date.isValid()) {
      //   message += "\nWaktu: ";
      //   message += String(gps.date.day()) + "/" + String(gps.date.month()) + "/" + String(gps.date.year()) + " ";
      //   message += String(gps.time.hour()) + ":" + String(gps.time.minute()) + ":" + String(gps.time.second()) + " UTC";
      // }
      
      bot.sendMessage(adminChatID, message);
      
      // Kirim notifikasi akses normal (tanpa foto) ke Telegram
      sendGPSDataToTelegram(true);
    }
    
        
    if (waitingForNewPassword) {
      bot.sendMessage(adminChatID, "Sidik jari berhasil diverifikasi. Silakan masukkan password baru (minimal 6 karakter):");
      displayOnLCD("Verifikasi OK", "Kirim Pass Baru");
    }
  }
  
  // Cek timeout mode fingerprint
  if (fingerprintModeActive && (millis() - fingerModeTimeoutStart > FINGERPRINT_TIMEOUT)) {
    fingerprintModeActive = false;
    Serial.println("Mode fingerprint timeout");
  }
  
  // Update tampilan LCD
  if (millis() - lastLCDUpdate > lcdUpdateInterval) {
    if (!waitingForPassword && !waitingForFingerprintName && 
        !waitingForDeleteConfirmation && !waitingForNewPassword &&
        !addNewFingerprint) {
      updateLCDDisplay();
    }
    lastLCDUpdate = millis();
  }
  
  // PEMROSESAN GPS - DIOPTIMASI
  // Baca data GPS secara teratur untuk memastikan pembaruan berkelanjutan
  sendGPSData();
  
  // Cek pesan Telegram
  unsigned long currentMillis = millis();
  if (currentMillis - botLastTime > botRequestDelay) {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    
    if (numNewMessages) {
      if (numNewMessages > 0 && waitingForPassword) {
        String text = bot.messages[0].text;
        if (text.startsWith("/") && text != "/buka") {
          waitingForPassword = false;
        }
      }
      
      handleNewMessages(numNewMessages);
    }
    
    botLastTime = currentMillis;
  }
  
  // Cek koneksi ESP32-CAM secara berkala
  if (currentMillis - lastESP32CAMStatusCheck > esp32CamCheckInterval) {
    esp32CamConnected = checkESP32CAMConnection();
    
    if (esp32CamConnected && chatIDSyncRequired && adminChatID != "") {
      sendChatIDToESP32CAM();
    }
    
    lastESP32CAMStatusCheck = currentMillis;
  }
  
  // Tangani respon dari ESP32-CAM
  handleESP32CAMResponse();
  
  // Sinkronisasi Chat ID dengan ESP32-CAM secara berkala
  if (currentMillis - lastSyncTime > syncInterval) {
    if (adminChatID != "" && esp32CamConnected) {
      sendChatIDToESP32CAM();
    }
    lastSyncTime = currentMillis;
  }
  
  // Cek sensor getaran
  int nilaiGetaran = analogRead(VIBRATION_SENSOR_PIN);
  
  if (nilaiGetaran > AMBANG_BATAS && isValidGetaran()) {
    Serial.println("⚠️ Getaran tinggi terdeteksi: " + String(nilaiGetaran) + " ⚠️");
    digitalWrite(BUZZER_PIN, LOW); // Aktifkan buzzer
    
    blinkMessage("! BAHAYA !", "Getaran Kuat", 5);
    
    sendGPSData();
    
    // Kirim notifikasi getaran mencurigakan (pesan berbeda)
    sendGPSDataToTelegram(false);
    
    // Hanya ambil foto jika memang mencurigakan (bukan karena baru buka kotak)
    if (isValidGetaran()) {
      sendTakePhotoRequest();
    }
    
    delay(5000); 
    digitalWrite(BUZZER_PIN, HIGH); // Matikan buzzer
  }
  
  // Cek sensor ultrasonik untuk deteksi kotak penuh
  float jarak = bacaJarak();
  
  if (jarak <= JARAK_AMBANG) {
    if (!penuh) {
      waktuMulai = millis();
      penuh = true;
      Serial.println("Potensi kotak penuh terdeteksi, menunggu konfirmasi...");
      displayOnLCD("Cek Kotak", "Mungkin Penuh");
    } else if ((millis() - waktuMulai >= DURASI_PENUH) && !notifikasiPenuhSudahDikirim) {
      Serial.println("⚠️ Kotak Amal PENUH! ⚠️");
      displayOnLCD("! PENUH !", "Perlu Dikosong");
      
      kirimNotifikasiKotakPenuh();
    }
  } else {
    if (penuh) {
      Serial.println("Kotak tidak lagi terdeteksi penuh.");
    }
    penuh = false;
  }

  // Jika mode tambah sidik jari aktif
  if (addNewFingerprint) {
    int result = enrollFingerprint(newFingerprintId);
    if (result != true) {
      bot.sendMessage(adminChatID, "Gagal menambahkan sidik jari. Kode error: " + String(result));
      displayOnLCD("Gagal Tambah", "Error: " + String(result));
      delay(2000);
    }
    addNewFingerprint = false;
  }

  yield(); // Allow ESP32 background tasks to run
}
