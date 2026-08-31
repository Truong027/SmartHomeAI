#include <Arduino.h>
#include "ai_agent.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "config.h"
#include "hw_audio.h" 
#include "hw_sensors.h"
#include "ui_lvgl.h"
#include <Firebase_ESP_Client.h>
#include <ir_Daikin.h>
#include "hw_eeprom.h"
#include <Adafruit_NeoPixel.h>
#include <IRsend.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <IRremoteESP8266.h>
#include <Preferences.h>
#include "hw_led.h"

extern FirebaseData fbdo;
extern bool firebase_ready;

extern bool relay1;
extern bool relay2;
extern int ledBrightness;
extern uint8_t audioVolume;
extern bool daikin_power;
extern uint8_t daikin_temp;
extern void sendDaikinCommand(bool power, uint8_t temp, uint8_t fan = 10, uint8_t mode = 2);
extern void saveSettingsToEEPROM(bool, bool, int);
extern Adafruit_NeoPixel pixels;
extern volatile bool uiUpdatePending;
extern Audio audio;

extern bool isMusicMode;
extern bool is_ir_learning_mode;
extern IRrecv irrecv;
extern int selected_ir_idx;
extern LearnedIR learned_ir[MAX_IR_SLOTS];
extern void showMainScreen();
extern void showIrScreen();
extern void stopMusicScreen();
extern void updateIrScreen(int slotIndex, String protocol, String hexCode);

// Biến lưu trạng thái tốc độ quạt (0: tắt, 1, 2, 3)
int current_fan_speed = 0;
bool isAiBusy = false;

// --- BỘ NHỚ LÂU DÀI BỀN VỮNG (LONG-TERM PERSISTENT MEMORY TRONG FLASH & FIREBASE) ---
Preferences aiMem;
String memUserName = "Trường";
String memAiName = "Nori";
String memUserFacts = "Ông chủ sáng chế ra hệ thống nhà thông minh";
String memCustomNotes = "";
bool memInitialized = false;

void initAiMemory() {
  if (memInitialized) return;
  aiMem.begin("ai_mem", false);
  memUserName = aiMem.getString("userName", "Trường");
  memAiName = aiMem.getString("aiName", "Nori");
  memUserFacts = aiMem.getString("userFacts", "Ông chủ sáng chế ra hệ thống nhà thông minh");
  memCustomNotes = aiMem.getString("customNotes", "");

  // Tự động làm sạch bộ nhớ nếu bị ô nhiễm dữ liệu câu prompt cũ
  if (memUserName.length() > 20 || memUserName.indexOf("Hãy") != -1 || memUserName.indexOf("nhạc") != -1 || memUserName.indexOf("Trường Sơn") != -1) {
    memUserName = "Trường";
    aiMem.putString("userName", "Trường");
  }
  if (memUserFacts.indexOf("Trường Sơn") != -1 || memUserFacts.indexOf("bố") != -1 || memUserFacts.length() > 100) {
    memUserFacts = "Ông chủ sáng chế ra hệ thống nhà thông minh";
    aiMem.putString("userFacts", memUserFacts);
  }
  if (memCustomNotes.indexOf("Trường Sơn") != -1 || memCustomNotes.indexOf("bố") != -1) {
    memCustomNotes = "";
    aiMem.putString("customNotes", "");
  }

  memInitialized = true;
  Serial.printf("🧠 [AI Memory Init] User: %s | AI: %s | Facts: %s | Notes: %s\n",
                memUserName.c_str(), memAiName.c_str(), memUserFacts.c_str(), memCustomNotes.c_str());
}

void saveAiMemory(String key, String value) {
  initAiMemory();
  value.trim();
  if (value.length() == 0) return;
  
  if (key == "userName") {
    // Chỉ lưu tên người dùng nếu là tên người thực tế (< 15 ký tự, không chứa từ khóa câu lệnh)
    if (value.length() <= 15 && value.indexOf("Hãy") == -1 && value.indexOf("nhớ") == -1 && value.indexOf("nhạc") == -1) {
      memUserName = value;
      aiMem.putString("userName", value);
      Serial.println("🧠 [AI Memory] Đã lưu tên người dùng: " + value);
    }
  } else if (key == "aiName") {
    if (value.length() <= 15) {
      memAiName = value;
      aiMem.putString("aiName", value);
      Serial.println("🧠 [AI Memory] Đã lưu tên AI: " + value);
    }
  } else if (key == "userFacts") {
    if (memUserFacts.length() > 0 && memUserFacts.indexOf(value) == -1) {
      memUserFacts += "; " + value;
    } else if (memUserFacts.length() == 0) {
      memUserFacts = value;
    }
    aiMem.putString("userFacts", memUserFacts);
    Serial.println("🧠 [AI Memory] Đã ghi nhớ thông tin mới: " + memUserFacts);
  } else if (key == "customNotes") {
    if (memCustomNotes.length() > 0 && memCustomNotes.indexOf(value) == -1) {
      memCustomNotes += "; " + value;
    } else if (memCustomNotes.length() == 0) {
      memCustomNotes = value;
    }
    aiMem.putString("customNotes", memCustomNotes);
    Serial.println("🧠 [AI Memory] Đã ghi nhớ dặn dò mới: " + memCustomNotes);
  }

  // Đồng bộ tức thì lên Firebase Realtime Database
  if (firebase_ready) {
    Firebase.RTDB.setStringAsync(&fbdo, "/ESP32_AI_Hub/ai_memory/" + key, value);
  }
}

// --- BỘ NHỚ LỊCH SỬ CHAT (SHORT-TERM MEMORY) ---
#define MAX_HISTORY 6 // Lưu tối đa 6 tin nhắn gần nhất (3 cặp hỏi-đáp)
struct ChatMessage {
  String role;
  String content;
};
ChatMessage chatHistory[MAX_HISTORY];
int historyCount = 0;

void addChatHistory(String role, String content) {
  if (historyCount < MAX_HISTORY) {
    chatHistory[historyCount].role = role;
    chatHistory[historyCount].content = content;
    historyCount++;
  } else {
    // Dịch chuyển mảng sang trái để xóa tin nhắn cũ nhất
    for (int i = 0; i < MAX_HISTORY - 1; i++) {
      chatHistory[i] = chatHistory[i + 1];
    }
    chatHistory[MAX_HISTORY - 1].role = role;
    chatHistory[MAX_HISTORY - 1].content = content;
  }
}
// ------------------------------------------------

String urlEncode(const String& str) {
  String encodedString = "";
  encodedString.reserve(str.length() * 3);
  const char hex[] = "0123456789ABCDEF";
  for (size_t i = 0; i < str.length(); i++) {
    uint8_t c = (uint8_t)str.charAt(i);
    if (c == ' ') {
      encodedString += "%20";
    } else if ((c >= '0' && c <= '9') ||
               (c >= 'A' && c <= 'Z') ||
               (c >= 'a' && c <= 'z') ||
               c == '-' || c == '_' || c == '.' || c == '~') {
      encodedString += (char)c;
    } else {
      encodedString += '%';
      encodedString += hex[(c >> 4) & 0x0F];
      encodedString += hex[c & 0x0F];
    }
  }
  return encodedString;
}

String base64UrlEncode(const String& input) {
  const char b64chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  String result = "";
  int len = input.length();
  int i = 0;
  while (i < len) {
    uint32_t octet_a = i < len ? (unsigned char)input.charAt(i++) : 0;
    uint32_t octet_b = i < len ? (unsigned char)input.charAt(i++) : 0;
    uint32_t octet_c = i < len ? (unsigned char)input.charAt(i++) : 0;
    uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;
    result += b64chars[(triple >> 18) & 0x3F];
    result += b64chars[(triple >> 12) & 0x3F];
    if (i > len + 1) break;
    result += b64chars[(triple >> 6) & 0x3F];
    if (i > len) break;
    result += b64chars[triple & 0x3F];
  }
  return result;
}

String pendingTtsUrl = "";
String pendingTtsSpeech = "";
bool hasPendingTts = false;

void playTTS(String text, bool isPromptOnly, String customTtsUrl) {
  // Lọc sạch ký tự xuống dòng '\n', '\r' thành dấu phẩy để Google TTS đọc mượt
  text.replace("\r", " ");
  text.replace("\n", ", ");
  while (text.indexOf("  ") != -1) {
    text.replace("  ", " ");
  }
  text.trim();

  if (text.length() == 0) return;

  // Giữ lại đầy đủ nội dung chi tiết (lên tới 700 ký tự)
  String ttsText = text;
  if (ttsText.length() > 700) {
    int cutIdx = -1;
    for (int i = 700; i >= 400; i--) {
      char c = ttsText.charAt(i);
      if (c == '.' || c == '?' || c == '!' || c == ';') {
        cutIdx = i + 1;
        break;
      }
    }
    if (cutIdx != -1) {
      ttsText = ttsText.substring(0, cutIdx);
    } else {
      ttsText = ttsText.substring(0, 700);
    }
    ttsText.trim();
  }

  Serial.println("🔊 Đang phát TTS: " + ttsText);
  extern void setAIChatDialogue(String userText, String aiText);
  setAIChatDialogue("", ttsText);
  if (!isPromptOnly) {
    setAIFaceState(AI_STATE_TALKING);
  } else {
    setAIFaceState(AI_STATE_THINKING);
  }

  if (customTtsUrl.length() > 0) {
    pendingTtsUrl = customTtsUrl;
    pendingTtsSpeech = "";
    Serial.printf("🔗 URL TTS tùy chỉnh: %s\n", customTtsUrl.c_str());
  } else if (ttsText.length() <= 160) {
    // Với các câu <= 160 ký tự: Dùng trực tiếp connecttospeech của ESP32-audioI2S (chuẩn m_f_tts, phát trọn vẹn 100% không nuốt âm)
    pendingTtsSpeech = ttsText;
    pendingTtsUrl = "";
    Serial.println("🗣️ [Native TTS Speech] Bàn giao câu thoại cho connecttospeech: " + ttsText);
  } else {
    // Với các câu dài > 160 ký tự: Dùng Vercel backend
    String b64 = base64UrlEncode(ttsText);
    pendingTtsUrl = "https://vercel-backend-woad-seven.vercel.app/api/tts.mp3?b64=" + b64 + "&lang=vi";
    pendingTtsSpeech = "";
    Serial.printf("🔗 URL TTS Vercel: %s\n", pendingTtsUrl.c_str());
  }

  // Bàn giao cho Main Loop trên Core 1 phát TTS an toàn
  hasPendingTts = true;
}

void createWavHeader(byte* header, int waveDataSize) {
  header[0] = 'R'; header[1] = 'I'; header[2] = 'F'; header[3] = 'F';
  unsigned int fileSize = waveDataSize + 36;
  header[4] = (byte)(fileSize & 0xFF);
  header[5] = (byte)((fileSize >> 8) & 0xFF);
  header[6] = (byte)((fileSize >> 16) & 0xFF);
  header[7] = (byte)((fileSize >> 24) & 0xFF);
  header[8] = 'W'; header[9] = 'A'; header[10] = 'V'; header[11] = 'E';
  header[12] = 'f'; header[13] = 'm'; header[14] = 't'; header[15] = ' ';
  header[16] = 16; header[17] = 0; header[18] = 0; header[19] = 0;
  header[20] = 1; header[21] = 0;
  header[22] = 1; header[23] = 0; // 1 channel
  header[24] = (byte)(SAMPLE_RATE & 0xFF);
  header[25] = (byte)((SAMPLE_RATE >> 8) & 0xFF);
  header[26] = (byte)((SAMPLE_RATE >> 16) & 0xFF);
  header[27] = (byte)((SAMPLE_RATE >> 24) & 0xFF);
  unsigned int byteRate = SAMPLE_RATE * 2; // 1 channel, 16 bit
  header[28] = (byte)(byteRate & 0xFF);
  header[29] = (byte)((byteRate >> 8) & 0xFF);
  header[30] = (byte)((byteRate >> 16) & 0xFF);
  header[31] = (byte)((byteRate >> 24) & 0xFF);
  header[32] = 2; header[33] = 0; // block align
  header[34] = 16; header[35] = 0; // bits per sample
  header[36] = 'd'; header[37] = 'a'; header[38] = 't'; header[39] = 'a';
  header[40] = (byte)(waveDataSize & 0xFF);
  header[41] = (byte)((waveDataSize >> 8) & 0xFF);
  header[42] = (byte)((waveDataSize >> 16) & 0xFF);
  header[43] = (byte)((waveDataSize >> 24) & 0xFF);
}

void processAudioAI(uint8_t* audioData, size_t size) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ Không thể gửi âm thanh: Chưa có kết nối WiFi!");
    isAiBusy = false;
    setAIFaceState(AI_STATE_IDLE);
    setLedMode(0);
    return;
  }

  Serial.println("🚀 Đang gửi âm thanh lên Groq Whisper STT...");
  if (isManualVoiceTrigger) {
    setAIFaceState(AI_STATE_THINKING);
    setLedMode(1);
  }
  
  String response = "";
  {
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(8000);
    client.setHandshakeTimeout(6);
    
    if (!client.connect("api.groq.com", 443)) {
      Serial.println("❌ Không thể kết nối Groq API (Thử lại...)");
      vTaskDelay(pdMS_TO_TICKS(250));
      if (!client.connect("api.groq.com", 443)) {
        Serial.println("❌ Lỗi kết nối Groq API!");
        isAiBusy = false;
        if (isManualVoiceTrigger) {
          setAIFaceState(AI_STATE_IDLE);
          setLedMode(0);
        }
        return;
      }
    }
  
  String boundary = "----ESP32Boundary";
  String head = "--" + boundary + "\r\n"
                "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n"
                "Content-Type: audio/wav\r\n\r\n";
  String tail = "\r\n--" + boundary + "\r\n"
                "Content-Disposition: form-data; name=\"model\"\r\n\r\n"
                "whisper-large-v3-turbo\r\n"
                "--" + boundary + "\r\n"
                "Content-Disposition: form-data; name=\"language\"\r\n\r\n"
                "vi\r\n"
                "--" + boundary + "\r\n"
                "Content-Disposition: form-data; name=\"temperature\"\r\n\r\n"
                "0\r\n"
                "--" + boundary + "\r\n"
                "Content-Disposition: form-data; name=\"prompt\"\r\n\r\n"
                "Hi Nori. Xin chào Nori. Nori ơi. Hey Nori. Chào Nori. Trở về màn hình chính. Mở remote. Học lệnh. Quay về dashboard. Trở về. Tôi muốn nghe nhạc. Mở nhạc cho tôi nghe. Phát bài hát. Bật đèn 1. Tắt đèn 1. Bật đèn 2. Tắt đèn 2. Mở quạt. Tắt quạt. Bật điều hòa. Tắt điều hòa. Tăng âm lượng. Giảm âm lượng. Độ sáng đèn. Edge Impulse. TensorFlow Lite. Arduino IDE. Thời tiết hôm nay thế nào. Âm lịch hôm nay. Cho tôi thông tin chi tiết về giá vàng ngày hôm nay. Tin tức thời sự.\r\n"
                "--" + boundary + "--\r\n";
                
  // audioData đã chứa sẵn 44 byte WAV header ở đầu (size đã bao gồm 44 bytes)
  uint32_t contentLen = head.length() + size + tail.length();
  
  client.println("POST /openai/v1/audio/transcriptions HTTP/1.1");
  client.println("Host: api.groq.com");
  client.println("Authorization: Bearer " + String(GROQ_API_KEY));
  client.println("Content-Type: multipart/form-data; boundary=" + boundary);
  client.print("Content-Length: ");
  client.println(contentLen);
  client.println();
  
  client.print(head);
  
  // Gửi toàn bộ mảng audioData theo chunk 2KB để đẩy dữ liệu cực nhanh qua TCP mà không nghẽn CPU
  size_t bytesSent = 0;
  while(bytesSent < size) {
    size_t chunk = min((size_t)2048, size - bytesSent);
    client.write(&audioData[bytesSent], chunk);
    bytesSent += chunk;
    vTaskDelay(pdMS_TO_TICKS(2));
  }
  client.print(tail);
  client.flush();
  
    // Đọc phản hồi từ Groq API (Cho phép tối đa 12 giây để Whisper nhận diện câu dài)
    unsigned long startWait = millis();
    while (client.connected() && (millis() - startWait < 12000)) {
      while (client.available()) {
        response += (char)client.read();
        startWait = millis();
      }
      int jsonStart = response.indexOf('{');
      int jsonEnd = response.lastIndexOf('}');
      if (jsonStart != -1 && jsonEnd != -1 && jsonEnd > jsonStart) {
        response = response.substring(jsonStart, jsonEnd + 1);
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(15));
    }
    
    client.stop();
  } // Giải phóng hoàn toàn mbedTLS SSL heap context trước khi gọi LLM
  
  vTaskDelay(pdMS_TO_TICKS(250)); // Đảm bảo lwIP dọn dẹp sạch sẽ socket cũ
  
  JsonDocument doc;
  deserializeJson(doc, response);
  const char* text = doc["text"];
  
  if (text && strlen(text) > 0) {
    String transcribedText = String(text);
    transcribedText.trim();
    
    extern String removeVietnameseAccents(String text);
    String lowerT = removeVietnameseAccents(transcribedText);
    lowerT.toLowerCase();

    // 🛑 LỌC BỎ ẢO GIÁC YOUTUBE (Whisper hallucination khi nhận âm thanh im lặng/tiếng ồn nền)
    if (lowerT.indexOf("subscribe") != -1 || lowerT.indexOf("dang ky kenh") != -1 ||
        lowerT.indexOf("ghien mi go") != -1 || lowerT.indexOf("la la school") != -1 ||
        lowerT.indexOf("like va share") != -1 || lowerT.indexOf("cam on da xem") != -1 ||
        lowerT.indexOf("hen gap lai") != -1 || lowerT.indexOf("video tiep theo") != -1 ||
        lowerT.indexOf("theo doi kenh") != -1 || lowerT.indexOf("nho like") != -1 ||
        lowerT.indexOf("chia se video") != -1 || lowerT.indexOf("chuc cac ban") != -1 ||
        lowerT.indexOf("thank you") != -1 || lowerT.indexOf("watching") != -1 ||
        lowerT == "bye" || lowerT == "tam biet" || lowerT == ".") {
      Serial.println("⚠️ [Whisper Filter] Đã lọc bỏ ảo giác YouTube từ tiếng ồn nền: " + transcribedText);
      isAiBusy = false;
      extern unsigned long lastWakeWordCheckFail;
      lastWakeWordCheckFail = millis();
      if (isManualVoiceTrigger) {
        setAIFaceState(AI_STATE_IDLE);
        setLedMode(0);
        extern void setAIChatDialogue(String userText, String aiText);
        setAIChatDialogue("", "Em chua nghe ro...");
        uiUpdatePending = true;
      }
      return;
    }

    // 🛑 Chuẩn hóa các biến thể & tiền tố ảo giác của Whisper về từ khóa "Hi Nori"
    String cleanTrans = lowerT;
    cleanTrans.replace(".", " ");
    cleanTrans.replace(",", " ");
    cleanTrans.replace("!", " ");
    cleanTrans.replace("?", " ");
    cleanTrans.trim();
    while (cleanTrans.indexOf("  ") != -1) cleanTrans.replace("  ", " ");
    
    if (cleanTrans == "cho anh hai nori" || cleanTrans == "cho em hai nori" || 
        cleanTrans == "cho toi hai nori" || cleanTrans == "cho minh hai nori" ||
        cleanTrans == "cho anh hi nori" || cleanTrans == "cho em hi nori" ||
        cleanTrans == "hai nori" || cleanTrans == "2 nori" || cleanTrans == "bay nori" ||
        cleanTrans == "hay nori" || cleanTrans == "he nori" || cleanTrans == "ha nori" ||
        cleanTrans == "day nori" || cleanTrans == "lay nori" || cleanTrans == "oi nori" ||
        cleanTrans == "hai no ri" || cleanTrans == "hai nory" || cleanTrans == "hi nori") {
      transcribedText = "Hi Nori";
      lowerT = "hi nori";
    }

    // 🛑 QUY TẮC BẮT BUỘC WAKE-WORD: NẾU GỌI BẰNG GIỌNG NÓI (KHÔNG PHẢI BẤM NÚT THỦ CÔNG)
    // THÌ BẮT BUỘC PHẢI CHỨA TỪ KHÓA "HI NORI" HOẶC "NORI" CHÍNH XÁC 100%!
    extern bool isManualVoiceTrigger;
    if (!isManualVoiceTrigger) {
      bool hasWakeWord = (lowerT.indexOf("nori") != -1 || lowerT.indexOf("no ri") != -1 || 
                          lowerT.indexOf("nory") != -1 || lowerT.indexOf("lo ri") != -1 ||
                          lowerT.indexOf("noly") != -1 || lowerT.indexOf("nuri") != -1);
      if (!hasWakeWord) {
        Serial.printf("⚠️ [Wake-Word Filter] Âm thanh không chứa từ khóa 'Hi Nori'/'Nori' (Nhận diện: \"%s\"). Bỏ qua 100%%!\n", transcribedText.c_str());
        isAiBusy = false;
        extern unsigned long lastWakeWordCheckFail;
        lastWakeWordCheckFail = millis();
        setAIFaceState(AI_STATE_IDLE);
        setLedMode(0);
        extern void setAIChatDialogue(String userText, String aiText);
        setAIChatDialogue("", "San sang...");
        uiUpdatePending = true;
        return;
      }
      Serial.printf("✨ [Wake-Word Match] Đã nhận diện chính xác từ khóa Wake-Word: \"%s\"\n", transcribedText.c_str());
    }

    setAIFaceState(AI_STATE_THINKING);
    setLedMode(1);
    extern void setAIChatDialogue(String userText, String aiText);
    setAIChatDialogue(transcribedText, "Dang suy nghi...");
    extern String ai_prediction_short;
    ai_prediction_short = "Dang suy nghi...";
    uiUpdatePending = true;

    Serial.println("🗣️ You said: " + transcribedText);
    vTaskDelay(pdMS_TO_TICKS(20)); // Nhường CPU cho IDLE0 reset watchdog
    sendToLLM(transcribedText);
    isAiBusy = false;
  } else {
    Serial.println("❌ Không nhận diện được giọng nói. Phản hồi server: " + response);
    isAiBusy = false;
    if (isManualVoiceTrigger) {
      setAIFaceState(AI_STATE_IDLE);
      setLedMode(0);
      extern void setAIChatDialogue(String userText, String aiText);
      setAIChatDialogue("", "Em chua nghe ro...");
      uiUpdatePending = true;
    }
  }
}

String pendingSongUrl = "";
String pendingSongTitle = "";

String foundSongDisplay = "";
bool isWaitingFollowupCommand = false;

String searchMusicUrl(String songTitle) {
  extern String removeVietnameseAccents(String text);
  String cleanTitle = removeVietnameseAccents(songTitle);
  Serial.println("🔎 Đang tìm kiếm bài hát theo tên qua Vercel Cloud Backend: " + songTitle);
  
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(6000);
  HTTPClient http;
  
  String encodedTitle = urlEncode(songTitle);
  String streamUrl = "";
  foundSongDisplay = songTitle;
  
  // 1. Thử Vercel Cloud Music Backend (ZingMP3 + Apple Music + SoundCloud Studio Master)
  String vercelUrl = "https://vercel-backend-woad-seven.vercel.app/api/music?q=" + encodedTitle + "&t=" + String(millis());
  http.begin(client, vercelUrl);
  int httpCode = http.GET();
  if (httpCode == 200) {
    String payload = http.getString();
    JsonDocument doc;
    deserializeJson(doc, payload);
    if (doc["success"] == true) {
      const char* pUrl = doc.containsKey("raw_url") ? doc["raw_url"] : doc["stream_url"];
      if (pUrl && strlen(pUrl) > 0) {
        streamUrl = String(pUrl);
        const char* trackTitle = doc["title"];
        const char* trackArtist = doc["artist"];
        const char* source = doc["source"];
        
        Serial.printf("✨ [Vercel Music Cloud] Nguồn: %s | Bài hát: %s - %s\n", source ? source : "CDN", trackTitle ? trackTitle : "", trackArtist ? trackArtist : "");
        if (trackTitle && strlen(trackTitle) > 0) {
          foundSongDisplay = String(trackTitle);
        }
        Serial.printf("✅ [%s MP3 Stream] Đã tìm thấy: %s\n", source ? source : "Cloud", foundSongDisplay.c_str());
        Serial.println("🔗 Stream MP3 URL: " + streamUrl);
      }
    }
  }
  http.end();
  client.stop();

  // 2. Thử Jamendo API dự phòng trực tiếp nếu Backend mất mạng
  if (streamUrl == "") {
    String encodedClean = urlEncode(cleanTitle);
    String jamendoUrl = "https://api.jamendo.com/v3.0/tracks/?client_id=56d30c4d&format=json&limit=1&namesearch=" + encodedClean;
    http.begin(client, jamendoUrl);
    httpCode = http.GET();
    if (httpCode == 200) {
      String payload = http.getString();
      JsonDocument doc;
      deserializeJson(doc, payload);
      if (doc.containsKey("results") && doc["results"].size() > 0) {
        const char* pUrl = doc["results"][0]["audio"];
        if (pUrl && strlen(pUrl) > 0) {
          streamUrl = String(pUrl);
          const char* trackName = doc["results"][0]["name"];
          Serial.printf("✅ [Jamendo Direct] Đã tìm thấy: %s\n", trackName ? trackName : "");
          Serial.println("🔗 Stream MP3 URL: " + streamUrl);
        }
      }
    }
    http.end();
  }
  
  // 3. Kho MP3 HTTPS trực tiếp dự phòng chất lượng cao (Acoustic / V-Pop / Lofi)
  if (streamUrl == "") {
    String lower = cleanTitle;
    lower.toLowerCase();
    if (lower.indexOf("lac troi") != -1) {
      streamUrl = "https://ia801602.us.archive.org/31/items/LacTroiAcoustic/LacTroi.mp3";
    } else if (lower.indexOf("am tham") != -1) {
      streamUrl = "https://ia801509.us.archive.org/27/items/AmThamBenEmPiano/AmThamBenEm.mp3";
    } else if (lower.indexOf("bolero") != -1 || lower.indexOf("sau tim") != -1) {
      streamUrl = "https://ia801500.us.archive.org/20/items/SauTimThiepHongBolero/SauTimThiepHong.mp3";
    } else if (lower.indexOf("ve nha") != -1 || lower.indexOf("den vau") != -1) {
      streamUrl = "https://ia801508.us.archive.org/12/items/DiVeNhaLofi/DiVeNha.mp3";
    } else {
      streamUrl = "https://ia801503.us.archive.org/15/items/NoiNayCoAnhAcoustic/NoiNayCoAnh.mp3";
    }
    Serial.println("🎵 Phát luồng MP3 V-Pop dự phòng: " + streamUrl);
  }
  
  return streamUrl;
}

void sendToLLM(String userText, bool isSilent) {
  Serial.println("🧠 Đang gọi Groq LLM (Llama 3)...");
  if (!isSilent) setAIFaceState(AI_STATE_THINKING);
  
  extern String removeVietnameseAccents(String text);
  String lowerText = removeVietnameseAccents(userText);
  lowerText.toLowerCase();
  lowerText.trim();

  // Xóa sạch toàn bộ dấu câu (. , ! ? ' " ; : -) để so khớp chính xác 100%
  String cleanText = "";
  for (unsigned int i = 0; i < lowerText.length(); i++) {
    char c = lowerText[i];
    if (c != '.' && c != ',' && c != '!' && c != '?' && c != '"' && c != '\'' && c != ';' && c != ':' && c != '-' && c != '_') {
      cleanText += c;
    }
  }
  cleanText.trim();
  while (cleanText.indexOf("  ") != -1) {
    cleanText.replace("  ", " ");
  }

  // 1. Loại bỏ các từ đệm/xưng hô mà Whisper hay thêm vào đầu câu khi thu âm ngắn
  const char* fillerPrefixes[] = {
    "cho anh ", "cho em ", "cho toi ", "cho minh ", "cho ban ",
    "anh ", "em ", "toi ", "minh ", "da ", "a ", "oi ", "alo ",
    "nay ", "hoi ", "cho hoi "
  };
  for (const char* fp : fillerPrefixes) {
    if (cleanText.startsWith(fp)) {
      cleanText = cleanText.substring(strlen(fp));
      cleanText.trim();
    }
  }

  String normalizedGreeting = cleanText;

  // Xử lý các trường hợp Whisper phiên âm nhầm từ tiếng Anh khi nói "Hi Nori" / "Hey Nori"
  if (normalizedGreeting == "bye" || normalizedGreeting == "bai" || normalizedGreeting == "hai" || 
      normalizedGreeting == "bye nori" || normalizedGreeting == "bai nori" || normalizedGreeting == "hai nori" ||
      normalizedGreeting == "2 nori" || normalizedGreeting == "hai no ri" || normalizedGreeting == "hai nory" ||
      normalizedGreeting == "bay" || normalizedGreeting == "bay nori" ||
      normalizedGreeting == "hay nori" || normalizedGreeting == "hay nori." || normalizedGreeting == "hay" ||
      normalizedGreeting == "ha nori" || normalizedGreeting == "he nori" || normalizedGreeting == "hoi nori" ||
      normalizedGreeting == "lay nori" || normalizedGreeting == "day nori" ||
      normalizedGreeting == "hey nori." || normalizedGreeting == "hi nori.") {
    normalizedGreeting = "hi nori";
  }

  // 🛑 KIỂM TRA Ý ĐỊNH ĐIỀU KHIỂN / CÂU HỎI KIẾN THỨC
  // Nếu câu nói có chứa bất kỳ từ khóa ra lệnh hoặc câu hỏi nào thì TUYỆT ĐỐI KHÔNG coi là chào hỏi đơn thuần!
  bool hasCommandIntent = (
    cleanText.indexOf("nhac") != -1 || cleanText.indexOf("bai hat") != -1 || cleanText.indexOf("ca khuc") != -1 || cleanText.indexOf("bai") != -1 ||
    cleanText.indexOf("den") != -1 || cleanText.indexOf("relay") != -1 || cleanText.indexOf("khoa") != -1 || cleanText.indexOf("cua") != -1 ||
    cleanText.indexOf("quat") != -1 || cleanText.indexOf("dieu hoa") != -1 || cleanText.indexOf("may lanh") != -1 ||
    cleanText.indexOf("nhiet do") != -1 || cleanText.indexOf("do am") != -1 || cleanText.indexOf("ap suat") != -1 ||
    cleanText.indexOf("thoi tiet") != -1 || cleanText.indexOf("am lich") != -1 || cleanText.indexOf("may gio") != -1 ||
    cleanText.indexOf("ngay bao nhieu") != -1 || cleanText.indexOf("ngay may") != -1 || cleanText.indexOf("am luong") != -1 ||
    cleanText.indexOf("loa") != -1 || cleanText.indexOf("do sang") != -1 || cleanText.indexOf("sang") != -1 ||
    cleanText.indexOf("bat") != -1 || cleanText.indexOf("tat") != -1 || cleanText.indexOf("mo") != -1 || cleanText.indexOf("dong") != -1 ||
    cleanText.indexOf("tang") != -1 || cleanText.indexOf("giam") != -1 || cleanText.indexOf("dung") != -1 || cleanText.indexOf("ngung") != -1 ||
    cleanText.indexOf("la gi") != -1 || cleanText.indexOf("the nao") != -1 || cleanText.indexOf("nhu the nao") != -1 || cleanText.indexOf("tai sao") != -1 ||
    cleanText.indexOf("ai la") != -1 || cleanText.indexOf("o dau") != -1 || cleanText.indexOf("bao nhieu") != -1 || cleanText.indexOf("khi nao") != -1 ||
    cleanText.indexOf("huong dan") != -1 || cleanText.indexOf("giai thich") != -1 || cleanText.indexOf("nghe") != -1 || cleanText.indexOf("hat") != -1 ||
    cleanText.indexOf("tim") != -1 || cleanText.indexOf("ke") != -1 || cleanText.indexOf("chuyen") != -1 || cleanText.indexOf("nho") != -1 ||
    cleanText.indexOf("edge impulse") != -1 || cleanText.indexOf("arduino") != -1 || cleanText.indexOf("esp32") != -1 || cleanText.indexOf("model") != -1 ||
    cleanText.indexOf("trang thai") != -1 || cleanText.indexOf("kiem tra") != -1 || cleanText.indexOf("tinh trang") != -1
  );

  // 1. Chỉ trả lời "Dạ em nghe đây ạ!" nếu câu nói HOÀN TOÀN CHỈ LÀ LỜI CHÀO/GỌI TÊN ĐƠN THUẦN
  bool isPureGreeting = !hasCommandIntent && (
    normalizedGreeting == "hi nori" || normalizedGreeting == "xin chao" || normalizedGreeting == "chao nori" || 
    normalizedGreeting == "hello" || normalizedGreeting == "nori oi" || normalizedGreeting == "hey nori" || 
    normalizedGreeting == "chao em" || normalizedGreeting == "alo nori" || normalizedGreeting == "chao ban" || 
    normalizedGreeting == "hi" || normalizedGreeting == "hello nori" || normalizedGreeting == "xin chao nori" || 
    normalizedGreeting == "nori" || normalizedGreeting == "no ri" || normalizedGreeting == "no ri oi" || 
    normalizedGreeting == "chao nori nha" || normalizedGreeting == "hi nori nha" || normalizedGreeting == "oi nori" || 
    normalizedGreeting == "e nori" ||
    ((normalizedGreeting.indexOf("chao nori") != -1 || normalizedGreeting.indexOf("hi nori") != -1 || 
      normalizedGreeting.indexOf("hello nori") != -1 || normalizedGreeting.indexOf("nori oi") != -1 || 
      normalizedGreeting.indexOf("xin chao nori") != -1) && normalizedGreeting.length() <= 15)
  );

  if (isPureGreeting) {
    isWaitingFollowupCommand = true; // Bật cờ chờ câu lệnh tiếp theo
    isAiBusy = false;
    if (!isSilent) playTTS("Dạ em nghe đây ạ!");
    return;
  }

  // 2. Nếu người dùng CHÀO RỒI HỎI LUÔN (Ví dụ: "Xin chào Nori, bật đèn cho tôi")
  // -> Cắt bỏ tiền tố chào hỏi để trích xuất đúng câu hỏi thực tế!
  const char* greetingPrefixes[] = {
    "xin chao nori", "chao nori",
    "hi nori", "hello nori",
    "hay nori", "he nori", "ha nori",
    "nori oi", "hey nori",
    "alo nori", "chao em",
    "xin chao", "hello", "hi", "hay"
  };
  
  for (const char* prefix : greetingPrefixes) {
    if (cleanText.startsWith(prefix)) {
      int pLen = strlen(prefix);
      userText = userText.substring(pLen);
      userText.trim();
      while (userText.startsWith(",") || userText.startsWith(".") || userText.startsWith("!") || userText.startsWith("?")) {
        userText = userText.substring(1);
        userText.trim();
      }
      lowerText = removeVietnameseAccents(userText);
      lowerText.toLowerCase();
      lowerText.trim();
      break;
    }
  }

  if (userText.length() == 0) {
    isWaitingFollowupCommand = true;
    if (!isSilent) playTTS("Dạ em nghe đây ạ!");
    return;
  }

  // ── 1. FAST-PATH DÀNH RIÊNG CHO LỆNH ÂM NHẠC (ƯU TIÊN TUYỆT ĐỐI) ──
  bool isStopMusic = (lowerText == "tat nhac" || lowerText == "dung nhac" || lowerText == "ngung nhac" ||
                      lowerText == "tat bai hat" || lowerText == "dung bai hat" || lowerText == "thoi hat" ||
                      lowerText == "tat loa" || lowerText == "im di" || lowerText == "stop music" ||
                      lowerText == "dung lai" || lowerText == "ngung hat");
  if (isStopMusic) {
    extern volatile bool hasPendingAudioStop;
    hasPendingAudioStop = true;
    stopMusicScreen();
    setAIFaceState(AI_STATE_IDLE);
    if (!isSilent) playTTS("Đã dừng phát nhạc rồi nha bạn!");
    return;
  }

  bool isPlayMusic = (
    lowerText.indexOf("bai hat") != -1 || lowerText.indexOf("ca khuc") != -1 || lowerText.indexOf("bai nhac") != -1 ||
    lowerText.indexOf("mo bai") != -1 || lowerText.indexOf("bat bai") != -1 || lowerText.indexOf("phat bai") != -1 ||
    lowerText.indexOf("hat bai") != -1 || lowerText.indexOf("nghe bai") != -1 || lowerText.indexOf("tim bai") != -1 ||
    lowerText.indexOf("mo nhac") != -1 || lowerText.indexOf("bat nhac") != -1 || lowerText.indexOf("phat nhac") != -1 ||
    lowerText.indexOf("nghe nhac") != -1 || lowerText.indexOf("hat nhac") != -1 || lowerText.indexOf("tim nhac") != -1 ||
    lowerText.indexOf("cho toi nghe") != -1 || lowerText.indexOf("cho minh nghe") != -1 || lowerText.indexOf("cho nghe") != -1 ||
    lowerText.indexOf("mo cho toi") != -1 || lowerText.indexOf("mo cho minh") != -1 || lowerText.indexOf("mo giup") != -1 || lowerText.indexOf("mo ho") != -1 ||
    lowerText.indexOf("bat cho toi") != -1 || lowerText.indexOf("bat cho minh") != -1 || lowerText.indexOf("bat giup") != -1 || lowerText.indexOf("bat ho") != -1 ||
    lowerText.indexOf("phat cho toi") != -1 || lowerText.indexOf("phat cho minh") != -1 || lowerText.indexOf("phat giup") != -1 || lowerText.indexOf("phat ho") != -1 ||
    lowerText.indexOf("muon nghe") != -1 || lowerText.indexOf("muon bat") != -1 || lowerText.indexOf("muon mo") != -1 || lowerText.indexOf("muon phat") != -1 ||
    lowerText.indexOf("toi nghe nhac") != -1 || lowerText.indexOf("toi muon nghe nhac") != -1 ||
    lowerText.startsWith("play ") || lowerText.startsWith("nhac ") || lowerText.startsWith("bai ") ||
    ((lowerText.indexOf("mo ") != -1 || lowerText.indexOf("bat ") != -1 || lowerText.indexOf("phat ") != -1) && (lowerText.indexOf(" bai ") != -1 || lowerText.indexOf(" nhac ") != -1))
  );

  if (isPlayMusic) {
    String songQuery = "";
    String cleanedSong = lowerText;
    cleanedSong.replace("mo giup toi", "");
    cleanedSong.replace("bat giup toi", "");
    cleanedSong.replace("phat giup toi", "");
    cleanedSong.replace("mo ho toi", "");
    cleanedSong.replace("bat ho toi", "");
    cleanedSong.replace("phat ho toi", "");
    cleanedSong.replace("mo cho toi", "");
    cleanedSong.replace("bat cho toi", "");
    cleanedSong.replace("phat cho toi", "");
    cleanedSong.replace("mo cho minh", "");
    cleanedSong.replace("bat cho minh", "");
    cleanedSong.replace("phat cho minh", "");
    cleanedSong.replace("cho toi nghe bai", "");
    cleanedSong.replace("cho minh nghe bai", "");
    cleanedSong.replace("cho toi nghe", "");
    cleanedSong.replace("cho minh nghe", "");
    cleanedSong.replace("cho nghe bai", "");
    cleanedSong.replace("cho nghe", "");
    cleanedSong.replace("toi muon nghe bai", "");
    cleanedSong.replace("toi muon nghe", "");
    cleanedSong.replace("toi muon mo", "");
    cleanedSong.replace("toi muon bat", "");
    cleanedSong.replace("toi muon phat", "");
    cleanedSong.replace("toi nghe nhac", "");
    cleanedSong.replace("toi nghe bai", "");
    cleanedSong.replace("muon nghe bai", "");
    cleanedSong.replace("muon nghe", "");
    cleanedSong.replace("muon mo", "");
    cleanedSong.replace("muon bat", "");
    cleanedSong.replace("muon phat", "");
    cleanedSong.replace("phat bai nhac", "");
    cleanedSong.replace("bat bai nhac", "");
    cleanedSong.replace("mo bai nhac", "");
    cleanedSong.replace("phat bai hat", "");
    cleanedSong.replace("bat bai hat", "");
    cleanedSong.replace("mo bai hat", "");
    cleanedSong.replace("hat bai hat", "");
    cleanedSong.replace("nghe bai hat", "");
    cleanedSong.replace("phat bai", "");
    cleanedSong.replace("bat bai", "");
    cleanedSong.replace("mo bai", "");
    cleanedSong.replace("hat bai", "");
    cleanedSong.replace("nghe bai", "");
    cleanedSong.replace("phat nhac", "");
    cleanedSong.replace("bat nhac", "");
    cleanedSong.replace("mo nhac", "");
    cleanedSong.replace("ca khuc", "");
    cleanedSong.replace("bai hat", "");
    cleanedSong.replace("bai nhac", "");
    cleanedSong.replace("play music", "");
    cleanedSong.replace("play ", "");
    cleanedSong.replace(" cua ", " ");
    cleanedSong.replace(" do ", " ");
    cleanedSong.replace(" boi ", " ");
    cleanedSong.replace("hat di", "");
    cleanedSong.replace("nghe di", "");
    
    // Tự động sửa các lỗi phát âm / phiên âm tiếng Anh phổ biến
    cleanedSong.replace("bay bay justin", "Baby Justin Bieber");
    cleanedSong.replace("bay bay", "Baby");
    cleanedSong.replace("bây bi", "Baby");
    cleanedSong.replace("bai bi", "Baby");
    cleanedSong.replace("đét ba xi tô", "Despacito");
    cleanedSong.replace("sếp ốp du", "Shape of You");
    cleanedSong.replace("phây đít", "Faded");
    cleanedSong.replace("xi du ờ gên", "See You Again");

    while (cleanedSong.indexOf("  ") != -1) {
      cleanedSong.replace("  ", " ");
    }
    cleanedSong.trim();

    if (cleanedSong.length() > 0 && cleanedSong != "nhac" && cleanedSong != "bai" && cleanedSong != "hat" && cleanedSong != "nghe") {
      songQuery = cleanedSong;
    } else {
      // Khi người dùng chỉ nói chung chung "mở nhạc", "tôi nghe nhạc", "phát nhạc"
      const char* hotHits[] = {
        "Lạc Trôi",
        "Âm Thầm Bên Em",
        "Đi Về Nhà",
        "Cắt Đôi Nỗi Sầu",
        "Nơi Này Có Anh",
        "Shape of You",
        "See You Again",
        "Nhạc Lofi chill"
      };
      songQuery = hotHits[random(0, 8)];
    }

    Serial.println("🎵 Nhận diện lệnh phát nhạc: " + songQuery);
    extern String pendingSongTitle;
    extern bool isMusicMode;
    pendingSongTitle = songQuery;
    isMusicMode = true;
    if (!isSilent) {
      playTTS("Dạ em đang tìm và phát bài " + songQuery + " cho bạn thưởng thức đây nè!");
    }
    return;
  }
  
  // --- FAST-PATH ĐIỀU HƯỚNG MÀN HÌNH (TRỞ VỀ / MỞ REMOTE) ---
  if (lowerText == "tro ve" || lowerText == "quay ve" || lowerText == "ve man hinh chinh" || 
      lowerText == "tro ve man hinh chinh" || lowerText == "man hinh chinh" || lowerText == "ve dashboard" ||
      lowerText == "dong ai" || lowerText.indexOf("ve man hinh") != -1 || lowerText.indexOf("tro ve man hinh") != -1 ||
      lowerText.indexOf("cho ve") != -1 || lowerText.indexOf("hanh tranh") != -1 || lowerText.indexOf("man hinh") != -1 ||
      lowerText.indexOf("quay ve") != -1 || lowerText.indexOf("tro ve") != -1) {
    if (audio.isRunning()) audio.stopSong();
    isMusicMode = false;
    if (is_ir_learning_mode) {
      is_ir_learning_mode = false;
      irrecv.disableIRIn();
    }
    stopMusicScreen();
    showMainScreen();
    setAIFaceState(AI_STATE_IDLE);
    setLedMode(0);
    uiUpdatePending = true;
    if (!isSilent) playTTS("Đã trở về màn hình chính cho bạn rồi nhé!");
    return;
  } else if (lowerText == "mo remote" || lowerText == "bat remote" || lowerText == "mo dieu khien" || 
             lowerText == "hoc lenh" || lowerText == "che do hoc lenh" || lowerText == "man hinh remote" ||
             lowerText.indexOf("mo remote") != -1 || lowerText.indexOf("man hinh remote") != -1 || lowerText.indexOf("che do hoc lenh") != -1 ||
             lowerText.indexOf("remote") != -1 || lowerText.indexOf("dieu khien") != -1 || lowerText.indexOf("hoc lenh") != -1) {
    if (audio.isRunning()) audio.stopSong();
    is_ir_learning_mode = true;
    irrecv.enableIRIn();
    showIrScreen();
    updateIrScreen(selected_ir_idx, typeToString(learned_ir[selected_ir_idx].type), String((uint32_t)(learned_ir[selected_ir_idx].value & 0xFFFFFFFF), HEX));
    uiUpdatePending = true;
    if (!isSilent) playTTS("Đã mở màn hình điều khiển và chế độ học lệnh hồng ngoại rồi nè!");
    return;
  }

  // --- FAST-PATH LẬP TỨC CHO CÁC LỆNH ĐIỀU KHIỂN & CÂU HỎI THƯỜNG GẶP (PHẢN HỒI TỨC THÌ < 20ms) ---
  if (lowerText == "bat den 1" || lowerText == "mo den 1" || lowerText == "bat relay 1" || lowerText == "mo relay 1" || lowerText == "mo khoa cua" || lowerText == "mo khoa") {
    relay1 = true; digitalWrite(RELAY1_PIN, LOW);
    if (firebase_ready) Firebase.RTDB.setBoolAsync(&fbdo, FIREBASE_NODE "/relay1", true);
    saveSettingsToEEPROM(relay1, relay2, ledBrightness);
    uiUpdatePending = true;
    if (!isSilent) playTTS("Đã bật relay 1 mở khóa cho bạn rồi nè!");
    return;
  } else if (lowerText == "tat den 1" || lowerText == "tat relay 1" || lowerText == "dong khoa cua" || lowerText == "khoa cua" || lowerText == "dong relay 1") {
    relay1 = false; digitalWrite(RELAY1_PIN, HIGH);
    if (firebase_ready) Firebase.RTDB.setBoolAsync(&fbdo, FIREBASE_NODE "/relay1", false);
    saveSettingsToEEPROM(relay1, relay2, ledBrightness);
    uiUpdatePending = true;
    if (!isSilent) playTTS("Đã tắt relay 1 đóng khóa rồi nha!");
    return;
  } else if (lowerText == "bat den 2" || lowerText == "mo den 2" || lowerText == "bat relay 2" || lowerText == "mo relay 2" || lowerText == "bat den" || lowerText == "mo den" || lowerText == "bat den phong") {
    relay2 = true; digitalWrite(RELAY2_PIN, LOW);
    if (firebase_ready) Firebase.RTDB.setBoolAsync(&fbdo, FIREBASE_NODE "/relay2", true);
    saveSettingsToEEPROM(relay1, relay2, ledBrightness);
    uiUpdatePending = true;
    if (!isSilent) playTTS("Đã bật đèn cho bạn sáng sủa rồi nhé!");
    return;
  } else if (lowerText == "tat den 2" || lowerText == "tat relay 2" || lowerText == "tat den" || lowerText == "tat den phong") {
    relay2 = false; digitalWrite(RELAY2_PIN, HIGH);
    if (firebase_ready) Firebase.RTDB.setBoolAsync(&fbdo, FIREBASE_NODE "/relay2", false);
    saveSettingsToEEPROM(relay1, relay2, ledBrightness);
    uiUpdatePending = true;
    if (!isSilent) playTTS("Đã tắt đèn rồi nha bạn!");
    return;
  } else if (lowerText == "bat tat ca" || lowerText == "bat het den" || lowerText == "mo tat ca" || lowerText == "bat het thiet bi") {
    relay1 = true; relay2 = true;
    digitalWrite(RELAY1_PIN, LOW); digitalWrite(RELAY2_PIN, LOW);
    if (firebase_ready) {
      Firebase.RTDB.setBoolAsync(&fbdo, FIREBASE_NODE "/relay1", true);
      Firebase.RTDB.setBoolAsync(&fbdo, FIREBASE_NODE "/relay2", true);
    }
    saveSettingsToEEPROM(relay1, relay2, ledBrightness);
    uiUpdatePending = true;
    if (!isSilent) playTTS("Đã bật tất cả đèn và thiết bị trong phòng rồi nè!");
    return;
  } else if (lowerText == "tat tat ca" || lowerText == "tat het den" || lowerText == "tat tat ca thiet bi" || lowerText == "tat het thiet bi" || lowerText == "tat het") {
    relay1 = false; relay2 = false;
    digitalWrite(RELAY1_PIN, HIGH); digitalWrite(RELAY2_PIN, HIGH);
    if (firebase_ready) {
      Firebase.RTDB.setBoolAsync(&fbdo, FIREBASE_NODE "/relay1", false);
      Firebase.RTDB.setBoolAsync(&fbdo, FIREBASE_NODE "/relay2", false);
    }
    saveSettingsToEEPROM(relay1, relay2, ledBrightness);
    uiUpdatePending = true;
    if (!isSilent) playTTS("Đã tắt toàn bộ thiết bị và đèn rồi nha bạn!");
    return;
  }

  // --- FAST-PATH ÂM LƯỢNG & ĐÈN NỀN / LED VÒNG ---
  if (lowerText.indexOf("tang am luong") != -1 || lowerText.indexOf("cho to len") != -1 || lowerText.indexOf("bat to len") != -1 || lowerText.indexOf("tang loa") != -1) {
    audioVolume = min((uint8_t)21, (uint8_t)(audioVolume + 3));
    audio.setVolume(audioVolume);
    if (firebase_ready) Firebase.RTDB.setIntAsync(&fbdo, FIREBASE_NODE "/settings/audioVolume", audioVolume);
    if (!isSilent) playTTS("Đã tăng âm lượng loa lên mức " + String(audioVolume) + " rồi nè!");
    return;
  } else if (lowerText.indexOf("giam am luong") != -1 || lowerText.indexOf("cho nho lai") != -1 || lowerText.indexOf("chinh nho loa") != -1 || lowerText.indexOf("giam loa") != -1) {
    audioVolume = max((uint8_t)3, (uint8_t)(audioVolume - 3));
    audio.setVolume(audioVolume);
    if (firebase_ready) Firebase.RTDB.setIntAsync(&fbdo, FIREBASE_NODE "/settings/audioVolume", audioVolume);
    if (!isSilent) playTTS("Đã giảm âm lượng loa xuống mức " + String(audioVolume) + " rồi nha!");
    return;
  } else if (lowerText.indexOf("den cau vong") != -1 || lowerText.indexOf("led cau vong") != -1 || lowerText.indexOf("doi mau den") != -1) {
    setLedMode(15);
    if (!isSilent) playTTS("Đã chuyển hiệu ứng đèn LED sang dải cầu vồng rực rỡ rồi nhé!");
    return;
  } else if (lowerText.indexOf("den nhip tho") != -1 || lowerText.indexOf("led nhip tho") != -1 || lowerText.indexOf("led breathing") != -1) {
    setLedMode(3);
    if (!isSilent) playTTS("Đã chuyển đèn sang chế độ nhịp thở êm dịu rồi nè!");
    return;
  } else if (lowerText.indexOf("tang do sang") != -1 || lowerText.indexOf("tang sang den") != -1) {
    ledBrightness = min(255, ledBrightness + 40);
    pixels.setBrightness(ledBrightness);
    pixels.show();
    saveSettingsToEEPROM(relay1, relay2, ledBrightness);
    if (firebase_ready) Firebase.RTDB.setIntAsync(&fbdo, FIREBASE_NODE "/settings/ledBrightness", ledBrightness);
    if (!isSilent) playTTS("Đã tăng độ sáng đèn vòng lên " + String((int)(ledBrightness * 100 / 255)) + " phần trăm rồi nha!");
    return;
  } else if (lowerText.indexOf("giam do sang") != -1 || lowerText.indexOf("giam sang den") != -1) {
    ledBrightness = max(10, ledBrightness - 40);
    pixels.setBrightness(ledBrightness);
    pixels.show();
    saveSettingsToEEPROM(relay1, relay2, ledBrightness);
    if (firebase_ready) Firebase.RTDB.setIntAsync(&fbdo, FIREBASE_NODE "/settings/ledBrightness", ledBrightness);
    if (!isSilent) playTTS("Đã giảm độ sáng đèn vòng xuống " + String((int)(ledBrightness * 100 / 255)) + " phần trăm rồi nhé!");
    return;
  } else if (lowerText.indexOf("do sang toi da") != -1 || lowerText.indexOf("den sang nhat") != -1) {
    ledBrightness = 255;
    pixels.setBrightness(255);
    pixels.show();
    saveSettingsToEEPROM(relay1, relay2, ledBrightness);
    if (firebase_ready) Firebase.RTDB.setIntAsync(&fbdo, FIREBASE_NODE "/settings/ledBrightness", ledBrightness);
    if (!isSilent) playTTS("Đã chỉnh đèn LED sáng tối đa 100 phần trăm rồi nha!");
    return;
  } else if (lowerText == "trang thai he thong" || lowerText == "kiem tra he thong" || lowerText == "bao cao he thong" || lowerText == "tinh trang nha" || lowerText == "trang thai nha") {
    String rep = "Báo cáo hệ thống nhà thông minh: Nhiệt độ phòng là " + String(indoorTemp, 1) + " độ C, độ ẩm " + String(indoorHum, 0) + " phần trăm, áp suất " + String(indoorPres, 0) + " héc-tô-pas-can. Relay 1 đang " + (relay1 ? "bật" : "tắt") + ", Relay 2 đang " + (relay2 ? "bật" : "tắt") + ", Điều hòa đang " + (daikin_power ? ("bật " + String(daikin_temp) + " độ") : "tắt") + ", Quạt đang ở số " + String(current_fan_speed) + ", Âm lượng loa " + String(audioVolume) + " trên 21, Độ sáng đèn " + String((int)(ledBrightness * 100 / 255)) + " phần trăm. Mọi thứ đang hoạt động rất tốt nha bạn!";
    if (!isSilent) playTTS(rep);
    return;
  }

  // --- FAST-PATH GIỜ & LỊCH ÂM DƯƠNG ---
  if (lowerText.indexOf("may gio") != -1 || lowerText.indexOf("gio bao nhieu") != -1) {
    if (!isSilent) playTTS("Bây giờ là " + hhmmText + " nha bạn!");
    return;
  } else if (lowerText.indexOf("am lich") != -1 || lowerText.indexOf("ngay am") != -1 || lowerText.indexOf("lich am") != -1) {
    extern int lunarDay_global;
    extern int lunarMonth_global;
    extern String canChiDay_global;
    extern String canChiYear_global;
    String ans = "Hôm nay là ngày " + String(lunarDay_global) + " tháng " + String(lunarMonth_global) + " âm lịch, năm " + canChiYear_global + ", ngày " + canChiDay_global + " nha bạn!";
    if (!isSilent) playTTS(ans);
    return;
  } else if (lowerText.indexOf("ngay bao nhieu") != -1 || lowerText.indexOf("hom nay ngay may") != -1 || lowerText.indexOf("ngay duong") != -1 || lowerText == "hom nay ngay gi") {
    extern int lunarDay_global;
    extern int lunarMonth_global;
    extern String canChiYear_global;
    String ans = "Hôm nay là ngày " + dateSolar + " dương lịch, tức ngày " + String(lunarDay_global) + " tháng " + String(lunarMonth_global) + " âm lịch năm " + canChiYear_global + " nha bạn!";
    if (!isSilent) playTTS(ans);
    return;
  }

  // --- FAST-PATH HƯỚNG DẪN EDGE IMPULSE & HUẤN LUYỆN MODEL NHẬN DIỆN GIỌNG NÓI ---
  if (lowerText.indexOf("edge impulse") != -1 || lowerText.indexOf("tu huan luyen model") != -1 ||
      lowerText.indexOf("huan luyen model") != -1 || lowerText.indexOf("huan luyen tu khoa") != -1 ||
      lowerText.indexOf("nhan dien giong noi bang arduino") != -1) {
    String eiAns = "Nếu bạn muốn làm mọi thứ bằng Arduino IDE và tự huấn luyện từ khóa riêng tiếng Việt, Edge Impulse là lựa chọn hoàn hảo. Bạn dùng điện thoại hoặc máy tính để thu âm hàng loạt các mẫu giọng nói như Bật đèn hay Tắt quạt, tải lên hệ thống Edge Impulse để huấn luyện, sau đó hệ thống sẽ xuất ra một file thư viện C++ dựa trên TensorFlow Lite for Microcontrollers để bạn import thẳng vào Arduino IDE và nạp vào ESP32 chạy trực tiếp!";
    Serial.println("🤖 Nori (Edge Impulse Fast-Path): " + eiAns);
    if (!isSilent) playTTS(eiAns);
    return;
  }

  // --- FAST-PATH ĐIỀU KHIỂN ĐIỀU HÒA / MÁY LẠNH (PHẢN HỒI TỨC THÌ < 20ms) ---
  if (lowerText == "bat dieu hoa" || lowerText == "mo dieu hoa" || lowerText == "bat may lanh" || lowerText == "mo may lanh" ||
      lowerText.indexOf("bat dieu hoa") != -1 || lowerText.indexOf("mo dieu hoa") != -1 ||
      lowerText.indexOf("bat may lanh") != -1 || lowerText.indexOf("mo may lanh") != -1) {
    int reqTemp = (daikin_temp >= 16 && daikin_temp <= 30) ? daikin_temp : 25;
    for (int t = 16; t <= 32; t++) {
      if (lowerText.indexOf(String(t) + " do") != -1 || lowerText.indexOf(String(t) + "do") != -1 || lowerText.indexOf(" " + String(t)) != -1) {
        reqTemp = t;
        break;
      }
    }
    sendDaikinCommand(true, reqTemp, 10);
    if (!isSilent) playTTS("Đã bật điều hòa " + String(reqTemp) + " độ mát mẻ cho bạn rồi nè!");
    return;
  } else if (lowerText == "tat dieu hoa" || lowerText == "tat may lanh" || 
             lowerText.indexOf("tat dieu hoa") != -1 || lowerText.indexOf("tat may lanh") != -1 ||
             lowerText.indexOf("ngung dieu hoa") != -1 || lowerText.indexOf("ngung may lanh") != -1) {
    sendDaikinCommand(false, daikin_temp, 10);
    if (!isSilent) playTTS("Đã tắt điều hòa rồi nha bạn!");
    return;
  } else if (lowerText.indexOf("tang nhiet do") != -1 || lowerText.indexOf("tang dieu hoa") != -1 || lowerText.indexOf("tang may lanh") != -1) {
    if (daikin_temp < 30) daikin_temp++;
    sendDaikinCommand(true, daikin_temp, 10);
    if (!isSilent) playTTS("Đã tăng nhiệt độ điều hòa lên " + String(daikin_temp) + " độ rồi nha!");
    return;
  } else if (lowerText.indexOf("giam nhiet do") != -1 || lowerText.indexOf("giam dieu hoa") != -1 || lowerText.indexOf("giam may lanh") != -1 || lowerText.indexOf("cho mat hon") != -1) {
    if (daikin_temp > 18) daikin_temp--;
    sendDaikinCommand(true, daikin_temp, 10);
    if (!isSilent) playTTS("Đã giảm điều hòa xuống " + String(daikin_temp) + " độ cho mát mẻ hơn rồi nè!");
    return;
  }

  String answerToSpeak = "";
  String emotionToSet = "";

  {
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(12000);
    client.setHandshakeTimeout(6);

    Serial.println("🧠 Đang kết nối tới Groq LLM (api.groq.com:443)...");
    bool connected = client.connect("api.groq.com", 443);
    if (!connected) {
      Serial.printf("⚠️ Thử kết nối lại tới Groq LLM (Free Heap: %u, Max Block: %u)...\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
      vTaskDelay(pdMS_TO_TICKS(350));
      connected = client.connect("api.groq.com", 443);
    }

    if (!connected) {
      Serial.println("❌ Không thể kết nối tới api.groq.com:443!");
      if (!isSilent) setAIFaceState(AI_STATE_IDLE);
      isAiBusy = false;
      return;
    }

    extern String userName;
    extern int ledBrightness;
    extern uint8_t audioVolume;
    extern String hhmmText;   // Giờ hiện tại từ RTC DS3231 (vd: "18:46")
    extern String dateSolar;  // Ngày hiện tại (vd: "12/08")
    extern float indoorTemp, indoorHum, indoorPres;
    extern float owmTemp, owmHum, owmWind;
    extern String owmDesc;

    initAiMemory();
    String systemPrompt = "Bạn là NORI - Trợ lý AI và Chatbot Đa Năng Cao Cấp của hệ thống Nhà Thông Minh ESP32. "
                          "Hệ thống phần cứng vi điều khiển do chính tay ông chủ " + memUserName + " chế tạo ra. "
                          "XƯNG HÔ: Tự xưng là 'em' hoặc 'Nori', gọi người dùng là '" + memUserName + "' hoặc 'ông chủ', 'bạn'. "
                          "Thời gian hiện tại: " + hhmmText + " ngày " + dateSolar + ". "
                          "BỘ NHỚ LÂU DÀI: "
                          "- Tên người dùng: " + memUserName + " (Tên AI: " + memAiName + "). "
                          "- Sở thích & Thông tin đã nhớ: " + (memUserFacts.length() > 0 ? memUserFacts : "Chưa có") + ". "
                          "- Ghi chú / Dặn dò: " + (memCustomNotes.length() > 0 ? memCustomNotes : "Chưa có") + ". "
                          "DỮ LIỆU PHẦN CỨNG THỰC TẾ TRONG PHÒNG: "
                          "- Cảm biến AHT20: Nhiệt độ trong phòng = " + String(indoorTemp, 1) + " độ C, Độ ẩm không khí = " + String(indoorHum, 1) + "%. "
                          "- Cảm biến BMP280: Áp suất không khí = " + String(indoorPres, 1) + " hPa. "
                          "- Ngoài trời (" + String(locationName) + "): Nhiệt độ = " + String(owmTemp, 1) + " độ C, Độ ẩm = " + String(owmHum, 1) + "%, Tốc độ gió = " + String(owmWind, 1) + " m/s, Tình trạng trời = " + owmDesc + ". "
                          "- Trạng thái thiết bị: Relay 1 đang " + (relay1 ? "BẬT" : "TẮT") + ", Relay 2 đang " + (relay2 ? "BẬT" : "TẮT") + ", Điều hòa đang " + (daikin_power ? ("BẬT " + String(daikin_temp) + " độ") : "TẮT") + ", Quạt đang ở số: " + String(current_fan_speed) + ", Độ sáng LED 12 vòng: " + String(ledBrightness) + "/255, Âm lượng loa: " + String(audioVolume) + "/21. "
                          "QUY TẮC PHẢN HỒI CỰC KỲ QUAN TRỌNG: "
                          "1. BẠN LÀ CHATBOT ĐA NĂNG TOÀN DIỆN: Có kiến thức chuyên sâu về mọi lĩnh vực. Đặc biệt am hiểu công nghệ vi điều khiển, Edge Impulse (huấn luyện model nhận diện giọng nói/từ khóa tiếng Việt bằng Arduino IDE và xuất thư viện C++ TensorFlow Lite for Microcontrollers), ESP32-S3, AI, y học, sinh học, khoa học, văn hóa, xã hội. "
                          "2. CÂU TRẢ LỜI CHUYÊN MÔN: Phải đầy đủ, chuẩn xác, mạch lạc (3-5 câu rõ ràng), giải thích cặn kẽ bản chất và ứng dụng. "
                          "3. ĐỐI VỚI CÂU HỎI CẢM BIẾN TRONG PHÒNG: Báo cáo chính xác số liệu AHT20 và BMP280, đưa ra đánh giá môi trường sống thực tế. "
                          "4. GHI NHỚ LÂU DÀI: Nếu người dùng bảo ghi nhớ điều gì, hãy lưu vào 'update_user_name', 'update_memory_fact' hoặc 'update_custom_note'. "
                          "5. ĐỊNH DẠNG ÂM THANH: Toàn bộ văn bản 'answer' phải viết trên 1 đoạn duy nhất, không xuống dòng (Enter), không ký tự đặc biệt (*, #, -), chỉ dùng dấu câu (, . ? !). "
                          "BẮT BUỘC phản hồi bằng JSON: "
                          "{\"answer\": \"câu trả lời\", \"action\": \"none\", \"ac_temp\": 26, \"fan_speed\": " + String(current_fan_speed) + ", \"led_brightness\": " + String(ledBrightness) + ", \"volume_level\": " + String(audioVolume) + ", \"emotion\": \"proud\", \"short_forecast\": \"" + (owmDesc.length() > 0 ? owmDesc : "Nang") + "\", \"weather_icon\": \"sun\", \"song_url\": \"\", \"song_name\": \"\", \"update_user_name\": \"\", \"update_memory_fact\": \"\", \"update_custom_note\": \"\"} "
                          "Trường 'emotion' chọn: neutral, happy, sad, surprised, confused, angry, excited, proud, curious, love, worried, tired, sleepy. "
                          "Trường 'action' chọn: none, set_relay1_on, set_relay1_off, set_relay2_on, set_relay2_off, set_all_relays_on, set_all_relays_off, set_ac_on, set_ac_off, set_fan, play_music, search_music, stop_music, goto_main_screen, goto_remote_screen. "
                          "Nếu người dùng bảo trở về, quay về hoặc màn hình chính: chọn action: 'goto_main_screen'. "
                          "Nếu người dùng bảo mở remote, điều khiển hoặc học lệnh: chọn action: 'goto_remote_screen'. "
                          "Nếu người dùng yêu cầu phát/nghe nhạc: BẮT BUỘC chọn action: 'search_music' và trả về 'song_name' là tên bài hát/ca sĩ. "
                          "Nếu người dùng bảo dừng/tắt nhạc: chọn action: 'stop_music'. "
                          "Nếu điều khiển quạt: chọn action: 'set_fan' và 'fan_speed' từ 0-3. "
                          "Nếu điều khiển đèn: chọn 'set_relay1_on'/'set_relay1_off' hoặc 'set_relay2_on'/'set_relay2_off' hoặc 'set_all_relays_on'/'set_all_relays_off'. "
                          "Nếu chỉnh độ sáng LED vòng: cập nhật 'led_brightness' (0-255). "
                          "Nếu chỉnh âm lượng loa: cập nhật 'volume_level' (0-21). "
                          "Nếu bật điều hòa: chọn action: 'set_ac_on' kèm 'ac_temp' (16-30).";

    JsonDocument doc;
    doc["model"] = "groq/compound-mini";
    doc["max_tokens"] = 600;
    doc["response_format"]["type"] = "json_object";
    
    JsonArray messages = doc["messages"].to<JsonArray>();
    
    JsonObject sysMsg = messages.add<JsonObject>();
    sysMsg["role"] = "system";
    sysMsg["content"] = systemPrompt;
    
    // Thêm câu hỏi hiện tại vào lịch sử
    if (!isSilent) addChatHistory("user", userText);
    
    // Đẩy toàn bộ lịch sử vào mảng messages
    for (int i = 0; i < historyCount; i++) {
      JsonObject msg = messages.add<JsonObject>();
      msg["role"] = chatHistory[i].role;
      msg["content"] = chatHistory[i].content;
    }
    
    String requestBody;
    serializeJson(doc, requestBody);

    client.println("POST /openai/v1/chat/completions HTTP/1.1");
    client.println("Host: api.groq.com");
    client.println("Authorization: Bearer " + String(GROQ_API_KEY));
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.print("Content-Length: ");
    client.println(requestBody.length());
    client.println();
    client.print(requestBody);
    client.flush();

    String payload = "";
    unsigned long startWait = millis();
    while ((client.connected() || client.available()) && (millis() - startWait < 15000)) {
      while (client.available()) {
        payload += (char)client.read();
        startWait = millis();
      }
      if (payload.indexOf("\"choices\"") != -1 && payload.indexOf("}]") != -1) {
        int jStart = payload.indexOf('{');
        int jEnd = payload.lastIndexOf('}');
        if (jStart != -1 && jEnd != -1 && jEnd > jStart) {
          payload = payload.substring(jStart, jEnd + 1);
          break;
        }
      }
      vTaskDelay(pdMS_TO_TICKS(15));
    }
    client.stop();

    if (payload.length() > 0 && payload.indexOf("\"choices\"") != -1) {
      vTaskDelay(pdMS_TO_TICKS(15)); // Nhường CPU cho IDLE0
      JsonDocument resDoc;
      deserializeJson(resDoc, payload);
      const char* ai_reply = resDoc["choices"][0]["message"]["content"];
      
      // Parse JSON từ AI
      JsonDocument actionDoc;
      deserializeJson(actionDoc, ai_reply);
      const char* answer = actionDoc["answer"];
      const char* action = actionDoc["action"];
      const char* emotion = actionDoc["emotion"];
      const char* forecast = actionDoc["short_forecast"];
      const char* icon = actionDoc["weather_icon"];
      int ac_temp = actionDoc.containsKey("ac_temp") ? actionDoc["ac_temp"].as<int>() : 26;
      
      int led_b = actionDoc.containsKey("led_brightness") ? actionDoc["led_brightness"].as<int>() : -1;
      int vol_l = actionDoc.containsKey("volume_level") ? actionDoc["volume_level"].as<int>() : -1;
      
      if (forecast && strlen(forecast) > 0) {
        ai_prediction_short = String(forecast);
        if (icon && strlen(icon) > 0) {
          ai_prediction_icon = String(icon);
        }
        uiUpdatePending = true;
      }
      
      // Cập nhật giá trị LED và Loa nếu có
      if (led_b >= 0 && led_b <= 255) {
        extern int ledBrightness;
        extern void saveSettingsToEEPROM(bool, bool, int);
        ledBrightness = led_b;
        extern Adafruit_NeoPixel pixels;
        pixels.setBrightness(ledBrightness);
        pixels.show(); // Áp dụng độ sáng mới ngay lập tức
        saveSettingsToEEPROM(relay1, relay2, ledBrightness);
        if (firebase_ready) Firebase.RTDB.setIntAsync(&fbdo, FIREBASE_NODE "/settings/ledBrightness", ledBrightness);
      }
      
      if (vol_l >= 0 && vol_l <= 21) {
        extern uint8_t audioVolume;
        audioVolume = vol_l;
        audio.setVolume(audioVolume);
        if (firebase_ready) Firebase.RTDB.setIntAsync(&fbdo, FIREBASE_NODE "/settings/audioVolume", audioVolume);
      }
      
      vTaskDelay(pdMS_TO_TICKS(15)); // Nhường CPU cho IDLE0 sau khi cập nhật thiết bị

      // Cập nhật bộ nhớ lâu dài nếu AI trích xuất được thông tin
      if (actionDoc.containsKey("update_user_name")) {
        const char* uName = actionDoc["update_user_name"];
        if (uName && strlen(uName) > 0) saveAiMemory("userName", String(uName));
      }
      if (actionDoc.containsKey("update_memory_fact")) {
        const char* uFact = actionDoc["update_memory_fact"];
        if (uFact && strlen(uFact) > 0) saveAiMemory("userFacts", String(uFact));
      }
      if (actionDoc.containsKey("update_custom_note")) {
        const char* uNote = actionDoc["update_custom_note"];
        if (uNote && strlen(uNote) > 0) saveAiMemory("customNotes", String(uNote));
      }
      
      vTaskDelay(pdMS_TO_TICKS(15)); // Nhường CPU cho IDLE0 sau khi ghi Flash NVS

      // Xử lý action
      if (ai_reply && !isSilent) {
        addChatHistory("assistant", String(ai_reply)); // Lưu lại câu trả lời vào trí nhớ
      }
      
      if (!isSilent && answer) {
        answerToSpeak = String(answer);
        Serial.println("🤖 Nori: " + answerToSpeak);
        if (firebase_ready) {
          Firebase.RTDB.setStringAsync(&fbdo, FIREBASE_NODE "/ai/last_question", userText);
          Firebase.RTDB.setStringAsync(&fbdo, FIREBASE_NODE "/ai/last_answer", answerToSpeak);
        }
      }

      if (emotion && strlen(emotion) > 0) {
        emotionToSet = String(emotion);
      }
      
      if (action) {
        String actStr = String(action);
        if (actStr == "set_relay1_on") {
          relay1 = true; digitalWrite(RELAY1_PIN, LOW);
          if (firebase_ready) Firebase.RTDB.setBoolAsync(&fbdo, FIREBASE_NODE "/relay1", true);
          saveSettingsToEEPROM(relay1, relay2, ledBrightness);
        }
        else if (actStr == "set_relay1_off") {
          relay1 = false; digitalWrite(RELAY1_PIN, HIGH);
          if (firebase_ready) Firebase.RTDB.setBoolAsync(&fbdo, FIREBASE_NODE "/relay1", false);
          saveSettingsToEEPROM(relay1, relay2, ledBrightness);
        }
        else if (actStr == "set_relay2_on") {
          relay2 = true; digitalWrite(RELAY2_PIN, LOW);
          if (firebase_ready) Firebase.RTDB.setBoolAsync(&fbdo, FIREBASE_NODE "/relay2", true);
          saveSettingsToEEPROM(relay1, relay2, ledBrightness);
        }
        else if (actStr == "set_relay2_off") {
          relay2 = false; digitalWrite(RELAY2_PIN, HIGH);
          if (firebase_ready) Firebase.RTDB.setBoolAsync(&fbdo, FIREBASE_NODE "/relay2", false);
          saveSettingsToEEPROM(relay1, relay2, ledBrightness);
        }
        else if (actStr == "set_all_relays_on") {
          relay1 = true; relay2 = true;
          digitalWrite(RELAY1_PIN, LOW); digitalWrite(RELAY2_PIN, LOW);
          if (firebase_ready) {
            Firebase.RTDB.setBoolAsync(&fbdo, FIREBASE_NODE "/relay1", true);
            Firebase.RTDB.setBoolAsync(&fbdo, FIREBASE_NODE "/relay2", true);
          }
          saveSettingsToEEPROM(relay1, relay2, ledBrightness);
        }
        else if (actStr == "set_all_relays_off") {
          relay1 = false; relay2 = false;
          digitalWrite(RELAY1_PIN, HIGH); digitalWrite(RELAY2_PIN, HIGH);
          if (firebase_ready) {
            Firebase.RTDB.setBoolAsync(&fbdo, FIREBASE_NODE "/relay1", false);
            Firebase.RTDB.setBoolAsync(&fbdo, FIREBASE_NODE "/relay2", false);
          }
          saveSettingsToEEPROM(relay1, relay2, ledBrightness);
        }
        else if (actStr == "set_ac_on") {
          uint8_t targetTemp = (ac_temp >= 16 && ac_temp <= 30) ? ac_temp : 25;
          sendDaikinCommand(true, targetTemp, 10);
        }
        else if (actStr == "set_ac_off") {
          sendDaikinCommand(false, daikin_temp, 10);
        }
        else if (actStr == "play_music") {
          const char* song_url = actionDoc["song_url"];
          if (song_url && strlen(song_url) > 0) {
            pendingSongUrl = String(song_url);
            Serial.println("🎵 Đã lên lịch phát nhạc từ URL: " + pendingSongUrl);
          }
        }
        else if (actStr == "search_music") {
          const char* s_name = actionDoc["song_name"];
          if (s_name && strlen(s_name) > 0) {
            pendingSongTitle = String(s_name);
            Serial.println("🔎 Đã lên lịch tìm bài hát theo tên: " + pendingSongTitle);
          } else {
            pendingSongTitle = "Lạc Trôi";
          }
        }
        else if (actStr == "stop_music") {
          extern bool isMusicMode;
          extern String pendingSongUrl;
          extern String pendingSongTitle;
          extern bool wasAudioRunning;
          extern bool audio_just_finished;
          pendingSongUrl = "";
          pendingSongTitle = "";
          isMusicMode = false;
          extern volatile bool hasPendingAudioStop;
          hasPendingAudioStop = true;
          setAIFaceState(AI_STATE_IDLE);
          Serial.println("🛑 AI đã dừng phát nhạc an toàn theo yêu cầu!");
        }
        else if (actStr == "goto_main_screen") {
          if (audio.isRunning()) audio.stopSong();
          isMusicMode = false;
          if (is_ir_learning_mode) {
            is_ir_learning_mode = false;
            irrecv.disableIRIn();
          }
          stopMusicScreen();
          showMainScreen();
          setAIFaceState(AI_STATE_IDLE);
          setLedMode(0);
          Serial.println("📱 AI đã chuyển về màn hình chính Dashboard!");
        }
        else if (actStr == "goto_remote_screen") {
          if (audio.isRunning()) audio.stopSong();
          is_ir_learning_mode = true;
          irrecv.enableIRIn();
          showIrScreen();
          updateIrScreen(selected_ir_idx, typeToString(learned_ir[selected_ir_idx].type), String((uint32_t)(learned_ir[selected_ir_idx].value & 0xFFFFFFFF), HEX));
          Serial.println("📱 AI đã chuyển sang màn hình Remote Học Lệnh!");
        }
        else if (actStr == "set_fan") {
          extern LearnedIR learned_ir[];
          extern IRsend irsend;
          int target_speed = actionDoc.containsKey("fan_speed") ? actionDoc["fan_speed"].as<int>() : 0;
          
          if (target_speed == 0) {
            // Tắt quạt (Gửi lệnh 2 lần để chắc chắn ăn lệnh vì nút tắt thường không xoay vòng)
            if (learned_ir[0].type != decode_type_t::UNKNOWN) {
              irsend.send(learned_ir[0].type, learned_ir[0].value, learned_ir[0].bits);
              delay(100);
              irsend.send(learned_ir[0].type, learned_ir[0].value, learned_ir[0].bits);
              Serial.println("📡 AI đã phát lệnh TẮT QUẠT (Slot 1 - 2 lần)");
            }
            current_fan_speed = 0;
          } else {
            // Đảm bảo giới hạn tốc độ từ 1 đến 3
            if (target_speed > 3) target_speed = 3;
            
            if (current_fan_speed == 0) {
              // Đang tắt -> Bật lên mức target_speed (Bấm phím nguồn N lần)
              if (learned_ir[1].type != decode_type_t::UNKNOWN) {
                for (int i = 0; i < target_speed; i++) {
                  irsend.send(learned_ir[1].type, learned_ir[1].value, learned_ir[1].bits);
                  delay(600); // Đợi quạt nhận tín hiệu và xử lý xong
                }
                Serial.printf("📡 AI đã BẬT QUẠT lên tốc độ %d\n", target_speed);
              }
            } else {
              // Đang bật
              if (target_speed > current_fan_speed) {
                // Tăng tốc độ -> Bấm thêm N lần
                int diff = target_speed - current_fan_speed;
                if (learned_ir[1].type != decode_type_t::UNKNOWN) {
                  for (int i = 0; i < diff; i++) {
                    irsend.send(learned_ir[1].type, learned_ir[1].value, learned_ir[1].bits);
                    delay(600);
                  }
                  Serial.printf("📡 AI đã TĂNG QUẠT lên tốc độ %d\n", target_speed);
                }
              } else if (target_speed < current_fan_speed) {
                // Giảm tốc độ -> Phải tắt đi bật lại vì quạt xoay vòng
                if (learned_ir[0].type != decode_type_t::UNKNOWN) {
                  irsend.send(learned_ir[0].type, learned_ir[0].value, learned_ir[0].bits);
                  delay(1200); // Đợi quạt tắt hẳn (cánh quạt giảm tốc)
                }
                if (learned_ir[1].type != decode_type_t::UNKNOWN) {
                  for (int i = 0; i < target_speed; i++) {
                    irsend.send(learned_ir[1].type, learned_ir[1].value, learned_ir[1].bits);
                    delay(600);
                  }
                }
                Serial.printf("📡 AI đã GIẢM QUẠT xuống tốc độ %d\n", target_speed);
              }
            }
            current_fan_speed = target_speed;
          }
        }
        uiUpdatePending = true;
      }
    } else {
      Serial.println("❌ Lỗi nhận phản hồi từ Groq LLM!");
      if (payload.length() > 0) {
        Serial.println("Chi tiết: " + payload);
      }
      if (!isSilent) setAIFaceState(AI_STATE_IDLE);
    }
  }
  // Giải phóng hoàn toàn bộ nhớ TLS/SSL socket trước khi gọi Audio TTS
  delay(80);

  if (!isSilent && answerToSpeak.length() > 0) {
    playTTS(answerToSpeak);
    if (emotionToSet.length() > 0) {
      setAIFaceEmotion(emotionToSet);
    }
  }
  isAiBusy = false;
  vTaskDelay(pdMS_TO_TICKS(50)); // Nhường CPU cho IDLE0
}

// ============================================================================
// --- HỆ THỐNG XỬ LÝ AI BẤT ĐỒNG BỘ TRÊN CORE 0 (NON-BLOCKING WORKER TASK) ---
// ============================================================================
enum AiWorkType {
  AI_WORK_NONE = 0,
  AI_WORK_AUDIO,
  AI_WORK_TEXT,
  AI_WORK_MUSIC
};

struct AiWorkItem {
  AiWorkType type;
  uint8_t* audioData;
  size_t audioSize;
  char textQuery[128];
};

QueueHandle_t aiWorkQueue = NULL;
TaskHandle_t aiWorkerTaskHandle = NULL;

void aiWorkerTask(void *pvParameters) {
  AiWorkItem item;
  while (true) {
    if (xQueueReceive(aiWorkQueue, &item, portMAX_DELAY) == pdTRUE) {
      if (item.type == AI_WORK_AUDIO) {
        if (item.audioData != NULL && item.audioSize > 0) {
          processAudioAI(item.audioData, item.audioSize);
        }
      } else if (item.type == AI_WORK_TEXT) {
        sendToLLM(String(item.textQuery), false);
      } else if (item.type == AI_WORK_MUSIC) {
        String title = String(item.textQuery);
        String mUrl = searchMusicUrl(title);
        if (mUrl.length() > 0) {
          Serial.println("🎵 [Core 0 AI Worker] Tìm thấy stream nhạc: " + mUrl);
          extern String foundSongDisplay;
          extern String pendingSongUrl;
          extern String pendingSongTitle;
          pendingSongTitle = (foundSongDisplay.length() > 0 ? foundSongDisplay : title);
          pendingSongUrl = mUrl;
        } else {
          extern bool isMusicMode;
          isMusicMode = false;
          extern volatile bool hasPendingAudioStop;
          hasPendingAudioStop = true;
          setAIFaceState(AI_STATE_IDLE);
          setLedMode(0);
          playTTS("Em không tìm thấy bài hát này rồi, bạn thử lại bài khác nha!");
        }
      }
      isAiBusy = false; // Đảm bảo luôn giải phóng cờ bận sau mỗi chu kỳ tác vụ
      vTaskDelay(pdMS_TO_TICKS(20)); // Nhường CPU cho hệ thống
    }
  }
}

void setupAiTask() {
  if (aiWorkQueue == NULL) {
    aiWorkQueue = xQueueCreate(4, sizeof(AiWorkItem));
  }
  if (aiWorkerTaskHandle == NULL) {
    // Tạo Task chạy riêng biệt trên Core 0, Priority 1, Stack 12288 bytes trong SRAM
    BaseType_t res = xTaskCreatePinnedToCore(
      aiWorkerTask,
      "aiWorkerTask",
      12288,
      NULL,
      1,
      &aiWorkerTaskHandle,
      0
    );
    if (res != pdPASS) {
      Serial.printf("❌ [AI Task] Lỗi tạo aiWorkerTask (Mã lỗi: %d)!\n", res);
    } else {
      Serial.println("✅ [AI Task] Đã khởi tạo AI Worker Task chạy độc lập trên Core 0 thành công!");
    }
  }
}

void triggerAiAudioProcess(uint8_t* audioData, size_t size) {
  if (aiWorkQueue != NULL) {
    AiWorkItem item;
    item.type = AI_WORK_AUDIO;
    item.audioData = audioData;
    item.audioSize = size;
    item.textQuery[0] = '\0';
    if (xQueueSend(aiWorkQueue, &item, pdMS_TO_TICKS(100)) != pdTRUE) {
      Serial.println("⚠️ [AI Task] Hàng đợi bận, gọi trực tiếp processAudioAI...");
      processAudioAI(audioData, size);
    } else {
      Serial.println("📤 [AI Task] Đã chuyển âm thanh sang AI Worker xử lý.");
    }
  } else {
    Serial.println("⚠️ [AI Task] Hàng đợi chưa tạo, gọi trực tiếp processAudioAI...");
    processAudioAI(audioData, size);
  }
}

void triggerAiTextProcess(String text) {
  if (aiWorkQueue != NULL) {
    AiWorkItem item;
    item.type = AI_WORK_TEXT;
    item.audioData = NULL;
    item.audioSize = 0;
    memset(item.textQuery, 0, sizeof(item.textQuery));
    strncpy(item.textQuery, text.c_str(), sizeof(item.textQuery) - 1);
    xQueueSend(aiWorkQueue, &item, pdMS_TO_TICKS(50));
  }
}

void triggerMusicSearchTask(String songTitle) {
  if (aiWorkQueue != NULL) {
    AiWorkItem item;
    item.type = AI_WORK_MUSIC;
    item.audioData = NULL;
    item.audioSize = 0;
    memset(item.textQuery, 0, sizeof(item.textQuery));
    strncpy(item.textQuery, songTitle.c_str(), sizeof(item.textQuery) - 1);
    xQueueSend(aiWorkQueue, &item, pdMS_TO_TICKS(50));
  }
}
