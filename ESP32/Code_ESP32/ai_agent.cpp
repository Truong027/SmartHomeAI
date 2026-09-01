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
volatile bool pendingReturnToMain = false;
volatile bool pendingReturnToRemote = false;

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

  // Giới hạn độ dài câu nói tối đa 220 ký tự để Google TTS đọc mượt mà, trôi chảy nhất
  String ttsText = text;
  if (ttsText.length() > 220) {
    int cutIdx = -1;
    for (int i = 220; i >= 120; i--) {
      char c = ttsText.charAt(i);
      if (c == '.' || c == '?' || c == '!' || c == ';') {
        cutIdx = i + 1;
        break;
      }
    }
    if (cutIdx == -1) {
      for (int i = 220; i >= 120; i--) {
        char c = ttsText.charAt(i);
        if (c == ',' || c == ':') {
          cutIdx = i;
          break;
        }
      }
    }
    if (cutIdx != -1) {
      ttsText = ttsText.substring(0, cutIdx);
    } else {
      ttsText = ttsText.substring(0, 220);
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
  } else {
    // Sử dụng HTTP thuần Port 80 (chống 100% lỗi BearSSL mConnectSSL / BR_SSL_SENDAPP, phát siêu tốc không tốn SSL RAM)
    String encodedText = urlEncode(ttsText);
    pendingTtsUrl = "http://translate.google.com/translate_tts?ie=UTF-8&client=tw-ob&tl=vi&q=" + encodedText;
    pendingTtsSpeech = "";
    Serial.println("🔊 [HTTP TTS Stream] Bàn giao URL cho Audio Engine: " + pendingTtsUrl);
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
    WiFi.setSleep(false);
    if (audio.isRunning()) {
      audio.stopSong();
    }
    vTaskDelay(pdMS_TO_TICKS(50));

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(12000);
    client.setHandshakeTimeout(6);

    // KÊNH 1: Gửi qua Vercel Serverless STT Proxy (Nhanh gấp 3 lần, truyền Stream trực tiếp)
    Serial.printf("🚀 Đang gửi %d bytes âm thanh lên Cloud STT Proxy (Free Heap: %u, Max Block: %u)...\n", 
                  size, ESP.getFreeHeap(), ESP.getMaxAllocHeap());

    bool connected = false;
    bool useVercel = true;

    if (client.connect("vercel-backend-woad-seven.vercel.app", 443)) {
      connected = true;
      useVercel = true;
    } else {
      char errBuf[100] = {0};
      client.lastError(errBuf, sizeof(errBuf));
      client.stop();
      Serial.printf("⚠️ [Vercel Proxy Connect Fail: %s] -> Chuyển sang kết nối Groq STT Direct...\n", errBuf);
      
      // KÊNH 2 (Fallback): Kết nối trực tiếp Groq
      if (client.connect("api.groq.com", 443)) {
        connected = true;
        useVercel = false;
      } else {
        client.lastError(errBuf, sizeof(errBuf));
        client.stop();
        Serial.printf("❌ [Groq Direct Fail: %s] Thử lại lần cuối...\n", errBuf);
        vTaskDelay(pdMS_TO_TICKS(500));
        if (client.connect("api.groq.com", 443)) {
          connected = true;
          useVercel = false;
        }
      }
    }
    
    if (!connected) {
      Serial.println("❌ Lỗi kết nối Cloud STT!");
      isAiBusy = false;
      if (isManualVoiceTrigger) {
        setAIFaceState(AI_STATE_IDLE);
        setLedMode(0);
      }
      return;
    }

    if (useVercel) {
      // Gửi Raw WAV Stream cực kỳ nhẹ nhàng lên Vercel STT Proxy
      client.println("POST /api/stt HTTP/1.1");
      client.println("Host: vercel-backend-woad-seven.vercel.app");
      client.println("Content-Type: audio/wav");
      client.print("Content-Length: ");
      client.println(size);
      client.println("Connection: close");
      client.println();

      size_t bytesSent = 0;
      while(bytesSent < size) {
        size_t chunk = min((size_t)1440, size - bytesSent);
        client.write(&audioData[bytesSent], chunk);
        bytesSent += chunk;
      }
      client.flush();
    } else {
      // Gửi Multipart Form-Data trực tiếp lên Groq
      String boundary = "----ESP32Boundary";
      String head = "--" + boundary + "\r\n"
                    "Content-Disposition: form-data; name=\"model\"\r\n\r\n"
                    "whisper-large-v3-turbo\r\n"
                    "--" + boundary + "\r\n"
                    "Content-Disposition: form-data; name=\"language\"\r\n\r\n"
                    "vi\r\n"
                    "--" + boundary + "\r\n"
                    "Content-Disposition: form-data; name=\"response_format\"\r\n\r\n"
                    "json\r\n"
                    "--" + boundary + "\r\n"
                    "Content-Disposition: form-data; name=\"temperature\"\r\n\r\n"
                    "0\r\n"
                    "--" + boundary + "\r\n"
                    "Content-Disposition: form-data; name=\"prompt\"\r\n\r\n"
                    "Hi Nori, Hey Nori, Nori oi, chao Nori\r\n"
                    "--" + boundary + "\r\n"
                    "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n"
                    "Content-Type: audio/wav\r\n\r\n";
      String tail = "\r\n--" + boundary + "--\r\n";
      uint32_t contentLen = head.length() + size + tail.length();
      
      client.println("POST /openai/v1/audio/transcriptions HTTP/1.1");
      client.println("Host: api.groq.com");
      client.println("Authorization: Bearer " + String(GROQ_API_KEY));
      client.println("Content-Type: multipart/form-data; boundary=" + boundary);
      client.print("Content-Length: ");
      client.println(contentLen);
      client.println("Connection: close");
      client.println();
      
      client.print(head);
      size_t bytesSent = 0;
      while(bytesSent < size) {
        size_t chunk = min((size_t)1440, size - bytesSent);
        client.write(&audioData[bytesSent], chunk);
        bytesSent += chunk;
      }
      client.print(tail);
      client.flush();
    }
    
    // Đọc phản hồi JSON từ Server (Timeout tối đa 6 giây)
    unsigned long startWait = millis();
    while (client.connected() && (millis() - startWait < 6000)) {
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
      vTaskDelay(pdMS_TO_TICKS(5));
    }
    
    client.stop();
  } // Giải phóng hoàn toàn mbedTLS SSL heap context trước khi gọi LLM
  
  vTaskDelay(pdMS_TO_TICKS(60));
  
  JsonDocument doc;
  deserializeJson(doc, response);
  const char* text = doc["text"];
  
  if (text && strlen(text) > 0) {
    String transcribedText = String(text);
    transcribedText.trim();
    Serial.printf("🎙️ [Groq Whisper STT] Nhận diện được: \"%s\"\n", transcribedText.c_str());
    
    // Nếu chuỗi rỗng hoặc chỉ có dấu câu lặt vặt
    if (transcribedText.length() == 0 || transcribedText == "." || transcribedText == "..." || 
        transcribedText == "?" || transcribedText == "!" || transcribedText == "-" || transcribedText == ",") {
      Serial.println("⚠️ [Whisper Filter] Chuỗi nhận diện rỗng/chỉ có dấu câu. Bỏ qua!");
      isAiBusy = false;
      return;
    }
    
    extern String removeVietnameseAccents(String text);
    String lowerT = removeVietnameseAccents(transcribedText);
    lowerT.toLowerCase();

    // 🛑 LỌC BỎ ẢO GIÁC YOUTUBE (Whisper hallucination khi nhận âm thanh im lặng/tiếng ồn nền/video)
    if (lowerT.indexOf("subscribe") != -1 || lowerT.indexOf("dang ky kenh") != -1 ||
        lowerT.indexOf("ghien mi go") != -1 || lowerT.indexOf("la la school") != -1 ||
        lowerT.indexOf("like va share") != -1 || lowerT.indexOf("cam on da xem") != -1 ||
        lowerT.indexOf("hen gap lai") != -1 || lowerT.indexOf("video tiep theo") != -1 ||
        lowerT.indexOf("theo doi kenh") != -1 || lowerT.indexOf("nho like") != -1 ||
        lowerT.indexOf("chia se video") != -1 || lowerT.indexOf("chuc cac ban") != -1 ||
        lowerT.indexOf("thank you") != -1 || lowerT.indexOf("watching") != -1 ||
        lowerT.indexOf("subtitles by") != -1 || lowerT.indexOf("amara.org") != -1 ||
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
    
    // Bảng các biến thể phát âm mà Whisper tiếng Việt hay nghe nhầm từ "Hi Nori" / "Hey Nori"
    const char* misheardWakePrefixes[] = {
      "thay nua roi", "thay no roi", "thay no di", "thay nori", "thay no ri",
      "hay nua roi", "say nua roi", "ai nua roi", "het nua roi",
      "cho anh hai nori", "cho em hai nori", "cho toi hai nori", "cho minh hai nori",
      "cho anh hi nori", "cho em hi nori", "cho anh nori", "cho em nori",
      "hai nori", "2 nori", "bay nori", "hay nori", "he nori", "ha nori",
      "day nori", "lay nori", "oi nori", "hai no ri", "hai nory", "hi nori",
      "bai nori", "bye nori", "bai no ri", "nori", "no ri", "nori oi", "no ri oi",
      "chao nori", "xin chao nori", "hello nori", "hey nori", "e nori", "alo nori",
      "hi no ri", "hi no di", "hen nori", "hen no ri", "hien nori", "hoi nori", "thoi nori"
    };

    for (const char* mis : misheardWakePrefixes) {
      if (cleanTrans == mis) {
        transcribedText = "Hi Nori";
        lowerT = "hi nori";
        break;
      } else if (cleanTrans.startsWith(String(mis) + " ")) {
        transcribedText = "Hi Nori, " + transcribedText.substring(strlen(mis));
        lowerT = "hi nori " + lowerT.substring(strlen(mis));
        break;
      }
    }

    // 🛑 QUY TẮC BẮT BUỘC WAKE-WORD: NẾU GỌI BẰNG GIỌNG NÓI (KHÔNG PHẢI BẤM NÚT THỦ CÔNG)
    // THÌ BẮT BUỘC PHẢI CHỨA TỪ KHÓA "HI NORI" HOẶC "NORI" CHÍNH XÁC 100%!
    extern bool isManualVoiceTrigger;
    if (!isManualVoiceTrigger) {
      bool hasWakeWord = (lowerT.indexOf("nori") != -1 || lowerT.indexOf("no ri") != -1 || 
                          lowerT.indexOf("nory") != -1 || lowerT.indexOf("lo ri") != -1 ||
                          lowerT.indexOf("noly") != -1 || lowerT.indexOf("nuri") != -1);
      if (!hasWakeWord) {
        Serial.printf("⚠️ [Wake-Word Filter] Âm thanh từ TV/YouTube/tiếng ồn không gọi 'Hi Nori' (Nhận diện: \"%s\"). Bỏ qua 100%%!\n", transcribedText.c_str());
        isAiBusy = false;
        extern unsigned long lastWakeWordCheckFail;
        lastWakeWordCheckFail = millis();
        setAIFaceState(AI_STATE_IDLE);
        setLedMode(0);
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

// ============================================================================
// --- GỬI PROMPT SANG GROQ LLM (LLAMA-3.3-70B-VERSATILE / COMPOUND-MINI) ---
// ============================================================================
void sendToLLM(String userText, bool isSilent) {
  isAiBusy = true;
  userText.trim();
  
  if (userText.length() == 0) {
    isAiBusy = false;
    return;
  }

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
      normalizedGreeting == "thay nua roi" || normalizedGreeting == "thay no roi" || normalizedGreeting == "thay nori" ||
      normalizedGreeting == "hay nua roi" || normalizedGreeting == "say nua roi" || normalizedGreeting == "ai nua roi" ||
      normalizedGreeting == "hey nori." || normalizedGreeting == "hi nori.") {
    normalizedGreeting = "hi nori";
  }

  // 🛑 KIỂM TRA Ý ĐỊNH ĐIỀU KHIỂN / CÂU HỎI KIẾN THỨC
  // Nếu câu nói có chứa bất kỳ từ khóa ra lệnh hoặc câu hỏi nào thì TUYỆT ĐỐI KHÔNG coi là chào hỏi đơn thuần!
  bool hasCommandIntent = (
    cleanText.indexOf("nhac") != -1 || cleanText.indexOf("bai hat") != -1 || cleanText.indexOf("ca khuc") != -1 || cleanText.indexOf("bai nhac") != -1 ||
    cleanText.indexOf("den") != -1 || cleanText.indexOf("relay") != -1 || cleanText.indexOf("khoa") != -1 || cleanText.indexOf("cua") != -1 ||
    cleanText.indexOf("quat") != -1 || cleanText.indexOf("dieu hoa") != -1 || cleanText.indexOf("may lanh") != -1 ||
    cleanText.indexOf("nhiet do") != -1 || cleanText.indexOf("do am") != -1 || cleanText.indexOf("ap suat") != -1 ||
    cleanText.indexOf("thoi tiet") != -1 || cleanText.indexOf("am lich") != -1 || cleanText.indexOf("may gio") != -1 ||
    cleanText.indexOf("ngay bao nhieu") != -1 || cleanText.indexOf("ngay may") != -1 || cleanText.indexOf("am luong") != -1 ||
    cleanText.indexOf("loa") != -1 || cleanText.indexOf("do sang") != -1 || cleanText.indexOf("sang") != -1 ||
    cleanText.indexOf("bat") != -1 || cleanText.indexOf("tat") != -1 || cleanText.indexOf("mo") != -1 || cleanText.indexOf("dong") != -1 ||
    cleanText.indexOf("tang") != -1 || cleanText.indexOf("giam") != -1 || cleanText.indexOf("dung") != -1 || cleanText.indexOf("ngung") != -1 ||
    cleanText.indexOf("man hinh") != -1 || cleanText.indexOf("tro ve") != -1 || cleanText.indexOf("quay ve") != -1 || cleanText.indexOf("quay lai") != -1 ||
    cleanText.indexOf("dashboard") != -1 || cleanText.indexOf("trang chu") != -1 || cleanText.indexOf("thoat") != -1 || cleanText.indexOf("remote") != -1 ||
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
    normalizedGreeting == "e nori" || normalizedGreeting == "bai nori" || normalizedGreeting == "bye nori" ||
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
    lowerText.indexOf("mo bai ") != -1 || lowerText.indexOf("bat bai ") != -1 || lowerText.indexOf("phat bai ") != -1 ||
    lowerText.indexOf("hat bai ") != -1 || lowerText.indexOf("nghe bai ") != -1 || lowerText.indexOf("tim bai ") != -1 ||
    lowerText.indexOf("mo nhac") != -1 || lowerText.indexOf("bat nhac") != -1 || lowerText.indexOf("phat nhac") != -1 ||
    lowerText.indexOf("nghe nhac") != -1 || lowerText.indexOf("hat nhac") != -1 || lowerText.indexOf("tim nhac") != -1 ||
    ((lowerText.indexOf("mo ") != -1 || lowerText.indexOf("bat ") != -1 || lowerText.indexOf("phat ") != -1) && (lowerText.indexOf(" bai ") != -1 || lowerText.indexOf(" nhac ") != -1))
  );

  if (isPlayMusic) {
    String songQuery = "";
    String s = lowerText;
    s.trim();

    // 1. Danh sách các cụm từ đệm dài đến ngắn
    const char* prefixes[] = {
      "hay mo giup toi bai hat", "hay mo giup toi bai nhac", "hay mo giup toi ca khuc", "hay mo giup toi bai", "hay mo giup toi",
      "mo giup toi bai hat", "mo giup toi bai nhac", "mo giup toi ca khuc", "mo giup toi bai", "mo giup toi",
      "bat giup toi bai hat", "bat giup toi bai nhac", "bat giup toi ca khuc", "bat giup toi bai", "bat giup toi",
      "phat giup toi bai hat", "phat giup toi bai nhac", "phat giup toi ca khuc", "phat giup toi bai", "phat giup toi",
      "mo cho toi nghe bai hat", "mo cho toi nghe bai nhac", "mo cho toi nghe ca khuc", "mo cho toi nghe bai", "mo cho toi nghe",
      "mo cho minh nghe bai hat", "mo cho minh nghe bai nhac", "mo cho minh nghe ca khuc", "mo cho minh nghe bai", "mo cho minh nghe",
      "mo cho toa bai hat", "mo cho toa bai nhac", "mo cho toa ca khuc", "mo cho toa bai", "mo cho toa",
      "mo cho to bai hat", "mo cho to bai nhac", "mo cho to ca khuc", "mo cho to bai", "mo cho to",
      "mo cho tao bai hat", "mo cho tao bai nhac", "mo cho tao ca khuc", "mo cho tao bai", "mo cho tao",
      "mo cho ta bai hat", "mo cho ta bai nhac", "mo cho ta ca khuc", "mo cho ta bai", "mo cho ta",
      "mo cho toi bai hat", "mo cho toi bai nhac", "mo cho toi ca khuc", "mo cho toi bai", "mo cho toi",
      "mo cho minh bai hat", "mo cho minh bai nhac", "mo cho minh ca khuc", "mo cho minh bai", "mo cho minh",
      "bat cho toa bai", "bat cho to bai", "bat cho toi bai", "bat cho minh bai", "bat cho tao bai",
      "phat cho toa bai", "phat cho to bai", "phat cho toi bai", "phat cho minh bai", "phat cho tao bai",
      "cho toa nghe bai", "cho to nghe bai", "cho toi nghe bai", "cho minh nghe bai", "cho tao nghe bai",
      "cho toa bai", "cho to bai", "cho toi bai", "cho minh bai", "cho tao bai",
      "cho toa", "cho to", "cho toi", "cho minh", "cho tao", "cho ta",
      "toi muon nghe bai hat", "toi muon nghe bai nhac", "toi muon nghe ca khuc", "toi muon nghe bai", "toi muon nghe",
      "minh muon nghe bai hat", "minh muon nghe bai nhac", "minh muon nghe ca khuc", "minh muon nghe bai", "minh muon nghe",
      "toi muon mo bai", "toi muon bat bai", "toi muon phat bai", "toi muon mo", "toi muon bat", "toi muon phat",
      "muon nghe bai hat", "muon nghe bai nhac", "muon nghe ca khuc", "muon nghe bai", "muon nghe",
      "muon mo bai", "muon bat bai", "muon phat bai", "muon mo", "muon bat", "muon phat",
      "phat bai nhac", "bat bai nhac", "mo bai nhac", "hat bai nhac", "nghe bai nhac",
      "phat bai hat", "bat bai hat", "mo bai hat", "hat bai hat", "nghe bai hat",
      "phat ca khuc", "bat ca khuc", "mo ca khuc", "hat ca khuc", "nghe ca khuc",
      "mot bai nhac", "mot bai hat", "mot ca khuc", "mot khuc nhac", "mot bai",
      "phat bai", "bat bai", "mo bai", "hat bai", "nghe bai", "tim bai",
      "phat nhac", "bat nhac", "mo nhac", "hat nhac", "nghe nhac", "tim nhac",
      "ca khuc", "bai hat", "bai nhac", "khuc nhac",
      "play music", "play song", "play "
    };

    for (const char* prefix : prefixes) {
      if (s.startsWith(prefix)) {
        s = s.substring(strlen(prefix));
        s.trim();
      }
    }

    // 2. Vòng lặp bóc tách từng từ đệm ở đầu chuỗi (xử lý triệt để "mo", "cho", "toa", "to", "bai", "mot"...)
    bool changed = true;
    while (changed) {
      changed = false;
      if (s.startsWith("hay ") || s.startsWith("mo ") || s.startsWith("bat ") || s.startsWith("phat ") || s.startsWith("nghe ") || s.startsWith("hat ") || s.startsWith("tim ")) {
        s = s.substring(s.indexOf(' ') + 1);
        s.trim();
        changed = true;
      }
      if (s.startsWith("cho ") || s.startsWith("giup ") || s.startsWith("ho ") || s.startsWith("voi ")) {
        s = s.substring(s.indexOf(' ') + 1);
        s.trim();
        changed = true;
      }
      if (s.startsWith("toi ") || s.startsWith("to ") || s.startsWith("toa ") || s.startsWith("tao ") || s.startsWith("minh ") || s.startsWith("em ") || s.startsWith("anh ") || s.startsWith("ta ") || s.startsWith("ban ")) {
        s = s.substring(s.indexOf(' ') + 1);
        s.trim();
        changed = true;
      }
      if (s.startsWith("nghe ") || s.startsWith("xem ")) {
        s = s.substring(s.indexOf(' ') + 1);
        s.trim();
        changed = true;
      }
      if (s.startsWith("mot ") || s.startsWith("vai ") || s.startsWith("bai ") || s.startsWith("ca khuc ") || s.startsWith("nhac ") || s.startsWith("khuc ")) {
        s = s.substring(s.indexOf(' ') + 1);
        s.trim();
        changed = true;
      }
    }

    // 3. Lọc từ đuôi câu thoại
    if (s.endsWith(" nhe")) s = s.substring(0, s.length() - 4);
    else if (s.endsWith(" nha")) s = s.substring(0, s.length() - 4);
    else if (s.endsWith(" di")) s = s.substring(0, s.length() - 3);
    else if (s.endsWith(" voi")) s = s.substring(0, s.length() - 4);
    else if (s.endsWith(" a")) s = s.substring(0, s.length() - 2);
    s.trim();

    String cleanedSong = s;
    cleanedSong.replace(" cua ", " ");
    cleanedSong.replace(" do ", " ");
    cleanedSong.replace(" boi ", " ");
    
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

    bool isRandomMusic = false;
    if (cleanedSong.length() == 0 || 
        cleanedSong == "nhac" || cleanedSong == "bai" || cleanedSong == "hat" || cleanedSong == "nghe" || 
        cleanedSong == "mot" || cleanedSong == "mot bai" || cleanedSong == "random" || cleanedSong == "ngau nhien" ||
        cleanedSong == "gi do" || cleanedSong == "nao do" || cleanedSong == "hay" || cleanedSong == "lofi" ||
        lowerText.indexOf("mot bai nhac") != -1 || lowerText.indexOf("mot bai hat") != -1 ||
        lowerText.indexOf("bai gi do") != -1 || lowerText.indexOf("ngau nhien") != -1 || lowerText.indexOf("random") != -1) {
      isRandomMusic = true;
    }

    // 4. Trích xuất tên bài hát giữ nguyên 100% TIẾNG VIỆT CÓ DẤU từ userText gốc
    String rawSongQuery = userText;
    rawSongQuery.trim();

    const char* rawPrefixes[] = {
      "Hãy mở giúp tôi bài hát", "Hãy mở giúp tôi bài nhạc", "Hãy mở giúp tôi ca khúc", "Hãy mở giúp tôi bài", "Hãy mở giúp tôi",
      "hãy mở giúp tôi bài hát", "hãy mở giúp tôi bài nhạc", "hãy mở giúp tôi ca khúc", "hãy mở giúp tôi bài", "hãy mở giúp tôi",
      "Mở giúp tôi bài hát", "Mở giúp tôi bài nhạc", "Mở giúp tôi ca khúc", "Mở giúp tôi bài", "Mở giúp tôi",
      "mở giúp tôi bài hát", "mở giúp tôi bài nhạc", "mở giúp tôi ca khúc", "mở giúp tôi bài", "mở giúp tôi",
      "Bật giúp tôi bài hát", "Bật giúp tôi bài nhạc", "Bật giúp tôi ca khúc", "Bật giúp tôi bài", "Bật giúp tôi",
      "bật giúp tôi bài hát", "bật giúp tôi bài nhạc", "bật giúp tôi ca khúc", "bật giúp tôi bài", "bật giúp tôi",
      "Phát giúp tôi bài hát", "Phát giúp tôi bài nhạc", "Phát giúp tôi ca khúc", "Phát giúp tôi bài", "Phát giúp tôi",
      "phát giúp tôi bài hát", "phát giúp tôi bài nhạc", "phát giúp tôi ca khúc", "phát giúp tôi bài", "phát giúp tôi",
      "Mở cho tôi nghe bài hát", "Mở cho tôi nghe bài nhạc", "Mở cho tôi nghe ca khúc", "Mở cho tôi nghe bài", "Mở cho tôi nghe",
      "mở cho tôi nghe bài hát", "mở cho tôi nghe bài nhạc", "mở cho tôi nghe ca khúc", "mở cho tôi nghe bài", "mở cho tôi nghe",
      "Mở cho mình nghe bài hát", "Mở cho mình nghe bài nhạc", "Mở cho mình nghe ca khúc", "Mở cho mình nghe bài", "Mở cho mình nghe",
      "mở cho mình nghe bài hát", "mở cho mình nghe bài nhạc", "mở cho mình nghe ca khúc", "mở cho mình nghe bài", "mở cho mình nghe",
      "Mở cho toa bài hát", "Mở cho toa bài nhạc", "Mở cho toa ca khúc", "Mở cho toa bài", "Mở cho toa",
      "mở cho toa bài hát", "mở cho toa bài nhạc", "mở cho toa ca khúc", "mở cho toa bài", "mở cho toa",
      "Mở cho to bài hát", "Mở cho to bài nhạc", "Mở cho to ca khúc", "Mở cho to bài", "Mở cho to",
      "mở cho to bài hát", "mở cho to bài nhạc", "mở cho to ca khúc", "mở cho to bài", "mở cho to",
      "Mở cho tôi bài hát", "Mở cho tôi bài nhạc", "Mở cho tôi ca khúc", "Mở cho tôi bài", "Mở cho tôi",
      "mở cho tôi bài hát", "mở cho tôi bài nhạc", "mở cho tôi ca khúc", "mở cho tôi bài", "mở cho tôi",
      "Mở cho mình bài hát", "Mở cho mình bài nhạc", "Mở cho mình ca khúc", "Mở cho mình bài", "Mở cho mình",
      "mở cho mình bài hát", "mở cho mình bài nhạc", "mở cho mình ca khúc", "mở cho mình bài", "mở cho mình",
      "Bật cho tôi bài", "bật cho tôi bài", "Bật cho mình bài", "bật cho mình bài",
      "Phát cho tôi bài", "phát cho tôi bài", "Phát cho mình bài", "phát cho mình bài",
      "Cho tôi nghe bài", "cho tôi nghe bài", "Cho mình nghe bài", "cho mình nghe bài",
      "Cho tôi bài", "cho tôi bài", "Cho mình bài", "cho mình bài",
      "Cho tôi", "cho tôi", "Cho mình", "cho mình",
      "Tôi muốn nghe bài hát", "tôi muốn nghe bài hát", "Tôi muốn nghe bài", "tôi muốn nghe bài", "Tôi muốn nghe", "tôi muốn nghe",
      "Mình muốn nghe bài hát", "mình muốn nghe bài hát", "Mình muốn nghe bài", "mình muốn nghe bài", "Mình muốn nghe", "mình muốn nghe",
      "Tôi muốn mở bài", "tôi muốn mở bài", "Tôi muốn bật bài", "tôi muốn bật bài", "Tôi muốn phát bài", "tôi muốn phát bài",
      "Tôi muốn mở", "tôi muốn mở", "Tôi muốn bật", "tôi muốn bật", "Tôi muốn phát", "tôi muốn phát",
      "Muốn nghe bài hát", "muốn nghe bài hát", "Muốn nghe bài", "muốn nghe bài", "Muốn nghe", "muốn nghe",
      "Muốn mở bài", "muốn mở bài", "Muốn bật bài", "muốn bật bài", "Muốn phát bài", "muốn phát bài",
      "Phát bài nhạc", "phát bài nhạc", "Bật bài nhạc", "bật bài nhạc", "Mở bài nhạc", "mở bài nhạc",
      "Phát bài hát", "phát bài hát", "Bật bài hát", "bật bài hát", "Mở bài hát", "mở bài hát",
      "Phát ca khúc", "phát ca khúc", "Bật ca khúc", "bật ca khúc", "Mở ca khúc", "mở ca khúc",
      "Một bài nhạc", "một bài nhạc", "Một bài hát", "một bài hát", "Một ca khúc", "một ca khúc", "Một bài", "một bài",
      "Phát bài", "phát bài", "Bật bài", "bật bài", "Mở bài", "mở bài", "Hát bài", "hát bài", "Nghe bài", "nghe bài", "Tìm bài", "tìm bài",
      "Phát nhạc", "phát nhạc", "Bật nhạc", "bật nhạc", "Mở nhạc", "mở nhạc", "Hát nhạc", "hát nhạc", "Nghe nhạc", "nghe nhạc", "Tìm nhạc", "tìm nhạc",
      "Ca khúc", "ca khúc", "Bài hát", "bài hát", "Bài nhạc", "bài nhạc", "Khúc nhạc", "khúc nhạc",
      "Play music", "play music", "Play song", "play song", "Play ", "play "
    };

    for (const char* prefix : rawPrefixes) {
      if (rawSongQuery.startsWith(prefix)) {
        rawSongQuery = rawSongQuery.substring(strlen(prefix));
        rawSongQuery.trim();
      }
    }

    // Bóc tách các từ đệm có dấu còn sót lại ở đầu chuỗi
    bool rawChanged = true;
    while (rawChanged) {
      rawChanged = false;
      String lowerRaw = removeVietnameseAccents(rawSongQuery);
      lowerRaw.toLowerCase();
      if (lowerRaw.startsWith("hay ") || lowerRaw.startsWith("mo ") || lowerRaw.startsWith("bat ") || 
          lowerRaw.startsWith("phat ") || lowerRaw.startsWith("nghe ") || lowerRaw.startsWith("hat ") || lowerRaw.startsWith("tim ")) {
        int sp = rawSongQuery.indexOf(' ');
        if (sp != -1) {
          rawSongQuery = rawSongQuery.substring(sp + 1);
          rawSongQuery.trim();
          rawChanged = true;
        }
      }
      else if (lowerRaw.startsWith("cho ") || lowerRaw.startsWith("giup ") || lowerRaw.startsWith("ho ") || lowerRaw.startsWith("voi ")) {
        int sp = rawSongQuery.indexOf(' ');
        if (sp != -1) {
          rawSongQuery = rawSongQuery.substring(sp + 1);
          rawSongQuery.trim();
          rawChanged = true;
        }
      }
      else if (lowerRaw.startsWith("toi ") || lowerRaw.startsWith("to ") || lowerRaw.startsWith("toa ") || lowerRaw.startsWith("tao ") || 
               lowerRaw.startsWith("minh ") || lowerRaw.startsWith("em ") || lowerRaw.startsWith("anh ") || lowerRaw.startsWith("ta ") || lowerRaw.startsWith("ban ")) {
        int sp = rawSongQuery.indexOf(' ');
        if (sp != -1) {
          rawSongQuery = rawSongQuery.substring(sp + 1);
          rawSongQuery.trim();
          rawChanged = true;
        }
      }
      else if (lowerRaw.startsWith("nghe ") || lowerRaw.startsWith("xem ")) {
        int sp = rawSongQuery.indexOf(' ');
        if (sp != -1) {
          rawSongQuery = rawSongQuery.substring(sp + 1);
          rawSongQuery.trim();
          rawChanged = true;
        }
      }
      else if (lowerRaw.startsWith("mot ") || lowerRaw.startsWith("vai ") || lowerRaw.startsWith("bai ") || 
               lowerRaw.startsWith("ca khuc ") || lowerRaw.startsWith("nhac ") || lowerRaw.startsWith("khuc ")) {
        int sp = rawSongQuery.indexOf(' ');
        if (sp != -1) {
          rawSongQuery = rawSongQuery.substring(sp + 1);
          rawSongQuery.trim();
          rawChanged = true;
        }
      }
    }

    // Lọc đuôi câu thoại có dấu
    String lowerCheck = removeVietnameseAccents(rawSongQuery);
    lowerCheck.toLowerCase();
    if (lowerCheck.endsWith(" nhe") || lowerCheck.endsWith(" nha") || lowerCheck.endsWith(" voi")) {
      int sp = rawSongQuery.lastIndexOf(' ');
      if (sp != -1) rawSongQuery = rawSongQuery.substring(0, sp);
    } else if (lowerCheck.endsWith(" di") || lowerCheck.endsWith(" a")) {
      int sp = rawSongQuery.lastIndexOf(' ');
      if (sp != -1) rawSongQuery = rawSongQuery.substring(0, sp);
    }
    rawSongQuery.trim();

    if (!isRandomMusic && rawSongQuery.length() > 1) {
      songQuery = rawSongQuery;
    } else {
      // Khi người dùng chỉ nói chung chung "mở giúp tôi một bài nhạc", "mở nhạc", "tôi nghe nhạc", "phát nhạc ngẫu nhiên"
      isRandomMusic = true;
      const char* hotHits[] = {
        "Lạc Trôi",
        "Âm Thầm Bên Em",
        "Đi Về Nhà",
        "Cắt Đôi Nỗi Sầu",
        "Nơi Này Có Anh",
        "Shape of You",
        "See You Again",
        "Tết Ơi Tết À",
        "Nhạc Lofi chill",
        "Ngày Đầu Tiên"
      };
      songQuery = hotHits[random(0, 10)];
    }

    Serial.println("🎵 Nhận diện lệnh phát nhạc: " + songQuery + (isRandomMusic ? " (Random Mode)" : " (Specific Song)"));
    extern String pendingSongTitle;
    extern bool isMusicMode;
    pendingSongTitle = songQuery;
    isMusicMode = true;
    if (!isSilent) {
      if (isRandomMusic) {
        playTTS("Dạ để em tìm một bài nhạc random thật hay cho bạn thưởng thức nhé!");
      } else {
        playTTS("Dạ em đang tìm và phát bài " + songQuery + " cho bạn thưởng thức đây nè!");
      }
    }
    return;
  }

  // --- FAST-PATH KẾT THÚC HỘI THOẠI LỊCH SỰ (CẢM ƠN, TẠM BIỆT, THÔI...) ---
  bool isGoodbye = (
    lowerText == "cam on" || lowerText == "cam on em" || lowerText == "cam on ban" || lowerText == "cam on nha" || 
    lowerText == "thank you" || lowerText == "thanks" || lowerText == "tam biet" || lowerText == "bye" || lowerText == "bye bye" || 
    lowerText == "chao nhe" || lowerText == "thoi" || lowerText == "thoi duoc roi" || lowerText == "duoc roi" || 
    lowerText == "xong roi" || lowerText == "khong can nua" || lowerText == "khong co gi"
  );
  if (isGoodbye) {
    pendingReturnToMain = true; // Kết thúc và chuyển về màn hình chính sau khi chào
    if (lowerText.indexOf("cam on") != -1 || lowerText.indexOf("thank") != -1) {
      if (!isSilent) playTTS("Dạ không có gì ạ! Cần gì bạn cứ gọi em nhé!");
    } else {
      if (!isSilent) playTTS("Dạ tạm biệt bạn nha! Chúc bạn một ngày vui vẻ!");
    }
    return;
  }
  
  // --- FAST-PATH ĐIỀU HƯỚNG MÀN HÌNH (TRỞ VỀ MÀN HÌNH CHÍNH / MỞ REMOTE) ---
  bool isGotoMainScreen = (
    lowerText == "tro ve" || lowerText == "quay ve" || lowerText == "ve man hinh chinh" || 
    lowerText == "tro ve man hinh chinh" || lowerText == "quay ve man hinh chinh" || lowerText == "chuyen ve man hinh chinh" ||
    lowerText == "ve man hinh" || lowerText == "tro ve man hinh" || lowerText == "quay ve man hinh" ||
    lowerText == "man hinh chinh" || lowerText == "ve dashboard" || lowerText == "dashboard" ||
    lowerText == "ve trang chu" || lowerText == "trang chu" || lowerText == "quay lai" ||
    lowerText == "dong ai" || lowerText == "tat ai" || lowerText == "thoat ai" || lowerText == "thoat" ||
    lowerText.indexOf("ve man hinh chinh") != -1 || lowerText.indexOf("tro ve man hinh") != -1 ||
    lowerText.indexOf("quay ve man hinh") != -1 || lowerText.indexOf("chuyen ve man hinh") != -1 ||
    lowerText.indexOf("man hinh chinh") != -1 || lowerText.indexOf("ve man hinh") != -1 ||
    lowerText.indexOf("dong ai") != -1 || lowerText.indexOf("thoat ai") != -1 ||
    lowerText.indexOf("ve dashboard") != -1 || lowerText.indexOf("ve trang chu") != -1 ||
    lowerText.indexOf("quay ve") != -1 || lowerText.indexOf("tro ve") != -1 ||
    lowerText.indexOf("cho ve") != -1 || lowerText.indexOf("hanh tranh") != -1
  );

  if (isGotoMainScreen) {
    if (audio.isRunning()) audio.stopSong();
    isMusicMode = false;
    if (is_ir_learning_mode) {
      is_ir_learning_mode = false;
      irrecv.disableIRIn();
    }
    stopMusicScreen();
    pendingReturnToMain = true; // 👉 ĐẶT CỜ TỰ ĐỘNG CHUYỂN VỀ MÀN HÌNH CHÍNH SAU KHI NÓI XONG
    pendingReturnToRemote = false;
    if (!isSilent) {
      playTTS("Đã trở về màn hình chính cho bạn rồi nhé!");
    } else {
      pendingReturnToMain = false;
      showMainScreen();
      setAIFaceState(AI_STATE_IDLE);
      setLedMode(0);
      uiUpdatePending = true;
    }
    return;
  } else if (lowerText == "mo remote" || lowerText == "bat remote" || lowerText == "mo dieu khien" || 
             lowerText == "hoc lenh" || lowerText == "che do hoc lenh" || lowerText == "man hinh remote" ||
             lowerText.indexOf("mo remote") != -1 || lowerText.indexOf("man hinh remote") != -1 || lowerText.indexOf("che do hoc lenh") != -1 ||
             lowerText.indexOf("remote") != -1 || lowerText.indexOf("dieu khien") != -1 || lowerText.indexOf("hoc lenh") != -1) {
    if (audio.isRunning()) audio.stopSong();
    is_ir_learning_mode = true;
    irrecv.enableIRIn();
    pendingReturnToRemote = true; // 👉 ĐẶT CỜ TỰ ĐỘNG CHUYỂN VỀ REMOTE SAU KHI NÓI XONG
    pendingReturnToMain = false;
    if (!isSilent) {
      playTTS("Đã mở màn hình điều khiển và chế độ học lệnh hồng ngoại rồi nè!");
    } else {
      pendingReturnToRemote = false;
      showIrScreen();
      updateIrScreen(selected_ir_idx, typeToString(learned_ir[selected_ir_idx].type), String((uint32_t)(learned_ir[selected_ir_idx].value & 0xFFFFFFFF), HEX));
      uiUpdatePending = true;
    }
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
    doc["model"] = "openai/gpt-oss-20b"; // Model OpenAI chính thức trên Groq, siêu tốc 1000 token/s, không bị lỗi 429
    doc["max_tokens"] = 500;
    doc["response_format"]["type"] = "json_object";
    
    JsonArray messages = doc["messages"].to<JsonArray>();
    
    JsonObject sysMsg = messages.add<JsonObject>();
    sysMsg["role"] = "system";
    sysMsg["content"] = systemPrompt;
    
    // Thêm câu hỏi hiện tại vào lịch sử
    if (!isSilent) addChatHistory("user", userText);
    
    // Đẩy lịch sử gần nhất vào mảng messages (tối đa 4 lượt để không tràn TPM)
    int startHist = max(0, historyCount - 4);
    for (int i = startHist; i < historyCount; i++) {
      JsonObject msg = messages.add<JsonObject>();
      msg["role"] = chatHistory[i].role;
      msg["content"] = chatHistory[i].content;
    }
    
    String requestBody;
    serializeJson(doc, requestBody);

    // ─── HỆ THỐNG MULTI-KEY ROTATION & AUTO-FAILOVER (3 GROQ API KEYS) ───
    static const char* GROQ_API_KEYS[] = {
      GROQ_KEY_1,
      GROQ_KEY_2,
      GROQ_KEY_3
    };
    static const int TOTAL_GROQ_KEYS = sizeof(GROQ_API_KEYS) / sizeof(GROQ_API_KEYS[0]);
    static int current_groq_key_idx = 0;

    String payload = "";
    bool requestSuccess = false;

    for (int attempt = 0; attempt < TOTAL_GROQ_KEYS; attempt++) {
      const char* activeApiKey = GROQ_API_KEYS[current_groq_key_idx];
      Serial.printf("🧠 [Groq LLM Engine] Đang kết nối Key #%d (%s)... (Model: openai/gpt-oss-20b)\n", 
                    current_groq_key_idx + 1, String(activeApiKey).substring(0, 10).c_str());

      WiFiClientSecure client;
      client.setInsecure();
      client.setTimeout(12000);
      client.setHandshakeTimeout(6);

      if (!client.connect("api.groq.com", 443)) {
        Serial.printf("⚠️ [Groq Connect Fail] Không kết nối được Key #%d (Free Heap: %u) -> Tự động chuyển Key tiếp theo...\n", 
                      current_groq_key_idx + 1, ESP.getFreeHeap());
        current_groq_key_idx = (current_groq_key_idx + 1) % TOTAL_GROQ_KEYS;
        vTaskDelay(pdMS_TO_TICKS(100));
        continue;
      }

      client.println("POST /openai/v1/chat/completions HTTP/1.1");
      client.println("Host: api.groq.com");
      client.println("User-Agent: ESP32_SmartHome_AI/1.0");
      client.println("Authorization: Bearer " + String(activeApiKey));
      client.println("Content-Type: application/json");
      client.println("Connection: close");
      client.print("Content-Length: ");
      client.println(requestBody.length());
      client.println();
      client.print(requestBody);
      client.flush();

      payload = "";
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

      // Kiểm tra nếu Key hiện tại bị dính lỗi 429 Too Many Requests -> Chuyển ngay Key khác
      if (payload.indexOf("429 Too Many Requests") != -1 || (payload.indexOf("\"error\"") != -1 && payload.indexOf("rate_limit") != -1)) {
        Serial.printf("⚠️ [Groq Rate Limit 429] Key #%d bị quá tải -> Tự động xoay sang Key #%d ngay lập tức!\n", 
                      current_groq_key_idx + 1, ((current_groq_key_idx + 1) % TOTAL_GROQ_KEYS) + 1);
        current_groq_key_idx = (current_groq_key_idx + 1) % TOTAL_GROQ_KEYS;
        vTaskDelay(pdMS_TO_TICKS(50));
        continue;
      }

      if (payload.length() > 0 && payload.indexOf("\"choices\"") != -1) {
        requestSuccess = true;
        // Luân chuyển đều tải cho câu thoại tiếp theo
        current_groq_key_idx = (current_groq_key_idx + 1) % TOTAL_GROQ_KEYS;
        break;
      }
    }

    if (!requestSuccess) {
      Serial.println("❌ [Groq LLM Fail] Cả 3 Key đều bận. Phản hồi câu trả lời thông minh...");
      if (!isSilent) {
        playTTS("Dạ bạn nói nhanh quá em chưa kịp nghĩ, bạn hỏi lại em một lần nữa nhé!");
      }
      return;
    }

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
          pendingReturnToMain = true; // 👉 ĐẶT CỜ CHUYỂN VỀ MÀN HÌNH CHÍNH SAU KHI NÓI XONG
          pendingReturnToRemote = false;
          Serial.println("📱 AI đã đặt cờ chuyển về màn hình chính Dashboard sau khi nói xong!");
        }
        else if (actStr == "goto_remote_screen") {
          if (audio.isRunning()) audio.stopSong();
          is_ir_learning_mode = true;
          irrecv.enableIRIn();
          pendingReturnToRemote = true; // 👉 ĐẶT CỜ CHUYỂN VỀ MÀN HÌNH REMOTE SAU KHI NÓI XONG
          pendingReturnToMain = false;
          Serial.println("📱 AI đã đặt cờ chuyển sang màn hình Remote Học Lệnh sau khi nói xong!");
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
          Serial.printf("📥 [AI Worker] Bắt đầu xử lý %d bytes âm thanh...\n", item.audioSize);
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
    // Cấp phát Stack 10240 bytes (10KB) trong Internal SRAM để an toàn tuyệt đối khi ghi Flash NVS / Preferences (không bao giờ crash Cache Disabled)
    BaseType_t res = xTaskCreatePinnedToCore(
      aiWorkerTask,
      "aiWorkerTask",
      10240,
      NULL,
      3,
      &aiWorkerTaskHandle,
      0
    );
    if (res != pdPASS) {
      Serial.printf("❌ [AI Task] Lỗi tạo aiWorkerTask (Mã lỗi: %d)!\n", res);
    } else {
      Serial.println("✅ [AI Task] Đã khởi tạo AI Worker Task (Internal SRAM 10KB, Flash Safe) thành công!");
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
