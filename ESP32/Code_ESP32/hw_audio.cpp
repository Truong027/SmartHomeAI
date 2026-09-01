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
static float agcGain = 3.5f;                 // Gain khởi điểm cân bằng
static const float AGC_TARGET_RMS = 3000.0f; // Mức RMS mong muốn cho giọng nói rõ ràng
static const float AGC_MIN_GAIN = 1.0f;
static const float AGC_MAX_GAIN = 8.0f;      // Khuếch đại giọng nói ở xa lên 8x chuẩn xác, không kéo nhiễu nền
static const float AGC_SMOOTH_OLD = 0.88f;
static const float AGC_SMOOTH_NEW = 0.12f;

// Bộ theo dõi mức ồn nền phòng tự động thích ứng (Adaptive Noise Floor)
static float adaptiveNoiseFloor = 100.0f; 

// Số block im lặng liên tiếp để ngắt (~0.6 giây ở 16ms/block)
static const int VAD_SILENCE_TRIGGER_BLOCKS = 30; 
static int silence_block_count = 0;
static int voice_activity_count = 0;

// Bộ đệm xoay vòng lưu trước 10 blocks (~160ms) âm thanh để không bị nuốt âm đầu ("Hi", "Xin")
static int16_t preroll_buffer[10][256];
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

static StaticTask_t *audioTaskBuffer = NULL;
static StackType_t *audioTaskStack = NULL;

void setupAudio() {
  // 1. Cấu hình phần cứng Loa I2S DAC (MAX98357A: BCLK 15, LRC 16, DIN 17 trên I2S_NUM_0)
  audio.setPinout(I2S_AMP_BCLK, I2S_AMP_LRC, I2S_AMP_DIN);
  audio.setVolume(21);
  audio.setConnectionTimeout(3000, 5000);
  Serial.println("🔊 Đã cấu hình phần cứng Loa I2S (MAX98357A).");

  // 2. Cấp phát bộ đệm recordBuffer trong PSRAM cho Groq STT
  recordBuffer = (uint8_t *)ps_malloc(RECORD_BUFFER_SIZE);
  // Cấp phát Ring Buffer 16KB ưu tiên trong PSRAM (SPIRAM) để giải phóng triệt để SRAM nội bộ cho TLS/SSL
  audio_ringbuf = xRingbufferCreateWithCaps(16 * 1024, RINGBUF_TYPE_BYTEBUF, MALLOC_CAP_SPIRAM);
  if (audio_ringbuf == NULL) {
    audio_ringbuf = xRingbufferCreate(8 * 1024, RINGBUF_TYPE_BYTEBUF);
  }
  if (audio_ringbuf == NULL) {
    Serial.println("❌ Lỗi cấp phát FreeRTOS Ring Buffer!");
    return;
  }
  Serial.println("✅ Cấp phát thành công FreeRTOS Ring Buffer (PSRAM Optimized)");

  // 4. Bật và cấu hình I2S Mic INMP441 trên kênh I2S_NUM_1 (Độc lập 100% với Loa I2S_NUM_0)
  if (rx_chan == NULL) {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 6;
    chan_cfg.dma_frame_num = 256;
    chan_cfg.auto_clear = true;
    if (i2s_new_channel(&chan_cfg, NULL, &rx_chan) != ESP_OK) {
      Serial.println("❌ Lỗi tạo kênh I2S RX!");
      return;
    }
  }

  i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000), // 16kHz
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
  std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

  if (i2s_channel_init_std_mode(rx_chan, &std_cfg) != ESP_OK) {
    Serial.println("❌ Lỗi cấu hình I2S chuẩn!");
    return;
  }

  if (i2s_channel_enable(rx_chan) != ESP_OK) {
    Serial.println("❌ Lỗi bật kênh I2S (Mic)!");
    return;
  }
  Serial.println("✅ Mic I2S (INMP441) đã mở kênh Always-On.");

  // Tạo Task Đọc Mic (Core 0, Ưu tiên 2, Stack 2048 bytes)
  xTaskCreatePinnedToCore(
      micReadTask,
      "micReadTask",
      2048,
      NULL,
      2,
      &micReadTaskHandle,
      0
  );

  // Cấp phát Stack của audioProcessTask trong 8MB PSRAM để giải phóng triệt để SRAM nội bộ!
  audioTaskBuffer = (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  audioTaskStack = (StackType_t *)heap_caps_malloc(8192 * sizeof(StackType_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

  if (audioTaskBuffer != NULL && audioTaskStack != NULL) {
    audioProcessTaskHandle = xTaskCreateStaticPinnedToCore(
      audioProcessTask,
      "audioProcessTask",
      8192,
      NULL,
      1,
      audioTaskStack,
      audioTaskBuffer,
      0
    );
    Serial.println("✅ [Audio DSP] Đã cấp phát audioProcessTask Stack (8KB) trong PSRAM thành công!");
  } else {
    xTaskCreatePinnedToCore(audioProcessTask, "audioProcessTask", 3072, NULL, 1, &audioProcessTaskHandle, 0);
  }
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
        // Đẩy liên tục vào Ring Buffer khi hệ thống sẵn sàng và AI không bận
        if (system_ready && WiFi.status() == WL_CONNECTED && !isAiBusy) {
          if (audio_ringbuf != NULL) {
            xRingbufferSend(audio_ringbuf, tempBuffer, bytesRead, pdMS_TO_TICKS(5));
          }
        }
      }
    } else {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelay(pdMS_TO_TICKS(2)); // Nhường CPU cho Task AI Worker và TLS Handshake
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
      flushAudioRingBuffer();
      // Xóa sạch toàn bộ bộ đệm pre-roll tiếng loa còn sót lại
      memset(preroll_buffer, 0, sizeof(preroll_buffer));
      silence_block_count = 0;
      voice_activity_count = 0;
      has_started_speaking = false;
    }
    wasAudioPlayingLast = currentAudioPlaying;

    size_t item_size = 0;
    // Chờ nhận dữ liệu từ Ring Buffer với timeout 100ms
    int32_t *item = (int32_t *)xRingbufferReceive(audio_ringbuf, &item_size, pdMS_TO_TICKS(100));

    if (item != NULL) {
      // Cooldown ngắn 350ms sau khi loa vừa dừng hoặc khi AI đang bận xử lý STT/LLM
      if (isAiBusy || (millis() - lastAudioStopTime < 350)) {
        vRingbufferReturnItem(audio_ringbuf, (void *)item);
        vTaskDelay(pdMS_TO_TICKS(5));
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
        // 4. QUẢN LÝ AUTO-VAD VÀ TÍNH NĂNG CẮT NGANG TIẾNG (BREAKWORD / BARGE-IN)
        // =========================================================

        // TRƯỜNG HỢP A: LOA ĐANG PHÁT ÂM THANH (TTS hoặc Nhạc)
        if (audio.isRunning()) {
          // 1. Theo dõi mức âm lượng thích ứng của chính chiếc loa phát ra mic (Acoustic Echo Tracking)
          static float speakerNoiseLevel = 2000.0f;
          speakerNoiseLevel = 0.95f * speakerNoiseLevel + 0.05f * blockRMS;
          if (speakerNoiseLevel < 800.0f) speakerNoiseLevel = 800.0f;

          // 2. Ngưỡng cướp lời (Barge-in): Yêu cầu giọng người nói to, tạo bước nhảy năng lượng (Energy Spike) vượt trên tiếng loa hiện tại
          float bargeInThreshold = max(4800.0f, speakerNoiseLevel * 1.65f + 1800.0f);
          
          // Kiểm tra tần số giọng nói người (ZCR 18 - 95)
          if (blockRMS >= bargeInThreshold && zcr >= 18 && zcr <= 95) {
            voice_activity_count++;
            if (voice_activity_count >= 5) { // 80ms năng lượng giọng nói người lấn át hoàn toàn tiếng loa
              voice_activity_count = 0;
              silence_block_count = 0;
              speakerNoiseLevel = 1000.0f;
              Serial.printf("🛑 [Breakword Barge-in] PHÁT HIỆN GIỌNG NÓI CƯỚP LỜI! (RMS: %.0f > Ngưỡng Loa: %.0f | ZCR: %d). Ngắt loa ngay!\n", 
                            blockRMS, bargeInThreshold, zcr);
              hasPendingAudioStop = true;
              if (audio.isRunning()) {
                audio.stopSong();
              }
              stopMusicScreen();
              extern bool isMusicMode;
              isMusicMode = false;
              hasPendingTts = false;
              pendingTtsUrl = "";
              pendingSongUrl = "";
              pendingSongTitle = "";
              isWaitingFollowupCommand = false;
              isAiBusy = false;
              setAIFaceState(AI_STATE_LISTENING);
              setLedMode(2); // LED Mode 2: Cyan Listening
              extern void setAIChatDialogue(String userText, String aiText);
              setAIChatDialogue("Dang nghe...", "...");
              requestScreen(SCREEN_AI); // Chuyển an toàn sang màn hình AI không bị pha trộn
              uiUpdatePending = true;
              flushAudioRingBuffer();
              startRecording(true); // Mở mic thu âm câu lệnh mới ngay lập tức
            }
          } else {
            if (voice_activity_count > 0) voice_activity_count--;
          }
        }
          // TRƯỜNG HỢP B: KHÔNG PHÁT ÂM THANH & ĐANG Ở TRẠNG THÁI CHỜ (STANDBY)
        else if (!isRecording) {
          // Lưu vào bộ đệm xoay vòng 10 blocks (~160ms)
          memcpy(preroll_buffer[preroll_head], pcmChunk, 512);
          preroll_head = (preroll_head + 1) % 10;

          // Cập nhật mức ồn nền phòng (Noise Floor) liên tục khi đang ở trạng thái chờ
          adaptiveNoiseFloor = 0.96f * adaptiveNoiseFloor + 0.04f * blockRMS;
          if (adaptiveNoiseFloor < 40.0f) adaptiveNoiseFloor = 40.0f;
          if (adaptiveNoiseFloor > 600.0f) adaptiveNoiseFloor = 600.0f;

          // Cơ chế chống lặp gián đoạn (Cooldown): Nếu vừa có âm thanh lạ bị lọc bỏ, tạm nghỉ 2.0s
          if (millis() - lastWakeWordCheckFail < 2000) {
            voice_activity_count = 0;
          } else {
            // Ngưỡng phát hiện tiếng gọi Wake-Word ("Hi Nori", "Nori ơi") CHUẨN XÁC, CHỐNG TIẾNG ỒN QUẠT/PHÒNG:
            float voiceStartThreshold = max(520.0f, adaptiveNoiseFloor * 1.6f + 220.0f);

            if (blockRMS >= voiceStartThreshold && zcr >= 16 && zcr <= 110 && !isAiBusy) {
              voice_activity_count++;
              if (voice_activity_count >= 5) { // 80ms có năng lượng giọng nói thực sự, loại bỏ 100% tiếng động cơ khí/tiếng ồn giật
                voice_activity_count = 0;
                silence_block_count = 0;
                Serial.printf("🎙️ [XiaoZhi-VAD] Phát hiện tiếng gọi! (RMS: %.0f | Ồn nền: %.0f | ZCR: %d). Bắt đầu thu âm...\n", 
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
            // Bỏ qua 150ms đầu tiên sau khi mở mic để lọc sạch click loa
            if (millis() - recordStartTime >= 150) {
              float voiceStartThreshold = max(380.0f, adaptiveNoiseFloor * 1.4f + 140.0f);
              if (blockRMS >= voiceStartThreshold && zcr >= 15) {
                voice_activity_count++;
                if (voice_activity_count >= 3) { // 48ms là xác nhận đang nói
                  has_started_speaking = true;
                  voice_activity_count = 0;
                  silence_block_count = 0;
                  Serial.printf("🗣️ [XiaoZhi-VAD] Đã bắt đầu nhận diện câu lệnh! (RMS: %.0f | ZCR: %d)\n", blockRMS, zcr);
                }
              } else {
                if (voice_activity_count > 0) voice_activity_count--;
              }
            }

            // Hết thời gian chờ im lặng (4.5s khi đối thoại liên tục/nhấn nút, 2.2s khi Auto-VAD) -> Tự động chuyển về IDLE
            unsigned long maxWaitStart = isManualVoiceTrigger ? 4500 : 2200;
            if (millis() - recordStartTime >= maxWaitStart) {
              Serial.println("⏱️ [Continuous Dialogue] Hết 4.5s im lặng -> Tự động đóng Mic và chuyển về màn hình IDLE.");
              cancelRecording();
            }
          }
          // PHA 2: NGƯỜI DÙNG ĐANG NÓI -> THEO DÕI DỨT CÂU SIÊU TỐC
          else {
            float voiceEndThreshold = isManualVoiceTrigger 
                                      ? max(240.0f, adaptiveNoiseFloor * 1.2f + 60.0f)
                                      : max(300.0f, adaptiveNoiseFloor * 1.3f + 80.0f);

            if (blockRMS < voiceEndThreshold) {
              silence_block_count++;
            } else {
              silence_block_count = 0;
            }

            // TỰ ĐỘNG NGẮT KHI DỨT CÂU SIÊU TỐC:
            // Khi tự động Wake-Word: Chờ 12 blocks (~192ms) im lặng là ngắt và gửi xử lý ngay!
            int requiredSilenceBlocks = isManualVoiceTrigger ? 30 : 12;
            size_t minRecordBytes = isManualVoiceTrigger ? 8000 : 8000;

            if (silence_block_count >= requiredSilenceBlocks && recordIndex >= minRecordBytes) {
              Serial.printf("🛑 [XiaoZhi-VAD] Đã dứt câu (RMS: %.0f < Ngưỡng: %.0f | Size: %d bytes). Ngắt thu âm gửi ngay!\n", 
                            blockRMS, voiceEndThreshold, recordIndex);
              stopRecording(true);
            }

            // Giới hạn thời gian nói tối đa (1.8s khi tự động Wake-Word, 7s khi nói câu lệnh)
            unsigned long maxRecDuration = isManualVoiceTrigger ? 7000 : 1800;
            if (millis() - recordStartTime >= maxRecDuration) {
              Serial.printf("🛑 [XiaoZhi-VAD] Đạt thời lượng ghi âm tối đa (%lus | %d bytes). Ngắt thu âm.\n", 
                            maxRecDuration / 1000, recordIndex);
              stopRecording(true);
            }
          }
        }
      }

      // Trả lại bộ nhớ cho Ring Buffer LUÔN LUÔN khi item khác NULL
      vRingbufferReturnItem(audio_ringbuf, (void *)item);
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
  
  if (isRecording) {
    return;
  }

  // Đảm bảo dừng hoàn toàn loa và giải phóng socket mạng của Audio
  if (audio.isRunning()) {
    audio.stopSong();
  }

  isManualVoiceTrigger = isManual;
  isRecording = true;
  recordStartTime = millis();
  recordIndex = 44; // Để dành 44 bytes đầu tiên cho WAV Header
  voice_activity_count = 0;
  silence_block_count = 0;
  has_started_speaking = false;
  
  // NẠP TRƯỚC ÂM THANH (PRE-ROLL) 10 BLOCKS (~160ms) ĐỂ KHÔNG MẤT ÂM ĐẦU CÂU ("Hi", "Bật", "Nori")
  if (recordBuffer != nullptr) {
    int idx = preroll_head;
    for (int b = 0; b < 10; b++) {
      memcpy(&recordBuffer[recordIndex], preroll_buffer[idx], 512);
      recordIndex += 512;
      idx = (idx + 1) % 10;
    }
  }

  Serial.println(isManual ? "🎙️ [Hybrid Mic] BẮT ĐẦU GHI ÂM THỦ CÔNG (Manual Button)..." 
                          : "🎙️ [Hybrid Mic] BẮT ĐẦU GHI ÂM TỰ ĐỘNG (Auto Wake-Word VAD)...");
}

void stopRecording(bool sendToAI) {
  if (!isRecording) return;
  isRecording = false;

  if (!sendToAI || recordIndex <= 44) {
    Serial.println("⏹️ [Hybrid Mic] Đã hủy phiên ghi âm (0 bytes âm thanh).");
    return;
  }

  // Ghi chuẩn WAV Header 16kHz, 16-bit, Mono vào 44 bytes đầu
  createWavHeader(recordBuffer, recordIndex - 44);

  // Tính RMS trung bình của toàn bộ file ghi âm
  int64_t total_sq = 0;
  int total_samples = (recordIndex - 44) / 2;
  int16_t *samples = (int16_t *)&recordBuffer[44];
  for (int i = 0; i < total_samples; i++) {
    total_sq += (int64_t)samples[i] * (int64_t)samples[i];
  }
  float avg_rms = (total_samples > 0) ? sqrtf((float)((double)total_sq / total_samples)) : 0.0f;

  Serial.printf("🛑 Kết thúc ghi âm. Dung lượng: %d bytes | RMS TB: %.0f (Manual: %s | Spoken: %s)\n", 
                recordIndex, avg_rms, isManualVoiceTrigger ? "YES" : "NO", has_started_speaking ? "YES" : "NO");

  // BỘ LỌC TẠI CHỖ (ACOUSTIC FILTER): Lọc sạch tiếng ồn nền/tiếng thở/vỗ tay
  bool isValidAudio = isManualVoiceTrigger 
                      ? (has_started_speaking && recordIndex >= 8000)
                      : (has_started_speaking && recordIndex >= 14000 && avg_rms >= 220.0f);

  // Gửi mảng âm thanh sang Background Worker Task trên Core 0 nếu người dùng THỰC SỰ ĐÃ NÓI
  if (isValidAudio) {
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
    Serial.printf("⚠️ [Audio Filter] Bỏ qua âm thanh rỗng/nhiễu phòng (RMS: %.0f, Size: %d bytes, Spoken: %s). 0 tốn API!\n", 
                  avg_rms, recordIndex, has_started_speaking ? "YES" : "NO");
    cancelRecording();
    lastWakeWordCheckFail = millis(); // Kích hoạt Cooldown 2.0s để không lặp lại liên tục
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
  
  // Tự động chuyển về trạng thái IDLE sẵn sàng khi hết thời gian chờ
  if (wasManual) {
    setAIFaceState(AI_STATE_IDLE);
    setLedMode(0);
    requestScreen(SCREEN_AI);
    extern void setAIChatDialogue(String userText, String aiText);
    setAIChatDialogue("San sang...", "...");
    uiUpdatePending = true;
    Serial.println("⏱️ [Continuous Dialogue] Không phát hiện câu hỏi tiếp theo -> Tự động chuyển về trạng thái IDLE sẵn sàng!");
  }
}

void processAudioLoop() {
  audio.loop();
}