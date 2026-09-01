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
uint8_t currentSecond = 0;
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
lv_obj_t * label_music_sub = nullptr;
lv_obj_t * label_music_status = nullptr;
lv_obj_t * eq_bar[9] = {nullptr};
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
// THREAD-SAFE SCREEN SWITCHING SYSTEM
// ==========================================
volatile ScreenType pendingScreenLoad = SCREEN_NONE;

void requestScreen(ScreenType scr) {
  pendingScreenLoad = scr;
  uiUpdatePending = true;
}

void handlePendingScreenSwitch() {
  if (pendingScreenLoad != SCREEN_NONE) {
    ScreenType target = pendingScreenLoad;
    pendingScreenLoad = SCREEN_NONE;
    if (target == SCREEN_MAIN && screen_main) {
      pendingAiFaceState = -1;
      lv_screen_load(screen_main);
    } else if (target == SCREEN_AI && screen_ai) {
      lv_screen_load(screen_ai);
    } else if (target == SCREEN_MUSIC && screen_music) {
      lv_screen_load(screen_music);
    } else if (target == SCREEN_IR && screen_ir) {
      lv_screen_load(screen_ir);
    }
  }
}

// ==========================================
// 1. MÀN HÌNH KHỞI ĐỘNG (BOOT SCREEN)
// ==========================================
void updateBootScreen(int progress) {
  handlePendingScreenSwitch();
  lv_timer_handler();
}

lv_obj_t * arc_temp_gauge = nullptr;

// ==========================================
// 2. MÀN HÌNH CHÍNH (MAIN DASHBOARD SCREEN - SQUARELINE STUDIO CYBER WATCHFACE)
// ==========================================
void setupMainScreen() {
  screen_main = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screen_main, lv_color_hex(0x030712), 0); // Atmospheric Deep Space Black

  // =========================================================================
  // 1. TOP CYBER STATUS BAR (Header)
  // =========================================================================
  lv_obj_t * top_bar = lv_obj_create(screen_main);
  lv_obj_set_size(top_bar, 156, 17);
  lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 1);
  lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x081020), 0);
  lv_obj_set_style_border_width(top_bar, 0, 0);
  lv_obj_set_style_pad_all(top_bar, 1, 0);
  lv_obj_set_scrollbar_mode(top_bar, LV_SCROLLBAR_MODE_OFF);

  label_date = lv_label_create(top_bar);
  lv_obj_set_style_text_color(label_date, lv_color_hex(0x38BDF8), 0); // Neon Sky Blue
  lv_obj_set_style_text_font(label_date, &lv_font_montserrat_10, 0);
  lv_label_set_text(label_date, "00/00 - 00/00 AL");
  lv_obj_align(label_date, LV_ALIGN_LEFT_MID, 3, 0);

  pred_icon = lv_obj_create(top_bar);
  lv_obj_set_size(pred_icon, 5, 5);
  lv_obj_set_style_radius(pred_icon, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(pred_icon, lv_color_hex(0x10B981), 0); // Live green dot
  lv_obj_set_style_border_width(pred_icon, 0, 0);
  lv_obj_align(pred_icon, LV_ALIGN_RIGHT_MID, -52, 0);

  label_pred = lv_label_create(top_bar);
  lv_obj_set_style_text_color(label_pred, lv_color_hex(0x00F0FF), 0);
  lv_obj_set_style_text_font(label_pred, &lv_font_montserrat_10, 0);
  lv_label_set_text(label_pred, "NORI AI");
  lv_obj_align(label_pred, LV_ALIGN_RIGHT_MID, -4, 0);

  // =========================================================================
  // 2. CENTER CYBER WATCH DIAL (Honeycomb Smartwatch Center)
  // =========================================================================
  lv_obj_t * dial_center = lv_obj_create(screen_main);
  lv_obj_set_size(dial_center, 88, 80);
  lv_obj_align(dial_center, LV_ALIGN_TOP_MID, 0, 20);
  lv_obj_set_style_bg_color(dial_center, lv_color_hex(0x081224), 0); // Dark Cyber Navy Glass
  lv_obj_set_style_border_width(dial_center, 1, 0);
  lv_obj_set_style_border_color(dial_center, lv_color_hex(0x1E293B), 0);
  lv_obj_set_style_radius(dial_center, 14, 0);
  lv_obj_set_style_pad_all(dial_center, 0, 0);
  lv_obj_set_scrollbar_mode(dial_center, LV_SCROLLBAR_MODE_OFF);

  // Dynamic Seconds Arc Gauge (Lấp đầy vòng tròn theo giây, màu sắc theo nhiệt độ)
  arc_temp_gauge = lv_arc_create(dial_center);
  lv_obj_set_size(arc_temp_gauge, 80, 80);
  lv_obj_align(arc_temp_gauge, LV_ALIGN_CENTER, 0, 0);
  lv_arc_set_rotation(arc_temp_gauge, 270);   // Bắt đầu từ đỉnh 12h
  lv_arc_set_bg_angles(arc_temp_gauge, 0, 360); // Vòng tròn đầy đủ 360°
  lv_arc_set_range(arc_temp_gauge, 0, 59);    // Range = 0-59 giây
  lv_arc_set_value(arc_temp_gauge, 0);
  lv_obj_remove_style(arc_temp_gauge, NULL, LV_PART_KNOB); // No knob
  lv_obj_set_style_arc_width(arc_temp_gauge, 4, LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc_temp_gauge, lv_color_hex(0x1E293B), LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc_temp_gauge, 4, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc_temp_gauge, lv_color_hex(0x10B981), LV_PART_INDICATOR); // Mặc định xanh ngọc
  lv_obj_set_style_arc_rounded(arc_temp_gauge, true, LV_PART_INDICATOR);

  // Big Glowing Digital Clock
  label_time = lv_label_create(dial_center);
  lv_obj_set_style_text_color(label_time, lv_color_hex(0xFF7A00), 0); // Glowing Neon Amber (As in picture!)
  lv_obj_set_style_text_font(label_time, &lv_font_montserrat_14, 0);
  lv_obj_align(label_time, LV_ALIGN_CENTER, 0, -8);
  lv_label_set_text(label_time, "00:00");

  // Center Metrics Badge (Temp & Hum)
  label_in_temp = lv_label_create(dial_center);
  lv_obj_set_style_text_color(label_in_temp, lv_color_hex(0xE2E8F0), 0); // Crisp Ice White
  lv_obj_set_style_text_font(label_in_temp, &lv_font_montserrat_10, 0);
  lv_obj_align(label_in_temp, LV_ALIGN_CENTER, 0, 14);
  lv_label_set_text(label_in_temp, "-- C | -- %");

  label_in_hum = nullptr; // Combined into label_in_temp

  // =========================================================================
  // 3. LEFT FLANK CAPSULE (Relay 1 & Barometric Pressure)
  // =========================================================================
  lv_obj_t * flank_left = lv_obj_create(screen_main);
  lv_obj_set_size(flank_left, 32, 80);
  lv_obj_align(flank_left, LV_ALIGN_TOP_LEFT, 2, 20);
  lv_obj_set_style_bg_color(flank_left, lv_color_hex(0x0A1324), 0);
  lv_obj_set_style_border_width(flank_left, 1, 0);
  lv_obj_set_style_border_color(flank_left, lv_color_hex(0x1E293B), 0);
  lv_obj_set_style_radius(flank_left, 8, 0);
  lv_obj_set_style_pad_all(flank_left, 1, 0);
  lv_obj_set_scrollbar_mode(flank_left, LV_SCROLLBAR_MODE_OFF);

  lv_obj_t * title_r1 = lv_label_create(flank_left);
  lv_obj_set_style_text_color(title_r1, lv_color_hex(0x94A3B8), 0);
  lv_obj_set_style_text_font(title_r1, &lv_font_montserrat_10, 0);
  lv_label_set_text(title_r1, "R1");
  lv_obj_align(title_r1, LV_ALIGN_TOP_MID, 0, 2);

  label_relay1 = lv_label_create(flank_left);
  lv_obj_set_style_text_color(label_relay1, lv_color_hex(0xEF4444), 0);
  lv_obj_set_style_text_font(label_relay1, &lv_font_montserrat_10, 0);
  lv_label_set_text(label_relay1, "OFF");
  lv_obj_align(label_relay1, LV_ALIGN_TOP_MID, 0, 16);

  label_in_pres = lv_label_create(flank_left);
  lv_obj_set_style_text_color(label_in_pres, lv_color_hex(0xA78BFA), 0); // Amethyst
  lv_obj_set_style_text_font(label_in_pres, &lv_font_montserrat_10, 0);
  lv_label_set_text(label_in_pres, "1013");
  lv_obj_align(label_in_pres, LV_ALIGN_BOTTOM_MID, 0, -14);

  lv_obj_t * unit_pres = lv_label_create(flank_left);
  lv_obj_set_style_text_color(unit_pres, lv_color_hex(0x64748B), 0);
  lv_obj_set_style_text_font(unit_pres, &lv_font_montserrat_10, 0);
  lv_label_set_text(unit_pres, "hPa");
  lv_obj_align(unit_pres, LV_ALIGN_BOTTOM_MID, 0, -2);

  // =========================================================================
  // 4. RIGHT FLANK CAPSULE (Relay 2 & Outdoor Temp)
  // =========================================================================
  lv_obj_t * flank_right = lv_obj_create(screen_main);
  lv_obj_set_size(flank_right, 32, 80);
  lv_obj_align(flank_right, LV_ALIGN_TOP_RIGHT, -2, 20);
  lv_obj_set_style_bg_color(flank_right, lv_color_hex(0x0A1324), 0);
  lv_obj_set_style_border_width(flank_right, 1, 0);
  lv_obj_set_style_border_color(flank_right, lv_color_hex(0x1E293B), 0);
  lv_obj_set_style_radius(flank_right, 8, 0);
  lv_obj_set_style_pad_all(flank_right, 1, 0);
  lv_obj_set_scrollbar_mode(flank_right, LV_SCROLLBAR_MODE_OFF);

  lv_obj_t * title_r2 = lv_label_create(flank_right);
  lv_obj_set_style_text_color(title_r2, lv_color_hex(0x94A3B8), 0);
  lv_obj_set_style_text_font(title_r2, &lv_font_montserrat_10, 0);
  lv_label_set_text(title_r2, "R2");
  lv_obj_align(title_r2, LV_ALIGN_TOP_MID, 0, 2);

  label_relay2 = lv_label_create(flank_right);
  lv_obj_set_style_text_color(label_relay2, lv_color_hex(0xEF4444), 0);
  lv_obj_set_style_text_font(label_relay2, &lv_font_montserrat_10, 0);
  lv_label_set_text(label_relay2, "OFF");
  lv_obj_align(label_relay2, LV_ALIGN_TOP_MID, 0, 16);

  label_out_temp = lv_label_create(flank_right);
  lv_obj_set_style_text_color(label_out_temp, lv_color_hex(0xF59E0B), 0);
  lv_obj_set_style_text_font(label_out_temp, &lv_font_montserrat_10, 0);
  lv_label_set_text(label_out_temp, "--");
  lv_obj_align(label_out_temp, LV_ALIGN_BOTTOM_MID, 0, -14);

  lv_obj_t * unit_out = lv_label_create(flank_right);
  lv_obj_set_style_text_color(unit_out, lv_color_hex(0x64748B), 0);
  lv_obj_set_style_text_font(unit_out, &lv_font_montserrat_10, 0);
  lv_label_set_text(unit_out, "OUT");
  lv_obj_align(unit_out, LV_ALIGN_BOTTOM_MID, 0, -2);

  // =========================================================================
  // 5. BOTTOM SMART MARQUEE TICKER CAPSULE (Weather & AI Intelligence)
  // =========================================================================
  lv_obj_t * capsule_bottom = lv_obj_create(screen_main);
  lv_obj_set_size(capsule_bottom, 156, 23);
  lv_obj_align(capsule_bottom, LV_ALIGN_BOTTOM_MID, 0, -2);
  lv_obj_set_style_bg_color(capsule_bottom, lv_color_hex(0x070F20), 0);
  lv_obj_set_style_border_width(capsule_bottom, 1, 0);
  lv_obj_set_style_border_color(capsule_bottom, lv_color_hex(0x00F0FF), 0); // Neon Cyan Border
  lv_obj_set_style_radius(capsule_bottom, 11, 0);
  lv_obj_set_style_pad_all(capsule_bottom, 2, 0);
  lv_obj_set_scrollbar_mode(capsule_bottom, LV_SCROLLBAR_MODE_OFF);

  label_weather = lv_label_create(capsule_bottom);
  lv_obj_set_style_text_color(label_weather, lv_color_hex(0x38BDF8), 0);
  lv_obj_set_style_text_font(label_weather, &lv_font_montserrat_10, 0);
  lv_label_set_long_mode(label_weather, LV_LABEL_LONG_DOT);
  lv_obj_set_width(label_weather, 146);
  lv_obj_set_style_text_align(label_weather, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(label_weather, LV_ALIGN_CENTER, 0, 0);
  lv_label_set_text(label_weather, "Dang khoi dong...");
}

void showMainScreen() {
  requestScreen(SCREEN_MAIN);
  setLedMode(0); // IDLE
}

// Cập nhật giao diện Main Dashboard với cơ chế Smart Dirty-Checking (Chỉ vẽ lại khi có số liệu thay đổi thực sự)
void updateLVGL_UI() {
  static String last_hhmm = "";
  static String last_full_date = "";
  static String last_in_metric = "";
  static String last_in_pres = "";
  static String last_out_temp = "";
  static int last_relay1 = -1;
  static int last_relay2 = -1;

  if (label_time && hhmmText != last_hhmm) {
    last_hhmm = hhmmText;
    lv_label_set_text(label_time, hhmmText.c_str());
  }

  String fullDateStr = dateSolar;
  if (dateLunar.length() > 0) fullDateStr += " - " + dateLunar;
  if (label_date && fullDateStr != last_full_date) {
    last_full_date = fullDateStr;
    lv_label_set_text(label_date, fullDateStr.c_str());
  }

  // Cập nhật Vòng Cung Giây (Lấp đầy 360° theo giây hiện tại, đổi màu theo nhiệt độ)
  static int last_sec_arc = -1;
  static int last_temp_color_band = -1;
  int secVal = (int)currentSecond;
  if (secVal < 0) secVal = 0;
  if (secVal > 59) secVal = 59;
  
  // Xác định dải màu nhiệt độ (0=lạnh, 1=mát, 2=ấm, 3=nóng)
  int tempBand = 1; // mặc định mát
  int arcTemp = (int)indoorTemp;
  if (arcTemp >= 35)      tempBand = 3; // Nóng bỏng
  else if (arcTemp >= 30) tempBand = 2; // Ấm
  else if (arcTemp >= 23) tempBand = 1; // Mát mẻ lý tưởng
  else                    tempBand = 0; // Lạnh
  
  bool needUpdate = (secVal != last_sec_arc) || (tempBand != last_temp_color_band);
  if (arc_temp_gauge && needUpdate) {
    last_sec_arc = secVal;
    lv_arc_set_value(arc_temp_gauge, secVal);
    
    // Đổi sắc Arc theo nhiệt độ phòng
    if (tempBand != last_temp_color_band) {
      last_temp_color_band = tempBand;
      if (tempBand == 3) {
        lv_obj_set_style_arc_color(arc_temp_gauge, lv_color_hex(0xEF4444), LV_PART_INDICATOR); // Đỏ nóng
      } else if (tempBand == 2) {
        lv_obj_set_style_arc_color(arc_temp_gauge, lv_color_hex(0xFF7A00), LV_PART_INDICATOR); // Cam ấm
      } else if (tempBand == 1) {
        lv_obj_set_style_arc_color(arc_temp_gauge, lv_color_hex(0x10B981), LV_PART_INDICATOR); // Xanh ngọc mát
      } else {
        lv_obj_set_style_arc_color(arc_temp_gauge, lv_color_hex(0x38BDF8), LV_PART_INDICATOR); // Xanh băng lạnh
      }
    }
  }

  // Cập nhật số liệu trung tâm trong Dial
  String inMetricStr = String(indoorTemp, 1) + "C | " + String(indoorHum, 0) + "%";
  if (label_in_temp && inMetricStr != last_in_metric) {
    last_in_metric = inMetricStr;
    lv_label_set_text(label_in_temp, inMetricStr.c_str());
  }

  String strInPres = String(indoorPres, 0);
  if (label_in_pres && strInPres != last_in_pres) {
    last_in_pres = strInPres;
    lv_label_set_text(label_in_pres, strInPres.c_str());
  }

  String strOutTemp = String(owmTemp, 0) + "C";
  if (label_out_temp && strOutTemp != last_out_temp) {
    last_out_temp = strOutTemp;
    lv_label_set_text(label_out_temp, strOutTemp.c_str());
  }

  if (label_relay1 && (int)relay1 != last_relay1) {
    last_relay1 = (int)relay1;
    lv_label_set_text(label_relay1, relay1 ? "ON" : "OFF");
    lv_obj_set_style_text_color(label_relay1, relay1 ? lv_color_hex(0x10B981) : lv_color_hex(0xEF4444), 0);
  }
  
  if (label_relay2 && (int)relay2 != last_relay2) {
    last_relay2 = (int)relay2;
    lv_label_set_text(label_relay2, relay2 ? "ON" : "OFF");
    lv_obj_set_style_text_color(label_relay2, relay2 ? lv_color_hex(0x10B981) : lv_color_hex(0xEF4444), 0);
  }

  // Hoạt ảnh Ticker xoay vòng 3 trang thông tin thông minh mượt mà (Không tốn CPU)
  static unsigned long lastTickerStep = 0;
  static int tickerPage = 0;
  static String last_rendered_ticker = "";
  unsigned long nowTicker = millis();
  if (nowTicker - lastTickerStep >= 3500) {
    lastTickerStep = nowTicker;
    tickerPage = (tickerPage + 1) % 3;
    
    String tickerText = "";
    if (tickerPage == 0) {
      String desc = owmDesc.length() > 0 ? owmDesc : (ai_prediction_short.length() > 0 ? ai_prediction_short : "Troi dep");
      tickerText = "Du bao: " + desc;
    } else if (tickerPage == 1) {
      tickerText = (indoorTemp > 31.0f) ? "Phong: Oi buc, nen bat dieu hoa" : ((indoorTemp < 22.0f) ? "Phong: Se lanh, giu am nha" : "Phong: Nhiet do ly tuong mat me");
    } else {
      tickerText = "NORI AI: San sang phuc vu ban!";
    }
    if (label_weather && tickerText != last_rendered_ticker) {
      last_rendered_ticker = tickerText;
      lv_label_set_text(label_weather, cleanDisplayText(tickerText).c_str());
    }
  }

  // Cập nhật nội dung đối thoại chia 2 bên (USER vs NORI AI)
  static String last_rendered_user_msg = "";
  if (label_user_msg && lastUserText.length() > 0 && lastUserText != last_rendered_user_msg) {
    last_rendered_user_msg = lastUserText;
    lv_label_set_text(label_user_msg, cleanDisplayText(lastUserText).c_str());
  }
  static String last_rendered_ai_msg = "";
  if (label_ai_msg && !isStreamingAiText && fullAiText.length() > 0 && fullAiText != last_rendered_ai_msg) {
    last_rendered_ai_msg = fullAiText;
    String disp = fullAiText.substring(streamDisplayStartIndex);
    disp.trim();
    lv_label_set_text(label_ai_msg, cleanDisplayText(disp).c_str());
  }
}

// ==========================================
// 3. MÀN HÌNH ĐỐI THOẠI AI CHIA 2 BÊN (USER vs NORI AI)
// ==========================================

// Callback hoạt họa AI Orb và dòng chữ chạy theo giọng nói (Typewriter Streaming)
// Callback hoạt họa AI Orb, Soundwave Spectrum và dòng chữ chạy theo giọng nói (Typewriter Streaming)
static lv_obj_t * ai_wave_bar[7];

static void ai_orb_anim_cb(lv_timer_t * timer) {
  if (lv_screen_active() != screen_ai) return; // Bỏ qua nếu không ở màn hình AI
  
  // 1. Dòng chữ AI xuất hiện mượt mà từng ký tự theo nhịp nói (Typewriter Stream với tính năng Auto-Paging)
  if (isStreamingAiText && label_ai_msg) {
    if (streamCharIndex < (int)fullAiText.length()) {
      streamCharIndex += 1; // Từng ký tự một cách mượt mà tự nhiên

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
          streamDisplayStartIndex = streamCharIndex - 1;
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

  // 2. Cập nhật dải sóng âm Mini Cyber Soundwave (7 cột)
  for (int i = 0; i < 7; i++) {
    if (ai_wave_bar[i]) {
      if (current_ai_state == AI_STATE_TALKING || current_ai_state == AI_STATE_LISTENING) {
        int h = random(3, 10);
        lv_obj_set_height(ai_wave_bar[i], h);
      } else if (current_ai_state == AI_STATE_THINKING) {
        int h = (i % 2 == 0) ? 5 : 2;
        lv_obj_set_height(ai_wave_bar[i], h);
      } else {
        lv_obj_set_height(ai_wave_bar[i], 2);
      }
    }
  }

  // 3. Cập nhật biểu cảm khuôn mặt AI Orb tròn nhỏ
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
  lv_obj_set_style_bg_color(screen_ai, lv_color_hex(0x020617), 0); // Deep Obsidian Slate
  lv_obj_set_scrollbar_mode(screen_ai, LV_SCROLLBAR_MODE_OFF);
  
  // =========================================================================
  // 1. TOP BAR HEADER: SHADCN/UI STYLE GLASS NAVBAR
  // =========================================================================
  lv_obj_t * top_bar = lv_obj_create(screen_ai);
  lv_obj_set_size(top_bar, 156, 21);
  lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 2);
  lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x081224), 0);
  lv_obj_set_style_border_width(top_bar, 1, 0);
  lv_obj_set_style_border_color(top_bar, lv_color_hex(0x1E293B), 0);
  lv_obj_set_style_radius(top_bar, 10, 0);
  lv_obj_set_style_pad_all(top_bar, 1, 0);
  lv_obj_set_scrollbar_mode(top_bar, LV_SCROLLBAR_MODE_OFF);

  // Cute Round Animated AI Orb (16x16 circle with neon cyan glow ring)
  ai_orb_face = lv_obj_create(top_bar);
  lv_obj_set_size(ai_orb_face, 16, 16);
  lv_obj_align(ai_orb_face, LV_ALIGN_LEFT_MID, 2, 0);
  lv_obj_set_style_radius(ai_orb_face, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(ai_orb_face, lv_color_hex(0x040D1E), 0);
  lv_obj_set_style_border_width(ai_orb_face, 1, 0);
  lv_obj_set_style_border_color(ai_orb_face, lv_color_hex(0x00F0FF), 0); // Cyan Neon Glow
  lv_obj_set_style_pad_all(ai_orb_face, 0, 0);
  lv_obj_set_scrollbar_mode(ai_orb_face, LV_SCROLLBAR_MODE_OFF);

  // Mini Eyes & Mouth inside Orb
  ai_orb_eye_l = lv_obj_create(ai_orb_face);
  lv_obj_set_size(ai_orb_eye_l, 3, 3);
  lv_obj_set_style_radius(ai_orb_eye_l, 1, 0);
  lv_obj_set_style_bg_color(ai_orb_eye_l, lv_color_hex(0x00F0FF), 0);
  lv_obj_set_style_border_width(ai_orb_eye_l, 0, 0);
  lv_obj_align(ai_orb_eye_l, LV_ALIGN_TOP_LEFT, 2, 3);

  ai_orb_eye_r = lv_obj_create(ai_orb_face);
  lv_obj_set_size(ai_orb_eye_r, 3, 3);
  lv_obj_set_style_radius(ai_orb_eye_r, 1, 0);
  lv_obj_set_style_bg_color(ai_orb_eye_r, lv_color_hex(0x00F0FF), 0);
  lv_obj_set_style_border_width(ai_orb_eye_r, 0, 0);
  lv_obj_align(ai_orb_eye_r, LV_ALIGN_TOP_RIGHT, -2, 3);

  ai_orb_mouth = lv_obj_create(ai_orb_face);
  lv_obj_set_size(ai_orb_mouth, 5, 2);
  lv_obj_set_style_radius(ai_orb_mouth, 1, 0);
  lv_obj_set_style_bg_color(ai_orb_mouth, lv_color_hex(0x00F0FF), 0);
  lv_obj_set_style_border_width(ai_orb_mouth, 0, 0);
  lv_obj_align(ai_orb_mouth, LV_ALIGN_BOTTOM_MID, 0, -2);

  // Title "NORI AI"
  lv_obj_t * title = lv_label_create(top_bar);
  lv_obj_set_style_text_color(title, lv_color_hex(0x00F0FF), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_10, 0);
  lv_label_set_text(title, "NORI AI");
  lv_obj_align(title, LV_ALIGN_LEFT_MID, 21, 0);

  // Status Badge with animated color dot (Right aligned in top bar)
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
  // 2. SOUNDWAVE VISUALIZER BAR (7 Bars across 156px)
  // =========================================================================
  lv_obj_t * wave_cont = lv_obj_create(screen_ai);
  lv_obj_set_size(wave_cont, 156, 10);
  lv_obj_align(wave_cont, LV_ALIGN_TOP_MID, 0, 24);
  lv_obj_set_style_bg_opa(wave_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(wave_cont, 0, 0);
  lv_obj_set_style_pad_all(wave_cont, 0, 0);
  lv_obj_set_scrollbar_mode(wave_cont, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_layout(wave_cont, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(wave_cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(wave_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  const uint32_t wave_colors[7] = {0x00F0FF, 0x10B981, 0x38BDF8, 0xA855F7, 0x38BDF8, 0x10B981, 0x00F0FF};
  for (int i = 0; i < 7; i++) {
    ai_wave_bar[i] = lv_obj_create(wave_cont);
    lv_obj_set_size(ai_wave_bar[i], 12, 2);
    lv_obj_set_style_bg_color(ai_wave_bar[i], lv_color_hex(wave_colors[i]), 0);
    lv_obj_set_style_radius(ai_wave_bar[i], 1, 0);
    lv_obj_set_style_border_width(ai_wave_bar[i], 0, 0);
  }

  // =========================================================================
  // 3. DIALOGUE CARDS (SPLIT VIEW): USER (LEFT) vs NORI AI (RIGHT)
  // =========================================================================
  // 1. KHUNG BÊN TRÁI: NGƯỜI DÙNG NÓI (USER - SHADCN STYLE)
  lv_obj_t * card_user = lv_obj_create(screen_ai);
  lv_obj_set_size(card_user, 76, 91);
  lv_obj_align(card_user, LV_ALIGN_TOP_LEFT, 2, 35);
  lv_obj_set_style_bg_color(card_user, lv_color_hex(0x051F16), 0); // Dark Emerald Slate
  lv_obj_set_style_border_width(card_user, 1, 0);
  lv_obj_set_style_border_color(card_user, lv_color_hex(0x10B981), 0); // Emerald Border
  lv_obj_set_style_radius(card_user, 8, 0);
  lv_obj_set_style_pad_all(card_user, 2, 0);
  lv_obj_set_scrollbar_mode(card_user, LV_SCROLLBAR_MODE_OFF);

  lv_obj_t * dot_user = lv_obj_create(card_user);
  lv_obj_set_size(dot_user, 4, 4);
  lv_obj_set_style_radius(dot_user, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(dot_user, lv_color_hex(0x10B981), 0);
  lv_obj_set_style_border_width(dot_user, 0, 0);
  lv_obj_align(dot_user, LV_ALIGN_TOP_LEFT, 2, 4);

  lv_obj_t * title_user = lv_label_create(card_user);
  lv_obj_set_style_text_color(title_user, lv_color_hex(0x34D399), 0); // Vibrant Mint Green
  lv_obj_set_style_text_font(title_user, &lv_font_montserrat_10, 0);
  lv_label_set_text(title_user, "USER");
  lv_obj_align(title_user, LV_ALIGN_TOP_LEFT, 9, 1);

  label_user_msg = lv_label_create(card_user);
  lv_obj_set_style_text_color(label_user_msg, lv_color_hex(0xF1F5F9), 0);
  lv_obj_set_style_text_font(label_user_msg, &lv_font_montserrat_10, 0);
  lv_label_set_long_mode(label_user_msg, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(label_user_msg, 70);
  lv_obj_align(label_user_msg, LV_ALIGN_TOP_LEFT, 2, 14);
  lv_label_set_text(label_user_msg, cleanDisplayText(lastUserText).c_str());

  // 2. KHUNG BÊN PHẢI: AI TRẢ LỜI (NORI AI - SHADCN STYLE)
  lv_obj_t * card_ai = lv_obj_create(screen_ai);
  lv_obj_set_size(card_ai, 77, 91);
  lv_obj_align(card_ai, LV_ALIGN_TOP_RIGHT, -2, 35);
  lv_obj_set_style_bg_color(card_ai, lv_color_hex(0x061830), 0); // Deep Cyber Indigo
  lv_obj_set_style_border_width(card_ai, 1, 0);
  lv_obj_set_style_border_color(card_ai, lv_color_hex(0x00F0FF), 0); // Cyan Neon Glow Border
  lv_obj_set_style_radius(card_ai, 8, 0);
  lv_obj_set_style_pad_all(card_ai, 2, 0);
  lv_obj_set_scrollbar_mode(card_ai, LV_SCROLLBAR_MODE_OFF);

  lv_obj_t * dot_ai = lv_obj_create(card_ai);
  lv_obj_set_size(dot_ai, 4, 4);
  lv_obj_set_style_radius(dot_ai, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(dot_ai, lv_color_hex(0x00F0FF), 0);
  lv_obj_set_style_border_width(dot_ai, 0, 0);
  lv_obj_align(dot_ai, LV_ALIGN_TOP_LEFT, 2, 4);

  lv_obj_t * title_ai = lv_label_create(card_ai);
  lv_obj_set_style_text_color(title_ai, lv_color_hex(0x00F0FF), 0); // Neon Cyan
  lv_obj_set_style_text_font(title_ai, &lv_font_montserrat_10, 0);
  lv_label_set_text(title_ai, "NORI AI");
  lv_obj_align(title_ai, LV_ALIGN_TOP_LEFT, 9, 1);

  label_ai_msg = lv_label_create(card_ai);
  lv_obj_set_style_text_color(label_ai_msg, lv_color_hex(0xF1F5F9), 0);
  lv_obj_set_style_text_font(label_ai_msg, &lv_font_montserrat_10, 0);
  lv_label_set_long_mode(label_ai_msg, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(label_ai_msg, 71);
  lv_obj_align(label_ai_msg, LV_ALIGN_TOP_LEFT, 2, 14);
  lv_label_set_text(label_ai_msg, cleanDisplayText(streamAiText).c_str());

  // Khởi tạo Timer chạy hoạt họa AI Orb & Typewriter Stream Text (40ms cực mượt)
  if (ai_orb_anim_timer == NULL) {
    ai_orb_anim_timer = lv_timer_create(ai_orb_anim_cb, 40, NULL);
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
// 4. MÀN HÌNH HỌC LỆNH HỒNG NGOẠI (IR CONTROL & LEARN - GUNA / BUNIFU UI)
// ==========================================
void setupIrScreen() {
  screen_ir = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screen_ir, lv_color_hex(0x020617), 0); // Deep Obsidian Slate
  lv_obj_set_scrollbar_mode(screen_ir, LV_SCROLLBAR_MODE_OFF);

  // =========================================================================
  // 1. HEADER BAR: GUNA UI STYLE ROUNDED CRIMSON CAPSULE
  // =========================================================================
  lv_obj_t * header_capsule = lv_obj_create(screen_ir);
  lv_obj_set_size(header_capsule, 156, 20);
  lv_obj_align(header_capsule, LV_ALIGN_TOP_MID, 0, 2);
  lv_obj_set_style_bg_color(header_capsule, lv_color_hex(0x0F172A), 0);
  lv_obj_set_style_border_width(header_capsule, 1, 0);
  lv_obj_set_style_border_color(header_capsule, lv_color_hex(0xEF4444), 0); // Neon Crimson Glow
  lv_obj_set_style_radius(header_capsule, 10, 0);
  lv_obj_set_style_pad_all(header_capsule, 1, 0);
  lv_obj_set_scrollbar_mode(header_capsule, LV_SCROLLBAR_MODE_OFF);

  lv_obj_t * ir_dot = lv_obj_create(header_capsule);
  lv_obj_set_size(ir_dot, 5, 5);
  lv_obj_set_style_radius(ir_dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(ir_dot, lv_color_hex(0xEF4444), 0);
  lv_obj_set_style_border_width(ir_dot, 0, 0);
  lv_obj_align(ir_dot, LV_ALIGN_LEFT_MID, 4, 0);

  label_ir_title = lv_label_create(header_capsule);
  lv_obj_set_style_text_color(label_ir_title, lv_color_hex(0xEF4444), 0); // Crimson
  lv_obj_set_style_text_font(label_ir_title, &lv_font_montserrat_10, 0);
  lv_label_set_text(label_ir_title, "IR CONTROL & LEARN");
  lv_obj_align(label_ir_title, LV_ALIGN_CENTER, 0, 0);

  // =========================================================================
  // 2. CENTRAL CONTROL DECK: BUNIFU UI DARK GLASS CARD
  // =========================================================================
  lv_obj_t * ir_card = lv_obj_create(screen_ir);
  lv_obj_set_size(ir_card, 156, 78);
  lv_obj_align(ir_card, LV_ALIGN_TOP_MID, 0, 24);
  lv_obj_set_style_bg_color(ir_card, lv_color_hex(0x081120), 0);
  lv_obj_set_style_border_width(ir_card, 1, 0);
  lv_obj_set_style_border_color(ir_card, lv_color_hex(0x1E293B), 0);
  lv_obj_set_style_radius(ir_card, 10, 0);
  lv_obj_set_style_pad_all(ir_card, 3, 0);
  lv_obj_set_scrollbar_mode(ir_card, LV_SCROLLBAR_MODE_OFF);

  // Inner Monospace Code Tile
  lv_obj_t * inner_tile = lv_obj_create(ir_card);
  lv_obj_set_size(inner_tile, 148, 70);
  lv_obj_align(inner_tile, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(inner_tile, lv_color_hex(0x030814), 0);
  lv_obj_set_style_border_width(inner_tile, 1, 0);
  lv_obj_set_style_border_color(inner_tile, lv_color_hex(0x1E293B), 0);
  lv_obj_set_style_radius(inner_tile, 6, 0);
  lv_obj_set_style_pad_all(inner_tile, 3, 0);
  lv_obj_set_scrollbar_mode(inner_tile, LV_SCROLLBAR_MODE_OFF);

  label_ir_info = lv_label_create(inner_tile);
  lv_obj_set_style_text_color(label_ir_info, lv_color_hex(0xF8FAFC), 0);
  lv_obj_set_style_text_font(label_ir_info, &lv_font_montserrat_10, 0);
  lv_label_set_long_mode(label_ir_info, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(label_ir_info, 142);
  lv_label_set_text(label_ir_info, "Dang cho tin hieu Hong Ngoai...");
  lv_obj_align(label_ir_info, LV_ALIGN_CENTER, 0, 0);

  // =========================================================================
  // 3. FOOTER SEGMENTED PILL BAR (B1, B2, B3)
  // =========================================================================
  lv_obj_t * footer_bar = lv_obj_create(screen_ir);
  lv_obj_set_size(footer_bar, 156, 21);
  lv_obj_align(footer_bar, LV_ALIGN_BOTTOM_MID, 0, -2);
  lv_obj_set_style_bg_color(footer_bar, lv_color_hex(0x081020), 0);
  lv_obj_set_style_border_width(footer_bar, 1, 0);
  lv_obj_set_style_border_color(footer_bar, lv_color_hex(0x1E293B), 0);
  lv_obj_set_style_radius(footer_bar, 10, 0);
  lv_obj_set_style_pad_all(footer_bar, 1, 0);
  lv_obj_set_scrollbar_mode(footer_bar, LV_SCROLLBAR_MODE_OFF);

  lv_obj_t * label_ir_help = lv_label_create(footer_bar);
  lv_obj_set_style_text_color(label_ir_help, lv_color_hex(0x38BDF8), 0);
  lv_obj_set_style_text_font(label_ir_help, &lv_font_montserrat_10, 0);
  lv_label_set_text(label_ir_help, "B1: Chon  |  B2: Phat  |  B3: Thoat");
  lv_obj_align(label_ir_help, LV_ALIGN_CENTER, 0, 0);
}

void showIrScreen() {
  requestScreen(SCREEN_IR);
  setLedMode(15);
}

void updateIrScreen(int index, String protocol, String hexCode) {
  if (label_ir_info) {
    if (index >= 10 && index <= 13) {
      String info = "DAIKIN AC: " + String(daikin_power ? "DANG BAT" : "DANG TAT") + "\n";
      info += "Nhiet: " + String(daikin_temp) + "C | Gio: ";
      if (daikin_fan == 10) info += "Auto\n";
      else if (daikin_fan == 11) info += "Quiet\n";
      else info += "So " + String(daikin_fan) + "\n";

      if (index == 10) info += "> Nguon AC (B2)";
      else if (index == 11) info += "> Tang Nhiet (B2)";
      else if (index == 12) info += "> Giam Nhiet (B2)";
      else if (index == 13) info += "> Toc Do Gio (B2)";
      
      lv_label_set_text(label_ir_info, info.c_str());
    } else {
      String slotName = "";
      if (index == 0) slotName = "Phim 1: Tat Quat";
      else if (index == 1) slotName = "Phim 2: Bat/Tang Quat";
      else if (index == 2) slotName = "Phim 3: Dao Gio Quat";
      else if (index == 3) slotName = "Phim 4: Den Ngu";
      else slotName = "Phim Tuy Chinh " + String(index + 1);

      String info = "Chuc nang: " + slotName + "\n";
      info += "Giao thuc: " + protocol + "\n";
      info += "Ma Hex: 0x" + hexCode;
      lv_label_set_text(label_ir_info, info.c_str());
    }
  }
}

// ==========================================
// 5. MÀN HÌNH PHÁT NHẠC (MUSIC VISUALIZER)
// ==========================================
void music_eq_cb(lv_timer_t * timer) {
  if (!screen_music) return;
  static float phase = 0.0f;
  phase += 0.35f;
  for (int i = 0; i < 9; i++) {
    if (eq_bar[i]) {
      // Sóng âm hài hòa sống động kết hợp ngẫu nhiên nhẹ
      float wave = sinf(phase + i * 0.75f) * 11.0f + 14.0f;
      int jitter = random(-3, 4);
      int h = (int)(wave + jitter);
      if (h < 4) h = 4;
      if (h > 30) h = 30;
      lv_obj_set_height(eq_bar[i], h);
    }
  }
}

void setupMusicScreen() {
  screen_music = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screen_music, lv_color_hex(0x030712), 0); // Ultra Deep Obsidian Velvet

  // 1. Header Capsule: NOW PLAYING • HI-FI
  lv_obj_t * header_capsule = lv_obj_create(screen_music);
  lv_obj_set_size(header_capsule, 126, 17);
  lv_obj_align(header_capsule, LV_ALIGN_TOP_MID, 0, 3);
  lv_obj_set_style_bg_color(header_capsule, lv_color_hex(0x0B132B), 0);
  lv_obj_set_style_border_width(header_capsule, 1, 0);
  lv_obj_set_style_border_color(header_capsule, lv_color_hex(0x00F0FF), 0);
  lv_obj_set_style_radius(header_capsule, 8, 0);
  lv_obj_set_style_pad_all(header_capsule, 0, 0);
  lv_obj_set_layout(header_capsule, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(header_capsule, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(header_capsule, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t * header = lv_label_create(header_capsule);
  lv_obj_set_style_text_color(header, lv_color_hex(0x00F0FF), 0);
  lv_obj_set_style_text_font(header, &lv_font_montserrat_10, 0);
  lv_label_set_text(header, "NOW PLAYING • HI-FI");

  // 2. Song Info Glass Card (Thẻ kính sang trọng)
  lv_obj_t * title_card = lv_obj_create(screen_music);
  lv_obj_set_size(title_card, 154, 34);
  lv_obj_align(title_card, LV_ALIGN_TOP_MID, 0, 23);
  lv_obj_set_style_bg_color(title_card, lv_color_hex(0x0D1B2A), 0);
  lv_obj_set_style_border_width(title_card, 1, 0);
  lv_obj_set_style_border_color(title_card, lv_color_hex(0x1B263B), 0);
  lv_obj_set_style_radius(title_card, 6, 0);
  lv_obj_set_style_pad_all(title_card, 2, 0);

  // Tên bài hát hiển thị chính giữa, cuộn tròn mượt mà
  label_music_title = lv_label_create(title_card);
  lv_obj_set_style_text_color(label_music_title, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(label_music_title, &lv_font_montserrat_12, 0);
  lv_obj_set_width(label_music_title, 146);
  lv_obj_set_style_text_align(label_music_title, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(label_music_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_label_set_text(label_music_title, "Dang tai bai hat...");
  lv_obj_align(label_music_title, LV_ALIGN_TOP_MID, 0, 0);

  // Dòng phụ đề nguồn phát nhạc
  label_music_sub = lv_label_create(title_card);
  lv_obj_set_style_text_color(label_music_sub, lv_color_hex(0x38BDF8), 0);
  lv_obj_set_style_text_font(label_music_sub, &lv_font_montserrat_10, 0);
  lv_obj_set_width(label_music_sub, 146);
  lv_obj_set_style_text_align(label_music_sub, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(label_music_sub, "Studio Master Direct Stream");
  lv_obj_align(label_music_sub, LV_ALIGN_BOTTOM_MID, 0, -1);

  // 3. Bộ Equalizer 9 Cột Spectrum Neon Gradient cực kỳ sống động
  lv_obj_t * eq_cont = lv_obj_create(screen_music);
  lv_obj_set_size(eq_cont, 150, 36);
  lv_obj_align(eq_cont, LV_ALIGN_TOP_MID, 0, 61);
  lv_obj_set_style_bg_opa(eq_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(eq_cont, 0, 0);
  lv_obj_set_style_pad_all(eq_cont, 0, 0);
  lv_obj_set_layout(eq_cont, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(eq_cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(eq_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);

  const uint32_t eq_colors[9] = {
    0x00F0FF, 0x38BDF8, 0x3B82F6, 0x6366F1, 0x8B5CF6, 
    0xEC4899, 0xF43F5E, 0xF59E0B, 0x10B981
  };

  for (int i = 0; i < 9; i++) {
    eq_bar[i] = lv_obj_create(eq_cont);
    lv_obj_set_size(eq_bar[i], 10, 8);
    lv_obj_set_style_bg_color(eq_bar[i], lv_color_hex(eq_colors[i]), 0);
    lv_obj_set_style_radius(eq_bar[i], 2, 0);
    lv_obj_set_style_border_width(eq_bar[i], 0, 0);
  }

  // 4. Footer Streaming Quality Badge
  label_music_status = lv_label_create(screen_music);
  lv_obj_set_style_text_color(label_music_status, lv_color_hex(0x10B981), 0);
  lv_obj_set_style_text_font(label_music_status, &lv_font_montserrat_10, 0);
  lv_label_set_text(label_music_status, "ONLINE • 128Kbps Lossless");
  lv_obj_align(label_music_status, LV_ALIGN_BOTTOM_MID, 0, -3);

  music_eq_timer = lv_timer_create(music_eq_cb, 80, NULL);
  lv_timer_pause(music_eq_timer);
}

void showMusicScreen(String songTitle) {
  if (!screen_music) return;
  if (label_music_title) {
    if (songTitle.length() > 0) {
      // Khử sạch 100% dấu tiếng Việt để loại bỏ triệt để lỗi ký tự ô vuông trên font ASCII
      String cleanTitle = cleanDisplayText(removeVietnameseAccents(songTitle));
      cleanTitle.trim();
      lv_label_set_text(label_music_title, cleanTitle.c_str());
    } else {
      lv_label_set_text(label_music_title, "Music Streaming");
    }
  }
  requestScreen(SCREEN_MUSIC);
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
