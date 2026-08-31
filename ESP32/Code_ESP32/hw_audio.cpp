#include "hw_audio.h"
#include "ai_agent.h"
#include "ui_lvgl.h"
#include "hw_led.h"

#include <WebSocketsClient.h> // Thư viện WebSocket Client truyền phát thời gian thực

// Khai báo đối tượng WebSocket Client toàn cục
WebSocketsClient webSocket;

Audio audio;
bool isRecording = false;
bool isAIMode = false; // Mode VAD tự động lắng nghe
bool isManualVoiceTrigger = false; // Cờ phân biệt ghi âm từ Nút cứng hay Tự động Wake-Word
bool system_ready = false; // Chỉ bật VAD khi hệ thống đã khởi động xong 100%
unsigned long lastWakeWordCheckFail = 0; // Thời điểm phát hiện câu nói không phải Wake-Word (chống lặp)
volatile bool hasPendingAudioStop = false; // Cờ yêu cầu ngắt âm thanh an toàn trên Core 1
uint8_t *recordBuffer = nullptr;
size_t recordIndex = 0;
i2s_chan_handle_t rx_chan = NULL;

// --- FreeRTOS Handles ---
RingbufHandle_t audio_ringbuf = NULL;
TaskHandle_t micReadTaskHandle = NULL;
TaskHandle_t audioProcessTaskHandle = NULL;

// --- AGC (Automatic Gain Control) & XiaoZhi VAD Constants ---
static float agcGain = 4.5f;                 // Gain khởi điểm nhạy bén
static const float AGC_TARGET_RMS = 4500.0f; // Mức RMS mong muốn cho giọng nói rõ ràng
static const float AGC_MIN_GAIN = 1.5f;
static const float AGC_MAX_GAIN = 12.0f;     // Khuếch đại giọng nói ở xa lên 12x êm ái
static const float AGC_SMOOTH_OLD = 0.85f;
static const float AGC_SMOOTH_NEW = 0.15f;

// Bộ theo dõi mức ồn nền phòng tự động thích ứng (Adaptive Noise Floor)
static float adaptiveNoiseFloor = 100.0f; 

// Số block im lặng liên tiếp để ngắt (~0.6 giây ở 16ms/block)
static const int VAD_SILENCE_TRIGGER_BLOCKS = 38; 
static int silence_block_count = 0;
static int voice_activity_count = 0;

// Bộ đệm xoay vòng lưu trước 8 blocks (~128ms) âm thanh để không bị nuốt âm đầu ("Hi", "Xin")
static int16_t preroll_buffer[8][256];
static int preroll_head = 0;

// Prototype cho các Task FreeRTOS
void micReadTask(void *pvParameters);
void audioProcessTask(void *pvParameters);

// --- Tạo WAV Header hỗ trợ Streaming (Data Size = 0xFFFFFFFF) ---
void generateStreamingWavHeader(uint8_t *header, uint32_t sampleRate, uint16_t numChannels, uint16_t bitsPerSample) {
    uint32_t byteRate = sampleRate * numChannels * (bitsPerSample / 8);
    uint16_t blockAlign = numChannels * (bitsPerSample / 8);

    // RIFF Chunk Descriptor
    header[0] = 'R'; header[1] = 'I'; header[2] = 'F'; header[3] = 'F';
    uint32_t chunkSize = 0xFFFFFFFF; // Luồng Vô Tận (Streaming)
    memcpy(&header[4], &chunkSize, 4);
    header[8] = 'W'; header[9] = 'A'; header[10] = 'V'; header[11] = 'E';

    // fmt Sub-chunk
    header[12] = 'f'; header[13] = 'm'; header[14] = 't'; header[15] = ' ';
    uint32_t subchunk1Size = 16;
    memcpy(&header[16], &subchunk1Size, 4);
    uint16_t audioFormat = 1; // PCM
    memcpy(&header[20], &audioFormat, 2);
    memcpy(&header[22], &numChannels, 2);
    memcpy(&header[24], &sampleRate, 4);
    memcpy(&header[28], &byteRate, 4);
    memcpy(&header[32], &blockAlign, 2);
    memcpy(&header[34], &bitsPerSample, 2);

    // data Sub-chunk
    header[36] = 'd'; header[37] = 'a'; header[38] = 't'; header[39] = 'a';
    uint32_t subchunk2Size = 0xFFFFFFFF; // Set 0xFFFFFFFF tại byte 40 để Server hiểu đây là Stream
    memcpy(&header[40], &subchunk2Size, 4);
}

void setupAudio() {
  // Cấp phát bộ đệm recordBuffer trong PSRAM cho Groq STT
  recordBuffer = (uint8_t *)ps_malloc(RECORD_BUFFER_SIZE);
  if (!recordBuffer) {
    Serial.println("❌ Lỗi cấp phát bộ nhớ PSRAM cho âm thanh!");
  } else {
    Serial.println("✅ Cấp phát thành công bộ đệm ghi âm (PSRAM)");
  }

  // Cấp phát Ring Buffer 16KB dạng ByteBuf của FreeRTOS
  audio_ringbuf = xRingbufferCreate(16 * 1024, RINGBUF_TYPE_BYTEBUF);
  if (audio_ringbuf == NULL) {
    Serial.println("❌ Lỗi cấp phát FreeRTOS Ring Buffer (16KB)!");
    return;
  }
  Serial.println("✅ Cấp phát thành công FreeRTOS Ring Buffer (16KB)");

  // Setup I2S Loa (MAX98357A)
  audio.setPinout(I2S_AMP_BCLK, I2S_AMP_LRC, I2S_AMP_DIN);
  audio.setVolume(21);
  audio.setConnectionTimeout(5000, 8000); // Tăng timeout kết nối và đọc HTTP stream

  // Setup I2S Mic (INMP441)
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
  if (i2s_new_channel(&chan_cfg, NULL, &rx_chan) != ESP_OK) {
    Serial.println("❌ Lỗi cấp phát kênh I2S (Mic)!");
    return;
  }

  i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
      .gpio_cfg = {
          .mclk = I2S_GPIO_UNUSED,
          .bclk = (gpio_num_t)I2S_MIC_SCK,
          .ws = (gpio_num_t)I2S_MIC_WS,
          .dout = I2S_GPIO_UNUSED,
          .din = (gpio_num_t)I2S_MIC_SD,
          .invert_flags = {
              .mclk_inv = false,
              .bclk_inv = false,
              .ws_inv = false,
          },
      },
  };
  std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT; // Đọc khe LEFT

  if (i2s_channel_init_std_mode(rx_chan, &std_cfg) != ESP_OK) {
    Serial.println("❌ Lỗi khởi tạo cấu hình I2S (Mic)!");
    return;
  }

  if (i2s_channel_enable(rx_chan) != ESP_OK) {
    Serial.println("❌ Lỗi bật kênh I2S (Mic)!");
    return;
  }
  Serial.println("✅ Mic I2S (INMP441) đã mở kênh Always-On.");

  // Tạo Task Đọc Mic (Core 0, Ưu tiên 5 - Cao nhất cho mic)
  xTaskCreatePinnedToCore(
      micReadTask,
      "micReadTask",
      4096,
      NULL,
      5,
      &micReadTaskHandle,
      0
  );

  // Tạo Task Xử lý DSP & Auto-VAD (Ghim cố định trên Core 0, không can thiệp Core 1)
  xTaskCreatePinnedToCore(
      audioProcessTask,
      "audioProcessTask",
      8192,
      NULL,
      1,
      &audioProcessTaskHandle,
      0
  );
}

// --- TASK 1: ĐỌC I2S LIÊN TỤC CHỐNG TRÀN DMA & ĐẨY VÀO RING BUFFER (CORE 0) ---
void micReadTask(void *pvParameters) {
  int32_t tempBuffer[256]; // 256 mẫu 32-bit (1024 bytes ~16ms)
  size_t bytesRead = 0;

  while (true) {
    if (rx_chan != NULL) {
      // LUÔN LUÔN đọc I2S liên tục để xả DMA buffer chống nghẽn phần cứng
      esp_err_t err = i2s_channel_read(rx_chan, tempBuffer, sizeof(tempBuffer), &bytesRead, pdMS_TO_TICKS(50));
      if (err == ESP_OK && bytesRead > 0) {
        // Luôn đẩy vào Ring Buffer khi hệ thống đã boot xong và có WiFi (hỗ trợ Barge-in ngắt tiếng)
        if (system_ready && WiFi.status() == WL_CONNECTED) {
          if (audio_ringbuf != NULL) {
            xRingbufferSend(audio_ringbuf, tempBuffer, bytesRead, pdMS_TO_TICKS(10));
          }
        }
      }
    } else {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelay(pdMS_TO_TICKS(1)); // Nhường CPU 1ms cho FreeRTOS IDLE0 và Task Watchdog
  }
}

// Biến đệm lưu thời điểm âm thanh vừa kết thúc
static unsigned long lastAudioStopTime = 0;
static bool wasAudioPlayingLast = false;

// Biến trạng thái VAD 2 Pha (Phase 1: Chờ nói -> Phase 2: Đang nói & Dứt câu)
static bool has_started_speaking = false;
static unsigned long recordStartTime = 0;

// Xả sạch toàn bộ mẫu rác còn sót trong FreeRTOS Ring Buffer
void flushAudioRingBuffer() {
  if (audio_ringbuf == NULL) return;
  size_t item_size = 0;
  void *item = NULL;
  while ((item = xRingbufferReceive(audio_ringbuf, &item_size, 0)) != NULL) {
    vRingbufferReturnItem(audio_ringbuf, item);
  }
}

// --- TASK 2: XỬ LÝ DSP, AUTO-VAD & TÍCH LŨY ÂM THANH SẠCH VÀO PSRAM (CORE 0) ---
void audioProcessTask(void *pvParameters) {
  int16_t pcmChunk[256]; // 256 mẫu 16-bit (512 bytes)

  while (true) {
    // Chỉ chạy VAD khi hệ thống đã sẵn sàng 100% và WiFi đã kết nối
    if (!system_ready || WiFi.status() != WL_CONNECTED) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    bool currentAudioPlaying = audio.isRunning();
    if (wasAudioPlayingLast && !currentAudioPlaying) {
      lastAudioStopTime = millis();
    }
    wasAudioPlayingLast = currentAudioPlaying;

    size_t item_size = 0;
    // Chờ nhận dữ liệu từ Ring Buffer với timeout 100ms
    int32_t *item = (int32_t *)xRingbufferReceive(audio_ringbuf, &item_size, pdMS_TO_TICKS(100));

    if (item != NULL) {
      // Cooldown 350ms sau khi loa vừa phát xong để loại bỏ 100% tiếng pop / xì của IC khuếch đại
      if (millis() - lastAudioStopTime < 350 || isAiBusy) {
        vRingbufferReturnItem(audio_ringbuf, (void *)item);
        vTaskDelay(pdMS_TO_TICKS(10));
        continue;
      }

      if (item_size == 1024) {
        int num_samples = item_size / 4; // 256 mẫu

        // 1. Bộ lọc số Butterworth High-Pass 2nd-order (fc = 350Hz @ 16kHz)
        // Triệt tiêu 100% tiếng gió quạt, tiếng máy lạnh, tiếng rung cơ học (< 300Hz)
        int32_t devSamples[256];
        int64_t sum_sq = 0;
        static float hp_x1 = 0, hp_x2 = 0, hp_y1 = 0, hp_y2 = 0;

        for (int i = 0; i < num_samples; i++) {
          float x0 = (float)((int16_t*)&item[i])[1]; // Đọc high word 16-bit
          float y0 = 0.9069f * x0 - 1.8138f * hp_x1 + 0.9069f * hp_x2 + 1.8055f * hp_y1 - 0.8221f * hp_y2;
          hp_x2 = hp_x1; hp_x1 = x0;
          hp_y2 = hp_y1; hp_y1 = y0;
          devSamples[i] = (int32_t)y0;
          sum_sq += (int64_t)((int32_t)y0) * (int64_t)((int32_t)y0);
        }

        // 2. Tính RMS và Tỷ lệ chuyển mức 0 (Zero Crossing Rate - ZCR)
        // Tiếng gió quạt có tần số thấp (ZCR < 18). Giọng nói người ("Hi Nori") có ZCR cao (> 20)
        float blockRMS = sqrtf((float)((double)sum_sq / num_samples));
        int zcr = 0;
        for (int i = 1; i < num_samples; i++) {
          if ((devSamples[i] >= 0 && devSamples[i-1] < 0) || (devSamples[i] < 0 && devSamples[i-1] >= 0)) {
            zcr++;
          }
        }

        // 3. Logic AGC (Tự động cân bằng âm lượng giọng nói rõ nét)
        float desiredGain = AGC_TARGET_RMS / (blockRMS > 100.0f ? blockRMS : 100.0f);
        if (desiredGain < AGC_MIN_GAIN) desiredGain = AGC_MIN_GAIN;
        if (desiredGain > AGC_MAX_GAIN) desiredGain = AGC_MAX_GAIN;

        agcGain = agcGain * AGC_SMOOTH_OLD + desiredGain * AGC_SMOOTH_NEW;

        // Nhân Gain và chống tràn (Clipping)
        for (int i = 0; i < num_samples; i++) {
          float s = (float)devSamples[i] * agcGain;
          if (s > 32767.0f) s = 32767.0f;
          else if (s < -32768.0f) s = -32768.0f;
          pcmChunk[i] = (int16_t)s;
        }

        // =========================================================
        // 4. QUẢN LÝ AUTO-VAD VÀ TÍNH NĂNG CẮT NGANG TIẾNG (BARGE-IN)
        // =========================================================

        // TRƯỜNG HỢP A: LOA ĐANG PHÁT ÂM THANH (TTS hoặc Nhạc)
        if (audio.isRunning()) {
          extern bool isMusicMode;
          // 1. Khi đang PHÁT NHẠC (isMusicMode == true): Hỗ trợ cướp lời khi gọi "Hi Nori" to rõ
          if (isMusicMode) {
            float bargeInThreshold = max(6800.0f, adaptiveNoiseFloor * 2.2f + 2500.0f);
            if (blockRMS >= bargeInThreshold && zcr >= 25 && zcr <= 85) {
              voice_activity_count++;
              if (voice_activity_count >= 6) { // 96ms có tiếng gọi mạnh mẽ vượt trên tiếng nhạc
                voice_activity_count = 0;
                silence_block_count = 0;
                Serial.println("🛑 [Barge-in] Cướp lời: Dừng phát nhạc và chuyển sang Lắng nghe...");
                hasPendingAudioStop = true; // Dừng nhạc ngay lập tức
                stopMusicScreen();
                isMusicMode = false;
                hasPendingTts = false;
                pendingTtsUrl = "";
                pendingSongUrl = "";
                pendingSongTitle = "";
                isWaitingFollowupCommand = false;
                isAiBusy = false;
                setAIFaceState(AI_STATE_LISTENING);
                setLedMode(2);
                extern void setAIChatDialogue(String userText, String aiText);
                setAIChatDialogue("Dang nghe...", "...");
                uiUpdatePending = true;
                startRecording(false);
              }
            } else {
              if (voice_activity_count > 0) voice_activity_count--;
              adaptiveNoiseFloor = 0.98f * adaptiveNoiseFloor + 0.02f * blockRMS;
            }
          }
          // 2. Khi AI đang NÓI CHUYỆN (TTS): Giữ nguyên để AI nói trọn vẹn 100% câu thoại, không tự ngắt bởi tiếng quạt/tiếng ồn
          // (Người dùng có thể nhấn Nút 3 bất kỳ lúc nào nếu muốn ngắt lời thủ công).
        }
        // TRƯỜNG HỢP B: KHÔNG PHÁT ÂM THANH & ĐANG Ở TRẠNG THÁI CHỜ (STANDBY)
        else if (!isRecording) {
          // Lưu vào bộ đệm xoay vòng 8 blocks (~128ms)
          memcpy(preroll_buffer[preroll_head], pcmChunk, 512);
          preroll_head = (preroll_head + 1) % 8;

          // Cập nhật mức ồn nền phòng (Noise Floor) liên tục khi đang ở trạng thái chờ
          adaptiveNoiseFloor = 0.96f * adaptiveNoiseFloor + 0.04f * blockRMS;
          if (adaptiveNoiseFloor < 30.0f) adaptiveNoiseFloor = 30.0f;
          if (adaptiveNoiseFloor > 800.0f) adaptiveNoiseFloor = 800.0f;

          // Cơ chế chống lặp gián đoạn (Cooldown): Nếu vừa có âm thanh lạ bị lọc bỏ, tạm nghỉ 1.2s
          if (millis() - lastWakeWordCheckFail < 1200) {
            voice_activity_count = 0;
          } else {
            // Ngưỡng phát hiện tiếng gọi Wake-Word ("Hi Nori", "Nori ơi"):
            // Nhạy bén đón nhận giọng nói tự nhiên từ khoảng cách 1-3 mét (RMS >= 380)
            float voiceStartThreshold = max(380.0f, adaptiveNoiseFloor * 1.4f + 120.0f);

            if (blockRMS >= voiceStartThreshold && zcr >= 10 && zcr <= 120 && !isAiBusy) {
              voice_activity_count++;
              if (voice_activity_count >= 3) { // 48ms có năng lượng giọng nói rõ ràng
                voice_activity_count = 0;
                silence_block_count = 0;
                Serial.printf("🎙️ [XiaoZhi-VAD] Phát hiện tiếng gọi! (RMS: %.0f | Ồn nền: %.0f | ZCR: %d). Bắt đầu thu âm xác thực Wake-Word...\n", 
                              blockRMS, adaptiveNoiseFloor, zcr);
                startRecording(false);
              }
            } else {
              if (voice_activity_count > 0) voice_activity_count--;
            }
          }
        }
        // TRƯỜNG HỢP C: ĐANG TRONG QUÁ TRÌNH GHI ÂM CÂU LỆNH (VAD 2 Pha)
        else {
          // 1. Tích lũy âm thanh sạch vào bộ đệm PSRAM
          if (recordBuffer != nullptr) {
            if (recordIndex + 512 <= RECORD_BUFFER_SIZE) {
              memcpy(&recordBuffer[recordIndex], pcmChunk, 512);
              recordIndex += 512;
            } else {
              Serial.println("🛑 [XiaoZhi-VAD] Đạt giới hạn bộ đệm -> Tự ngắt.");
              stopRecording(true);
            }
          }

          // PHA 1: CHỜ NGƯỜI DÙNG BẮT ĐẦU NÓI CÂU LỆNH
          if (!has_started_speaking) {
            // Bỏ qua 300ms đầu tiên sau khi mở mic để lọc sạch tiếng vang phòng hoặc click loa
            if (millis() - recordStartTime >= 300) {
              float voiceStartThreshold = max(320.0f, adaptiveNoiseFloor * 1.4f + 100.0f);
              if (blockRMS >= voiceStartThreshold && zcr >= 12) {
                voice_activity_count++;
                if (voice_activity_count >= 6) { // Yêu cầu 96ms có năng lượng giọng nói thực sự, chống kích hoạt sớm bởi tiếng thở/click
                  has_started_speaking = true;
                  voice_activity_count = 0;
                  silence_block_count = 0;
                  Serial.printf("🗣️ [XiaoZhi-VAD] Đã bắt đầu nhận diện câu lệnh! (RMS: %.0f | ZCR: %d)\n", blockRMS, zcr);
                }
              } else {
                if (voice_activity_count > 0) voice_activity_count--;
              }
            }

            // Hết thời gian chờ im lặng (6 giây rộng rãi) -> Tự hủy về IDLE mượt mà
            if (millis() - recordStartTime >= 6000) {
              Serial.println("⏱️ [XiaoZhi-VAD] Hết thời gian chờ (6s im lặng). Tự động đóng mic về IDLE.");
              cancelRecording();
            }
          }
          // PHA 2: NGƯỜI DÙNG ĐANG NÓI -> THEO DÕI DỨT CÂU
          else {
            // Ngưỡng phát hiện dứt câu / im lặng: Khi âm lượng tụt về gần mức ồn nền phòng
            float voiceEndThreshold = max(180.0f, adaptiveNoiseFloor * 1.15f + 60.0f);

            if (blockRMS < voiceEndThreshold) {
              silence_block_count++;
            } else {
              silence_block_count = 0;
            }

            // TỰ ĐỘNG NGẮT KHI DỨT CÂU:
            // Khi người dùng bấm nút thủ công / hỏi tiếp: Chờ 70 blocks (~1.12 giây) im lặng để người dùng nói chậm rãi, ngắt nghỉ suy nghĩ mà không bị cắt lời!
            // Khi tự động kiểm tra Wake-Word: Chờ 25 blocks (~400ms) im lặng để phản hồi nhanh
            int requiredSilenceBlocks = isManualVoiceTrigger ? 70 : 25;
            size_t minRecordBytes = isManualVoiceTrigger ? 24000 : 16000;

            if (silence_block_count >= requiredSilenceBlocks && recordIndex >= minRecordBytes) {
              Serial.printf("🛑 [XiaoZhi-VAD] Đã nói xong (RMS: %.0f < Ngưỡng: %.0f). Tự động ngắt thu âm!\n", 
                            blockRMS, voiceEndThreshold);
              stopRecording(true);
            }

            // Giới hạn thời gian nói tối đa (2.5s khi tự động Wake-Word, 10s khi nhấn nút)
            unsigned long maxRecDuration = isManualVoiceTrigger ? 10000 : 2500;
            if (millis() - recordStartTime >= maxRecDuration) {
              Serial.printf("🛑 [XiaoZhi-VAD] Đạt thời lượng ghi âm tối đa (%lus). Ngắt thu âm.\n", maxRecDuration / 1000);
              stopRecording(true);
            }
          }
        }
      }

      // Trả lại bộ nhớ cho Ring Buffer LUÔN LUÔN khi item khác NULL
      vRingbufferReturnItem(audio_ringbuf, (void *)item);
      vTaskDelay(pdMS_TO_TICKS(2)); // Nhường CPU tick cho FreeRTOS IDLE0 và Task Watchdog
    } else {
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }
}

// --- CÁC HÀM ĐIỀU KHIỂN HỆ THỐNG GHI ÂM (HYBRID MODE) ---
void startRecording(bool isManual) {
  if (rx_chan == NULL) {
    Serial.println("❌ startRecording thất bại: Mic chưa được khởi tạo!");
    return;
  }

  // Ghi nhận nguồn kích hoạt: Thủ công từ nút cứng / Follow-up hay Tự động Wake-Word
  isManualVoiceTrigger = isManual;

  // Luôn giải phóng cờ bận khi bắt đầu ghi âm
  isAiBusy = false;

  // Dọn sạch rác cũ trong Ring Buffer trước khi thu âm
  flushAudioRingBuffer();

  Serial.printf("🎙️ Bắt đầu ghi âm (%s)...\n", isManual ? "Thủ công (Nút/Followup)" : "Tự động (VAD)");
  
  // 1. Khởi tạo trạng thái
  has_started_speaking = false; // Luôn chờ người dùng thực sự mở miệng nói (chờ tối đa 6 giây)
  recordStartTime = millis();
  silence_block_count = 0;
  voice_activity_count = 0;
  agcGain = 4.5f;
  
  extern void setAIChatDialogue(String userText, String aiText);
  if (isManual) {
    setAIChatDialogue("Dang nghe...", "...");
  }
  
  // 2. Đặt recordIndex = 44 (chừa 44 byte đầu cho WAV Header)
  recordIndex = 44;
  if (recordBuffer) {
    memset(recordBuffer, 0, RECORD_BUFFER_SIZE);
    // Chèn 8 blocks (~128ms) âm thanh thu trước vào đầu file để giữ trọn vẹn âm đầu
    for (int b = 0; b < 8; b++) {
      int idx = (preroll_head + b) % 8;
      memcpy(&recordBuffer[recordIndex], preroll_buffer[idx], 512);
      recordIndex += 512;
    }
  }

  // 3. Bật cờ ghi âm
  isRecording = true;
}

void stopRecording(bool forceProcess) {
  if (!isRecording) return;
  isRecording = false; // Tắt cờ ghi âm (Mic I2S vẫn đọc xả DMA bình thường)

  silence_block_count = 0;
  voice_activity_count = 0;

  Serial.printf("🛑 Kết thúc ghi âm. Dung lượng: %d bytes (Manual: %s)\n", 
                recordIndex, isManualVoiceTrigger ? "YES" : "NO");

  // Ghi 44 byte WAV Header chuẩn vào đầu mảng recordBuffer
  if (recordBuffer != nullptr && recordIndex > 44) {
    createWavHeader(recordBuffer, recordIndex - 44);
  }

  // Tính RMS trung bình của toàn bộ file ghi âm để xác nhận có tiếng nói thật sự
  int64_t total_sq = 0;
  int total_samples = (recordIndex - 44) / 2;
  int16_t *samples = (int16_t *)&recordBuffer[44];
  for (int i = 0; i < total_samples; i++) {
    total_sq += (int64_t)samples[i] * (int64_t)samples[i];
  }
  float avg_rms = (total_samples > 0) ? sqrtf((float)((double)total_sq / total_samples)) : 0.0f;

  // Lọc xung âm thanh tại chỗ (Impulse Filter): Loại bỏ hoàn toàn tiếng vỗ tay, ho, gõ bàn ngắn (<200ms)
  if (!isManualVoiceTrigger && (recordIndex < 14000 || avg_rms < 220.0f)) {
    Serial.printf("⚠️ [Acoustic Filter] Bỏ qua xung âm ngắn/tiếng ồn/vỗ tay (RMS: %.0f, Size: %d bytes). 0 tốn API!\n", 
                  avg_rms, recordIndex);
    cancelRecording();
    return;
  }

  // Gửi mảng âm thanh sang Background Worker Task trên Core 0 nếu người dùng THỰC SỰ ĐÃ NÓI
  if (has_started_speaking && recordIndex > 12000 && avg_rms > 110.0f) {
    if (isManualVoiceTrigger) {
      setAIFaceState(AI_STATE_THINKING);
      setLedMode(1); // Chuyển sang hiệu ứng LED Thinking
      extern String ai_prediction_short;
      ai_prediction_short = "Dang suy nghi...";
      extern void setAIChatDialogue(String userText, String aiText);
      setAIChatDialogue("", "Dang suy nghi...");
      uiUpdatePending = true;
    }
    isAiBusy = true; // Khóa Auto-VAD ngay lập tức để không kích hoạt trùng lặp
    triggerAiAudioProcess(recordBuffer, recordIndex);
  } else {
    Serial.printf("⚠️ [Audio Filter] Bỏ qua âm thanh rỗng/chưa nói câu lệnh (RMS: %.0f, Size: %d bytes, Spoken: %s).\n", 
                  avg_rms, recordIndex, has_started_speaking ? "YES" : "NO");
    cancelRecording();
  }
  has_started_speaking = false;
}

void cancelRecording() {
  if (!isRecording) return;
  bool wasManual = isManualVoiceTrigger;
  isRecording = false;
  recordIndex = 0;
  silence_block_count = 0;
  voice_activity_count = 0;
  has_started_speaking = false;
  isManualVoiceTrigger = false;
  isAiBusy = false;
  
  // Chỉ reset màn hình về IDLE nếu trước đó là chế độ gọi AI (Manual)
  // Nếu là Auto-Wake-Word chạy ngầm không hợp lệ -> giữ nguyên màn hình chính mà không làm gián đoạn
  if (wasManual) {
    setAIFaceState(AI_STATE_IDLE);
    setLedMode(0);
    uiUpdatePending = true;
  }
}

void processAudioLoop() {
  if (audio.isRunning()) {
    audio.loop();
    audio.loop();
  }
}