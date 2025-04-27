// ------------------------------------------------------------------------------------------------------------------------------
// ----------------           KALO Library - Deepgram SpeechToText API call with ESP32-S3 & SD Card                ----------------
// ----------------                                      July 22, 2024                                           ----------------
// ----------------                                                                                              ---------------- 
// ----------------            Coded by KALO (with support from Sandra, Deepgram team, June 2024)                ----------------
// ----------------      workflow: sending https POST message request, sending message WAV bytes in chunks       ----------------
// ----------------                                                                                              ----------------   
// ----------------   CALL: 'text_response = SpeechToText_Deepgram(SD_audio_file)' [no Initialization needed]   ----------------
// ------------------------------------------------------------------------------------------------------------------------------


// *** HINT: in case of an 'Sketch too Large' Compiler Warning/ERROR in Arduino IDE (ESP32 Dev Module:
// -> select a larger 'Partition Scheme' via menu > tools: e.g. using 'No OTA (2MB APP / 2MB SPIFFS) ***

// Keep in mind: Deepgram SpeechToText services are AI based, means 'whole sentences with context' typically have a much 
// higher recognition quality (confidence) than a sending single words or short commands only (my observation).


// --- includes ----------------

#include <WiFiClientSecure.h>   // only here needed
/* #include <SD.h>              // library also needed, but already included in main.ino: */


// --- defines & macros --------

#ifndef DEBUG                   // user can define favorite behaviour ('true' displays addition info)
#  define DEBUG true            // <- define your preference here  
#  define DebugPrint(x);        if(DEBUG){Serial.print(x);}   /* do not touch */
#  define DebugPrintln(x);      if(DEBUG){Serial.println(x);} /* do not touch */ 
#endif


// --- PRIVATE credentials & user favorites -----  

const char* deepgramApiKey =    "dffa61074e8fa62922fcdaf6508a4b128bae6277";   // Kendi Deepgram API anahtarınızı buraya ekleyin
                                                           // https://deepgram.com adresinden ücretsiz hesap oluşturabilirsiniz

#define STT_LANGUAGE      "tr"    // Türkçe dil kodu
                                // keep EMPTY ("") if you want Deepgram to detect & understand 'your' language automatically, 
                                // language abbreviation examples: "en", "en-US", "en-IN", "de" etc.
                                // all see here: https://developers.deepgram.com/docs/models-languages-overview

#define TIMEOUT_DEEPGRAM   35   // define your preferred max. waiting time [sec] for Deepgram transcription response
                                // increased from 12 to 35 for ESP32-S3 which might be slower with SD card access

#define RETRY_COUNT       3    // Number of retries for Deepgram API calls in case of failure

#define STT_KEYWORDS            "&keywords=KALO&keywords=Janthip&keywords=Google"  // optional, forcing STT to listen exactly 
/*#define STT_KEYWORDS          "&keywords=KALO&keywords=Sachin&keywords=Google"   // optional, forcing STT to listen exactly */


// --- global vars -------------
WiFiClientSecure client;       

// External function declarations to avoid redefinition errors
extern void Deepgram_KeepAlive();
extern String DEEPGRAM_API_KEY;


// ----------------------------------------------------------------------------------------------------------------------------

String SpeechToText_Deepgram( String audio_filename )
{ 
  // WiFi bağlantısı yoksa işlemi atlayalım
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi bağlantısı yok, yeniden bağlanmaya çalışılıyor...");
    
    // Yeniden bağlanmayı dene
    WiFi.disconnect();
    delay(1000);
    WiFi.reconnect();
    
    // 5 saniye bekle
    int timeout = 0;
    while (WiFi.status() != WL_CONNECTED && timeout < 10) {
      delay(500);
      Serial.print(".");
      timeout++;
    }
    
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("\nWiFi bağlantısı kurulamadı, Deepgram çevirisi yapılamıyor.");
      return "";
    } else {
      Serial.println("\nWiFi bağlantısı yeniden kuruldu!");
    }
  }
  
  uint32_t t_start = millis(); 
  
  // ---------- Connect to Deepgram Server (only if needed, e.g. on INIT and after lost connection)

  if ( !client.connected() )
  { 
    DebugPrintln("> Initialize Deepgram Server connection ... ");
    client.setInsecure();
    
    // Bağlantı denemesi (3 kez)
    bool connected = false;
    for (int attempt = 0; attempt < 3 && !connected; attempt++) {
      Serial.print("Deepgram bağlantı denemesi #" + String(attempt+1) + "...");
      if (client.connect("api.deepgram.com", 443)) {
        connected = true;
        Serial.println(" başarılı!");
      } else {
        Serial.println(" başarısız!");
        delay(500);
      }
    }
    
    if (!connected) {
      Serial.println("\nERROR - WifiClientSecure connection to Deepgram Server failed!");
      client.stop(); 
      return ("");
    }
    
    DebugPrintln("Done. Connected to Deepgram Server.");
  }
  uint32_t t_connected = millis();  

        
  // ---------- Check if AUDIO file exists, check file size 
  
  // Dosya yolunu kontrol et
  if (audio_filename.startsWith("/")) {
    DebugPrintln("Dosya yolu '/' ile başlıyor, göreceli yol kullanılacak");
    audio_filename = audio_filename.substring(1);
  }
  
  // Tam dosya yolunu oluştur - kök dizine kaydet
  String fullPath = "/" + audio_filename;
  DebugPrintln("Kullanılacak tam dosya yolu: " + fullPath);
  
  // Dosya var mı kontrol et
  if (!SD_MMC.exists(fullPath)) {
    Serial.println("ERROR - File does not exist: " + fullPath);
    return "";
  }
  
  File audioFile = SD_MMC.open(fullPath);    
  if (!audioFile) {
    Serial.println("ERROR - Failed to open file for reading: " + fullPath);
    return ("");
  }
  size_t audio_size = audioFile.size();
  audioFile.close();
  DebugPrintln("> Audio File [" + fullPath + "] found, size: " + (String) audio_size );
  

  // ---------- flush (remove) potential inbound streaming data before we start with any new user request
  // reason: increasing reliability, also needed in case we have pending data from earlier Deepgram_KeepAlive()

  String socketcontent = "";
  while (client.available()) 
  { char c = client.read(); socketcontent += String(c);
  } int RX_flush_len = socketcontent.length(); 
 

  // ---------- Sending HTTPS request header to Deepgram Server

  String optional_param;                          // see: https://developers.deepgram.com/docs/stt-streaming-feature-overview
  // Türkçe dil ve diğer parametreler
  optional_param =  "?model=nova-2";              // Nova-2 modeli
  optional_param += "&language=tr";               // Türkçe dil
  optional_param += "&smart_format=true";         // Akıllı biçimlendirme
  optional_param += "&punctuate=true";            // Noktalama işaretleri

  Serial.println("Türkçe dil için Deepgram parametreleri: " + optional_param);
  
  client.println("POST /v1/listen" + optional_param + " HTTP/1.1"); 
  client.println("Host: api.deepgram.com");
  client.println("Authorization: Token " + DEEPGRAM_API_KEY);
  client.println("Content-Type: audio/wav");
  client.println("Content-Length: " + String(audio_size));
  client.println();   // header complete, now sending binary body (wav bytes) .. 
  
  DebugPrintln("> POST Request to Deepgram Server started, sending WAV data now ..." );
  uint32_t t_headersent = millis();     

  
  // ---------- Reading the AUDIO wav file, sending in CHUNKS (closing file after done)
  // idea found here (WiFiClientSecure.h issue): https://www.esp32.com/viewtopic.php?t=4675
  
  File file = SD_MMC.open(fullPath, FILE_READ);
  if (!file) {
    Serial.println("ERROR - Failed to open file for sending: " + fullPath);
    return "";
  }
  
  const size_t bufferSize = 1024;      // best values seem anywhere between 1024 and 2048; 
  uint8_t buffer[bufferSize];
  size_t bytesRead;
  while (file.available()) 
  { bytesRead = file.read(buffer, sizeof(buffer));
    if (bytesRead > 0) {client.write(buffer, bytesRead);}   // sending WAV AUDIO data       
  }
  file.close();
  DebugPrintln("> All bytes sent, waiting Deepgram transcription");
  uint32_t t_wavbodysent = millis();  


  // ---------- Waiting (!) to Deepgram Server response (stop waiting latest after TIMEOUT_DEEPGRAM [secs])
 
  String response = "";   // waiting until available() true and all data completely received
  uint32_t start_wait = millis();
  bool ping_check = false;
  
  while (response == "" && millis() < (t_wavbodysent + TIMEOUT_DEEPGRAM*1000))   
  { 
    while (client.available())                         
    { 
      char c = client.read();
      response += String(c);      
    }
    
    // Her 10 saniyede bir ping kontrolü yap
    if (millis() > (start_wait + 10000) && !ping_check) {
      Serial.println("Uzun süredir yanıt yok, sunucu bağlantısı kontrol ediliyor...");
      
      // İkinci bir bağlantı ile sunucu kontrolü
      WiFiClientSecure ping_client;
      ping_client.setInsecure();
      if (ping_client.connect("api.deepgram.com", 443)) {
        Serial.println("Sunucu erişilebilir, yanıt bekleniyor...");
        ping_client.stop();
      } else {
        Serial.println("UYARI: Sunucuya erişilemiyor!");
      }
      ping_check = true;
    }
    
    // printing dots '.' each 500ms while waiting response (faster feedback)
    DebugPrint(".");  delay(500);           
  } 
  DebugPrintln();
  
  if (response == "") {
    if (millis() >= (t_wavbodysent + TIMEOUT_DEEPGRAM*1000)) {
      Serial.print("\n*** TIMEOUT ERROR - forced TIMEOUT after " + (String) TIMEOUT_DEEPGRAM + " seconds");
      Serial.println(" (is your Deepgram API Key valid ?) ***\n");    
    } else {
      Serial.println("\n*** CONNECTION ERROR - Sunucudan yanıt alınamadı ***\n");
    }
  }
  
  uint32_t t_response = millis();


  // ---------- closing connection to Deepgram 
  client.stop();     // observation: unfortunately needed, otherwise the 'audio_play.openai_speech() in AUDIO.H not working !
                     // so we close for now, but will be opened again on next call (and regularly in Deepgram_KeepAlive())
                     
    
  // ---------- Parsing json response, extracting transcription etc.
  
  int    response_len  = response.length();
  String transcription = json_object( response, "\"transcript\":" );
  String language      = json_object( response, "\"detected_language\":" );
  String wavduration   = json_object( response, "\"duration\":" );
  String confidence    = json_object( response, "\"confidence\":" );

  DebugPrintln( "---------------------------------------------------" );
  // Tam yanıtı göster (debugging için)
  Serial.println( "-> Deepgram yanıtı başlangıcı: " + response.substring(0, 100) + "..." );
  
  if (response_len > 0 && transcription.length() == 0) {
    // Yanıt var ama transcription bulunamadı
    int pos_transcript = response.indexOf("transcript");
    if (pos_transcript > 0) {
      Serial.println("transcript kelimesi yanıtta bulundu pozisyon: " + String(pos_transcript));
      Serial.println("O bölgede yanıt: " + response.substring(pos_transcript-10, pos_transcript+50));
    } else {
      Serial.println("transcript bulunamadı, alternatif anahtar kelimeleri arıyorum...");
      String words[] = {"text", "result", "transcription", "speech"};
      for (int i = 0; i < 4; i++) {
        int pos = response.indexOf(words[i]);
        if (pos > 0) {
          Serial.println(words[i] + " bulundu pozisyon: " + String(pos));
          Serial.println("O bölgede yanıt: " + response.substring(pos-10, pos+50));
        }
      }
    }
  }
  
  // Boş transcription için ek debug
  if (transcription.length() == 0 && response_len > 0) {
    Serial.println("\n=================== DEEPGRAM TANIMA BAŞARISIZ ===================");
    Serial.println("Deepgram API ses kaydını aldı, ancak konuşma tanıma başarısız oldu.");
    Serial.println("Algılanan dil: [" + language + "]");
    Serial.println("Güven düzeyi: [" + confidence + "]");
    
    if (language == "uk" || language == "ru") {
      Serial.println("\nÖNEMLİ: Deepgram API gürültüyü yanlış dil olarak tanımış olabilir.");
      Serial.println("Bu genellikle mikrofonun ses yakalamadığını gösterir.");
    }
    
    Serial.println("\nSORUN ÇÖZÜMÜ:");
    Serial.println("1. Mikrofona daha yakından ve net konuşun (10-15 cm mesafeden)");
    Serial.println("2. INMP441 L/R pini bağlantısını kontrol edin (3.3V veya GND)");
    Serial.println("3. Ortam gürültüsünü azaltın");
    Serial.println("4. Daha uzun cümleler söylemeyi deneyin (3-5 saniye)");
    Serial.println("================================================================\n");
  }
  
  DebugPrintln( "-> Latency Server (Re)CONNECT [t_connected]:   " + (String) ((float)((t_connected-t_start))/1000) );;   
  DebugPrintln( "-> Latency sending HEADER [t_headersent]:      " + (String) ((float)((t_headersent-t_connected))/1000) );   
  DebugPrintln( "-> Latency sending WAV file [t_wavbodysent]:   " + (String) ((float)((t_wavbodysent-t_headersent))/1000) );   
  DebugPrintln( "-> Latency DEEPGRAM response [t_response]:     " + (String) ((float)((t_response-t_wavbodysent))/1000) );   
  DebugPrintln( "=> TOTAL Duration [sec]: ..................... " + (String) ((float)((t_response-t_start))/1000) ); 
  DebugPrintln( "=> RX data prior request [bytes]: " + (String) RX_flush_len );
  DebugPrintln( "=> Server detected audio length [sec]: " + wavduration );
  DebugPrintln( "=> Server response length [bytes]: " + (String) response_len );
  DebugPrintln( "=> Detected language (optional): [" + language + "]" ); 
  DebugPrintln( "=> Transcription: [" + transcription + "]" );
  DebugPrintln( "---------------------------------------------------\n" );

  
  // ---------- return transcription String 
  return transcription;   
}



// ----------------------------------------------------------------------------------------------------------------------------
// JSON String Extract: Searching [element] in [input], example: element = "transcript": -> returns content 'How are you?'

String json_object( String input, String element )
{ 
  String content = "";
  int pos_start = input.indexOf(element);      
  if (pos_start > 0)                                      // if element found:
  {  
     pos_start += element.length();                       // pos_start points now to begin of element content     
     
     // Debug - ne tür bir json yapısı var?
     Serial.println("JSON pozisyon: " + String(pos_start) + ", karakter: " + input.substring(pos_start, pos_start+10));
     
     // Farklı JSON formatları için kontrol
     if (input.charAt(pos_start) == '"') {
       // Standart JSON string formatı: "transcript":"metin"
       pos_start++; // çift tırnak karakterini atla
       int pos_end = input.indexOf("\"", pos_start);      // pos_end points to ending "
       if (pos_end > pos_start) {
         content = input.substring(pos_start, pos_end);
       }
     } 
     else if (input.charAt(pos_start) == ':') {
       // Farklı bir format: "transcript": "metin" (boşluk var)
       pos_start++; // iki nokta üst üste karakterini atla
       // Boşlukları atla
       while (pos_start < input.length() && input.charAt(pos_start) == ' ') {
         pos_start++;
       }
       if (input.charAt(pos_start) == '"') {
         pos_start++; // çift tırnak karakterini atla
         int pos_end = input.indexOf("\"", pos_start);
         if (pos_end > pos_start) {
           content = input.substring(pos_start, pos_end);
         }
       }
     }
     else {
       // Eski yaklaşım
       int pos_end = input.indexOf(",\"", pos_start);      // pos_end points to ," (start of next element)  
       if (pos_end > pos_start) {                          // memo: "garden".substring(from3,to4) is 1 char "d" ..
         content = input.substring(pos_start, pos_end);    // .. thats why we use for 'to' the pos_end (on ," ):
       } 
       content.trim();                                    // remove optional spaces between the json objects
       if (content.startsWith("\"")) {                    // String objects typically start & end with quotation marks "    
         content = content.substring(1, content.length()-1);   // remove both existing quotation marks (if exist)
       }
     }
  }  
  return content;
}
