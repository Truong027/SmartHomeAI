#ifndef UI_LVGL_H
#define UI_LVGL_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include "config.h"

// Bộ nhớ đệm cho màn hình 160x128
#define DRAW_BUF_SIZE (SCREEN_W * SCREEN_H * 2) 

// Đối tượng điều khiển màn hình
extern TFT_eSPI *tft;
extern uint8_t *draw_buf;

// Biến điều khiển cập nhật màn hình
extern volatile bool uiUpdatePending;

// Các biến dữ liệu hiển thị (Được cập nhật từ Code_ESP32.ino)
extern String hhmmText;
extern String dateSolar;
extern float indoorTemp;
extern float indoorHum;
extern float indoorPres;
extern float owmTemp;
extern float owmHum;
extern float owmWind;
extern String owmDesc;
extern String ai_prediction_short;
extern String ai_prediction_icon;
extern bool relay1;
extern bool relay2;

// Trạng thái Điều hòa Daikin
extern bool daikin_power;
extern uint8_t daikin_temp;
extern uint8_t daikin_fan;

// Các đối tượng giao diện (Labels)
extern lv_obj_t * screen_boot;
extern lv_obj_t * screen_main;
extern lv_obj_t * screen_ai; // Màn hình AI
extern lv_obj_t * screen_ir; // Màn hình Học lệnh Hồng ngoại
extern lv_obj_t * img_logo;

extern lv_obj_t * label_time;
extern lv_obj_t * label_date;
extern lv_obj_t * label_in_temp;
extern lv_obj_t * label_in_hum;
extern lv_obj_t * label_in_pres;
extern lv_obj_t * label_out_temp;
extern lv_obj_t * label_weather;
extern lv_obj_t * pred_icon;
extern lv_obj_t * label_pred;
extern lv_obj_t * label_relay1;
extern lv_obj_t * label_relay2;

extern lv_obj_t * label_ir_title;
extern lv_obj_t * label_ir_info;

// Các hàm khởi tạo và cập nhật
void initTftBacklight();
void setTftBacklight(uint8_t brightness);
void setupLVGL();
void setupAIScreen();
void setupIrScreen();
void updateBootScreen(int progress);
void showMainScreen();
void showIrScreen();
void updateIrScreen(int index, String protocol, String hexCode);
void updateLVGL_UI();
void my_disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t * px_map);

// Hàm cho giao diện AI & Music
#define AI_STATE_IDLE      0
#define AI_STATE_LISTENING 1
#define AI_STATE_THINKING  2
#define AI_STATE_TALKING   3
#define AI_STATE_MUSIC     4
extern volatile int pendingAiFaceState;
extern String lastUserText;
extern String lastAiText;
void setAIFaceState(int state);
void applyAIFaceState(int state);
void setAIFaceEmotion(String emotion);
void setAIChatDialogue(String userText, String aiText);

extern lv_obj_t * screen_music;
void setupMusicScreen();
void showMusicScreen(String songTitle);
void stopMusicScreen();

#endif // UI_LVGL_H