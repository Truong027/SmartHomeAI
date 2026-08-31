# NORI SMART HOME - WORKSPACE ARCHITECTURE & CODING RULES

Tài liệu này là bộ nhớ kiến trúc và quy tắc vĩnh cửu của dự án Smart Home ESP32. Bất kỳ AI Assistant nào làm việc trong dự án này BẮT BUỘC phải tuân thủ và ghi nhớ các quy tắc sau:

---

## 1. CẤU HÌNH PHẦN CỨNG VI ĐIỀU KHIỂN (HARDWARE SPECS)

- **Vi điều khiển chính**: ESP32-S3 (Dual-core 240MHz, 8MB Flash).
- **Màn hình hiển thị**: TFT ST7735 SPI 1.8 inch (160x128 pixel, Rotation 1), đồ họa LVGL / Direct TFT.
- **Đèn LED Vòng**: **12 bóng WS2812B** (Chân D42). 
  - *QUY TẮC BẮT BUỘC*: `NUM_LEDS` luôn là `12` (không phải 16).
  - Thuật toán chuyển động: Sub-pixel Floating Point Motion (Cubic Smoothstep, 50 FPS) lướt nhẹ nhàng ~2.5s/vòng.
- **Microphone I2S**: INMP441 (SCK: GPIO 4, WS: GPIO 5, SD: GPIO 6).
- **Mạch khuếch đại loa I2S**: MAX98357A (BCLK: GPIO 15, LRC: GPIO 16, DIN: GPIO 17).
- **Cảm biến I2C (SDA: 8, SCL: 9)**:
  - RTC DS3231 (Địa chỉ 0x68): Đồng hồ thời gian thực và lịch thiên văn.
  - AHT20 / AHT30 (Địa chỉ 0x38): Nhiệt độ & Độ ẩm phòng thực tế.
  - BMP280 (Địa chỉ 0x76 hoặc 0x77): Áp suất khí quyển.
- **Bộ thu phát hồng ngoại**: IR Send (GPIO 18), IR Recv (GPIO 19) - Điều khiển điều hòa Daikin ARC433/ARC480.
- **Nút cảm ứng / Nút cứng**: Touch 1 (GPIO 1), Touch 2 (GPIO 2), Touch 3 (GPIO 3).
- **Relay**: Relay 1 (GPIO 39 - Khóa điện 12V), Relay 2 (GPIO 40 - Quạt/Đèn).

---

## 2. THUẬT TOÁN ÂM LỊCH THIÊN VĂN (VIETNAMESE LUNAR CALENDAR)

- **Múi giờ**: GMT+7 (Asia/Ho_Chi_Minh).
- **Thuật toán**: Giải thuật thiên văn Hồ Ngọc Đức / Jean Meeus.
- **Hiệu chuẩn ranh giới ngày**:
  ```cpp
  double val = JdNew + 0.5 + timeZone / 24.0;
  if ((val - floor(val)) > 0.98) {
    return (long)(floor(val) + 1);
  }
  return (long)floor(val);
  ```
- **Xác thực mốc chuẩn**: Ngày 15/08/2026 dương lịch $\rightarrow$ **Ngày 03/07/2026 âm lịch** (Tân Dậu, năm Bính Ngọ).

---

## 3. CƠ CHẾ ÂM THANH & PHÁT NHẠC (AUDIO & STREAMING)

- Sử dụng thư viện `ESP32-audioI2S` nối trực tiếp đến link CDN CloudFront MP3 tốc độ cao (không dùng chuỗi Base64 dài gây tràn RAM).
- Khi người dùng ra lệnh phát nhạc:
  1. AI phản hồi câu chào TTS: *"Dạ em đang tìm và phát bài [Tên bài] cho bạn đây nè!"*.
  2. Đặt `pendingSongTitle = songQuery; isMusicMode = true;`.
  3. Khi câu TTS kết thúc, hệ thống tự động gọi API `searchMusicUrl` và kích hoạt luồng phát nhạc mượt mà.
  4. Bộ đếm thời gian TTS không được ngắt dòng nhạc đang phát.

---

## 4. TÌM KIẾM TRI THỨC & TRẢ LỜI KHOA HỌC (CLOUD SEARCH ENGINE)

- **Backend Endpoint**: `https://vercel-backend-woad-seven.vercel.app/api/search`.
- **Nguồn tri thức**:
  - Khoa học, Sinh học, Động thực vật, Y dược, Khái niệm: **Bách khoa toàn thư Wikipedia Tiếng Việt REST API** (`/api/rest_v1/page/summary/`) + DuckDuckGo Instant Answer.
  - Tin tức thời sự: Google News RSS + Tuổi Trẻ / VnExpress.
  - Dự báo thời tiết: OpenWeatherMap.
- Trả lời đầy đủ, logic, chính xác phân loại sinh học (ví dụ: con rắn thuộc phân bộ *Serpentes*, bò sát có vảy, không chân, có màng ối).
- Toàn bộ nội dung trả về cho ESP32 TTS phải ở định dạng 1 đoạn văn liền mạch, không dấu xuống dòng (Enter), không ký tự đặc biệt (*, #).

---

## 5. BỘ NHỚ LÂU DÀI CỦA AI (LONG-TERM PERSISTENT MEMORY)

- Lưu trữ trong bộ nhớ Flash `Preferences` (NVS) và đồng bộ Firebase `/smart_home/ai_memory`.
- Ghi nhớ: Tên người dùng, sở thích, bài hát yêu thích, thói quen, ghi chú nhắc nhở.
- Tự động trích xuất thông tin người dùng dặn dò trong hội thoại để cập nhật vào bộ nhớ lâu dài.
