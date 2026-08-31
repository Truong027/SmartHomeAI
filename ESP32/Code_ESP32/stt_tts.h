#ifndef STT_TTS_H
#define STT_TTS_H

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "hw_audio.h"
#include "groq_ai.h"
#include "hw_led.h"
#include "ui_lvgl.h"

// ─── Recording Config ─────────────────────────
#define MAX_REC_SEC 10
#define REC_SAMPLE_RATE 16000
#define REC_BYTES_PER_SEC (REC_SAMPLE_RATE * 2) // 16-bit mono
#define MAX_REC_BYTES (MAX_REC_SEC * REC_BYTES_PER_SEC + 44)

uint8_t *rec_buffer = nullptr;
size_t rec_size = 0;
bool is_recording = false;
bool play_tts = false;
String tts_url = "";

// Generate WAV Header
void createWavHeader(uint8_t* header, int waveDataSize) {
  header[0] = 'R'; header[1] = 'I'; header[2] = 'F'; header[3] = 'F';
  uint32_t fileSize = waveDataSize + 36;
  header[4] = (uint8_t)(fileSize & 0xFF);
  header[5] = (uint8_t)((fileSize >> 8) & 0xFF);
  header[6] = (uint8_t)((fileSize >> 16) & 0xFF);
  header[7] = (uint8_t)((fileSize >> 24) & 0xFF);
  header[8] = 'W'; header[9] = 'A'; header[10] = 'V'; header[11] = 'E';
  header[12] = 'f'; header[13] = 'm'; header[14] = 't'; header[15] = ' ';
  header[16] = 16; header[17] = 0; header[18] = 0; header[19] = 0;
  header[20] = 1; header[21] = 0; // PCM
  header[22] = 1; header[23] = 0; // 1 channel
  header[24] = (uint8_t)(REC_SAMPLE_RATE & 0xFF);
  header[25] = (uint8_t)((REC_SAMPLE_RATE >> 8) & 0xFF);
  header[26] = (uint8_t)((REC_SAMPLE_RATE >> 16) & 0xFF);
  header[27] = (uint8_t)((REC_SAMPLE_RATE >> 24) & 0xFF);
  uint32_t byteRate = REC_SAMPLE_RATE * 2;
  header[28] = (uint8_t)(byteRate & 0xFF);
  header[29] = (uint8_t)((byteRate >> 8) & 0xFF);
  header[30] = (uint8_t)((byteRate >> 16) & 0xFF);
  header[31] = (uint8_t)((byteRate >> 24) & 0xFF);
  header[32] = 2; header[33] = 0; // block align
  header[34] = 16; header[35] = 0; // 16 bits
  header[36] = 'd'; header[37] = 'a'; header[38] = 't'; header[39] = 'a';
  header[40] = (uint8_t)(waveDataSize & 0xFF);
  header[41] = (uint8_t)((waveDataSize >> 8) & 0xFF);
  header[42] = (uint8_t)((waveDataSize >> 16) & 0xFF);
  header[43] = (uint8_t)((waveDataSize >> 24) & 0xFF);
}

void startRecording() {
  if (!rec_buffer) {
    rec_buffer = (uint8_t *)heap_caps_malloc(MAX_REC_BYTES, MALLOC_CAP_SPIRAM);
    if (!rec_buffer) {
      Serial.println("PSRAM alloc failed!");
      return;
    }
  }
  rec_size = 44; 
  is_recording = true;
  // Clear buffer effectively by reading some dummy samples
  int32_t dummy[64];
  for(int i=0; i<4; i++) i2s_mic.readBytes((char*)dummy, sizeof(dummy));
  
  Serial.println("Recording...");
  playBeep(1500, 50);
  setLedMode(3); // Mode 3: Cyan breathing
  aiAdvice = "Dang nghe...";
  updateLVGL_UI();
}

void stopRecordingAndProcess() {
  if (!is_recording) return;
  is_recording = false;
  Serial.println("Stop Recording. Processing...");
  playBeep(2000, 100);
  
  if (rec_size <= 44 + 8000) { // < 0.5s
    Serial.println("Audio too short");
    setLedMode(0);
    aiAdvice = "Chua nghe ro.";
    updateLVGL_UI();
    return;
  }
  
  createWavHeader(rec_buffer, rec_size - 44);
  setLedMode(1); // Mode 1: Thinking
  aiAdvice = "Dang dich giong noi...";
  updateLVGL_UI();
  
  HTTPClient http;
  http.begin("https://api.groq.com/openai/v1/audio/transcriptions");
  http.addHeader("Authorization", String("Bearer ") + GROQ_API_KEY);
  
  String boundary = "----ESP32Boundary" + String(millis());
  http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
  
  String head = "--" + boundary + "\r\n";
  head += "Content-Disposition: form-data; name=\"model\"\r\n\r\nwhisper-large-v3-turbo\r\n";
  head += "--" + boundary + "\r\n";
  head += "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n";
  head += "Content-Type: audio/wav\r\n\r\n";
  
  String tail = "\r\n--" + boundary + "--\r\n";
  
  uint32_t totalLen = head.length() + rec_size + tail.length();
  uint8_t *payload = (uint8_t *)heap_caps_malloc(totalLen, MALLOC_CAP_SPIRAM);
  
  if (!payload) {
    Serial.println("Payload PSRAM alloc failed!");
    setLedMode(2); // Error
    return;
  }
  
  memcpy(payload, head.c_str(), head.length());
  memcpy(payload + head.length(), rec_buffer, rec_size);
  memcpy(payload + head.length() + rec_size, tail.c_str(), tail.length());
  
  int code = http.POST(payload, totalLen);
  heap_caps_free(payload);
  
  if (code == 200) {
    String resp = http.getString();
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, resp);
    String text = doc["text"] | "";
    text.trim();
    if (text.length() > 0) {
      Serial.println("Whisper: " + text);
      aiAdvice = "Ban: " + text;
      updateLVGL_UI();
      // Ask Groq
      String reply = askGroq(text);
      
      // Prepare TTS URL (Google Translate TTS)
      // Limit to 200 chars for Google TTS
      String tts_text = reply;
      if (tts_text.length() > 150) tts_text = tts_text.substring(0, 150);
      tts_text.replace(" ", "%20");
      tts_url = "https://translate.google.com/translate_tts?ie=UTF-8&tl=vi&client=tw-ob&q=" + tts_text;
      play_tts = true; 
      
      setLedMode(4); // Mode 4: Flashing Green (Success)
    } else {
      aiAdvice = "Khong nghe thay gi.";
      setLedMode(2);
    }
  } else {
    Serial.printf("Groq Whisper error: %d\n", code);
    Serial.println(http.getString());
    aiAdvice = "Loi nhan dien giong noi.";
    setLedMode(2); // Error
  }
  http.end();
  updateLVGL_UI();
}

void processRecordingTask() {
  if (is_recording && rec_size < MAX_REC_BYTES) {
    size_t bytes_read = 0;
    int32_t sample32[64];
    bytes_read = i2s_mic.readBytes((char*)sample32, sizeof(sample32));
    
    int num_samples = bytes_read / 4;
    int16_t sample16[64];
    for(int i=0; i<num_samples; i++) {
      sample16[i] = sample32[i] >> 14; // Shift down
    }
    
    size_t to_copy = num_samples * 2;
    if (rec_size + to_copy > MAX_REC_BYTES) {
      to_copy = MAX_REC_BYTES - rec_size;
    }
    memcpy(rec_buffer + rec_size, sample16, to_copy);
    rec_size += to_copy;
    
    if (rec_size >= MAX_REC_BYTES) {
      stopRecordingAndProcess();
    }
  }
}

#endif
