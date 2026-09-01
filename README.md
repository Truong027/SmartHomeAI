# 🏠 NORI SMART HOME AI - NEXT-GEN EMBEDDED AI ASSISTANT

<div align="center">

![ESP32-S3](https://img.shields.io/badge/Microcontroller-ESP32--S3_Dual--Core_240MHz-red?style=for-the-badge&logo=espressif)
![LVGL](https://img.shields.io/badge/GUI_Engine-LVGL_v9-orange?style=for-the-badge&logo=cplusplus)
![Groq](https://img.shields.io/badge/LLM_Engine-Groq_AI_Cloud-blue?style=for-the-badge&logo=fastapi)
![Firebase](https://img.shields.io/badge/Cloud_Database-Firebase_Realtime-yellow?style=for-the-badge&logo=firebase)
![License](https://img.shields.io/badge/License-MIT-green?style=for-the-badge)

**Hệ sinh thái Trợ lý Nhà Thông Minh AI Nhúng Thế Hệ Mới trên nền tảng ESP32-S3**  
*Tích hợp Thị giác Đồ họa LVGL • Trò chuyện Ngữ cảnh Đa Tầng • Học Lệnh Điều Hòa Hồng Ngoại • Âm Lịch Thiên Văn • Phát Nhạc Hi-Fi MP3 Streaming*

</div>

---

## 📖 MỤC LỤC
1. [Giới thiệu tổng quan](#-giới-thiệu-tổng-quan)
2. [Sơ đồ chân phần cứng (Hardware Pinout)](#-sơ-đồ-chân-phần-cứng-hardware-pinout)
3. [Tính năng đột phá](#-tính-năng-đột-phá)
4. [Cấu trúc mã nguồn](#-cấu-trúc-mã-nguồn)
5. [Hướng dẫn cài đặt & Nạp Firmware](#-hướng-dẫn-cài-đặt--nạp-firmware)
6. [Bảng khẩu lệnh giọng nói mẫu](#-bảng-khẩu-lệnh-giọng-nói-mẫu)

---

## 🌟 GIỚI THIỆU TỔNG QUAN

**Nori Smart Home AI** là dự án trạm điều khiển nhà thông minh độc lập kết hợp trí tuệ nhân tạo thế hệ mới (Next-Gen AI Companion), được thiết kế tối ưu trên vi điều khiển **ESP32-S3 (16MB Flash, 8MB PSRAM)**:
- **Đồ họa nhúng đỉnh cao**: Màn hình TFT 1.8 inch ST7735 vận hành bởi engine **LVGL** với các hiệu ứng kính mờ (Glassmorphism), biểu cảm robot Nori sống động và bộ cân bằng âm thanh 9-bar Equalizer.
- **Trí tuệ nhân tạo siêu tốc**: Kết nối trực tiếp tới đám mây **Groq Cloud (Llama 3 / GPT-OSS 20B)** mang lại tốc độ phản hồi thần tốc **< 250ms**, hiểu sâu ngữ cảnh tiếng Việt có dấu.
- **Hội thoại đa tầng (Continuous Multi-turn Dialogue)**: Tự động mở mic chờ 4.5 giây sau mỗi câu trả lời, cho phép đàm thoại tự nhiên liên tục mà không cần nhắc lại từ khóa kích hoạt.
- **Phát nhạc trực tuyến Hi-Fi**: Tìm kiếm bài hát thông minh qua máy chủ Vercel Serverless Backend và phát trực tiếp luồng âm thanh MP3 CDN qua I2S DAC MAX98357A.

---

## 🔌 SƠ ĐỒ CHÂN PHẦN CỨNG (HARDWARE PINOUT)

| Khối Chức Năng | Tên Linh Kiện | Chân ESP32-S3 | Ghi Chú Kỹ Thuật |
|---|---|---|---|
| **Vi điều khiển** | ESP32-S3 N16R8 | — | Dual-core 240MHz, 16MB Flash, 8MB Octal PSRAM |
| **Màn hình TFT** | ST7735 1.8" SPI (160x128) | MOSI: `11`, SCLK: `12`, CS: `10`, DC: `14`, RST: `13`, BL: `47` | Rotation 1, PWM Backlight 5kHz |
| **Vòng LED RGB** | 12 x WS2812B Ring | Data: **`GPIO 42`** | 12 bóng LED NeoPixel, Smoothstep Motion |
| **Microphone I2S** | INMP441 Digital Mic | SCK: `GPIO 4`, WS: `GPIO 5`, SD: `GPIO 6` | Thu âm 16kHz, 16-bit Mono, VAD 2 pha |
| **Mạch Loa I2S** | MAX98357A I2S DAC | BCLK: `GPIO 15`, LRC: `GPIO 16`, DIN: `GPIO 17` | Âm lượng phần cứng 21 mức, Stream MP3/TTS |
| **Cảm biến I2C** | RTC DS3231, AHT20/30, BMP280 | SDA: **`GPIO 8`**, SCL: **`GPIO 9`** | RTC (0x68), AHT20 (0x38), BMP280 (0x76/0x77) |
| **Hồng ngoại IR** | Mắt phát IR / Mắt thu IR | Send: `GPIO 18`, Recv: `GPIO 19` | Hỗ trợ điều hòa Daikin ARC433/ARC480 & Học lệnh |
| **Relay Đầu Ra** | Module Relay 2 Kênh | Relay 1: `GPIO 39`, Relay 2: `GPIO 40` | Relay 1 (Khóa điện 12V), Relay 2 (Đèn/Quạt) |
| **Nút Cảm Ứng** | Touch 1, 2, 3 | Touch 1: `GPIO 1`, Touch 2: `GPIO 2`, Touch 3: `GPIO 3` | Điều khiển cảm ứng điện dung trực tiếp |

---

## 🚀 TÍNH NĂNG ĐỘT PHÁ

### 1. 🎙️ Hội thoại đa tầng & Cắt lời thông minh (Continuous Dialogue & Barge-in)
- Tự động duy trì trạng thái lắng nghe trong **4.5 giây** sau khi AI nói xong.
- Tính năng **Breakword Barge-in**: Tự động theo dõi mức ồn của loa (`speakerNoiseLevel`) $\rightarrow$ Bạn có thể cất giọng nói cắt ngang ngay khi loa đang phát nhạc hoặc đang nói TTS để ra lệnh mới.

### 2. ⚡ Hệ thống Multi-Key Groq Load Balancing (Zero 429 Error)
- Cơ chế **Xoay vòng đa API Key (Multi-Key Rotation & Auto-Failover 0ms)**: Luân chuyển đều tải giữa 3 API Keys, hỗ trợ tới **43.200 lượt gọi/ngày** và **90 câu hỏi/phút** mà không bao giờ bị nghẽn mạng `429 Too Many Requests`.

### 3. 🌙 Thuật toán Âm Lịch Thiên Văn Jean Meeus / Hồ Ngọc Đức
- Tính toán chính xác 100% ngày âm lịch, tháng nhuận, can chi ngày/tháng/năm theo tọa độ thiên văn múi giờ GMT+7 Việt Nam.

### 4. 🎨 Giao diện Màn hình Hallmark & Spectrum Equalizer
- Top Capsule `NOW PLAYING • HI-FI`, hiệu ứng chuyển động cột sóng âm nhạc 9-bar Equalizer.
- Bộ lọc `removeVietnameseAccents` hiển thị ký tự mượt mà, chống lỗi font ASCII ô vuông trên màn hình TFT nhúng.

### 5. 🌐 Web App Quản trị Glassmorphism & Đồng bộ Firebase
- Giao diện Web tương thích điện thoại/máy tính, điều khiển Relay, đổi màu LED, chỉnh điều hòa Daikin và xem biểu đồ thời gian thực.

---

## 📁 CẤU TRÚC MÃ NGUỒN

```text
smart-home-esp32/
├── ESP32/
│   └── Code_ESP32/
│       ├── Code_ESP32.ino          # Chương trình chính (Setup, Loop, FreeRTOS Tasks)
│       ├── config.example.h        # File mẫu cấu hình WiFi, Firebase, Groq Keys
│       ├── config.h                # File cấu hình nội bộ (đã được .gitignore bảo mật)
│       ├── partitions.csv          # Bảng phân vùng Flash 16MB (8MB App, LittleFS)
│       ├── ai_agent.cpp / .h       # Xử lý Cloud STT, Multi-Key Groq LLM, TTS, Logic AI
│       ├── hw_audio.cpp / .h       # Driver I2S Mic INMP441, Loa MAX98357A, VAD, Echo Tracking
│       ├── hw_sensors.h            # Đọc cảm biến DS3231, AHT20, BMP280, Thuật toán Âm lịch
│       ├── hw_led.h                # Hiệu ứng vòng 12 LED WS2812B NeoPixel
│       ├── hw_ir.h                 # Driver phát/học lệnh hồng ngoại Daikin
│       └── ui_lvgl.cpp / .h        # Giao diện đồ họa LVGL v9, Robot Face, Music Player
├── vercel_backend/                 # Serverless API Backend (Node.js)
│   ├── api/
│   │   ├── music.js                # Tìm kiếm và lấy link MP3 trực tiếp (ZingMP3 / Apple Music)
│   │   ├── search.js               # Công cụ tìm kiếm tri thức Wikipedia & Tin tức
│   │   ├── stt.js                  # Proxy chuyển đổi giọng nói siêu tốc
│   │   └── tts.js                  # Engine chuyển văn bản thành giọng nói
│   └── vercel.json                 # Cấu hình định tuyến Vercel Serverless
├── web_app/                        # Bảng điều khiển Web Dashboard
│   ├── index.html                  # Giao diện HTML5 Glassmorphism
│   ├── style.css                   # Hiệu ứng CSS Modern Dark Mode
│   ├── app.js                      # Logic JavaScript kết nối Firebase Realtime Database
│   ├── config.example.js           # File mẫu cấu hình Firebase Web
│   └── firebase.json               # Cấu hình triển khai Firebase Hosting
├── .gitignore                      # Bảo vệ 100% các file chứa API Keys & Secrets
└── README.md                       # Tài liệu hướng dẫn toàn diện
```

---

## 🛠️ HƯỚNG DẪN CÀI ĐẶT & NẠP FIRMWARE

### Bước 1: Chuẩn bị Môi trường Arduino IDE
1. Cài đặt **Arduino IDE** (phiên bản 2.x trở lên).
2. Thêm URL ESP32 Package vào Preferences:
   ```text
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. Cài đặt **esp32 by Espressif Systems** (phiên bản khuyên dùng: `3.0.x` hoặc `3.3.x`).

### Bước 2: Cài đặt các thư viện cần thiết
Vào **Library Manager** của Arduino IDE và cài đặt:
- `lvgl` (phiên bản 9.x)
- `TFT_eSPI`
- `ESP32-audioI2S` (bởi schreibfaul1)
- `Adafruit NeoPixel`
- `ArduinoJson` (phiên bản 7.x)
- `RTClib` (bởi Adafruit)
- `Adafruit AHTX0`
- `Adafruit BMP280 Library`
- `IRremoteESP8266`

### Bước 3: Cấu hình API Keys & Thông số mạng
1. Điều hướng vào thư mục `ESP32/Code_ESP32/`.
2. Tạo file `config.h` từ file mẫu `config.example.h`:
   ```bash
   cp config.example.h config.h
   ```
3. Mở file `config.h` và điền thông tin:
   - Tên WiFi & Mật khẩu.
   - 3 API Keys Groq (lấy miễn phí tại [Groq Cloud Console](https://console.groq.com/keys)).
   - Thông tin kết nối Firebase Realtime Database.
   - API Key OpenWeatherMap.

### Bước 4: Thiết lập thông số Board trong Arduino IDE
- **Board**: `ESP32S3 Dev Module`
- **Flash Size**: `16MB (128Mb)`
- **Partition Scheme**: `Custom` (chọn file `partitions.csv` trong thư mục)
- **PSRAM**: `OPI PSRAM` (hoặc `QSPI PSRAM` tùy phiên bản chip của bạn)
- **Flash Mode**: `QIO 80MHz`
- **Upload Speed**: `921600`
- **USB CDC On Boot**: `Enabled`

Nhấn **Upload** để nạp firmware vào ESP32-S3.

---

## 🗣️ BẢNG KHẨU LỆNH GIỌNG NÓI MẪU

| Nhóm Chức Năng | Câu Lệnh Mẫu | Hành Động Của Trợ Lý Nori |
|---|---|---|
| **Chào hỏi / Trò chuyện** | *"Hi Nori"*, *"Chào bạn"*, *"Bạn tên là gì?"* | Chào hỏi vui vẻ, tự xưng Nori, mở mic chờ câu tiếp theo |
| **Phát nhạc trực tuyến** | *"Mở bài Cắt Đôi Nỗi Sầu"*, *"Mở bài Lạc Trôi"* | Tìm kiếm trên Cloud CDN và phát nhạc kèm 9-bar Equalizer |
| **Dừng phát nhạc** | *"Dừng nhạc"*, *"Tắt nhạc đi"*, *"Ngừng hát"* | Dừng luồng phát ngay lập tức và chuyển về màn hình chính |
| **Hội thoại đa tầng** | *"Thời tiết hôm nay thế nào?"* $\rightarrow$ *"Còn ngày mai?"* | Trả lời liên tục 2 câu mà không cần gọi lại từ khóa |
| **Điều khiển thiết bị** | *"Bật đèn 1"*, *"Tắt đèn phòng"*, *"Mở khóa cửa"* | Đóng/ngắt Relay tương ứng và đồng bộ lên Web App |
| **Cảm biến phòng** | *"Nhiệt độ phòng bao nhiêu?"*, *"Độ ẩm phòng"* | Đọc số liệu thực tế từ cảm biến AHT20/BMP280 |
| **Thời gian & Lịch âm** | *"Bây giờ là mấy giờ?"*, *"Hôm nay ngày mấy âm lịch?"* | Báo giờ chuẩn từ RTC DS3231 và Âm lịch thiên văn |
| **Điều hòa Daikin** | *"Bật điều hòa 25 độ"*, *"Tắt máy lạnh"* | Gửi mã hồng ngoại Daikin ARC433 điều khiển tức thì |
| **Chuyển màn hình** | *"Về màn hình chính"*, *"Mở màn hình remote học lệnh"* | Tự động chuyển đổi giao diện trên màn hình ST7735 |

---

## 🚀 TỰ ĐỘNG HÓA CI/CD (GITHUB ACTIONS PIPELINE)

Dự án được tích hợp sẵn luồng **CI/CD tự động** (`.github/workflows/ci_cd.yml`):
- **Tự động kiểm tra cú pháp**: Chạy test kiểm tra toàn bộ API Node.js và cấu trúc file Firmware ESP32 mỗi khi tạo Pull Request hoặc Push code.
- **Tự động triển khai Vercel Backend**: Cập nhật Production Serverless API ngay khi code mới được merge vào nhánh `main`.
- **Tự động triển khai Firebase Hosting**: Build và đưa giao diện Web App lên hosting trực tiếp.

### 🔑 Cấu hình GitHub Secrets (Dành cho Tự động Deploy):
Trong repo GitHub, vào **Settings > Secrets and variables > Actions > New repository secret** và thêm:
- `VERCEL_TOKEN`: Token tài khoản Vercel của bạn.
- `VERCEL_ORG_ID`: Org ID của Vercel Project.
- `VERCEL_PROJECT_ID`: Project ID của Vercel Project.
- `FIREBASE_SERVICE_ACCOUNT`: Service Account JSON key từ Firebase Console.

---

## 📄 GIẤY PHÉP (LICENSE)
Dự án được phân phối dưới giấy phép mã nguồn mở **MIT License**. Bạn được toàn quyền sử dụng, tùy biến và phát triển tiếp cho mục đích cá nhân hoặc thương mại.
