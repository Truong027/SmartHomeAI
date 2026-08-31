#ifndef HW_IR_H
#define HW_IR_H

#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>
#include "config.h"

// Initialize IR Receiver
const uint16_t kRecvBuffer = 1024;
const uint8_t kTimeout = 50;
const uint16_t kCaptureBufferSize = 1024;
IRrecv irrecv(IR_RX_PIN, kRecvBuffer, kTimeout, true);
decode_results results;

// Initialize IR Sender
IRsend irsend(IR_TX_PIN);

inline void setupIR() {
  irrecv.enableIRIn();  // Start the receiver
  irsend.begin();       // Start the sender
  Serial.println("IR Tx/Rx initialized");
}

inline void handleIR() {
  if (irrecv.decode(&results)) {
    Serial.print("IR Received: ");
    serialPrintUint64(results.value, HEX);
    Serial.println("");
    irrecv.resume(); // Receive the next value
  }
}

// Hàm khung để gửi lệnh (VD: Máy lạnh Panasonic, Daikin...)
inline void sendIR_AC_PowerOn() {
  // Thay thế bằng mã thực tế hoặc hàm send của thư viện (VD: irsend.sendPanasonic(...))
  Serial.println("Sending IR Power On command...");
}

#endif // HW_IR_H
