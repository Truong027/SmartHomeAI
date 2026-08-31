#ifndef HW_AUDIO_H
#define HW_AUDIO_H

#include <Arduino.h>
#include <driver/i2s_std.h>
#include "Audio.h" // Thư viện ESP32-audioI2S
#include "config.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/ringbuf.h>

#define I2S_MIC_PORT I2S_NUM_1
#define SAMPLE_RATE 16000
#define MAX_RECORD_TIME 10 // Giây
#define RECORD_BUFFER_SIZE (SAMPLE_RATE * 2 * MAX_RECORD_TIME) // ~320KB

extern Audio audio;
extern bool isRecording;
extern bool isAIMode; // Mode VAD tự động lắng nghe
extern bool isManualVoiceTrigger; // Cờ phân biệt ghi âm từ Nút cứng hay Tự động Wake-Word
extern bool system_ready; // Cờ báo hệ thống đã khởi động xong 100%
extern unsigned long lastWakeWordCheckFail; // Thời điểm phát hiện câu nói không phải Wake-Word (chống lặp)
extern volatile bool hasPendingAudioStop; // Cờ yêu cầu ngắt âm thanh an toàn trên Core 1
extern i2s_chan_handle_t rx_chan;

// Khai báo Ring Buffer & Task Handles cho FreeRTOS
extern RingbufHandle_t audio_ringbuf;
extern TaskHandle_t micReadTaskHandle;
extern TaskHandle_t audioProcessTaskHandle;

void setupAudio();
void flushAudioRingBuffer();
void startRecording(bool isManual = false);
void stopRecording(bool forceProcess = false);
void cancelRecording();
void processAudioLoop();

#endif
