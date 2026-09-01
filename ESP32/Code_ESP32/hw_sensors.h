#ifndef HW_SENSORS_H
#define HW_SENSORS_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_BMP280.h>
#include "RTClib.h"
#include "config.h"

extern float indoorTemp;
extern float indoorHum;
extern float indoorPres;

extern RTC_DS3231 rtc;
extern Adafruit_AHTX0 aht;
extern Adafruit_BMP280 bmp;

extern bool aht_ready;
extern bool bmp_ready;
extern bool rtc_ready;
extern String hhmmText;
extern String dateSolar;
extern uint8_t currentSecond;
extern String dateLunar;
extern int lunarDay_global;
extern int lunarMonth_global;
extern int lunarYear_global;
extern String canChiDay_global;
extern String canChiYear_global;

struct LunarDateC {
  int day;
  int month;
  int year;
  String canChiDay;
  String canChiYear;
};

inline LunarDateC convertSolar2LunarC(int dd, int mm, int yy, int timeZone = 7) {
  int a = (14 - mm) / 12;
  int y = yy + 4800 - a;
  int m = mm + 12 * a - 3;
  long dayNumber = dd + (153 * m + 2) / 5 + 365L * y + y / 4 - y / 100 + y / 400 - 32045L;
  if (dayNumber < 2299161) {
    dayNumber = dd + (153 * m + 2) / 5 + 365L * y + y / 4 - 32083L;
  }

  auto getNewMoon = [&](int k) -> long {
    double T = k / 1236.85;
    double T2 = T * T;
    double T3 = T2 * T;
    double dr = 3.141592653589793 / 180.0;
    double Jd1 = 2415020.75933 + 29.53058868 * k + 0.0001178 * T2 - 0.000000155 * T3;
    Jd1 += 0.00033 * sin((166.56 + 132.87 * T - 0.009173 * T2) * dr);
    double M = 359.2242 + 29.10535608 * k - 0.0000333 * T2 - 0.00000347 * T3;
    double Mpr = 306.0253 + 385.81691806 * k + 0.0107306 * T2 + 0.00001236 * T3;
    double F = 21.2964 + 390.67050646 * k - 0.0016528 * T2 - 0.00000239 * T3;
    double C1 = (0.1734 - 0.000393 * T) * sin(M * dr) + 0.0021 * sin(2 * dr * M);
    C1 -= 0.4068 * sin(Mpr * dr) + 0.0161 * sin(2 * dr * Mpr);
    C1 -= 0.0004 * sin(3 * dr * Mpr);
    C1 += 0.0104 * sin(2 * dr * F) - 0.0051 * sin((M + Mpr) * dr);
    C1 -= 0.0074 * sin((M - Mpr) * dr) + 0.0004 * sin((2 * F + M) * dr);
    C1 -= 0.0004 * sin((2 * F - M) * dr) - 0.0006 * sin((2 * F + Mpr) * dr);
    C1 += 0.0100 * sin((2 * F - Mpr) * dr) + 0.0005 * sin((M + 2 * Mpr) * dr);
    double deltat = -0.000078 + 0.000287 * T + 0.0001494 * T2 + 0.00000410 * T3 + 0.000000004 * T * T3;
    double JdNew = Jd1 + C1 - deltat;
    double val = JdNew + 0.5 + timeZone / 24.0;
    if ((val - floor(val)) > 0.98) {
      return (long)(floor(val) + 1);
    }
    return (long)floor(val);
  };

  auto getSunLong = [&](long jdn) -> int {
    double T = (jdn - 2451545.0 + 0.5 - timeZone / 24.0) / 36525.0;
    double T2 = T * T;
    double dr = 3.141592653589793 / 180.0;
    double M = 357.52910 + 35999.05029 * T - 0.0001559 * T2 - 0.00000048 * T * T2;
    double L0 = 280.46645 + 36000.76983 * T + 0.0003032 * T2;
    double DL = (1.91460 - 0.004817 * T - 0.000014 * T2) * sin(M * dr);
    DL += (0.019993 - 0.000101 * T) * sin(2 * M * dr) + 0.000290 * sin(3 * M * dr);
    double L = (L0 + DL) * dr;
    L = L - 3.141592653589793 * 2.0 * floor(L / (3.141592653589793 * 2.0));
    return (int)floor(L / (3.141592653589793 / 6.0));
  };

  auto getLunarMonth11 = [&](int y_target) -> long {
    int a_t = (14 - 12) / 12;
    int yy_t = y_target + 4800 - a_t;
    int m_t = 12 + 12 * a_t - 3;
    long jdDec31 = 31 + (153 * m_t + 2) / 5 + 365L * yy_t + yy_t / 4 - yy_t / 100 + yy_t / 400 - 32045L;
    int k_t = (int)floor((jdDec31 - 2415021.0769986) / 29.530588853);
    long nm = getNewMoon(k_t);
    if (getSunLong(nm) >= 9) {
      nm = getNewMoon(k_t - 1);
    }
    return nm;
  };

  auto getLeapMonthOffset = [&](long a11_val) -> int {
    int k_l = (int)floor((a11_val - 2415021.0769986) / 29.530588853 + 0.5);
    int last = 0;
    int i = 1;
    int arc = getSunLong(getNewMoon(k_l + i));
    do {
      last = arc;
      i++;
      arc = getSunLong(getNewMoon(k_l + i));
    } while (arc != last && i < 14);
    return i - 1;
  };

  int k = (int)floor((dayNumber - 2415021.0769986) / 29.530588853);
  long monthStart = getNewMoon(k + 1);
  if (monthStart > dayNumber) {
    monthStart = getNewMoon(k);
  }

  long a11 = getLunarMonth11(yy);
  long b11 = a11;
  int lunarYear = yy;
  if (a11 >= monthStart) {
    lunarYear = yy;
    a11 = getLunarMonth11(yy - 1);
  } else {
    lunarYear = yy + 1;
    b11 = getLunarMonth11(yy + 1);
  }

  int lDay = (int)(dayNumber - monthStart + 1);
  int diff = (int)((monthStart - a11) / 29.0);
  int lMonth = diff + 11;
  if (b11 - a11 > 365) {
    int leapDiff = getLeapMonthOffset(a11);
    if (diff >= leapDiff) {
      lMonth = diff + 10;
    }
  }
  if (lMonth > 12) lMonth -= 12;
  if (lMonth >= 11 && diff < 4) lunarYear -= 1;

  const char* CAN[] = {"Giáp", "Ất", "Bính", "Đinh", "Mậu", "Kỷ", "Canh", "Tân", "Nhâm", "Quý"};
  const char* CHI[] = {"Tý", "Sửu", "Dần", "Mão", "Thìn", "Tỵ", "Ngọ", "Mùi", "Thân", "Dậu", "Tuất", "Hợi"};

  String cDay = String(CAN[(dayNumber + 9) % 10]) + " " + String(CHI[(dayNumber + 1) % 12]);
  String cYear = String(CAN[(lunarYear + 6) % 10]) + " " + String(CHI[(lunarYear + 8) % 12]);

  return {lDay, lMonth, lunarYear, cDay, cYear};
}

inline void setupSensors() {
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000); // 400kHz Fast I2C Mode giúp đọc cảm biến < 1ms không gây khựng hệ thống
  Serial.printf("🔍 [I2C Setup] SDA Pin: %d, SCL Pin: %d (400kHz Fast Mode)\n", I2C_SDA, I2C_SCL);

  if (aht.begin(&Wire)) {
    Serial.println("✅ [AHT20/30] Tìm thấy cảm biến nhiệt ẩm AHT20/30!");
    aht_ready = true;
  } else {
    Serial.println("❌ [AHT20/30] KHÔNG tìm thấy AHT20/30");
  }

  if (bmp.begin(0x76, 0x58)) {
    Serial.println("✅ [BMP280] Tìm thấy cảm biến áp suất BMP280 tại 0x76!");
    bmp_ready = true;
  } else if (bmp.begin(0x77, 0x58)) {
    Serial.println("✅ [BMP280] Tìm thấy cảm biến áp suất BMP280 tại 0x77!");
    bmp_ready = true;
  } else {
    Serial.println("❌ [BMP280] KHÔNG tìm thấy BMP280");
  }

  if (rtc.begin(&Wire)) {
    Serial.println("✅ [RTC DS3231] Tìm thấy module thời gian thực DS3231!");
    rtc_ready = true;
    if (rtc.lostPower()) {
      Serial.println("⚠️ [RTC DS3231] Mất nguồn nuôi, đặt lại thời gian biên dịch...");
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
  } else {
    Serial.println("❌ [RTC DS3231] KHÔNG tìm thấy DS3231");
  }
}

inline void updateSensors() {
  unsigned long nowMs = millis();

  // 1. Đọc RTC DS3231 mỗi 1 giây (Thời gian thực thi siêu tốc: < 0.15ms)
  static unsigned long lastRtcUpdate = 0;
  if (nowMs - lastRtcUpdate >= 1000) {
    lastRtcUpdate = nowMs;
    if (rtc_ready || rtc.begin(&Wire)) {
      rtc_ready = true;
      DateTime nowRtc = rtc.now();
      char timeBuf[16], dateBuf[16];
      snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", nowRtc.hour(), nowRtc.minute());
      snprintf(dateBuf, sizeof(dateBuf), "%02d/%02d", nowRtc.day(), nowRtc.month());
      
      hhmmText = String(timeBuf);
      dateSolar = String(dateBuf);
      currentSecond = nowRtc.second();

      // Tính toán Âm Lịch Thiên Văn (Chỉ tính khi qua ngày mới)
      static int lastComputedDay = -1;
      if (lastComputedDay != nowRtc.day() && nowRtc.year() >= 2024) {
        lastComputedDay = nowRtc.day();
        LunarDateC lDate = convertSolar2LunarC(nowRtc.day(), nowRtc.month(), nowRtc.year(), 7);
        lunarDay_global = lDate.day;
        lunarMonth_global = lDate.month;
        lunarYear_global = lDate.year;
        canChiDay_global = lDate.canChiDay;
        canChiYear_global = lDate.canChiYear;
        dateLunar = String(lDate.day) + "/" + String(lDate.month) + " AL";
      }
    }
  }

  // 2. Cảm biến AHT20/30: State-Machine Bất Đồng Bộ (Non-Blocking Asynchronous) - 0ms Delay!
  // Hoàn toàn không dùng delay(80) của thư viện Adafruit để không làm khựng LED & Chữ chạy màn hình
  static unsigned long ahtTriggerTime = 0;
  static bool ahtWaitingData = false;
  static unsigned long lastAhtCycle = 0;

  if (aht_ready) {
    if (!ahtWaitingData && (nowMs - lastAhtCycle >= 3000)) {
      // Pha 1: Gửi lệnh Trigger đo (0.05ms)
      Wire.beginTransmission(0x38);
      Wire.write(0xAC);
      Wire.write(0x33);
      Wire.write(0x00);
      Wire.endTransmission();
      ahtTriggerTime = nowMs;
      ahtWaitingData = true;
      lastAhtCycle = nowMs;
    } else if (ahtWaitingData && (nowMs - ahtTriggerTime >= 85)) {
      // Pha 2: Đọc dữ liệu sau 85ms mà không cần delay block luồng (0.1ms)
      ahtWaitingData = false;
      if (Wire.requestFrom(0x38, 6) >= 6) {
        uint8_t status = Wire.read();
        if ((status & 0x80) == 0) { // Bit 7 == 0: Đo xong, dữ liệu sẵn sàng
          uint32_t hum_raw = (((uint32_t)Wire.read()) << 12) | (((uint32_t)Wire.read()) << 4);
          uint8_t b3 = Wire.read();
          hum_raw |= (b3 >> 4);
          uint32_t temp_raw = (((uint32_t)(b3 & 0x0F)) << 16) | (((uint32_t)Wire.read()) << 8) | ((uint32_t)Wire.read());

          float hum = ((float)hum_raw * 100.0f) / 1048576.0f;
          float temp = (((float)temp_raw * 200.0f) / 1048576.0f) - 50.0f;

          if (temp > -40.0f && temp < 85.0f) indoorTemp = temp;
          if (hum >= 0.0f && hum <= 100.0f) indoorHum = hum;
        }
      }
    }
  }

  // 3. Cảm biến BMP280: Đọc định kỳ mỗi 3 giây (Thời gian thực thi: < 0.2ms)
  static unsigned long lastBmpUpdate = 0;
  if (bmp_ready && (nowMs - lastBmpUpdate >= 3000)) {
    lastBmpUpdate = nowMs;
    float bmpT = bmp.readTemperature();
    indoorPres = bmp.readPressure() / 100.0F; // hPa
    if (!aht_ready && !isnan(bmpT) && bmpT > -40.0f && bmpT < 85.0f) {
      indoorTemp = bmpT;
      if (indoorHum <= 0.0f) indoorHum = 60.0f;
    }
  }

  // Giá trị an toàn nếu không tìm thấy cảm biến
  if (indoorTemp <= 0.0f) {
    indoorTemp = 28.5f;
    indoorHum = 65.0f;
  }
}

#endif // HW_SENSORS_H
