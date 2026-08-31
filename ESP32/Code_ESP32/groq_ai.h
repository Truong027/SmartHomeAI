#ifndef GROQ_AI_H
#define GROQ_AI_H

#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

const char* GROQ_API_KEY = "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";
const char* GROQ_URL = "https://api.groq.com/openai/v1/chat/completions";

extern Preferences preferences;
String aiName = "Assistant";
String userName = "Bạn";
extern String aiAdvice; // Global from Code_ESP32.ino
extern volatile bool uiUpdatePending;
extern SemaphoreHandle_t stringMutex;

inline void setupGroq() {
  aiName = preferences.getString("aiName", "Assistant");
  userName = preferences.getString("userName", "Bạn");
  Serial0.printf("Groq AI Init - AI Name: %s, User Name: %s\n", aiName.c_str(), userName.c_str());
}

inline String askGroq(String question) {
  if (WiFi.status() != WL_CONNECTED) {
    return "Không có kết nối WiFi.";
  }

  Serial.println("Asking Groq: " + question);

  HTTPClient http;
  http.begin(GROQ_URL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + GROQ_API_KEY);

  extern float indoorTemp, indoorHum, indoorPres;
  extern float owmTemp, owmHum, owmWind;
  extern String owmDesc;
  extern const char* locationName;

  // Xây dựng System Prompt yêu cầu Groq luôn trả về JSON với dữ liệu cảm biến & thời tiết thực tế
  String systemPrompt = "Bạn là trợ lý AI thông minh NORI của ngôi nhà thông minh ESP32. "
                        "Tên hiện tại của bạn: " + aiName + ", Tên người dùng: " + userName + ". "
                        "DỮ LIỆU CẢM BIẾN & THỜI TIẾT THỰC TẾ: "
                        "- Cảm biến phòng: Nhiệt độ = " + String(indoorTemp, 1) + "°C, Độ ẩm = " + String(indoorHum, 1) + "%, Áp suất = " + String(indoorPres, 1) + " hPa. "
                        "- Thời tiết OpenWeatherMap tại " + String(locationName) + ": Nhiệt độ ngoài trời = " + String(owmTemp, 1) + "°C, Độ ẩm ngoài trời = " + String(owmHum, 1) + "%, Gió = " + String(owmWind, 1) + " m/s, Bầu trời = " + owmDesc + ". "
                        "Khi được hỏi về nhiệt độ, độ ẩm, thời tiết hôm nay hoặc ngoài trời, BẮT BUỘC dùng đúng các con số trên để trả lời chi tiết, chính xác, không trả lời chung chung. "
                        "LUÔN LUÔN trả về duy nhất một chuỗi JSON hợp lệ với định dạng chính xác sau: "
                        "{\"aiName\": \"tên_của_bạn\", \"userName\": \"tên_của_người_dùng\", \"reply\": \"câu_trả_lời_chi_tiết_của_bạn\"}. "
                        "Nếu người dùng nói 'đặt lại tên tôi là X' hoặc 'đặt lại tên bạn là Y', hãy cập nhật trường tương ứng trong JSON. "
                        "KHÔNG được trả về bất kỳ văn bản nào ngoài JSON.";

  StaticJsonDocument<2048> doc;
  doc["model"] = "llama-3.1-8b-instant";
  
  JsonArray messages = doc.createNestedArray("messages");
  
  JsonObject sysMsg = messages.createNestedObject();
  sysMsg["role"] = "system";
  sysMsg["content"] = systemPrompt;

  JsonObject usrMsg = messages.createNestedObject();
  usrMsg["role"] = "user";
  usrMsg["content"] = question;

  doc["temperature"] = 0.7;

  String requestBody;
  serializeJson(doc, requestBody);

  int httpResponseCode = http.POST(requestBody);
  String responseStr = "";

  if (httpResponseCode == 200) {
    String payload = http.getString();
    // Parse response
    DynamicJsonDocument respDoc(4096);
    DeserializationError error = deserializeJson(respDoc, payload);
    
    if (!error) {
      String aiResponseContent = respDoc["choices"][0]["message"]["content"].as<String>();
      
      // Parse nội dung trả về vì nó được yêu cầu là JSON
      DynamicJsonDocument contentDoc(2048);
      DeserializationError contentError = deserializeJson(contentDoc, aiResponseContent);
      
      if (!contentError) {
        String newAiName = contentDoc["aiName"] | aiName;
        String newUserName = contentDoc["userName"] | userName;
        String reply = contentDoc["reply"] | "Lỗi: Không tìm thấy nội dung reply.";

        // Kiểm tra và lưu nếu có đổi tên
        bool nameChanged = false;
        if (newAiName != aiName && newAiName != "") {
          aiName = newAiName;
          preferences.putString("aiName", aiName);
          nameChanged = true;
        }
        if (newUserName != userName && newUserName != "") {
          userName = newUserName;
          preferences.putString("userName", userName);
          nameChanged = true;
        }
        
        if (nameChanged) {
          Serial.printf("Names Updated - AI: %s, User: %s\n", aiName.c_str(), userName.c_str());
        }

        if (stringMutex) xSemaphoreTake(stringMutex, portMAX_DELAY);
        aiAdvice = reply;
        if (stringMutex) xSemaphoreGive(stringMutex);
        responseStr = reply;
      } else {
        Serial.println("Error parsing AI JSON content");
        Serial.println(aiResponseContent);
        // Fallback: nếu AI không trả về JSON chuẩn
        if (stringMutex) xSemaphoreTake(stringMutex, portMAX_DELAY);
        aiAdvice = aiResponseContent;
        if (stringMutex) xSemaphoreGive(stringMutex);
        responseStr = aiResponseContent;
      }
    } else {
      Serial.println("Error parsing Groq API response");
      responseStr = "Lỗi phản hồi API.";
    }
  } else {
    Serial.printf("Groq API Request failed, HTTP Code: %d\n", httpResponseCode);
    Serial.println(http.getString());
    responseStr = "Lỗi kết nối API: " + String(httpResponseCode);
  }

  http.end();
  
  uiUpdatePending = true; // Cập nhật màn hình sau khi có trả lời
  return responseStr;
}

#endif // GROQ_AI_H
