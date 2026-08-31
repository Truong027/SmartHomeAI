#ifndef HW_EEPROM_H
#define HW_EEPROM_H

#include <Arduino.h>
#include <Wire.h>
#include <IRremoteESP8266.h>

#define AT24C32_ADDR 0x57

// Hàm tự viết giao tiếp I2C cho AT24C32 (Không cần cài thư viện)
inline void eepromWriteByte(uint16_t mem_addr, uint8_t data) {
    Wire.beginTransmission(AT24C32_ADDR);
    Wire.write((uint8_t)(mem_addr >> 8));   // MSB
    Wire.write((uint8_t)(mem_addr & 0xFF)); // LSB
    Wire.write(data);
    Wire.endTransmission();
    delay(5); // AT24C32 cần khoảng 5ms để ghi xong 1 byte
}

inline uint8_t eepromReadByte(uint16_t mem_addr) {
    Wire.beginTransmission(AT24C32_ADDR);
    Wire.write((uint8_t)(mem_addr >> 8));   // MSB
    Wire.write((uint8_t)(mem_addr & 0xFF)); // LSB
    Wire.endTransmission();
    
    Wire.requestFrom((uint8_t)AT24C32_ADDR, (uint8_t)1);
    if (Wire.available()) {
        return Wire.read();
    }
    return 0xFF;
}

// ---------------------------------------------------------
// VÙNG 1: QUẢN LÝ HỒNG NGOẠI (0x0000 -> 0x00FF)
// ---------------------------------------------------------
#define EEPROM_IR_START_ADDR 0x0000
#define MAX_IR_SLOTS 10

// 16 bytes cho 1 mã hồng ngoại
struct LearnedIR {
  decode_type_t type; // int32_t hoặc enum (chiếm 4 bytes)
  uint64_t value;     // 8 bytes
  uint16_t bits;      // 2 bytes
  uint16_t padding;   // 2 bytes để tròn 16 bytes
};

extern LearnedIR learned_ir[MAX_IR_SLOTS];

// Ghi 1 Struct vào EEPROM (byte-by-byte)
template <class T> int eepromWriteStruct(int ee, const T& value) {
    const uint8_t* p = (const uint8_t*)(const void*)&value;
    unsigned int i;
    for (i = 0; i < sizeof(value); i++) {
        eepromWriteByte(ee++, *p++);
    }
    return i;
}

// Đọc 1 Struct từ EEPROM
template <class T> int eepromReadStruct(int ee, T& value) {
    uint8_t* p = (uint8_t*)(void*)&value;
    unsigned int i;
    for (i = 0; i < sizeof(value); i++) {
        *p++ = eepromReadByte(ee++);
    }
    return i;
}

inline void loadIRCodesFromAT24() {
  for (int i = 0; i < MAX_IR_SLOTS; i++) {
    int addr = EEPROM_IR_START_ADDR + (i * sizeof(LearnedIR));
    eepromReadStruct(addr, learned_ir[i]);
  }
  Serial.println("📂 Đã tải 10 mã IR từ AT24C32 (EEPROM).");
}

inline void saveIRCodeToAT24(int index) {
  if (index < 0 || index >= MAX_IR_SLOTS) return;
  int addr = EEPROM_IR_START_ADDR + (index * sizeof(LearnedIR));
  eepromWriteStruct(addr, learned_ir[index]);
  Serial.println("💾 Đã lưu mã IR vào AT24C32 tại slot " + String(index + 1));
}

// ---------------------------------------------------------
// VÙNG 2: DATALOGGING (0x0100 -> 0x0FFF)
// ---------------------------------------------------------
#define EEPROM_LOG_START_ADDR 0x0100
#define EEPROM_MAX_ADDR 0x0FFF // 4095
#define MAX_LOG_RECORDS ((EEPROM_MAX_ADDR - EEPROM_LOG_START_ADDR) / sizeof(SensorLog))

// 16 bytes cho 1 bản ghi
struct SensorLog {
  uint32_t timestamp; // 4 bytes (Unix time từ RTC)
  float temp;         // 4 bytes
  float hum;          // 4 bytes
  float pres;         // 4 bytes
};

// Lưu vị trí bản ghi hiện tại vào byte đầu tiên (hoặc biến ngoài)
// Để đơn giản, ta dùng 2 bytes ở địa chỉ 0x00F0 để trỏ vị trí write_index
#define EEPROM_LOG_INDEX_ADDR 0x00F0 

inline uint16_t getLogIndex() {
  uint16_t idx = 0;
  eepromReadStruct(EEPROM_LOG_INDEX_ADDR, idx);
  if (idx >= MAX_LOG_RECORDS) {
    idx = 0;
    eepromWriteStruct(EEPROM_LOG_INDEX_ADDR, idx);
  }
  return idx;
}

inline void logSensorDataOffline(uint32_t unixTime, float t, float h, float p) {
  uint16_t currentIdx = getLogIndex();
  
  SensorLog record;
  record.timestamp = unixTime;
  record.temp = t;
  record.hum = h;
  record.pres = p;

  int addr = EEPROM_LOG_START_ADDR + (currentIdx * sizeof(SensorLog));
  eepromWriteStruct(addr, record);

  Serial.printf("📝 Đã lưu Log Offline #%d (T: %.1fC, H: %.1f%%)\n", currentIdx, t, h);

  // Tăng index vòng tròn
  currentIdx++;
  if (currentIdx >= MAX_LOG_RECORDS) currentIdx = 0;
  eepromWriteStruct(EEPROM_LOG_INDEX_ADDR, currentIdx);
}

inline void printAllOfflineLogs() {
  Serial.println("\n📊 --- LỊCH SỬ NHIỆT ĐỘ OFFLINE TRÊN AT24C32 ---");
  int total = MAX_LOG_RECORDS;
  int count = 0;
  for (int i = 0; i < total; i++) {
    SensorLog record;
    int addr = EEPROM_LOG_START_ADDR + (i * sizeof(SensorLog));
    eepromReadStruct(addr, record);
    
    // Nếu có dữ liệu hợp lệ
    if (record.timestamp > 1700000000 && record.timestamp < 2000000000) { 
      Serial.printf("Log #%d - TS: %lu | T: %.1fC | H: %.1f%% | P: %.1fhPa\n", 
                    i, record.timestamp, record.temp, record.hum, record.pres);
      count++;
    }
  }
  if (count == 0) Serial.println("Chưa có dữ liệu offline nào.");
  Serial.println("----------------------------------------------\n");
}

// ---------------------------------------------------------
// VÙNG 3: SETTINGS (Cài đặt)
// ---------------------------------------------------------
#define EEPROM_SETTINGS_ADDR 0x00F4 
struct DeviceSettings {
  bool relay1;
  bool relay2;
  int ledBrightness;
  uint16_t checksum;
};

inline void saveSettingsToEEPROM(bool r1, bool r2, int bright) {
  DeviceSettings s;
  s.relay1 = r1;
  s.relay2 = r2;
  s.ledBrightness = bright;
  s.checksum = 0xAA55;
  eepromWriteStruct(EEPROM_SETTINGS_ADDR, s);
  Serial.println("💾 Đã lưu trạng thái Relay/Đèn vào EEPROM.");
}

inline bool loadSettingsFromEEPROM(bool &r1, bool &r2, int &bright) {
  DeviceSettings s;
  eepromReadStruct(EEPROM_SETTINGS_ADDR, s);
  if (s.checksum == 0xAA55) {
    r1 = s.relay1;
    r2 = s.relay2;
    bright = s.ledBrightness;
    Serial.println("📂 Đã tải trạng thái cũ từ EEPROM.");
    return true;
  }
  return false;
}

#endif
