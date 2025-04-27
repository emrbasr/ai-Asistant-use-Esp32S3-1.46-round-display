// ------------------------------------------------------------------------------------------------------------------------------
// ------------------                      VOICE Assistant - Demo (Code snippets, examples)                    ------------------
// ----------------                                       July 22, 2024                                        ------------------
// ------------------                                                                                          ------------------
// ------------------              Voice RECORDING with variable length [native I2S code]                      ------------------
// ------------------                   SpeechToText [using Deepgram API service]                              ------------------
// ------------------                                                                                          ------------------
// ------------------                    HW: ESP32-S3 with connected Micro SD Card                             ------------------
// ------------------                    SD Card: using pins 14,17,16                                          ------------------
// ------------------------------------------------------------------------------------------------------------------------------/*


/*
Connections for ESP32-S3 Round Display

SD Card Module       ESP32-S3 
GND                  GND
Vcc                  VIn
MISO                 D16 (SD_D0)
MOSI                 D17 (SD_CMD)
SCK                  D14 (SD_CLK)

I2S MIC              ESP32-S3
GND                  GND
VDD                  3.3V
SD                   D39                  
SCK                  D15
WS                   D2
L/R                  3.3V

I2S Audio Output     ESP32-S3
BCLK                 D48
DOUT                 D47
WCLK (LRC)           D38

I2C                  ESP32-S3
SCL                  D10
SDA                  D11

*/

// *** HINT: in case of an 'Sketch too Large' Compiler Warning/ERROR in Arduino IDE (ESP32 Dev Module):
// -> select a larger 'Partition Scheme' via menu > tools: e.g. using 'No OTA (2MB APP / 2MB SPIFFS) ***


#define VERSION "\n=== KALO ESP32-S3 Voice Assistant (last update: July 22, 2024) ======================"

#include <WiFi.h>       // Include WiFi library
#include <SD.h>         // Include SD library
#include <SPI.h>        // Include SPI library
#include <FS.h>         // Include File System library
#include <SD_MMC.h>     // Include SD MMC library for ESP32-S3
#include <Wire.h>        // Include Wire library for I2C communication

#include <Audio.h>      // needed for PLAYING Audio (via I2S Amplifier, e.g. MAX98357) with ..
                        // Audio.h library from Schreibfaul1: https://github.com/schreibfaul1/ESP32-audioI2S
                        // .. ensure you have actual version (July 18, 2024 or newer needed for 8bit wav files!)
#define AUDIO_FILE        "/Audio.wav"    // mandatory, filename for the AUDIO recording

// --- PRIVATE credentials -----

const char* ssid = "Zyxel_3B81_EXT";         // ## INSERT your wlan ssid
const char* password = "GXHGG34P44";  // ## INSERT your password

// Deepgram API key - both lib_ai_services.ino and lib_audio_transcription.ino need access to this
String DEEPGRAM_API_KEY = "dffa61074e8fa62922fcdaf6508a4b128bae6277";

 // mandatory, filename for the AUDIO recording


// --- PIN assignments for ESP32-S3 ---------

// SD Card pins
#define SD_CLK_PIN     14  // SCK pin for SD card
#define SD_CMD_PIN     17  // CMD (MOSI) pin for SD card
#define SD_D0_PIN      16  // D0 (MISO) pin for SD card

// I2S Microphone pins
#define I2S_WS         2   // WS (Word Select/LRCK) pin for INMP441
#define I2S_SD         39  // SD (Serial Data) pin for INMP441
#define I2S_SCK        15  // SCK (Serial Clock) pin for INMP441

// I2S Audio Output pins
#define I2S_DOUT       47  // DOUT pin for audio output
#define I2S_BCLK       48  // BCLK pin for audio output
#define I2S_LRC        38  // LRC (same as WS) pin for audio output

// GPIO pins for other functions
#define pin_RECORD_BTN 6   // Record button pin
#define LED            5   // LED indicator pin

// --- global Objects ----------

Audio audio_play;


// declaration of functions in other modules (not mandatory but ensures compiler checks correctly)
// splitting Sketch into multiple tabs see e.g. here: https://www.youtube.com/watch?v=HtYlQXt14zU

bool I2S_Record_Init();
bool Record_Start(String filename);
bool Record_Available(String filename, float* audiolength_sec);

String SpeechToText_Deepgram(String filename);
void Deepgram_KeepAlive();
void playRecordedAudio(String audioFile);

// TCA9554PWR için sabitler ve fonksiyonlar
#define TCA9554_ADDRESS         0x20                      // TCA9554PWR I2C adresi
#define TCA9554_INPUT_REG       0x00
#define TCA9554_OUTPUT_REG      0x01
#define TCA9554_CONFIG_REG      0x03

#define Low   0
#define High  1
#define EXIO_PIN4   4   // SD_D3 pini için

// I2C pinleri
#define I2C_SDA_PIN    11
#define I2C_SCL_PIN    10

// TCA9554PWR fonksiyonları
uint8_t I2C_Read_EXIO(uint8_t REG) {
  Wire.beginTransmission(TCA9554_ADDRESS);                
  Wire.write(REG);                                        
  uint8_t result = Wire.endTransmission();               
  if (result != 0) {                                     
    Serial.println("I2C okuma hatası!");
    return 0;
  }
  Wire.requestFrom(TCA9554_ADDRESS, 1); 
  uint8_t bitsStatus = 0;
  if (Wire.available()) {                  
    bitsStatus = Wire.read(); 
  }                       
  return bitsStatus;                                     
}

uint8_t I2C_Write_EXIO(uint8_t REG, uint8_t Data) {
  Wire.beginTransmission(TCA9554_ADDRESS);                
  Wire.write(REG);                                        
  Wire.write(Data);                                       
  uint8_t result = Wire.endTransmission();                  
  if (result != 0) {    
    Serial.println("I2C yazma hatası!");
    return 1;
  }
  return 0;                                             
}

void Set_EXIO(uint8_t Pin, uint8_t State) {
  uint8_t Data;
  if(State < 2 && Pin < 9 && Pin > 0) {  
    uint8_t bitsStatus = I2C_Read_EXIO(TCA9554_OUTPUT_REG);
    if(State == 1)                                     
      Data = (0x01 << (Pin-1)) | bitsStatus; 
    else if(State == 0)                  
      Data = (~(0x01 << (Pin-1))) & bitsStatus;      
    uint8_t result = I2C_Write_EXIO(TCA9554_OUTPUT_REG, Data);  
    if (result != 0) {                         
      Serial.println("GPIO ayarlama hatası!");
    }
  }
  else {                                          
    Serial.println("Parametre hatası!");
  }
}

// SD kart kontrol fonksiyonları
void SD_D3_Dis() {
  Set_EXIO(EXIO_PIN4, Low);
  delay(10);
  Serial.println("SD_D3 devre dışı bırakıldı");
}

void SD_D3_EN() {
  Set_EXIO(EXIO_PIN4, High);
  delay(10);
  Serial.println("SD_D3 etkinleştirildi");
}

void TCA9554PWR_Init() {
  // Tüm pinleri çıkış olarak ayarla (0 = çıkış modu)
  I2C_Write_EXIO(TCA9554_CONFIG_REG, 0x00);
  Serial.println("TCA9554PWR başlatıldı");
}

// ------------------------------------------------------------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println(VERSION);
  Serial.println("ESP32-S3 Round Display - Voice Assistant");
  
  // Pin assignments:
  pinMode(LED, OUTPUT);
  pinMode(pin_RECORD_BTN, INPUT);  // use INPUT_PULLUP if no external Pull-Up connected ##

  // I2C başlat
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  
  // TCA9554PWR başlat
  TCA9554PWR_Init();
  
  // Connecting to WLAN
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("WiFi'ya bağlanıyor");
  
  int timeout = 0;
  while (WiFi.status() != WL_CONNECTED && timeout < 20) {
    delay(500);
    Serial.print(".");
    timeout++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.println("WiFi bağlantısı başarılı!");
    Serial.print("IP Adresi: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println();
    Serial.println("WiFi bağlantısı başarısız, devam ediliyor...");
  }
  
  digitalWrite(LED, LOW);
  
  // SD kartı için SD_D3 pinini etkinleştir
  SD_D3_EN();
  
  // SD kart başlat
  if (!initSDCard_MMC()) {
    Serial.println("SD_MMC başarısız, SD ile deneniyor...");
    // SD kart denemesi 2: SD kütüphanesi ile
    if (!initSDCard_Regular()) {
      Serial.println("SD kartı başlatılamadı! Ses kaydı yapılamayacak.");
    }
  }
  
  // SD kart bilgileri
  uint8_t cardType = SD_MMC.cardType();
  if(cardType == CARD_NONE) {
    Serial.println("SD kart takılı değil!");
    return;
  }
  
  Serial.print("SD Kart Tipi: ");
  if(cardType == CARD_MMC) {
    Serial.println("MMC");
  } else if(cardType == CARD_SD) {
    Serial.println("SDSC");
  } else if(cardType == CARD_SDHC) {
    Serial.println("SDHC");
  } else {
    Serial.println("BILINMIYOR");
  }
  
  uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
  Serial.printf("SD Kart Boyutu: %lluMB\n", cardSize);

  // Initialize audio output for playback
  audio_play.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  Serial.println("Ses çıkışı başlatıldı");
  
  // Initialize I2S recording
  if (I2S_Record_Init()) {
    Serial.println("I2S kayıt başlatıldı");
  } else {
    Serial.println("I2S kayıt başlatma başarısız!");
  }

  // INIT done, starting user interaction
  Serial.println("> Ses kaydı için butona basılı tutun, bıraktığınızda kayıt bitecek ve Deepgram ile çeviri yapılacak");
}

void loop() {
  if (digitalRead(pin_RECORD_BTN) == LOW)  // Recording started (ongoing)
  {
    digitalWrite(LED, HIGH);
    delay(30);  // unbouncing & suppressing button 'click' noise in begin of audio recording

    //Start Recording
    Record_Start(AUDIO_FILE);
  }

  if (digitalRead(pin_RECORD_BTN) == HIGH)  // Recording not started yet .. OR stopped now (on release button)
  {
    digitalWrite(LED, LOW);

    float recorded_seconds;
    if (Record_Available(AUDIO_FILE, &recorded_seconds))  //  true once when recording finalized (.wav file available)
    {
      if (recorded_seconds > 0.4)  // ignore short btn TOUCH (e.g. <0.4 secs, used for 'audio_play.stopSong' only)
      {
        // [SpeechToText] - Transcript the Audio (waiting here until done)
        digitalWrite(LED, HIGH);

        // Sadece WiFi bağlantısı varsa Deepgram'ı çağır
        if (WiFi.status() == WL_CONNECTED) {
          String transcription = SpeechToText_Deepgram(AUDIO_FILE);
          
          digitalWrite(LED, LOW);
          Serial.println("Deepgram'dan alınan metin: " + transcription);
          
          // Metin boş değilse Gemini'ye gönder
          if (transcription.length() > 0) {
            digitalWrite(LED, HIGH);
            Serial.println("Gemini'ye metin gönderiliyor: " + transcription);
            String geminiResponse = sendToGemini(transcription);
            digitalWrite(LED, LOW);
            
            Serial.println("Gemini yanıtı: " + geminiResponse);
            
            // Text-to-Speech ile yanıtı seslendir
            if (geminiResponse.length() > 0) {
              digitalWrite(LED, HIGH);
              Serial.println("Yanıt sesli olarak çalınıyor...");
              textToSpeech(geminiResponse);
              digitalWrite(LED, LOW);
            }
          } else {
            Serial.println("Deepgram ses tanıma başarısız. Kaydı çalıyorum...");
            playRecordedAudio(AUDIO_FILE);
          }
        } else {
          Serial.println("WiFi bağlantısı olmadığı için ses çevirisi yapılamıyor.");
          Serial.println("Ses kaydı SD kartta '/Audio.wav' olarak saklandı.");
          digitalWrite(LED, LOW);
        }
      }
    }
  }

  // [Optional]: Stabilize WiFiClientSecure.h + Improve Speed of STT Deepgram response
  if (digitalRead(pin_RECORD_BTN) == HIGH && !audio_play.isRunning())  // but don't do it during recording or playing
  {
    static uint32_t millis_ping_before;
    if (millis() > (millis_ping_before + 5000)) {
      millis_ping_before = millis();
      
      // Sadece WiFi bağlantısı varsa Deepgram KeepAlive'ı çağır
      if (WiFi.status() == WL_CONNECTED) {
        digitalWrite(LED, HIGH);  // short LED OFF means: 'Reconnection server, can't record in moment'
        Deepgram_KeepAlive();
        digitalWrite(LED, LOW);
      }
    }
  }
}

void listDir(fs::FS &fs, const char *dirname, uint8_t levels) {
  Serial.printf("SD kart içeriği (%s):\n", dirname);
  
  File root = fs.open(dirname);
  if(!root) {
    Serial.println("- Dizin açılamadı!");
    return;
  }
  if(!root.isDirectory()) {
    Serial.println("- Bu bir dizin değil!");
    return;
  }
  
  File file = root.openNextFile();
  while(file) {
    if(file.isDirectory()) {
      Serial.print("  DIR : ");
      Serial.println(file.name());
      if(levels) {
        listDir(fs, file.name(), levels - 1);
      }
    } else {
      Serial.print("  DOSYA: ");
      Serial.print(file.name());
      Serial.print("\tBOYUT: ");
      Serial.println(file.size());
    }
    file = root.openNextFile();
  }
}

// SD_MMC ile ses kaydı için yeni fonksiyonlar
bool Record_Start_Custom(String audio_filename) {
  static bool flg_is_recording = false;
  static i2s_chan_handle_t rx_handle = NULL;
  static bool is_initialized = false;
  
  if (!is_initialized) {
    // I2S kanalını bir kere başlat
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    
    // I2S konfigürasyonu oluştur
    i2s_std_config_t std_cfg = {
      .clk_cfg = {
        .sample_rate_hz = 16000,  // SAMPLE_RATE değerini kullan
        .clk_src = I2S_CLK_SRC_DEFAULT,
        .mclk_multiple = I2S_MCLK_MULTIPLE_256,
      },
      .slot_cfg = {
        .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
        .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
        .slot_mode = I2S_SLOT_MODE_MONO,
        .slot_mask = I2S_STD_SLOT_RIGHT,
        .ws_width = I2S_DATA_BIT_WIDTH_16BIT,
        .ws_pol = false,
        .bit_shift = true,
      },
      .gpio_cfg = {
        .mclk = I2S_GPIO_UNUSED,
        .bclk = (gpio_num_t)I2S_SCK,
        .ws = (gpio_num_t)I2S_WS,
        .dout = I2S_GPIO_UNUSED,
        .din = (gpio_num_t)I2S_SD,
        .invert_flags = {
          .mclk_inv = false,
          .bclk_inv = false,
          .ws_inv = false,
        },
      },
    };
    
    i2s_new_channel(&chan_cfg, NULL, &rx_handle);    // Allocate a new RX channel
    i2s_channel_init_std_mode(rx_handle, &std_cfg);  // Initialize the channel
    i2s_channel_enable(rx_handle);                   // Start the RX channel
    is_initialized = true;
    
    Serial.println("I2S kanal başlatıldı");
  }
  
  if (!flg_is_recording) {
    flg_is_recording = true;
    
    // Eski dosyayı sil (varsa)
    if (SD_MMC.exists(audio_filename)) {
      SD_MMC.remove(audio_filename);
      Serial.println("Eski ses dosyası silindi");
    }
    
    // WAV header oluştur
    File audio_file = SD_MMC.open(audio_filename, FILE_WRITE);
    if (!audio_file) {
      Serial.println("Dosya oluşturulamadı!");
      flg_is_recording = false;
      return false;
    }
    
    // Basit WAV header yaz
    byte header[44];
    memset(header, 0, sizeof(header));
    
    // RIFF header
    header[0] = 'R'; header[1] = 'I'; header[2] = 'F'; header[3] = 'F';
    // ChunkSize - to be filled in later
    // WAVE
    header[8] = 'W'; header[9] = 'A'; header[10] = 'V'; header[11] = 'E';
    // fmt subchunk
    header[12] = 'f'; header[13] = 'm'; header[14] = 't'; header[15] = ' ';
    // Subchunk1Size (16 for PCM)
    header[16] = 16; header[17] = 0; header[18] = 0; header[19] = 0;
    // AudioFormat (1 for PCM)
    header[20] = 1; header[21] = 0;
    // NumChannels (1 for mono)
    header[22] = 1; header[23] = 0;
    // SampleRate (16000 Hz)
    header[24] = 0x80; header[25] = 0x3E; header[26] = 0; header[27] = 0;
    // ByteRate (SampleRate * NumChannels * BitsPerSample/8)
    header[28] = 0x80; header[29] = 0x3E; header[30] = 0; header[31] = 0;
    // BlockAlign (NumChannels * BitsPerSample/8)
    header[32] = 2; header[33] = 0;
    // BitsPerSample (16 bits)
    header[34] = 16; header[35] = 0;
    // data subchunk
    header[36] = 'd'; header[37] = 'a'; header[38] = 't'; header[39] = 'a';
    // Subchunk2Size - to be filled in later
    
    audio_file.write(header, sizeof(header));
    audio_file.close();
    
    Serial.println("WAV header oluşturuldu, kayıt başladı");
  }
  
  // Kayıt devam ederken sesli veri al
  if (flg_is_recording && rx_handle != NULL) {
    // I2S buffer için bellek ayır
    int16_t audio_buffer[1024];  // 16-bit buffer for I2S samples
    size_t bytes_read = 0;
    
    // I2S mikrofondan veri oku
    esp_err_t ret = i2s_channel_read(rx_handle, audio_buffer, sizeof(audio_buffer), &bytes_read, portMAX_DELAY);
    if (ret != ESP_OK) {
      Serial.println("I2S veri okunamadı!");
      return false;
    }
    
    // Ses seviyesini artır (GAIN)
    for (int i = 0; i < bytes_read / 2; i++) {
      audio_buffer[i] = audio_buffer[i] * 45; // Gain factor 45
    }
    
    // SD karta kaydet
    File audio_file = SD_MMC.open(audio_filename, FILE_APPEND);
    if (!audio_file) {
      Serial.println("Ses dosyası açılamadı!");
      return false;
    }
    
    audio_file.write((uint8_t*)audio_buffer, bytes_read);
    audio_file.close();
  }
  
  return true;
}

bool Record_Available_Custom(String audio_filename, float* audiolength_sec) {
  static bool flg_is_recording = true;
  
  if (!flg_is_recording) {
    return false;
  }
  
  flg_is_recording = false;
  
  File audio_file = SD_MMC.open(audio_filename, FILE_WRITE);
  if (!audio_file) {
    Serial.println("Kayıt dosyasına erişilemedi!");
    return false;
  }
  
  long filesize = audio_file.size();
  
  // WAV header'ı güncelle
  uint32_t dataSize = filesize - 44;
  uint32_t fileSize = dataSize + 36;
  
  audio_file.seek(4);
  byte fileSizeBytes[4] = {byte(fileSize & 0xFF), byte((fileSize >> 8) & 0xFF), 
                          byte((fileSize >> 16) & 0xFF), byte((fileSize >> 24) & 0xFF)};
  audio_file.write(fileSizeBytes, 4);
  
  audio_file.seek(40);
  byte dataSizeBytes[4] = {byte(dataSize & 0xFF), byte((dataSize >> 8) & 0xFF), 
                          byte((dataSize >> 16) & 0xFF), byte((dataSize >> 24) & 0xFF)};
  audio_file.write(dataSizeBytes, 4);
  
  audio_file.close();
  
  *audiolength_sec = (float)(filesize - 44) / (16000 * 2);
  
  Serial.println("> ... Done. Audio Recording finished.");
  Serial.print("> New AUDIO file: '");
  Serial.print(audio_filename);
  Serial.print("', filesize [bytes]: ");
  Serial.print(filesize);
  Serial.print(", length [sec]: ");
  Serial.println(*audiolength_sec);
  
  return true;
}

// SpeechToText_Deepgram fonksiyonunu SD_MMC ile çalışacak şekilde güncelleme
String SpeechToText_Deepgram_Custom(String audio_filename) { 
  uint32_t t_start = millis(); 
  
  // ---------- Connect to Deepgram Server (only if needed, e.g. on INIT and after lost connection)
  WiFiClientSecure client;
  
  if (!client.connected()) { 
    Serial.println("> Deepgram sunucusuna bağlanılıyor..."); 
    client.setInsecure();
    
    if (!client.connect("api.deepgram.com", 443)) { 
      Serial.println("\nHATA - Deepgram sunucusuna bağlantı başarısız!"); 
      client.stop();
      return (""); 
    }
    Serial.println("Bağlantı başarılı."); 
  }
  uint32_t t_connected = millis();  

  // ---------- Check if AUDIO file exists, check file size 
  if (!SD_MMC.exists(audio_filename)) {
    Serial.println("HATA - Ses dosyası bulunamadı: " + audio_filename);
    return "";
  }
  
  File audioFile = SD_MMC.open(audio_filename);    
  if (!audioFile) {
    Serial.println("HATA - Ses dosyası açılamadı");
    return "";
  }
  size_t audio_size = audioFile.size();
  audioFile.close();
  Serial.println("> Ses dosyası [" + audio_filename + "] bulundu, boyut: " + String(audio_size) + " byte");
  
  // ---------- flush (remove) potential inbound streaming data before we start with any new user request
  String socketcontent = "";
  while (client.available()) { 
    char c = client.read(); 
    socketcontent += String(c);
  } 
  int RX_flush_len = socketcontent.length(); 
 
  // ---------- Sending HTTPS request header to Deepgram Server
  String optional_param;
  optional_param = "?model=nova-2";
  optional_param += "&language=tr";       // Türkçe dil desteği
  optional_param += "&smart_format=true"; // Akıllı biçimlendirme
  optional_param += "&punctuate=true";    // Noktalama işaretleri
  
  client.println("POST /v1/listen" + optional_param + " HTTP/1.1"); 
  client.println("Host: api.deepgram.com");
  client.println("Authorization: Token " + DEEPGRAM_API_KEY);
  client.println("Content-Type: audio/wav");
  client.println("Content-Length: " + String(audio_size));
  client.println();
  
  Serial.println("> Deepgram sunucusuna POST isteği gönderiliyor, WAV verisi yükleniyor..."); 
  uint32_t t_headersent = millis();     

  // ---------- Reading the AUDIO wav file, sending in CHUNKS
  File file = SD_MMC.open(audio_filename, FILE_READ);
  if (!file) {
    Serial.println("HATA - Ses dosyası okunamadı");
    return "";
  }
  
  const size_t bufferSize = 1024;
  uint8_t buffer[bufferSize];
  size_t bytesRead;
  
  while (file.available()) { 
    bytesRead = file.read(buffer, sizeof(buffer));
    if (bytesRead > 0) {
      client.write(buffer, bytesRead);
    }
  }
  file.close();
  
  Serial.println("> Tüm veriler gönderildi, Deepgram çevirisi bekleniyor");
  uint32_t t_wavbodysent = millis();  

  // ---------- Waiting for Deepgram Server response (up to TIMEOUT_DEEPGRAM seconds)
  const int TIMEOUT_DEEPGRAM = 25;  // 25 saniye
  String response = "";
  
  while (response == "" && millis() < (t_wavbodysent + TIMEOUT_DEEPGRAM*1000)) {   
    while (client.available()) {                         
      char c = client.read();
      response += String(c);      
    }
    Serial.print(".");
    delay(100);           
  } 
  Serial.println();
  
  if (millis() >= (t_wavbodysent + TIMEOUT_DEEPGRAM*1000)) {
    Serial.println("\n*** TIMEOUT HATASI - " + String(TIMEOUT_DEEPGRAM) + " saniye sonra zorla durduruldu ***");
  } 
  uint32_t t_response = millis();  

  // ---------- closing connection to Deepgram 
  client.stop();
                     
  // ---------- Parsing json response, extracting transcription etc.
  String transcription = "";
  int pos_transcript = response.indexOf("\"transcript\":");
  
  if (pos_transcript > 0) {
    pos_transcript += 13;  // "transcript": uzunluğu
    int pos_end = response.indexOf("\",", pos_transcript);
    
    if (pos_end > pos_transcript) {
      transcription = response.substring(pos_transcript, pos_end);
    }
  }
  
  int response_len = response.length();
  String language = "tr";  // Varsayılan olarak Türkçe
  String wavduration = "0";
  
  Serial.println("---------------------------------------------------");
  Serial.println("=> Deepgram yanıt uzunluğu [byte]: " + String(response_len));
  Serial.println("=> Algılanan dil: [" + language + "]");
  Serial.println("=> Çeviri: [" + transcription + "]");
  Serial.println("---------------------------------------------------\n");
  
  return transcription;    
}

// SD kart başlatma fonksiyonu (SD_MMC ile)
bool initSDCard_MMC() {
  // SD MMC pinlerini ayarla
  Serial.println("SD MMC pinleri ayarlanıyor...");
  if(!SD_MMC.setPins(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN, -1, -1, -1)) {
    Serial.println("SD MMC: Pin değişikliği başarısız!");
    return false;
  }
  
  // SD kartı 1-bit modunda başlat
  Serial.println("SD kartı başlatılıyor (1-bit modu)...");
  if(!SD_MMC.begin("/sdcard", true, false)) {  // Single data line mode (1-bit), format zorlamadan
    Serial.println("SD kart MMC başlatma başarısız!");
    return false;
  }

  // SD kartın erişilebilirliğini kontrol et
  if(!SD_MMC.exists("/")) {
    Serial.println("SD kart kök dizini erişilebilir değil!");
    if(SD_MMC.mkdir("/")) {
      Serial.println("Kök dizin oluşturuldu");
    } else {
      Serial.println("Kök dizin oluşturulamadı!");
      return false;
    }
  }
  
  Serial.println("SD kart SD_MMC ile başarıyla başlatıldı!");
  return true;
}

// SD kart başlatma fonksiyonu (normal SD kütüphanesi ile)
bool initSDCard_Regular() {
  Serial.println("SD kart başlatılıyor (normal SD)...");
  
  // CS pini olarak SD_CLK_PIN kullan
  if (!SD.begin(SD_CLK_PIN)) {
    Serial.println("SD kart başlatma başarısız!");
    return false;
  }
  
  // Test et
  File testFile = SD.open("/test.txt", FILE_WRITE);
  if (!testFile) {
    Serial.println("SD kart test başarısız!");
    return false;
  }
  
  testFile.println("SD test");
  testFile.close();
  
  Serial.println("SD kart normal SD kütüphanesi ile başarıyla başlatıldı!");
  return true;
}

// Kaydedilen ses dosyasını çal
void playRecordedAudio(String audioFile) {
  Serial.println("Kaydedilen ses dosyası çalınıyor: " + audioFile);
  
  // Tam yoldan emin olalım
  if (!audioFile.startsWith("/")) {
    audioFile = "/" + audioFile;
  }
  
  // Audio.h kütüphanesini kullanarak çal
  audio_play.connecttoFS(SD_MMC, audioFile.c_str());
  
  // 5 saniye bekle (ses bitene kadar)
  uint32_t startPlay = millis();
  while(audio_play.isRunning() && millis() - startPlay < 5000) {
    audio_play.loop();
    delay(10);
  }
  
  audio_play.stopSong();
  Serial.println("Ses çalma tamamlandı.");
}
