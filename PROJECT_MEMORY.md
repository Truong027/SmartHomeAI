# 🧠 BỘ NHỚ LÂU DÀI & QUY TẮC THIẾT KẾ TOÀN DIỆN (PROJECT MEMORY)

Tài liệu này lưu trữ toàn bộ các thông số kỹ thuật, cấu hình phần cứng, các thuật toán đã tinh chỉnh và bộ nhớ học tập của AI cho hệ thống **Nori Smart Home ESP32**. Mọi phiên làm việc tiếp theo của AI đều tự động nạp và ghi nhớ các quy tắc này.

---

## 📌 1. Cấu hình Phần cứng & Tinh chỉnh Đèn LED
* **Vi điều khiển**: ESP32-S3 (Dual Core 240MHz).
* **Đèn LED Vòng tròn WS2812B (Chân D42)**:
  * **Số lượng bóng**: **Chính xác 12 bóng LED** (`#define NUM_LEDS 12`).
  * **Thuật toán chuyển động**: Sub-pixel Floating Point Motion (50 FPS, chu kỳ ~2.5 giây/vòng).
  * **Đường cong suy giảm ánh sáng**: Cubic Smoothstep (`norm * norm * (3.0f - 2.0f * norm)`) với độ sáng nền dịu nhẹ (3% ambient glow).
  * **Hiệu ứng**: Lướt sao băng mềm mại, không chớp giật, không đứt đoạn chu kỳ.
* **Màn hình**: ST7735 SPI 1.8 inch (160x128 pixel, Rotation 1).
* **Microphone & Loa I2S**:
  * INMP441: SCK = 4, WS = 5, SD = 6.
  * MAX98357A: BCLK = 15, LRC = 16, DIN = 17.
* **Cảm biến I2C (SDA = 8, SCL = 9)**:
  * AHT20/30 (Nhiệt độ & Độ ẩm thực tế).
  * BMP280 (Áp suất khí quyển).
  * RTC DS3231 (Thời gian thực).

---

## 🌙 2. Thuật toán Âm lịch Thiên văn Chuẩn Việt Nam (GMT+7)
* **Giải thuật**: Thiên văn Hồ Ngọc Đức / Jean Meeus có **hiệu chuẩn ranh giới nửa đêm 0.98**:
  ```cpp
  double val = JdNew + 0.5 + timeZone / 24.0;
  if ((val - floor(val)) > 0.98) {
    return (long)(floor(val) + 1);
  }
  return (long)floor(val);
  ```
* **Mốc chuẩn xác thực**:
  * Dương lịch 13/08/2026 = 01/07/2026 Âm lịch (Mùng 1).
  * Dương lịch 14/08/2026 = 02/07/2026 Âm lịch (Mùng 2).
  * **Dương lịch 15/08/2026 = 03/07/2026 Âm lịch (Mùng 3, Tân Dậu, năm Bính Ngọ)**.

---

## 🎵 3. Cơ chế Phát nhạc Trực tuyến & Âm thanh
* **Luồng Stream CDN**: Kết nối trực tiếp link CDN CloudFront MP3 từ Vercel Backend (`/api/music`).
* **Hàng đợi thông minh**:
  1. Khi nhận lệnh mở nhạc, AI phát câu chào TTS *"Dạ em đang tìm và phát bài [X] cho bạn đây nè!"*.
  2. Gán `pendingSongTitle = songQuery; isMusicMode = true;`.
  3. Khi câu TTS kết thúc, hệ thống tự động gọi API `searchMusicUrl` và kích hoạt stream MP3 mượt mà.
  4. Timer bảo vệ TTS không can thiệp ngắt nhạc đang phát.

---

## 🔍 4. Công cụ Tìm kiếm Đa nguồn & Trả lời Khoa học Logic
* **Backend Endpoint**: `https://vercel-backend-woad-seven.vercel.app/api/search`.
* **Nguồn dữ liệu Bách khoa**: Wikipedia Tiếng Việt REST API (`https://vi.wikipedia.org/api/rest_v1/page/summary/`).
* **Phân loại sinh học chuẩn xác 100%**: Ví dụ tra cứu *"con rắn"* trả về phân bộ *Serpentes*, bò sát ăn thịt không chân, có màng ối, có xương sống.
* **Quy chuẩn TTS**: Toàn bộ câu trả lời dạng 1 đoạn văn liền mạch, không dấu Enter, không markdown (*, #).

---

## 🧠 5. Bộ nhớ Lâu dài (Persistent Long-term Memory) của AI
* **Vị trí lưu trữ**: Flash NVS `Preferences` (Namespace `"ai_mem"`) & Firebase `/ESP32_AI_Hub/ai_memory/`.
* **Dữ liệu được lưu giữ**:
  * `userName`: Tên của người dùng (mặc định "Trường").
  * `aiName`: Tên của AI (mặc định "Nori").
  * `userFacts`: Sở thích, thói quen, thông tin cá nhân.
  * `customNotes`: Các dặn dò, ghi chú nhắc nhở.
* **Cơ chế tự học & ghi nhớ**: Khi người dùng nói *"Hãy nhớ tôi thích nghe bài Lạc trôi"*, *"Tên tôi là An"*, *"Ghi nhớ là..."*, AI tự động trích xuất thông tin và ghi vào Flash + Firebase. Khi khởi động lại máy, AI vẫn nhớ nguyên vẹn!
