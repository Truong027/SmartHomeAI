#ifndef HW_LED_H
#define HW_LED_H

#include <Arduino.h>
#include <math.h>
#include <Adafruit_NeoPixel.h>
#include "config.h"

#ifndef NUM_LEDS
#define NUM_LEDS 12
#endif

extern Adafruit_NeoPixel pixels;
extern int currentLedMode;
extern float indoorTemp;

inline void setupLED() {
  pixels.begin();
  pixels.setBrightness(50);
  pixels.clear();
  pixels.show();
}

inline void setLedMode(int mode) {
  currentLedMode = mode;
}

// Hàm tính khoảng cách góc theo chiều kim đồng hồ giữa 2 điểm trên vòng tròn 12 LED
inline float getCircularDist(float fromPos, float toPos, float maxLeds = (float)NUM_LEDS) {
  float d = fromPos - toPos;
  while (d < 0.0f) d += maxLeds;
  while (d >= maxLeds) d -= maxLeds;
  return d;
}

// Render 1 vệt sáng mượt mà tuyệt đối với vị trí số thực (Sub-pixel Floating Point Motion)
inline void renderSubpixelComet(float headPos, float tailLength, uint8_t r, uint8_t g, uint8_t b, float minGlow = 0.03f) {
  pixels.clear();
  for (int i = 0; i < NUM_LEDS; i++) {
    float dist = getCircularDist(headPos, (float)i, (float)NUM_LEDS);
    float brightness = 0.0f;
    
    if (dist <= tailLength) {
      // Đường cong suy giảm ánh sáng mềm mại (Smoothstep falloff)
      float norm = 1.0f - (dist / tailLength); // 1.0 tại đỉnh, 0.0 tại đuôi
      brightness = norm * norm * (3.0f - 2.0f * norm);
      brightness = brightness * (1.0f - minGlow) + minGlow;
    } else {
      brightness = minGlow; // Ánh sáng nền dịu nhẹ không bị tắt cụt
    }
    
    if (brightness > 1.0f) brightness = 1.0f;
    if (brightness < 0.0f) brightness = 0.0f;
    
    uint8_t curR = (uint8_t)(r * brightness);
    uint8_t curG = (uint8_t)(g * brightness);
    uint8_t curB = (uint8_t)(b * brightness);
    pixels.setPixelColor(i, pixels.Color(curR, curG, curB));
  }
  pixels.show();
}

// Render 2 vệt sáng đối xứng xoay đuổi nhau cực êm (Dual Sub-pixel Chasing)
inline void renderDualSubpixelComet(float headPos, float tailLength, uint8_t r, uint8_t g, uint8_t b, float minGlow = 0.02f) {
  pixels.clear();
  for (int c = 0; c < 2; c++) {
    float head = headPos + (float)c * ((float)NUM_LEDS / 2.0f);
    while (head >= (float)NUM_LEDS) head -= (float)NUM_LEDS;
    
    for (int i = 0; i < NUM_LEDS; i++) {
      float dist = getCircularDist(head, (float)i, (float)NUM_LEDS);
      if (dist <= tailLength) {
        float norm = 1.0f - (dist / tailLength);
        float brightness = norm * norm * (3.0f - 2.0f * norm);
        brightness = brightness * (1.0f - minGlow) + minGlow;
        
        uint8_t curR = (uint8_t)(r * brightness);
        uint8_t curG = (uint8_t)(g * brightness);
        uint8_t curB = (uint8_t)(b * brightness);
        
        uint32_t ex = pixels.getPixelColor(i);
        uint8_t exR = (ex >> 16) & 0xFF;
        uint8_t exG = (ex >> 8) & 0xFF;
        uint8_t exB = ex & 0xFF;
        pixels.setPixelColor(i, pixels.Color(max(exR, curR), max(exG, curG), max(exB, curB)));
      } else if (minGlow > 0.0f) {
        uint32_t ex = pixels.getPixelColor(i);
        uint8_t exR = (ex >> 16) & 0xFF;
        uint8_t exG = (ex >> 8) & 0xFF;
        uint8_t exB = ex & 0xFF;
        uint8_t gR = (uint8_t)(r * minGlow);
        uint8_t gG = (uint8_t)(g * minGlow);
        uint8_t gB = (uint8_t)(b * minGlow);
        pixels.setPixelColor(i, pixels.Color(max(exR, gR), max(exG, gG), max(exB, gB)));
      }
    }
  }
  pixels.show();
}

// Render Lưỡng Cực Xoáy (Dipole Vortex Swirl) - 2 cực đối xứng 180° xoáy lướt nhẹ nhàng, hòa sắc đa tầng siêu mượt
inline void renderDipoleVortexSwirl(float headPos, float tailLength, uint8_t r1, uint8_t g1, uint8_t b1, uint8_t r2, uint8_t g2, uint8_t b2, float minGlow = 0.02f) {
  pixels.clear();
  float rAcc[NUM_LEDS] = {0.0f};
  float gAcc[NUM_LEDS] = {0.0f};
  float bAcc[NUM_LEDS] = {0.0f};

  for (int p = 0; p < 2; p++) {
    float head = headPos + (float)p * ((float)NUM_LEDS / 2.0f); // 2 cực đối xứng đúng 180 độ (cách nhau 6 LED)
    while (head >= (float)NUM_LEDS) head -= (float)NUM_LEDS;
    while (head < 0.0f) head += (float)NUM_LEDS;

    uint8_t pr = (p == 0) ? r1 : r2;
    uint8_t pg = (p == 0) ? g1 : g2;
    uint8_t pb = (p == 0) ? b1 : b2;

    for (int i = 0; i < NUM_LEDS; i++) {
      float dist = getCircularDist(head, (float)i, (float)NUM_LEDS);
      if (dist <= tailLength) {
        float norm = 1.0f - (dist / tailLength);
        float brightness = norm * norm * (3.0f - 2.0f * norm); // Cubic Smoothstep
        brightness = brightness * (1.0f - minGlow) + minGlow;

        rAcc[i] += (float)pr * brightness;
        gAcc[i] += (float)pg * brightness;
        bAcc[i] += (float)pb * brightness;
      } else if (minGlow > 0.0f) {
        rAcc[i] += (float)pr * minGlow;
        gAcc[i] += (float)pg * minGlow;
        bAcc[i] += (float)pb * minGlow;
      }
    }
  }

  for (int i = 0; i < NUM_LEDS; i++) {
    uint8_t cr = (uint8_t)min(255.0f, rAcc[i]);
    uint8_t cg = (uint8_t)min(255.0f, gAcc[i]);
    uint8_t cb = (uint8_t)min(255.0f, bAcc[i]);
    pixels.setPixelColor(i, pixels.Color(cr, cg, cb));
  }
  pixels.show();
}

// Hàm điều khiển hoạt ảnh 12 LED - Lướt nhẹ nhàng, mượt mà, chuyển động liên tục theo thời gian thực (Time-Delta)
inline void handleLedAnimation() {
  static unsigned long lastLedUpdate = 0;
  static float subPos = 0.0f;
  static float breathPhase = 0.0f;
  static int successStep = 0;
  static bool sweepDir = true;
  
  unsigned long now = millis();
  if (lastLedUpdate == 0) lastLedUpdate = now;
  unsigned long elapsed = now - lastLedUpdate;
  if (elapsed < 16) return; // Khóa trần ~60 FPS giúp tiết kiệm CPU và mượt mà
  lastLedUpdate = now;
  
  float dt = (float)elapsed / 1000.0f; // Thời gian trôi qua thực tế (giây)
  float fFact = dt * 50.0f; // Hệ số chuẩn hóa theo thang 50 FPS
  if (fFact > 2.5f) fFact = 2.5f; // Chống giật vọt bước khi có ngắt hệ thống
  if (fFact < 0.1f) fFact = 0.1f;
  
  // --- 0. IDLE MODE: LƯỠNG CỰC XOÁY (DIPOLE VORTEX SWIRL) - 2 CỰC CYAN & VIOLET XOÁY ĐỐI XỨNG SIÊU MƯỢT ---
  if (currentLedMode == 0) {
    float speed = 0.075f; // Tốc độ xoáy thư thái, nhẹ nhàng (~3.2s / vòng)
    
    subPos += speed * fFact;
    while (subPos >= (float)NUM_LEDS) subPos -= (float)NUM_LEDS;
    while (subPos < 0.0f) subPos += (float)NUM_LEDS;
    
    // Cực 1 & Cực 2 đổi sắc theo nhiệt độ phòng thực tế
    uint8_t r1 = 0, g1 = 235, b1 = 255;   // Cực 1: Cyber Aqua / Cyan Neon
    uint8_t r2 = 175, g2 = 50, b2 = 255;  // Cực 2: Neon Amethyst / Violet
    
    if (indoorTemp >= 30.0f && indoorTemp <= 35.0f) {
      r1 = 255; g1 = 170; b1 = 20;  // Vàng Hổ Phách
      r2 = 255; g2 = 70;  b2 = 130; // Hồng San Hô
    } else if (indoorTemp > 35.0f) {
      r1 = 255; g1 = 40;  b1 = 60;  // Đỏ Lửa
      r2 = 255; g2 = 140; b2 = 0;   // Cam Hoàng Kim
    } else if (indoorTemp < 22.0f && indoorTemp > 0.0f) {
      r1 = 0;   g1 = 190; b1 = 255; // Băng Tuyết
      r2 = 80;  g2 = 100; b2 = 255; // Xanh Đêm Sâu
    }
    
    renderDipoleVortexSwirl(subPos, 4.8f, r1, g1, b1, r2, g2, b2, 0.02f);
  }
  
  // --- 1. THINKING MODE: Lưỡng cực xoáy tím & trắng ánh bạc xoay tập trung ---
  else if (currentLedMode == 1) {
    subPos += 0.10f * fFact;
    while (subPos >= (float)NUM_LEDS) subPos -= (float)NUM_LEDS;
    while (subPos < 0.0f) subPos += (float)NUM_LEDS;
    renderDipoleVortexSwirl(subPos, 4.2f, 190, 70, 255, 230, 240, 255, 0.03f);
  }
  
  // --- 2. ERROR MODE: Nhịp thở đỏ êm (Không chớp nháy gắt) ---
  else if (currentLedMode == 2) {
    breathPhase += 0.08f * fFact;
    float bVal = (sin(breathPhase) + 1.0f) * 0.5f; // 0.0 -> 1.0
    uint8_t r = (uint8_t)(255 * bVal);
    pixels.fill(pixels.Color(r, 0, 0));
    pixels.show();
  }
  
  // --- 3. LISTENING MODE: Sóng thở Cyan kết hợp xoay êm ---
  else if (currentLedMode == 3) {
    subPos += 0.08f * fFact;
    while (subPos >= (float)NUM_LEDS) subPos -= (float)NUM_LEDS;
    while (subPos < 0.0f) subPos += (float)NUM_LEDS;
    breathPhase += 0.07f * fFact;
    float bVal = 0.4f + 0.6f * (sin(breathPhase) + 1.0f) * 0.5f;
    
    pixels.clear();
    for (int i = 0; i < NUM_LEDS; i++) {
      float wave = sin(((float)i + subPos) * 3.14159f / 6.0f);
      if (wave < 0.0f) wave = 0.0f;
      uint8_t val = (uint8_t)(255.0f * bVal * wave);
      pixels.setPixelColor(i, pixels.Color(0, val, (uint8_t)(val * 0.9f)));
    }
    pixels.show();
  }
  
  // --- 4. SUCCESS MODE: Vòng xanh ngọc bung nở rồi về Idle ---
  else if (currentLedMode == 4) {
    breathPhase += 0.09f * fFact;
    float bVal = (sin(breathPhase) + 1.0f) * 0.5f;
    uint8_t g = (uint8_t)(255 * bVal);
    pixels.fill(pixels.Color(0, g, (uint8_t)(g * 0.6f)));
    pixels.show();
    
    successStep++;
    if (successStep >= 75) { // ~1.5 giây
      successStep = 0;
      breathPhase = 0.0f;
      currentLedMode = 0;
    }
  }
  
  // --- CÁC TRẠNG THÁI CẢM XÚC (10 - 22) CHUYỂN ĐỘNG MƯỢT NHẸ TRÊN 12 LED ---
  else if (currentLedMode == 10) { // happy: Vệt vàng chanh rạng rỡ xoay êm
    subPos += 0.10f * fFact;
    while (subPos >= (float)NUM_LEDS) subPos -= (float)NUM_LEDS;
    while (subPos < 0.0f) subPos += (float)NUM_LEDS;
    renderSubpixelComet(subPos, 5.0f, 210, 240, 10, 0.03f);
  }
  else if (currentLedMode == 11) { // sad: Xanh biển sâu trôi chậm rãi
    subPos += 0.06f * fFact;
    while (subPos >= (float)NUM_LEDS) subPos -= (float)NUM_LEDS;
    while (subPos < 0.0f) subPos += (float)NUM_LEDS;
    renderSubpixelComet(subPos, 4.5f, 10, 80, 230, 0.02f);
  }
  else if (currentLedMode == 12) { // surprised: Vệt ánh bạc ngọc trai xoay đôi
    subPos += 0.12f * fFact;
    while (subPos >= (float)NUM_LEDS) subPos -= (float)NUM_LEDS;
    while (subPos < 0.0f) subPos += (float)NUM_LEDS;
    renderDualSubpixelComet(subPos, 3.5f, 240, 245, 255);
  }
  else if (currentLedMode == 13) { // confused: Vệt Tím & Cam xen kẽ lướt nhẹ
    subPos += 0.09f * fFact;
    while (subPos >= (float)NUM_LEDS) subPos -= (float)NUM_LEDS;
    while (subPos < 0.0f) subPos += (float)NUM_LEDS;
    pixels.clear();
    for (int i = 0; i < NUM_LEDS; i++) {
      float dist = getCircularDist(subPos, (float)i, (float)NUM_LEDS);
      float norm = 0.5f + 0.5f * sin((dist / (float)NUM_LEDS) * 6.28318f);
      uint8_t r = (uint8_t)(160 * norm + 255 * (1.0f - norm));
      uint8_t g = (uint8_t)(140 * (1.0f - norm));
      uint8_t b = (uint8_t)(180 * norm);
      pixels.setPixelColor(i, pixels.Color((uint8_t)(r * 0.7f), (uint8_t)(g * 0.7f), (uint8_t)(b * 0.7f)));
    }
    pixels.show();
  }
  else if (currentLedMode == 14) { // angry: Vệt đỏ lửa lướt đều
    subPos += 0.11f * fFact;
    while (subPos >= (float)NUM_LEDS) subPos -= (float)NUM_LEDS;
    while (subPos < 0.0f) subPos += (float)NUM_LEDS;
    renderSubpixelComet(subPos, 5.0f, 255, 30, 30, 0.05f);
  }
  else if (currentLedMode == 15) { // excited: Cầu vồng Cyber chuyển sắc liên tục siêu êm
    subPos += 0.08f * fFact;
    while (subPos >= (float)NUM_LEDS) subPos -= (float)NUM_LEDS;
    while (subPos < 0.0f) subPos += (float)NUM_LEDS;
    for (int i = 0; i < NUM_LEDS; i++) {
      float hueNorm = fmod((float)i / (float)NUM_LEDS + subPos / (float)NUM_LEDS, 1.0f);
      uint32_t color = pixels.ColorHSV((uint16_t)(hueNorm * 65535.0f), 220, 240);
      pixels.setPixelColor(i, color);
    }
    pixels.show();
  }
  else if (currentLedMode == 16) { // proud: Vệt vàng ánh kim hoàng gia
    subPos += 0.09f * fFact;
    while (subPos >= (float)NUM_LEDS) subPos -= (float)NUM_LEDS;
    while (subPos < 0.0f) subPos += (float)NUM_LEDS;
    renderSubpixelComet(subPos, 5.2f, 255, 180, 20, 0.04f);
  }
  else if (currentLedMode == 17) { // curious: Vệt xanh ngọc quét qua lại như con lắc
    if (sweepDir) {
      subPos += 0.09f * fFact;
      if (subPos >= (float)NUM_LEDS - 1.0f) sweepDir = false;
    } else {
      subPos -= 0.09f * fFact;
      if (subPos <= 0.0f) sweepDir = true;
    }
    renderSubpixelComet(subPos, 4.0f, 0, 240, 160, 0.02f);
  }
  else if (currentLedMode == 18) { // love: Vệt hồng Neon xoay đôi ấm áp
    subPos += 0.09f * fFact;
    while (subPos >= (float)NUM_LEDS) subPos -= (float)NUM_LEDS;
    while (subPos < 0.0f) subPos += (float)NUM_LEDS;
    renderDualSubpixelComet(subPos, 3.8f, 255, 40, 140);
  }
  else if (currentLedMode == 19) { // worried: Sóng xanh lam nhạt gợn nhẹ
    subPos += 0.07f * fFact;
    while (subPos >= (float)NUM_LEDS) subPos -= (float)NUM_LEDS;
    while (subPos < 0.0f) subPos += (float)NUM_LEDS;
    renderSubpixelComet(subPos, 4.5f, 60, 160, 255, 0.03f);
  }
  else if (currentLedMode == 20) { // tired: Vệt xám tro thư giãn
    subPos += 0.06f * fFact;
    while (subPos >= (float)NUM_LEDS) subPos -= (float)NUM_LEDS;
    while (subPos < 0.0f) subPos += (float)NUM_LEDS;
    renderSubpixelComet(subPos, 3.8f, 90, 100, 110, 0.02f);
  }
  else if (currentLedMode == 21) { // sleepy: 2 vệt xanh đêm dịu êm
    subPos += 0.05f * fFact;
    while (subPos >= (float)NUM_LEDS) subPos -= (float)NUM_LEDS;
    while (subPos < 0.0f) subPos += (float)NUM_LEDS;
    renderDualSubpixelComet(subPos, 3.5f, 10, 40, 130);
  }
  else if (currentLedMode == 22) {
    currentLedMode = 0;
  }
}

#endif // HW_LED_H
