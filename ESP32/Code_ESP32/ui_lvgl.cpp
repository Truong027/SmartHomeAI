#include "ui_lvgl.h"
#include "hw_sensors.h"
#include "hw_led.h"

// Biến toàn cục giao diện
TFT_eSPI *tft = nullptr;
uint8_t *draw_buf = nullptr;

volatile bool uiUpdatePending = false;

// Biến dữ liệu
String hhmmText = "00:00";
String dateSolar = "00/00/0000";
float indoorTemp = 0.0f;
float indoorHum = 0.0f;
float indoorPres = 0.0f;
float owmTemp = 0.0f;
float owmHum = 0.0f;
float owmWind = 0.0f;
String owmDesc = "Đang tải...";
String ai_prediction_short = "";
String ai_prediction_icon = "";
bool relay1 = false;
bool relay2 = false;

// Đối tượng LVGL
lv_obj_t * screen_boot = nullptr;
lv_obj_t * screen_main = nullptr;
lv_obj_t * screen_ai = nullptr; 
lv_obj_t * screen_ir = nullptr;
lv_obj_t * screen_music = nullptr;
lv_obj_t * img_logo = nullptr; 

extern "C" {
  extern const lv_image_dsc_t binex_logo;
}

lv_obj_t * label_time = nullptr;
lv_obj_t * label_date = nullptr;
lv_obj_t * label_in_temp = nullptr;
lv_obj_t * label_in_hum = nullptr;
lv_obj_t * label_in_pres = nullptr;
lv_obj_t * label_out_temp = nullptr;
lv_obj_t * label_weather = nullptr;
lv_obj_t * pred_icon = nullptr;
lv_obj_t * label_pred = nullptr;
lv_obj_t * label_relay1 = nullptr;
lv_obj_t * label_relay2 = nullptr;

lv_obj_t * label_ir_title = nullptr;
lv_obj_t * label_ir_info = nullptr;

lv_obj_t * label_music_title = nullptr;
lv_obj_t * label_music_status = nullptr;
lv_obj_t * eq_bar[7] = {nullptr};
lv_timer_t * music_eq_timer = nullptr;

// AI Face, Orb & Dialogue objects
lv_obj_t * label_ai_status = nullptr;
lv_obj_t * ai_status_dot = nullptr;
lv_obj_t * label_user_msg = nullptr;
lv_obj_t * label_ai_msg = nullptr;
String lastUserText = "...";
String lastAiText = "San sang...";

// Cute Round AI Orb Avatar
lv_obj_t * ai_orb_face = nullptr;
lv_obj_t * ai_orb_eye_l = nullptr;
lv_obj_t * ai_orb_eye_r = nullptr;
lv_obj_t * ai_orb_mouth = nullptr;
lv_timer_t * ai_orb_anim_timer = nullptr;

// Typewriter Streaming Text Effect
String fullAiText = "San sang...";
String streamAiText = "San sang...";
int streamCharIndex = 0;
int streamDisplayStartIndex = 0; // Điểm bắt đầu trang hiển thị hiện tại trong ô AI
bool isStreamingAiText = false;
int orb_talk_phase = 0;
int current_ai_state = 0;

extern String removeVietnameseAccents(String text);

String cleanDisplayText(String text) {
  text.replace("“", "\"");
  text.replace("”", "\"");
  text.replace("‘", "'");
  text.replace("’", "'");
  text.replace("—", "-");
  text.replace("–", "-");
  text.replace("…", "...");
  String clean = removeVietnameseAccents(text);
  String result = "";
  for (unsigned int i = 0; i < clean.length(); i++) {
    char c = clean[i];
    if ((uint8_t)c >= 32 && (uint8_t)c <= 126) {
      result += c;
    } else if (c == '\n' || c == '\r') {
      result += ' ';
    }
  }
  result.trim();
  return result;
}

// Hàm gửi dữ liệu từ LVGL sang màn hình TFT
void my_disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t * px_map) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);

  if (tft) {
    tft->setSwapBytes(true);
    tft->pushImage(area->x1, area->y1, w, h, (uint16_t *)px_map);
  }
  lv_display_flush_ready(disp);
}

// ==========================================
// 1. MÀN HÌNH KHỞI ĐỘNG (BOOT SCREEN)
// ==========================================
void updateBootScreen(int progress) {
  lv_timer_handler();
}

// ==========================================
// 2. MÀN HÌNH CHÍNH (MAIN DASHBOARD SCREEN)
// ==========================================
void setupMainScreen() {
  screen_main = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screen_main, lv_color_hex(0x050914), 0); // Atmospheric Deep Cyber Navy

  // --- HEADER: Clock, Prediction Pill, Date ---
  lv_obj_t * row_header = lv_obj_create(screen_main);
  lv_obj_set_size(row_header, 160, 24);
  lv_obj_align(row_header, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(row_header, lv_color_hex(0x0B132B), 0);
  lv_obj_set_style_border_width(row_header, 0, 0);
  lv_obj_set_style_border_color(row_header, lv_color_hex(0x1E293B), 0);
  lv_obj_set_style_border_side(row_header, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_pad_all(row_header, 3, 0);
  lv_obj_set_layout(row_header, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(row_header, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row_header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  // Time label
  label_time = lv_label_create(row_header);
  lv_obj_set_style_text_color(label_time, lv_color_hex(0x00F0FF), 0);
  lv_obj_set_style_text_font(label_time, &lv_font_montserrat_14, 0);
  lv_label_set_text(label_time, "00:00");

  // Center Weather/AI Prediction Pill
  lv_obj_t * pred_container = lv_obj_create(row_header);
  lv_obj_set_size(pred_container, 58, 18);
  lv_obj_set_style_bg_color(pred_container, lv_color_hex(0x0F172A), 0);
  lv_obj_set_style_border_width(pred_container, 1, 0);
  lv_obj_set_style_border_color(pred_container, lv_color_hex(0x334155), 0);
  lv_obj_set_style_radius(pred_container, 9, 0);
  lv_obj_set_style_pad_all(pred_container, 2, 0);
  lv_obj_set_layout(pred_container, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(pred_container, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(pred_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  pred_icon = lv_obj_create(pred_container);
  lv_obj_set_size(pred_icon, 6, 6);
  lv_obj_set_style_radius(pred_icon, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(pred_icon, lv_color_hex(0xF59E0B), 0);
  lv_obj_set_style_border_width(pred_icon, 0, 0);

  label_pred = lv_label_create(pred_container);
  lv_obj_set_style_text_color(label_pred, lv_color_hex(0xF8FAFC), 0);
  lv_obj_set_style_text_font(label_pred, &lv_font_montserrat_10, 0);
  lv_label_set_long_mode(label_pred, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_width(label_pred, 42);
  lv_label_set_text(label_pred, "");
  lv_obj_set_style_pad_left(label_pred, 3, 0);

  // Date label
  label_date = lv_label_create(row_header);
  lv_obj_set_style_text_color(label_date, lv_color_hex(0x94A3B8), 0);
  lv_obj_set_style_text_font(label_date, &lv_font_montserrat_10, 0);
  lv_label_set_text(label_date, "00/00");

  // --- MIDDLE: Dual Sensor Cards (INDOOR & OUTDOOR) ---
  lv_obj_t * row_sensors = lv_obj_create(screen_main);
  lv_obj_set_size(row_sensors, 160, 74);
  lv_obj_align(row_sensors, LV_ALIGN_TOP_MID, 0, 26);
  lv_obj_set_style_bg_opa(row_sensors, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row_sensors, 0, 0);
  lv_obj_set_style_pad_hor(row_sensors, 3, 0);
  lv_obj_set_style_pad_ver(row_sensors, 2, 0);
  lv_obj_set_layout(row_sensors, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(row_sensors, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row_sensors, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  // Card 1: INDOOR (Left)
  lv_obj_t * card_in = lv_obj_create(row_sensors);
  lv_obj_set_size(card_in, 74, 70);
  lv_obj_set_style_bg_color(card_in, lv_color_hex(0x0F172A), 0);
  lv_obj_set_style_border_width(card_in, 1, 0);
  lv_obj_set_style_border_color(card_in, lv_color_hex(0x1E293B), 0);
  lv_obj_set_style_radius(card_in, 6, 0);
  lv_obj_set_style_pad_all(card_in, 3, 0);
  lv_obj_set_layout(card_in, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(card_in, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card_in, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t * title_in = lv_label_create(card_in);
  lv_obj_set_style_text_color(title_in, lv_color_hex(0xF59E0B), 0); // Warm Amber
  lv_obj_set_style_text_font(title_in, &lv_font_montserrat_10, 0);
  lv_label_set_text(title_in, "INDOOR");

  label_in_temp = lv_label_create(card_in);
  lv_obj_set_style_text_color(label_in_temp, lv_color_hex(0xF8FAFC), 0);
  lv_obj_set_style_text_font(label_in_temp, &lv_font_montserrat_12, 0);
  lv_label_set_text(label_in_temp, "-- C");

  label_in_hum = lv_label_create(card_in);
  lv_obj_set_style_text_color(label_in_hum, lv_color_hex(0x38BDF8), 0); // Sky Blue
  lv_obj_set_style_text_font(label_in_hum, &lv_font_montserrat_10, 0);
  lv_label_set_text(label_in_hum, "-- %");

  label_in_pres = lv_label_create(card_in);
  lv_obj_set_style_text_color(label_in_pres, lv_color_hex(0xA78BFA), 0); // Violet
  lv_obj_set_style_text_font(label_in_pres, &lv_font_montserrat_10, 0);
  lv_label_set_long_mode(label_in_pres, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_width(label_in_pres, lv_pct(100));
  lv_obj_set_style_text_align(label_in_pres, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(label_in_pres, "-- hPa");

  // Card 2: OUTDOOR (Right)
  lv_obj_t * card_out = lv_obj_create(row_sensors);
  lv_obj_set_size(card_out, 74, 70);
  lv_obj_set_style_bg_color(card_out, lv_color_hex(0x0F172A), 0);
  lv_obj_set_style_border_width(card_out, 1, 0);
  lv_obj_set_style_border_color(card_out, lv_color_hex(0x1E293B), 0);
  lv_obj_set_style_radius(card_out, 6, 0);
  lv_obj_set_style_pad_all(card_out, 3, 0);
  lv_obj_set_layout(card_out, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(card_out, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card_out, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t * title_out = lv_label_create(card_out);
  lv_obj_set_style_text_color(title_out, lv_color_hex(0x10B981), 0); // Emerald Green
  lv_obj_set_style_text_font(title_out, &lv_font_montserrat_10, 0);
  lv_label_set_text(title_out, "OUTDOOR");

  label_out_temp = lv_label_create(card_out);
  lv_obj_set_style_text_color(label_out_temp, lv_color_hex(0xF8FAFC), 0);
  lv_obj_set_style_text_font(label_out_temp, &lv_font_montserrat_12, 0);
  lv_label_set_text(label_out_temp, "-- C");

  label_weather = lv_label_create(card_out);
  lv_obj_set_style_text_color(label_weather, lv_color_hex(0x38BDF8), 0);
  lv_obj_set_style_text_font(label_weather, &lv_font_montserrat_10, 0);
  lv_label_set_long_mode(label_weather, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_width(label_weather, lv_pct(100));
  lv_obj_set_style_text_align(label_weather, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(label_weather, "Dang tai...");

  // --- FOOTER: Relay Control Indicators ---
  lv_obj_t * row_footer = lv_obj_create(screen_main);
  lv_obj_set_size(row_footer, 160, 24);
  lv_obj_align(row_footer, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(row_footer, lv_color_hex(0x0B132B), 0);
  lv_obj_set_style_border_width(row_footer, 0, 0);
  lv_obj_set_style_border_color(row_footer, lv_color_hex(0x1E293B), 0);
  lv_obj_set_style_border_side(row_footer, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_pad_all(row_footer, 2, 0);
  lv_obj_set_layout(row_footer, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(row_footer, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row_footer, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  label_relay1 = lv_label_create(row_footer);
  lv_obj_set_style_text_color(label_relay1, lv_color_hex(0x64748B), 0);
  lv_obj_set_style_text_font(label_relay1, &lv_font_montserrat_10, 0);
  lv_label_set_text(label_relay1, "R1: OFF");

  label_relay2 = lv_label_create(row_footer);
  lv_obj_set_style_text_color(label_relay2, lv_color_hex(0x64748B), 0);
  lv_obj_set_style_text_font(label_relay2, &lv_font_montserrat_10, 0);
  lv_label_set_text(label_relay2, "R2: OFF");
}

void showMainScreen() {
  pendingAiFaceState = -1; // Tránh main loop load lại screen_ai
  if (screen_main) {
    lv_screen_load(screen_main);
    setLedMode(0); // IDLE
  }
}

// Cập nhật giao diện Main Dashboard
void updateLVGL_UI() {
  if (label_time) lv_label_set_text_fmt(label_time, "%s", hhmmText.c_str());
  if (label_date) lv_label_set_text_fmt(label_date, "%s", dateSolar.c_str());
  
  if (label_in_temp) lv_label_set_text_fmt(label_in_temp, "%s C", String(indoorTemp, 1).c_str());
  if (label_in_hum) lv_label_set_text_fmt(label_in_hum, "%s %%", String(indoorHum, 0).c_str());
  if (label_in_pres) lv_label_set_text_fmt(label_in_pres, "%s hPa", String(indoorPres, 0).c_str());

  if (label_out_temp) lv_label_set_text_fmt(label_out_temp, "%s C", String(owmTemp, 1).c_str());
  if (label_weather) lv_label_set_text_fmt(label_weather, "%s", owmDesc.c_str());
  
  // Hiển thị dự báo thời tiết thông minh cho Center Capsule Pill
  String displayForecast = ai_prediction_short;
  if (displayForecast == "" || displayForecast == "Dang suy nghi...") {
    if (owmDesc.length() > 0 && owmDesc != "Đang tải...") {
      displayForecast = owmDesc;
    } else {
      displayForecast = (indoorTemp > 31.0f) ? "Oi buc" : ((indoorTemp < 22.0f) ? "Se lanh" : "Mat me");
    }
  }
  if (label_pred) lv_label_set_text(label_pred, displayForecast.c_str());

  if (pred_icon) {
    String lowerDesc = owmDesc;
    lowerDesc.toLowerCase();
    if (lowerDesc.indexOf("mua") != -1 || ai_prediction_icon == "rain") {
      lv_obj_set_style_bg_color(pred_icon, lv_color_hex(0x38BDF8), 0); // Mưa -> Chấm Xanh lam
    } else if (lowerDesc.indexOf("may") != -1 || ai_prediction_icon == "cloud") {
      lv_obj_set_style_bg_color(pred_icon, lv_color_hex(0x94A3B8), 0); // Mây -> Chấm Xám bạc
    } else if (lowerDesc.indexOf("dong") != -1 || lowerDesc.indexOf("sam") != -1 || ai_prediction_icon == "storm") {
      lv_obj_set_style_bg_color(pred_icon, lv_color_hex(0xA78BFA), 0); // Giông bão -> Chấm Tím
    } else {
      lv_obj_set_style_bg_color(pred_icon, lv_color_hex(0xF59E0B), 0); // Nắng / Mát -> Chấm Vàng Amber
    }
  }

  if (label_relay1) {
    lv_label_set_text_fmt(label_relay1, "R1: %s", relay1 ? "ON" : "OFF");
    lv_obj_set_style_text_color(label_relay1, relay1 ? lv_color_hex(0x10B981) : lv_color_hex(0xEF4444), 0);
  }
  
  if (label_relay2) {
    lv_label_set_text_fmt(label_relay2, "R2: %s", relay2 ? "ON" : "OFF");
    lv_obj_set_style_text_color(label_relay2, relay2 ? lv_color_hex(0x10B981) : lv_color_hex(0xEF4444), 0);
  }

  // Cập nhật nội dung đối thoại chia 2 bên (USER vs NORI AI)
  if (label_user_msg && lastUserText.length() > 0) {
    lv_label_set_text(label_user_msg, cleanDisplayText(lastUserText).c_str());
  }
  if (label_ai_msg && !isStreamingAiText && fullAiText.length() > 0) {
    String disp = fullAiText.substring(streamDisplayStartIndex);
    disp.trim();
    lv_label_set_text(label_ai_msg, cleanDisplayText(disp).c_str());
  }
}

// ==========================================
// 3. MÀN HÌNH ĐỐI THOẠI AI CHIA 2 BÊN (USER vs NORI AI)
// ==========================================

// Callback hoạt họa AI Orb và dòng chữ chạy theo giọng nói (Typewriter Streaming)
static void ai_orb_anim_cb(lv_timer_t * timer) {
  // 1. Dòng chữ AI xuất hiện dần dần theo nhịp nói (Typewriter Stream với tính năng Auto-Paging cuộn về đầu ô khi viết đầy)
  if (isStreamingAiText && label_ai_msg) {
    if (streamCharIndex < (int)fullAiText.length()) {
      streamCharIndex += 2; // Nhảy 2 ký tự mỗi 70ms (~28 ký tự/giây theo nhịp nói TTS)
      if (streamCharIndex > (int)fullAiText.length()) {
        streamCharIndex = fullAiText.length();
      }

      // Khi đoạn chữ trong ô AI đạt đến giới hạn chiều cao hiển thị (~55 ký tự = 5-6 dòng)
      // Tự động tìm dấu ngắt từ gần nhất để cuộn trang và viết tiếp bắt đầu từ dòng đầu tiên!
      if (streamCharIndex - streamDisplayStartIndex > 55) {
        int breakPos = -1;
        for (int p = streamCharIndex - 1; p >= streamDisplayStartIndex + 35; p--) {
          char ch = fullAiText.charAt(p);
          if (ch == ' ' || ch == '.' || ch == ',' || ch == ';' || ch == '!' || ch == '?') {
            breakPos = p + 1;
            break;
          }
        }
        if (breakPos != -1 && breakPos > streamDisplayStartIndex) {
          streamDisplayStartIndex = breakPos;
        } else {
          streamDisplayStartIndex = streamCharIndex - 2;
        }
      }

      streamAiText = fullAiText.substring(streamDisplayStartIndex, streamCharIndex);
      streamAiText.trim();
      lv_label_set_text(label_ai_msg, streamAiText.c_str());
    } else {
      isStreamingAiText = false;
      streamAiText = fullAiText.substring(streamDisplayStartIndex, fullAiText.length());
      streamAiText.trim();
      lv_label_set_text(label_ai_msg, streamAiText.c_str());
    }
  }

  // 2. Cập nhật biểu cảm khuôn mặt AI Orb tròn nhỏ
  if (!ai_orb_face || !ai_orb_eye_l || !ai_orb_eye_r || !ai_orb_mouth) return;

  static int orb_tick = 0;
  orb_tick++;

  if (current_ai_state == AI_STATE_TALKING) {
    orb_talk_phase = (orb_talk_phase + 1) % 4;
    if (orb_talk_phase == 0) {
      lv_obj_set_size(ai_orb_mouth, 6, 2);
    } else if (orb_talk_phase == 1) {
      lv_obj_set_size(ai_orb_mouth, 6, 4);
    } else if (orb_talk_phase == 2) {
      lv_obj_set_size(ai_orb_mouth, 5, 5);
    } else {
      lv_obj_set_size(ai_orb_mouth, 4, 3);
    }
    lv_obj_set_size(ai_orb_eye_l, 3, 4);
    lv_obj_set_size(ai_orb_eye_r, 3, 4);
  }
  else if (current_ai_state == AI_STATE_LISTENING) {
    lv_obj_set_size(ai_orb_eye_l, 3, 5);
    lv_obj_set_size(ai_orb_eye_r, 3, 5);
    lv_obj_set_size(ai_orb_mouth, 5, 2);
  }
  else if (current_ai_state == AI_STATE_THINKING) {
    lv_obj_set_size(ai_orb_eye_l, 3, 3);
    lv_obj_set_size(ai_orb_eye_r, 3, 4);
    lv_obj_set_size(ai_orb_mouth, 3, 3);
  }
  else {
    // IDLE: Chớp mắt tự nhiên
    if (orb_tick % 35 == 0) {
      lv_obj_set_size(ai_orb_eye_l, 3, 1);
      lv_obj_set_size(ai_orb_eye_r, 3, 1);
    } else if (orb_tick % 35 == 2) {
      lv_obj_set_size(ai_orb_eye_l, 3, 4);
      lv_obj_set_size(ai_orb_eye_r, 3, 4);
    }
    lv_obj_set_size(ai_orb_mouth, 6, 2);
  }
}

void setupAIScreen() {
  screen_ai = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screen_ai, lv_color_hex(0x030712), 0); // Obsidian Black Void
  
  // =========================================================================
  // TOP BAR HEADER: CUTE ANIMATED AI ORB + TITLE + LIVE STATUS BADGE
  // =========================================================================
  lv_obj_t * top_bar = lv_obj_create(screen_ai);
  lv_obj_set_size(top_bar, 156, 22);
  lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 2);
  lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x0B132B), 0);
  lv_obj_set_style_border_width(top_bar, 1, 0);
  lv_obj_set_style_border_color(top_bar, lv_color_hex(0x1E293B), 0);
  lv_obj_set_style_radius(top_bar, 8, 0);
  lv_obj_set_style_pad_all(top_bar, 0, 0);

  // 1. Cute Round Animated AI Orb (18x18 circle)
  ai_orb_face = lv_obj_create(top_bar);
  lv_obj_set_size(ai_orb_face, 18, 18);
  lv_obj_align(ai_orb_face, LV_ALIGN_LEFT_MID, 2, 0);
  lv_obj_set_style_radius(ai_orb_face, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(ai_orb_face, lv_color_hex(0x0F172A), 0);
  lv_obj_set_style_border_width(ai_orb_face, 1, 0);
  lv_obj_set_style_border_color(ai_orb_face, lv_color_hex(0x00F0FF), 0); // Cyan Neon Glow
  lv_obj_set_style_pad_all(ai_orb_face, 0, 0);

  // Mini Eyes & Mouth inside Orb
  ai_orb_eye_l = lv_obj_create(ai_orb_face);
  lv_obj_set_size(ai_orb_eye_l, 3, 4);
  lv_obj_set_style_radius(ai_orb_eye_l, 2, 0);
  lv_obj_set_style_bg_color(ai_orb_eye_l, lv_color_hex(0x00F0FF), 0);
  lv_obj_set_style_border_width(ai_orb_eye_l, 0, 0);
  lv_obj_align(ai_orb_eye_l, LV_ALIGN_TOP_LEFT, 3, 4);

  ai_orb_eye_r = lv_obj_create(ai_orb_face);
  lv_obj_set_size(ai_orb_eye_r, 3, 4);
  lv_obj_set_style_radius(ai_orb_eye_r, 2, 0);
  lv_obj_set_style_bg_color(ai_orb_eye_r, lv_color_hex(0x00F0FF), 0);
  lv_obj_set_style_border_width(ai_orb_eye_r, 0, 0);
  lv_obj_align(ai_orb_eye_r, LV_ALIGN_TOP_RIGHT, -3, 4);

  ai_orb_mouth = lv_obj_create(ai_orb_face);
  lv_obj_set_size(ai_orb_mouth, 6, 2);
  lv_obj_set_style_radius(ai_orb_mouth, 2, 0);
  lv_obj_set_style_bg_color(ai_orb_mouth, lv_color_hex(0x00F0FF), 0);
  lv_obj_set_style_border_width(ai_orb_mouth, 0, 0);
  lv_obj_align(ai_orb_mouth, LV_ALIGN_BOTTOM_MID, 0, -2);

  // 2. Title "NORI AI"
  lv_obj_t * title = lv_label_create(top_bar);
  lv_obj_set_style_text_color(title, lv_color_hex(0x00F0FF), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_10, 0);
  lv_label_set_text(title, "NORI AI");
  lv_obj_align(title, LV_ALIGN_LEFT_MID, 23, 0);

  // 3. Status Badge with animated color dot (Right aligned in top bar)
  ai_status_dot = lv_obj_create(top_bar);
  lv_obj_set_size(ai_status_dot, 5, 5);
  lv_obj_set_style_radius(ai_status_dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(ai_status_dot, lv_color_hex(0x10B981), 0);
  lv_obj_set_style_border_width(ai_status_dot, 0, 0);
  lv_obj_align(ai_status_dot, LV_ALIGN_RIGHT_MID, -66, 0);

  label_ai_status = lv_label_create(top_bar);
  lv_obj_set_style_text_color(label_ai_status, lv_color_hex(0x94A3B8), 0);
  lv_obj_set_style_text_font(label_ai_status, &lv_font_montserrat_10, 0);
  lv_obj_align(label_ai_status, LV_ALIGN_RIGHT_MID, -2, 0);
  lv_label_set_text(label_ai_status, "SAN SANG");

  // =========================================================================
  // DIALOGUE CARDS (SPLIT VIEW): USER (LEFT) vs NORI AI (RIGHT)
  // =========================================================================
  // 1. KHUNG BÊN TRÁI: NGƯỜI DÙNG NÓI (USER)
  lv_obj_t * card_user = lv_obj_create(screen_ai);
  lv_obj_set_size(card_user, 75, 100);
  lv_obj_align(card_user, LV_ALIGN_TOP_LEFT, 2, 26);
  lv_obj_set_style_bg_color(card_user, lv_color_hex(0x06251C), 0); // Dark Emerald Glass
  lv_obj_set_style_border_width(card_user, 1, 0);
  lv_obj_set_style_border_color(card_user, lv_color_hex(0x10B981), 0); // Emerald Border
  lv_obj_set_style_radius(card_user, 6, 0);
  lv_obj_set_style_pad_all(card_user, 2, 0);
  lv_obj_set_scrollbar_mode(card_user, LV_SCROLLBAR_MODE_OFF);

  lv_obj_t * title_user = lv_label_create(card_user);
  lv_obj_set_style_text_color(title_user, lv_color_hex(0x34D399), 0); // Vibrant Green
  lv_obj_set_style_text_font(title_user, &lv_font_montserrat_10, 0);
  lv_label_set_text(title_user, "USER");
  lv_obj_align(title_user, LV_ALIGN_TOP_LEFT, 2, 1);

  label_user_msg = lv_label_create(card_user);
  lv_obj_set_style_text_color(label_user_msg, lv_color_hex(0xF1F5F9), 0);
  lv_obj_set_style_text_font(label_user_msg, &lv_font_montserrat_10, 0);
  lv_label_set_long_mode(label_user_msg, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(label_user_msg, 69);
  lv_obj_align(label_user_msg, LV_ALIGN_TOP_LEFT, 2, 14);
  lv_label_set_text(label_user_msg, cleanDisplayText(lastUserText).c_str());

  // 2. KHUNG BÊN PHẢI: AI TRẢ LỜI (NORI AI)
  lv_obj_t * card_ai = lv_obj_create(screen_ai);
  lv_obj_set_size(card_ai, 77, 100);
  lv_obj_align(card_ai, LV_ALIGN_TOP_RIGHT, -2, 26);
  lv_obj_set_style_bg_color(card_ai, lv_color_hex(0x0A1C38), 0); // Dark Cyber Navy
  lv_obj_set_style_border_width(card_ai, 1, 0);
  lv_obj_set_style_border_color(card_ai, lv_color_hex(0x00F0FF), 0); // Cyan Neon Border
  lv_obj_set_style_radius(card_ai, 6, 0);
  lv_obj_set_style_pad_all(card_ai, 2, 0);
  lv_obj_set_scrollbar_mode(card_ai, LV_SCROLLBAR_MODE_OFF);

  lv_obj_t * title_ai = lv_label_create(card_ai);
  lv_obj_set_style_text_color(title_ai, lv_color_hex(0x00F0FF), 0); // Neon Cyan
  lv_obj_set_style_text_font(title_ai, &lv_font_montserrat_10, 0);
  lv_label_set_text(title_ai, "NORI AI");
  lv_obj_align(title_ai, LV_ALIGN_TOP_LEFT, 2, 1);

  label_ai_msg = lv_label_create(card_ai);
  lv_obj_set_style_text_color(label_ai_msg, lv_color_hex(0xF1F5F9), 0);
  lv_obj_set_style_text_font(label_ai_msg, &lv_font_montserrat_10, 0);
  lv_label_set_long_mode(label_ai_msg, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(label_ai_msg, 71);
  lv_obj_align(label_ai_msg, LV_ALIGN_TOP_LEFT, 2, 14);
  lv_label_set_text(label_ai_msg, cleanDisplayText(streamAiText).c_str());

  // Khởi tạo Timer chạy hoạt họa AI Orb & Typewriter Stream Text (70ms)
  if (ai_orb_anim_timer == NULL) {
    ai_orb_anim_timer = lv_timer_create(ai_orb_anim_cb, 70, NULL);
  }
}

void setAIChatDialogue(String userText, String aiText) {
  if (userText.length() > 0) {
    lastUserText = cleanDisplayText(userText);
    if (lastUserText.length() > 70) {
      lastUserText = lastUserText.substring(0, 67) + "...";
    }
  }
  if (aiText.length() > 0) {
    fullAiText = cleanDisplayText(aiText);
    streamAiText = "";
    streamCharIndex = 0;
    streamDisplayStartIndex = 0; // Luôn bắt đầu viết từ dòng đầu tiên của ô AI
    isStreamingAiText = true;
  }
  uiUpdatePending = true;
}

volatile int pendingAiFaceState = -1;

void setAIFaceState(int state) {
  pendingAiFaceState = state;
  uiUpdatePending = true;
}

void applyAIFaceState(int state) {
  current_ai_state = state;
  if (!screen_ai) return;
  if (state != AI_STATE_IDLE || lv_screen_active() == screen_ai) {
    lv_screen_load(screen_ai);
  }
  
  if (state == AI_STATE_IDLE) {
    setLedMode(0); // IDLE
    if (label_ai_status) lv_label_set_text(label_ai_status, "SAN SANG");
    if (ai_status_dot) lv_obj_set_style_bg_color(ai_status_dot, lv_color_hex(0x64748B), 0);
    if (ai_orb_face) lv_obj_set_style_border_color(ai_orb_face, lv_color_hex(0x64748B), 0);
  }
  else if (state == AI_STATE_LISTENING) {
    setLedMode(3); // Listening Green
    if (label_ai_status) lv_label_set_text(label_ai_status, "DANG NGHE...");
    if (ai_status_dot) lv_obj_set_style_bg_color(ai_status_dot, lv_color_hex(0x10B981), 0);
    if (ai_orb_face) lv_obj_set_style_border_color(ai_orb_face, lv_color_hex(0x10B981), 0);
  } 
  else if (state == AI_STATE_THINKING) {
    setLedMode(1); // Thinking Amber
    if (label_ai_status) lv_label_set_text(label_ai_status, "SUY NGHI...");
    if (ai_status_dot) lv_obj_set_style_bg_color(ai_status_dot, lv_color_hex(0xF59E0B), 0);
    if (ai_orb_face) lv_obj_set_style_border_color(ai_orb_face, lv_color_hex(0xF59E0B), 0);
  }
  else if (state == AI_STATE_TALKING) {
    setLedMode(4); // Talking Cyan
    if (label_ai_status) lv_label_set_text(label_ai_status, "DANG NOI...");
    if (ai_status_dot) lv_obj_set_style_bg_color(ai_status_dot, lv_color_hex(0x00F0FF), 0);
    if (ai_orb_face) lv_obj_set_style_border_color(ai_orb_face, lv_color_hex(0x00F0FF), 0);
    isStreamingAiText = true;
  }
}

void setAIFaceEmotion(String emotion) {
  emotion.toLowerCase();
  if (emotion == "excited") {
    setLedMode(15);
    if (ai_orb_face) lv_obj_set_style_border_color(ai_orb_face, lv_color_hex(0xA855F7), 0);
  } else if (emotion == "love" || emotion == "happy") {
    setLedMode(18);
    if (ai_orb_face) lv_obj_set_style_border_color(ai_orb_face, lv_color_hex(0xF43F5E), 0);
  } else if (emotion == "angry") {
    setLedMode(16);
    if (ai_orb_face) lv_obj_set_style_border_color(ai_orb_face, lv_color_hex(0xEF4444), 0);
  } else if (emotion == "proud") {
    setLedMode(16);
    if (ai_orb_face) lv_obj_set_style_border_color(ai_orb_face, lv_color_hex(0x10B981), 0);
  } else if (emotion == "surprised" || emotion == "curious") {
    setLedMode(17);
    if (ai_orb_face) lv_obj_set_style_border_color(ai_orb_face, lv_color_hex(0xF59E0B), 0);
  } else if (emotion == "sad" || emotion == "worried") {
    setLedMode(14);
    if (ai_orb_face) lv_obj_set_style_border_color(ai_orb_face, lv_color_hex(0x38BDF8), 0);
  }
}

// ==========================================
// 4. MÀN HÌNH HỌC LỆNH HỒNG NGOẠI (IR LEARNING)
// ==========================================
void setupIrScreen() {
  screen_ir = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screen_ir, lv_color_hex(0x050914), 0);

  // Header Title
  label_ir_title = lv_label_create(screen_ir);
  lv_obj_set_style_text_color(label_ir_title, lv_color_hex(0xEF4444), 0); // Crimson
  lv_obj_set_style_text_font(label_ir_title, &lv_font_montserrat_12, 0);
  lv_label_set_text(label_ir_title, "IR LEARNING MODE");
  lv_obj_align(label_ir_title, LV_ALIGN_TOP_MID, 0, 8);

  // Info Container Card
  lv_obj_t * ir_card = lv_obj_create(screen_ir);
  lv_obj_set_size(ir_card, 148, 64);
  lv_obj_align(ir_card, LV_ALIGN_CENTER, 0, 2);
  lv_obj_set_style_bg_color(ir_card, lv_color_hex(0x0F172A), 0);
  lv_obj_set_style_border_width(ir_card, 1, 0);
  lv_obj_set_style_border_color(ir_card, lv_color_hex(0x1E293B), 0);
  lv_obj_set_style_radius(ir_card, 6, 0);
  lv_obj_set_style_pad_all(ir_card, 4, 0);

  label_ir_info = lv_label_create(ir_card);
  lv_obj_set_style_text_color(label_ir_info, lv_color_hex(0xF8FAFC), 0);
  lv_obj_set_style_text_font(label_ir_info, &lv_font_montserrat_10, 0);
  lv_label_set_text(label_ir_info, "Waiting for IR signal...");
  lv_obj_align(label_ir_info, LV_ALIGN_CENTER, 0, 0);

  // Footer Navigation Hint
  lv_obj_t * label_ir_help = lv_label_create(screen_ir);
  lv_obj_set_style_text_color(label_ir_help, lv_color_hex(0x94A3B8), 0);
  lv_obj_set_style_text_font(label_ir_help, &lv_font_montserrat_10, 0);
  lv_label_set_text(label_ir_help, "B1: Next | B2: Send | B3: Exit");
  lv_obj_align(label_ir_help, LV_ALIGN_BOTTOM_MID, 0, -4);
}

void showIrScreen() {
  if (screen_ir) {
    lv_screen_load(screen_ir);
    setLedMode(15);
  }
}

void updateIrScreen(int index, String protocol, String hexCode) {
  if (label_ir_info) {
    if (index >= 10 && index <= 13) {
      String info = "Daikin AC: " + String(daikin_power ? "ON" : "OFF") + "\n";
      info += "Temp: " + String(daikin_temp) + "C | Fan: ";
      if (daikin_fan == 10) info += "Auto\n";
      else if (daikin_fan == 11) info += "Quiet\n";
      else info += "Speed " + String(daikin_fan) + "\n";

      if (index == 10) info += "> Toggle Power (Btn 2)";
      else if (index == 11) info += "> Temp UP (Btn 2)";
      else if (index == 12) info += "> Temp DOWN (Btn 2)";
      else if (index == 13) info += "> Fan Speed (Btn 2)";
      
      lv_label_set_text(label_ir_info, info.c_str());
    } else {
      String info = "Slot " + String(index + 1) + "/10\n";
      info += "Type: " + protocol + "\n";
      info += "Hex: 0x" + hexCode;
      lv_label_set_text(label_ir_info, info.c_str());
    }
  }
}

// ==========================================
// 5. MÀN HÌNH PHÁT NHẠC (MUSIC VISUALIZER)
// ==========================================
void music_eq_cb(lv_timer_t * timer) {
  if (!screen_music) return;
  for (int i = 0; i < 7; i++) {
    if (eq_bar[i]) {
      int h = random(5, 34);
      lv_obj_set_height(eq_bar[i], h);
    }
  }
}

void setupMusicScreen() {
  screen_music = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screen_music, lv_color_hex(0x050816), 0); // Atmospheric Deep Indigo

  // Header Title Capsule: NOW PLAYING
  lv_obj_t * header_capsule = lv_obj_create(screen_music);
  lv_obj_set_size(header_capsule, 110, 18);
  lv_obj_align(header_capsule, LV_ALIGN_TOP_MID, 0, 4);
  lv_obj_set_style_bg_color(header_capsule, lv_color_hex(0x0F172A), 0);
  lv_obj_set_style_border_width(header_capsule, 1, 0);
  lv_obj_set_style_border_color(header_capsule, lv_color_hex(0x1E293B), 0);
  lv_obj_set_style_radius(header_capsule, 9, 0);
  lv_obj_set_style_pad_all(header_capsule, 0, 0);
  lv_obj_set_layout(header_capsule, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(header_capsule, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(header_capsule, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t * header = lv_label_create(header_capsule);
  lv_obj_set_style_text_color(header, lv_color_hex(0x00F0FF), 0);
  lv_obj_set_style_text_font(header, &lv_font_montserrat_10, 0);
  lv_label_set_text(header, "NOW PLAYING");

  // Song Title Glass Card
  lv_obj_t * title_card = lv_obj_create(screen_music);
  lv_obj_set_size(title_card, 152, 26);
  lv_obj_align(title_card, LV_ALIGN_CENTER, 0, -22);
  lv_obj_set_style_bg_color(title_card, lv_color_hex(0x0F172A), 0);
  lv_obj_set_style_border_width(title_card, 1, 0);
  lv_obj_set_style_border_color(title_card, lv_color_hex(0x1E293B), 0);
  lv_obj_set_style_radius(title_card, 6, 0);
  lv_obj_set_style_pad_all(title_card, 2, 0);

  label_music_title = lv_label_create(title_card);
  lv_obj_set_style_text_color(label_music_title, lv_color_hex(0xF8FAFC), 0);
  lv_obj_set_style_text_font(label_music_title, &lv_font_montserrat_12, 0);
  lv_obj_set_width(label_music_title, 144);
  lv_obj_set_style_text_align(label_music_title, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(label_music_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_label_set_text(label_music_title, "Dang tai bai hat...");
  lv_obj_align(label_music_title, LV_ALIGN_CENTER, 0, 0);

  // Equalizer Spectrum Container (7 Bars with Neon Gradient)
  lv_obj_t * eq_cont = lv_obj_create(screen_music);
  lv_obj_set_size(eq_cont, 144, 40);
  lv_obj_align(eq_cont, LV_ALIGN_CENTER, 0, 16);
  lv_obj_set_style_bg_opa(eq_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(eq_cont, 0, 0);
  lv_obj_set_style_pad_all(eq_cont, 0, 0);
  lv_obj_set_layout(eq_cont, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(eq_cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(eq_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);

  const uint32_t eq_colors[7] = {0x00F0FF, 0x06B6D4, 0x10B981, 0x8B5CF6, 0xF43F5E, 0xF59E0B, 0x00F0FF};

  for (int i = 0; i < 7; i++) {
    eq_bar[i] = lv_obj_create(eq_cont);
    lv_obj_set_size(eq_bar[i], 12, 8);
    lv_obj_set_style_bg_color(eq_bar[i], lv_color_hex(eq_colors[i]), 0);
    lv_obj_set_style_radius(eq_bar[i], 3, 0);
    lv_obj_set_style_border_width(eq_bar[i], 0, 0);
  }

  // Footer Streaming Quality Badge
  label_music_status = lv_label_create(screen_music);
  lv_obj_set_style_text_color(label_music_status, lv_color_hex(0x10B981), 0);
  lv_obj_set_style_text_font(label_music_status, &lv_font_montserrat_10, 0);
  lv_label_set_text(label_music_status, "128kbps Hi-Fi Audio Stream");
  lv_obj_align(label_music_status, LV_ALIGN_BOTTOM_MID, 0, -4);

  music_eq_timer = lv_timer_create(music_eq_cb, 100, NULL);
  lv_timer_pause(music_eq_timer);
}

void showMusicScreen(String songTitle) {
  if (!screen_music) return;
  if (label_music_title) {
    if (songTitle.length() > 0) {
      lv_label_set_text(label_music_title, songTitle.c_str());
    } else {
      lv_label_set_text(label_music_title, "Music Streaming");
    }
  }
  lv_screen_load(screen_music);
  if (music_eq_timer) lv_timer_resume(music_eq_timer);
  setLedMode(15); // Rainbow effect theo nhịp nhạc
}

void stopMusicScreen() {
  if (music_eq_timer) lv_timer_pause(music_eq_timer);
}

// ==========================================
// 6. ĐIỀU KHIỂN ĐÈN NỀN & KHỞI TẠO TỔNG THỂ LVGL
// ==========================================

// Điều khiển đèn nền màn hình TFT (Chống rung/nhấp nháy bằng phần cứng LEDC PWM tần số cao 5kHz)
void initTftBacklight() {
  pinMode(TFT_LED, OUTPUT);
  gpio_set_drive_capability((gpio_num_t)TFT_LED, GPIO_DRIVE_CAP_3); // Ép công suất xuất chân mức tối đa 40mA

#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  ledcAttach(TFT_LED, TFT_LED_FREQ, 8); // Tần số 5kHz, 8-bit resolution (ESP32 core 3.x)
  ledcWrite(TFT_LED, 255);
#else
  ledcSetup(TFT_LED_CHANNEL, TFT_LED_FREQ, 8); // Kênh 7, 5kHz, 8-bit resolution (ESP32 core 2.x)
  ledcAttachPin(TFT_LED, TFT_LED_CHANNEL);
  ledcWrite(TFT_LED_CHANNEL, 255);
#endif
  digitalWrite(TFT_LED, HIGH); // Đảm bảo mức cao vững chắc tuyệt đối
}

void setTftBacklight(uint8_t brightness) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  ledcWrite(TFT_LED, brightness);
#else
  ledcWrite(TFT_LED_CHANNEL, brightness);
#endif
}

void setupLVGL() {
  // Khởi tạo đèn nền TFT vững chắc ngay từ đầu
  initTftBacklight();

  tft = new TFT_eSPI(); 
  if (tft == nullptr) {
    Serial.println("Lỗi: Không đủ RAM cho TFT_eSPI!");
    while(1);
  }
  
  tft->begin();
  tft->setRotation(1); // Xoay ngang (160x128)
  tft->fillScreen(TFT_BLACK);

  lv_init();

  lv_display_t * disp = lv_display_create(SCREEN_W, SCREEN_H);
  lv_display_set_flush_cb(disp, my_disp_flush);
  
  // Ưu tiên cấp phát buffer trong Internal RAM (SRAM) để tốc độ vẽ siêu nhanh và tuyệt đối không xung đột bus SPI với chân GPIO 47 / PSRAM
  draw_buf = (uint8_t *)heap_caps_malloc(DRAW_BUF_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (draw_buf == nullptr) {
    draw_buf = (uint8_t *)malloc(DRAW_BUF_SIZE);
  }
  lv_display_set_buffers(disp, draw_buf, NULL, DRAW_BUF_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);

  // Khởi tạo các màn hình
  screen_boot = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screen_boot, lv_color_hex(0x000000), 0);
  
  img_logo = lv_img_create(screen_boot);
  lv_img_set_src(img_logo, &binex_logo);
  lv_obj_align(img_logo, LV_ALIGN_CENTER, 0, 0);
  
  lv_screen_load(screen_boot);

  setupMainScreen();
  setupAIScreen();
  setupIrScreen();
  setupMusicScreen();
}
