#ifndef CODE_ESP32_CONFIG_H
#define CODE_ESP32_CONFIG_H

#include <Arduino.h>

// ─── TFT Display (1.8" ST7735 SPI 128x160 -> Rotated 160x128) ─────────────────
#define TFT_LED           47   // Chân điều khiển đèn nền TFT (Backlight)
#define TFT_LED_FREQ      5000 // Tần số PWM 5kHz chống nhấp nháy đèn màn hình 100%
#define TFT_LED_CHANNEL   7    // Kênh LEDC phần cứng cho đèn nền


// ─── Touch Pins (ESP32-S3) ──────────────────────────────────────────────────────
#define TOUCH1_PIN        1   // Chân Touch 1
#define TOUCH2_PIN        2   // Chân Touch 2
#define TOUCH3_PIN        3   // Chân Touch 3
#define TOUCH_THRESHOLD   4000 // Độ nhạy: 4000
#define TOUCH_DEBOUNCE_MS 50   // Chống rung phím 50ms (rất quan trọng để nhận cú chạm nhanh)

// ─── I2C Bus Pins (RTC DS3231, AHT20/30, BMP280) ────────────────────────────
#define I2C_SDA           8
#define I2C_SCL           9
#define RTC_SQW           10

// ─── I2S Audio Pins (INMP441 Mic & MAX98357A Amp) ─────────────────────────────
#define I2S_MIC_SCK       4
#define I2S_MIC_WS        5
#define I2S_MIC_SD        6

#define I2S_AMP_BCLK      15
#define I2S_AMP_LRC       16
#define I2S_AMP_DIN       17

// ─── WS2812B LED Ring ─────────────────────────────────────────────────────────
#define WS2812B_PIN       42
#define NUM_LEDS          12

// ─── Relays ───────────────────────────────────────────────────────────────────
#define RELAY1_PIN        39  // Relay 1: Khóa điện 12V
#define RELAY2_PIN        40  // Relay 2: Đèn / Quạt

// ─── IR Transmitter & Receiver ────────────────────────────────────────────────
#define IR_SEND_PIN       18
#define IR_RECV_PIN       19

// ─── Màn hình (Rotation 1: 160x128) ──────────────────────────────────────────
#define SCREEN_W          160
#define SCREEN_H          128

// ─── Time Configuration ────────────────────────────────────────────────────────
static const long gmtOffset_sec = 7L * 3600L; // Múi giờ Việt Nam (UTC+7)
static const int daylightOffset_sec = 0;

// ─── OpenWeatherMap Configuration ─────────────────────────────────────────────
static const char* locationName       = "Da Nang, VN";
static const double weatherLat         = 16.007999;
static const double weatherLon         = 108.189924;
static const char* weatherApiKey      = "6e117d37cbcacbcef8db7c37ca75044e";
static const char* weatherApiUrl     = "https://api.openweathermap.org/data/2.5/weather";

// ─── Firebase Realtime Database Configuration ─────────────────────────────────
#define FIREBASE_API_KEY       "AIzaSyDND5fdH_tduPrnFHPsAo2Ggxzu1zJk18o"
#define FIREBASE_URL           "https://esp32app-30335-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define FIREBASE_USER_EMAIL    "admin@esp32.local"
#define FIREBASE_USER_PASSWORD "123456"

// ─── Groq API Configuration ───────────────────────────────────────────────────
#define GROQ_API_KEY           "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"

#endif // CODE_ESP32_CONFIG_H
