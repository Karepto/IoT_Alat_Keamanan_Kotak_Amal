#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_camera.h"
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <EEPROM.h>

// Konfigurasi WiFi (gunakan kredensial yang sama dengan Wemos D1 R32)
const char* ssid = "Xiaomi 13T renjak";
const char* password = "12345678";

// Konfigurasi Telegram Bot (gunakan token yang sama dengan Wemos D1 R32)
#define BOT_TOKEN "7764421612:AAHJYHSp1i1F-L3zuAs6VA7TP2RqZvh2MFg"

// Pin untuk menerima sinyal dari Wemos D1 R32
#define TRIGGER_PIN 13  // Pin trigger dari Wemos D1 R32

// Pin Serial pada ESP32-CAM
// ESP32-CAM menggunakan U0T (GPIO1) dan U0R (GPIO3) untuk komunikasi serial
// dengan komputer, jadi kita tidak perlu mendefinisikan pin serial khusus
// Serial default sudah menggunakan pin ini

// Flash LED Pin untuk ESP32-CAM
#define FLASH_LED_PIN 4

// EEPROM untuk menyimpan Chat ID
#define EEPROM_SIZE 64
#define CHAT_ID_ADDR 0

// Chat ID akan diterima dari Wemos D1 R32
String adminChatID = "";
bool isChatIDReceived = false;

// Variabel untuk mencegah pengambilan foto berulang dalam waktu singkat
unsigned long lastPhotoTime = 0;
const unsigned long photoMinInterval = 10000; // Minimal 10 detik antara dua foto

// Buffer untuk menerima perintah via Serial
String serialBuffer = "";
bool commandComplete = false;

// Timestamp terakhir kali menerima perintah
unsigned long lastCommandTime = 0;

// Konfigurasi kamera untuk ESP32-CAM
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

// Fungsi untuk menyimpan Chat ID ke EEPROM
void saveChatIDToEEPROM(String chatID) {
  // Bersihkan area EEPROM terlebih dahulu
  for (int i = 0; i < 50; i++) {
    EEPROM.write(CHAT_ID_ADDR + i, 0);
  }
  
  // Tulis Chat ID ke EEPROM
  for (int i = 0; i < chatID.length(); i++) {
    EEPROM.write(CHAT_ID_ADDR + i, chatID[i]);
  }
  
  EEPROM.commit();
  Serial.println("Chat ID disimpan ke EEPROM: " + chatID);
}

// Fungsi untuk membaca Chat ID dari EEPROM
String readChatIDFromEEPROM() {
  String chatID = "";
  for (int i = 0; i < 50; i++) {
    char c = EEPROM.read(CHAT_ID_ADDR + i);
    if (c == 0) break;
    chatID += c;
  }
  
  return chatID;
}

// Fungsi untuk inisialisasi kamera
bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  
  // Konfigurasi kualitas tinggi untuk ESP32-CAM
  config.frame_size = FRAMESIZE_XGA; // 1024x768 resolusi
  config.jpeg_quality = 10; // Kualitas terbaik (0-63, semakin kecil semakin bagus)
  config.fb_count = 2;
  
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Inisialisasi kamera gagal dengan error 0x%x\n", err);
    return false;
  }
  
  // Setting kamera untuk hasil yang lebih baik
  sensor_t * s = esp_camera_sensor_get();
  s->set_brightness(s, 1);      // -2 sampai 2
  s->set_contrast(s, 1);        // -2 sampai 2
  s->set_saturation(s, 1);      // -2 sampai 2
  s->set_special_effect(s, 0);  // 0 = tidak ada efek
  s->set_whitebal(s, 1);        // 1 = aktifkan white balance otomatis
  s->set_awb_gain(s, 1);        // 1 = aktifkan white balance gain otomatis
  s->set_wb_mode(s, 0);         // 0 = mode white balance otomatis
  s->set_exposure_ctrl(s, 1);   // 1 = aktifkan exposure otomatis
  s->set_gain_ctrl(s, 1);       // 1 = aktifkan gain control otomatis
  s->set_aec2(s, 0);            // 0 = nonaktifkan koreksi otomatis
  s->set_ae_level(s, 0);        // -2 sampai 2
  s->set_aec_value(s, 300);     // 0-1200
  s->set_agc_gain(s, 0);        // 0-30
  s->set_gainceiling(s, (gainceiling_t)0);  // 0-6
  s->set_bpc(s, 0);             // 0 = nonaktifkan black pixel correction
  s->set_wpc(s, 1);             // 1 = aktifkan white pixel correction
  s->set_raw_gma(s, 1);         // 1 = aktifkan GMA
  s->set_lenc(s, 1);            // 1 = aktifkan koreksi lensa
  s->set_hmirror(s, 0);         // 0 = nonaktifkan mirror horizontal
  s->set_vflip(s, 0);           // 0 = nonaktifkan flip vertikal
  
  Serial.println("Kamera berhasil diinisialisasi!");
  return true;
}

// Fungsi untuk mengirim foto ke Telegram
bool sendPhotoToTelegram() {
  if (adminChatID == "") {
    Serial.println("ERROR: Admin Chat ID belum diterima!");
    return false;
  }
  
  // Aktifkan LED flash
  digitalWrite(FLASH_LED_PIN, HIGH);
  delay(500); // Tunggu agar LED mencapai kecerahan penuh
  
  // Ambil foto
  camera_fb_t *fb = esp_camera_fb_get();
  
  // Matikan LED flash
  digitalWrite(FLASH_LED_PIN, LOW);
  
  if (!fb) {
    Serial.println("Gagal mengambil gambar dari kamera!");
    return false;
  }
  
  Serial.println("Foto berhasil diambil. Mengirim ke Telegram...");
  Serial.println("Ukuran gambar: " + String(fb->len) + " bytes");
  
  // Kirim foto ke Telegram menggunakan multipart form data
  String caption = "⚠️ PERINGATAN: Terdeteksi aktivitas mencurigakan pada kotak amal! ⚠️";
  
  secured_client.setCACert(nullptr); // Skip sertifikat untuk memudahkan koneksi
  secured_client.setInsecure();
  
  String HEAD = "--RandomNerdTutorials\r\nContent-Disposition: form-data; name=\"chat_id\"; \r\n\r\n" + adminChatID + "\r\n--RandomNerdTutorials\r\nContent-Disposition: form-data; name=\"photo\"; filename=\"esp32-cam.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n";
  String TAIL = "\r\n--RandomNerdTutorials\r\nContent-Disposition: form-data; name=\"caption\"; \r\n\r\n" + caption + "\r\n--RandomNerdTutorials--\r\n";
  
  uint16_t imageLen = fb->len;
  uint16_t extraLen = HEAD.length() + TAIL.length();
  uint16_t totalLen = imageLen + extraLen;
  
  // Koneksi ke server Telegram
  if (!secured_client.connect("api.telegram.org", 443)) {
    Serial.println("Koneksi ke server Telegram gagal!");
    esp_camera_fb_return(fb);
    return false;
  }
  
  // Kirim HTTP POST request
  secured_client.println("POST /bot" + String(BOT_TOKEN) + "/sendPhoto HTTP/1.1");
  secured_client.println("Host: api.telegram.org");
  secured_client.println("User-Agent: ESP32-CAM");
  secured_client.println("Content-Length: " + String(totalLen));
  secured_client.println("Content-Type: multipart/form-data; boundary=RandomNerdTutorials");
  secured_client.println();
  secured_client.print(HEAD);
  
  // Kirim data gambar
  uint8_t *fbBuf = fb->buf;
  size_t fbLen = fb->len;
  for (size_t n = 0; n < fbLen; n += 1024) {
    if (n + 1024 < fbLen) {
      secured_client.write(fbBuf, 1024);
      fbBuf += 1024;
    } else if (fbLen % 1024 > 0) {
      size_t remainder = fbLen % 1024;
      secured_client.write(fbBuf, remainder);
    }
  }
  
  secured_client.print(TAIL);
  
  // Bebaskan buffer memori
  esp_camera_fb_return(fb);
  
  // Tunggu respon dari server Telegram
  long startTimer = millis();
  bool finishedHeaders = false;
  bool currentLineIsBlank = true;
  String response = "";
  while (!secured_client.available() && millis() - startTimer < 10000) {
    delay(100);
  }
  
  while (secured_client.available()) {
    char c = secured_client.read();
    response += c;
  }
  
  secured_client.stop();
  
  Serial.println(response);
  bool success = response.indexOf("\"ok\":true") > 0;
  
  if (success) {
    Serial.println("Foto berhasil dikirim ke Telegram!");
    
    // Konfirmasi ke Wemos D1 R32
    Serial.println("PHOTO_SENT");
  } else {
    Serial.println("Gagal mengirim foto ke Telegram!");
  }
  
  return success;
}

// Fungsi untuk memproses perintah dari Wemos D1 R32
void processCommand(String command) {
  Serial.println("Memproses perintah: " + command);
  
  if (command == "PING") {
    Serial.println("PONG");
  }
  else if (command.startsWith("CHATID:")) {
    adminChatID = command.substring(7);
    
    // Validasi Chat ID (harus hanya berisi angka)
    bool validChatID = true;
    for (int i = 0; i < adminChatID.length(); i++) {
      if (!isDigit(adminChatID.charAt(i))) {
        validChatID = false;
        break;
      }
    }
    
    if (validChatID && adminChatID.length() > 0) {
      saveChatIDToEEPROM(adminChatID);
      Serial.println("CHATID_CONFIRMED");
      isChatIDReceived = true;
    } else {
      adminChatID = "";
      Serial.println("CHATID_INVALID");
    }
  }
  else if (command == "TAKE_PHOTO") {
    unsigned long currentTime = millis();
    
    if (isChatIDReceived) {
      if (currentTime - lastPhotoTime > photoMinInterval) {
        digitalWrite(FLASH_LED_PIN, HIGH); // Aktifkan flash terlebih dahulu
        delay(100);
        
        if (sendPhotoToTelegram()) {
          Serial.println("PHOTO_SENT");
        } else {
          Serial.println("PHOTO_FAILED");
        }
        
        lastPhotoTime = currentTime;
      } else {
        Serial.println("PHOTO_TOO_SOON");
      }
    } else {
      Serial.println("CHATID_NOT_SET");
    }
  }
  else {
    Serial.println("UNKNOWN_COMMAND");
  }
}

void setup() {
  // Nonaktifkan detektor brown-out untuk menghindari reset
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  
  // Inisialisasi komunikasi serial
  // ESP32-CAM secara default menggunakan U0T (GPIO1) dan U0R (GPIO3) untuk Serial
  Serial.begin(115200);
  
  // Tunggu 1 detik untuk memastikan serial console siap
  delay(1000);
  
  Serial.println("\n==== ESP32-CAM Sistem Keamanan Kotak Amal ====");
  Serial.println("Versi: 1.2 (Hardware Serial)");
  
  // Inisialisasi EEPROM
  EEPROM.begin(EEPROM_SIZE);
  
  // Konfigurasi pin LED flash
  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW);
  
  // Konfigurasi pin trigger dengan pull-up internal
  pinMode(TRIGGER_PIN, INPUT_PULLUP);
  
  // Koneksi WiFi
  WiFi.begin(ssid, password);
  Serial.print("Menghubungkan ke WiFi");
  
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    delay(500);
    Serial.print(".");
    retries++;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nKoneksi WiFi gagal! Merestart...");
    delay(3000);
    ESP.restart();
    return;
  }
  
  Serial.println("");
  Serial.println("WiFi terhubung!");
  Serial.println("Alamat IP: " + WiFi.localIP().toString());
  
  // Inisialisasi kamera
  if (!initCamera()) {
    Serial.println("Inisialisasi kamera gagal! Merestart...");
    delay(3000);
    ESP.restart();
    return;
  }
  
  // Baca Chat ID dari EEPROM jika tersedia
  adminChatID = readChatIDFromEEPROM();
  if (adminChatID != "") {
    Serial.println("Chat ID ditemukan di EEPROM: " + adminChatID);
    isChatIDReceived = true;
  } else {
    Serial.println("Chat ID tidak ditemukan di EEPROM");
  }
  
  // Siapkan buffer serial untuk menerima perintah
  serialBuffer.reserve(100);
  
  // Tes flash LED saat startup
  digitalWrite(FLASH_LED_PIN, HIGH);
  delay(500);
  digitalWrite(FLASH_LED_PIN, LOW);
  
  // Tampilkan informasi pinout untuk membantu troubleshooting
  Serial.println("\nKonfigurasi Pin ESP32-CAM:");
  Serial.println("- Serial RX: GPIO3 (U0R)");
  Serial.println("- Serial TX: GPIO1 (U0T)");
  Serial.println("- Flash LED: GPIO4");
  Serial.println("- Trigger Pin: GPIO13");
  
  Serial.println("\nESP32-CAM siap menerima perintah!");
  Serial.println("Menunggu perintah dari Wemos D1 R32...");
}

void loop() {
  // Cek perintah dari Serial (Wemos D1 R32)
  while (Serial.available() > 0) {
    char inChar = (char)Serial.read();
    
    // Tambahkan karakter ke buffer kecuali jika newline
    if (inChar != '\n' && inChar != '\r') {
      serialBuffer += inChar;
      lastCommandTime = millis();
    } 
    // Jika menerima newline, tandai perintah lengkap
    else if (inChar == '\n') {
      commandComplete = true;
    }
  }
  
  // Jika buffer berisi data tapi tidak ada newline, cek timeout
  if (serialBuffer.length() > 0 && !commandComplete && (millis() - lastCommandTime > 200)) {
    // Timeout - proses buffer meskipun tidak ada newline
    commandComplete = true;
  }
  
  // Proses perintah yang lengkap
  if (commandComplete && serialBuffer.length() > 0) {
    Serial.println("Perintah diterima: " + serialBuffer);
    
    // Proses perintah
    processCommand(serialBuffer);
    
    // Reset buffer
    serialBuffer = "";
    commandComplete = false;
  }
  
  // Cek pin trigger (komunikasi hardware) - aktif LOW
  if (digitalRead(TRIGGER_PIN) == LOW) {
    Serial.println("Trigger pin aktif!");
    
    // Kedipkan LED flash sebagai konfirmasi
    digitalWrite(FLASH_LED_PIN, HIGH);
    delay(100);
    digitalWrite(FLASH_LED_PIN, LOW);
    
    if (isChatIDReceived) {
      unsigned long currentTime = millis();
      if (currentTime - lastPhotoTime > photoMinInterval) {
        sendPhotoToTelegram();
        lastPhotoTime = currentTime;
      } else {
        Serial.println("Terlalu cepat untuk foto baru! Tunggu interval minimum.");
      }
    } else {
      Serial.println("GAGAL: Chat ID belum diterima!");
    }
    
    // Delay untuk mencegah multiple trigger
    delay(2000); 
  }
  
  // Cek koneksi WiFi dan reconnect jika diperlukan
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Koneksi WiFi terputus! Mencoba menghubungkan kembali...");
    WiFi.reconnect();
    delay(5000);
  }
  
  // Sedikit delay untuk CPU
  delay(10);
}