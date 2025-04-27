#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <base64.h>

// DEBUG makrosunu tanımla
#ifndef DEBUG
#  define DEBUG true
#  define DebugPrint(x);        if(DEBUG){Serial.print(x);}
#  define DebugPrintln(x);      if(DEBUG){Serial.println(x);}
#endif

// External declarations to avoid redefinition errors
extern String DEEPGRAM_API_KEY; // Declare that this variable is defined elsewhere

// Gemini API anahtarı ve endpoint
const char* geminiApiKey = "AIzaSyBWlyqUuWkJCrYyA4Yb4nqJHde6XR2sSLA";
const char* geminiEndpoint = "https://generativelanguage.googleapis.com/v1/models/gemini-1.5-pro:generateContent";

// Text-to-Speech için API anahtarı ve endpoint
const char* ttsApiKey = "AIzaSyAmDSEtmezYyG1T0SfAoVS4x_ljxS6Zl1M";
const char* ttsEndpoint = "https://texttospeech.googleapis.com/v1/text:synthesize";

// Deepgram'dan gelen metni Gemini'ye gönderen fonksiyon
String sendToGemini(String transcription) {
  // Giriş kontrolü - boş metin gönderme
  if (transcription.isEmpty() || transcription.length() < 2) {
    Serial.println("Gemini'ye gönderilecek metin çok kısa veya boş!");
    return "Metin anlaşılamadı";
  }
  
  // WiFi bağlantısı kontrolü
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi bağlantısı yok, Gemini API'ye istek gönderilemedi");
    return "Bağlantı hatası";
  }
  
  Serial.println("Gemini'ye gönderiliyor: '" + transcription + "'");
  
  // HTTP istemcisi oluştur
  WiFiClientSecure client;
  client.setInsecure(); // SSL sertifika doğrulamasını devre dışı bırak
  
  // API isteği için HTTP istemcisi
  HTTPClient http;
  
  // Özel karakterleri temizle ve escape et
  transcription.replace("\"", "\\\""); // Çift tırnakları escape et
  transcription.replace("\n", " "); // Satır sonlarını kaldır
  
  // API endpoint'ine bağlan
  String url = String(geminiEndpoint) + "?key=" + String(geminiApiKey);
  
  Serial.println("Gemini API isteği gönderiliyor...");
  Serial.println("URL: " + url);
  
  if (http.begin(client, url)) {
    http.addHeader("Content-Type", "application/json");
    
    // Güncellenmiş JSON formatı (gemini-1.0-pro modeli için)
    String requestBody = "{\"contents\":[{\"parts\":[{\"text\":\"" + transcription + "\"}]}],";
    requestBody += "\"generationConfig\":{\"temperature\":0.7,\"topK\":1,\"topP\":0.95,\"maxOutputTokens\":800}}";
    
    Serial.println("İstek gövdesi: " + requestBody);
    
    // POST isteği gönder
    int httpCode = http.POST(requestBody);
    
    // Yanıt kontrolü
    if (httpCode == HTTP_CODE_OK) {
      String response = http.getString();
      Serial.println("Gemini yanıtı alındı, uzunluk: " + String(response.length()) + " byte");
      
      // JSON yanıtı işle
      DynamicJsonDocument doc(8192); // Buffer boyutunu ayarlayın
      DeserializationError error = deserializeJson(doc, response);
      
      if (error) {
        Serial.print("JSON işleme hatası: ");
        Serial.println(error.c_str());
        http.end();
        return "JSON işleme hatası";
      }
      
      // JSON yanıtını yazdır (debug için)
      Serial.println("JSON Yanıtı:");
      serializeJsonPretty(doc, Serial);
      Serial.println();
      
      // Gemini yanıtını çıkar - güncel yol
      if (doc.containsKey("candidates") && doc["candidates"].size() > 0 &&
          doc["candidates"][0].containsKey("content") &&
          doc["candidates"][0]["content"].containsKey("parts") &&
          doc["candidates"][0]["content"]["parts"].size() > 0 &&
          doc["candidates"][0]["content"]["parts"][0].containsKey("text")) {
        
        String geminiResponse = doc["candidates"][0]["content"]["parts"][0]["text"].as<String>();
        http.end();
        return geminiResponse;
      } else {
        Serial.println("Gemini yanıtında beklenen alanlar bulunamadı");
        Serial.println("Yanıt: " + response);
        http.end();
        return "Gemini yanıt formatı hatası";
      }
    } else {
      Serial.printf("Gemini API isteği başarısız, hata kodu: %d\n", httpCode);
      if (httpCode > 0) {
        String response = http.getString();
        Serial.println("Hata yanıtı: " + response);
      }
      http.end();
      return "API hatası: " + String(httpCode);
    }
  } else {
    Serial.println("HTTP bağlantısı kurulamadı");
    return "Bağlantı hatası";
  }
}

// Text-to-Speech fonksiyonu
bool textToSpeech(String text) {
  // HTTP istemcisi oluştur
  WiFiClientSecure client;
  client.setInsecure();
  
  // API isteği için HTTP istemcisi
  HTTPClient http;
  
  // JSON formatında istek gövdesi oluştur - Türkçe dil desteği
  String requestBody = "{\"input\":{\"text\":\"" + text + "\"},";
  requestBody += "\"voice\":{\"languageCode\":\"tr-TR\",\"ssmlGender\":\"NEUTRAL\"},";
  requestBody += "\"audioConfig\":{\"audioEncoding\":\"MP3\"}}";
  
  // API endpoint'ine bağlan
  String url = String(ttsEndpoint) + "?key=" + String(ttsApiKey);
  if (http.begin(client, url)) {
    http.addHeader("Content-Type", "application/json");
    
    // POST isteği gönder
    int httpCode = http.POST(requestBody);
    
    // Yanıt kontrolü
    if (httpCode == HTTP_CODE_OK) {
      String response = http.getString();
      
      // JSON yanıtı işle
      DynamicJsonDocument doc(16384); // Buffer boyutunu ayarlayın
      deserializeJson(doc, response);
      
      // Base64 kodlu ses verisini çıkar
      String audioContent = doc["audioContent"].as<String>();
      
      // Base64'ten decode et
      String decodedContent = base64_decode(audioContent);
      int mp3Len = decodedContent.length();
      
      // SD karta kaydet
      File audioFile = SD_MMC.open("/tts_output.mp3", FILE_WRITE);
      if (!audioFile) {
        Serial.println("TTS dosyası oluşturulamadı");
        return false;
      }
      
      audioFile.write((uint8_t*)decodedContent.c_str(), mp3Len);
      audioFile.close();
      
      // Ses dosyasını çal
      playRecordedAudio("/tts_output.mp3");
      return true;
    } else {
      Serial.printf("TTS API isteği başarısız, hata kodu: %d\n", httpCode);
      return false;
    }
    
    http.end();
  } else {
    Serial.println("HTTP bağlantısı kurulamadı");
    return false;
  }
}

// Extern declaration to avoid redefinition
extern void playRecordedAudio(String audioFile);

// Base64 işlemleri için yardımcı fonksiyonlar
String base64_decode(String input) {
  const char* base64chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  
  while (input.length() % 4 != 0) {
    input += '=';
  }
  
  int input_length = input.length();
  int output_length = input_length * 3 / 4;
  char* decoded = new char[output_length];
  
  // Base64 decoding algoritması
  for (int i = 0, j = 0; i < input_length; i += 4, j += 3) {
    uint32_t triple = 0;
    
    for (int k = 0; k < 4; k++) {
      char c = input.charAt(i + k);
      if (c == '=') {
        output_length--;
        continue;
      }
      
      const char* p = strchr(base64chars, c);
      if (p) {
        triple |= (p - base64chars) << (3-k)*6;
      }
    }
    
    if (j < output_length) decoded[j] = (triple >> 16) & 0xFF;
    if (j+1 < output_length) decoded[j+1] = (triple >> 8) & 0xFF;
    if (j+2 < output_length) decoded[j+2] = triple & 0xFF;
  }
  
  String result;
  for (int i = 0; i < output_length; i++) {
    result += decoded[i];
  }
  
  delete[] decoded;
  return result;
}

// Deepgram AI için KeepAlive fonksiyonu
void Deepgram_KeepAlive() {
  // WiFi bağlantısını canlı tutmak için Deepgram API'ye periyodik istek gönderme
  // Bu fonksiyon loop() içinde belirli aralıklarla çağrılır
  
  // WiFi bağlantısı yoksa işlemi atlayalım
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  
  uint32_t t_start = millis();
  DebugPrint("* Deepgram KeepAlive | ");
  
  WiFiClientSecure client;
  client.setInsecure();
  
  // Deepgram sunucusuna bağlan
  if (!client.connect("api.deepgram.com", 443)) {
    Serial.println("\n* PING Error - Server Connection failed.");
    return;
  }
  
  DebugPrint("Done, connected.  -->  Connect Latency [sec]: ");
  DebugPrintln(String((float)((millis()-t_start))/1000));
  
  // Sessiz 16Khz/8bit WAV dosyası gönder (~1ms)
  uint8_t empty_wav[] = {
    0x52,0x49,0x46,0x46, 0x40,0x00,0x00,0x00, 0x57,0x41,0x56,0x45,0x66,0x6D,0x74,0x20, 
    0x10,0x00,0x00,0x00,0x01,0x00,0x01,0x00,0x80,0x3E,0x00,0x00,0x80,0x3E,0x00,0x00,
    0x01,0x00,0x08,0x00,0x64,0x61,0x74,0x61, 0x14,0x00,0x00,0x00, 0x80,0x80,0x80,0x80, 
    0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80
  };
  
  client.println("POST /v1/listen HTTP/1.1"); 
  client.println("Host: api.deepgram.com");
  client.println("Authorization: Token " + DEEPGRAM_API_KEY);
  client.println("Content-Type: audio/wav");
  client.println("Content-Length: " + String(sizeof(empty_wav)));
  client.println();
  client.write(empty_wav, sizeof(empty_wav));
  
  // Yanıtı al (beklemeden)
  String response = "";
  while (client.available()) { 
    char c = client.read();
    response += String(c);
  }
  int RX_len = response.length();
  
  // Bağlantıyı kapatma, KeepAlive'ı korumak için
  
  // Debug bilgisi
  DebugPrint("TX (WAV): " + String(sizeof(empty_wav)) + " bytes | ");
  DebugPrint("RX (TXT): " + String(RX_len) + " bytes  -->  ");
  DebugPrintln("Total Latency [sec]: " + String((float)((millis()-t_start))/1000));
} 