import re

with open('e:/smart/ESP32/Code_ESP32/tft_display.h', 'r', encoding='utf-8') as f:
    code = f.read()

# Protect setupTFT
setup_idx = code.find('inline void setupTFT')
setup_end = code.find('}', setup_idx)
setup_code = code[setup_idx:setup_end+1]

# Replace tft. with canvas.
code = code.replace('tft.', 'canvas.')

# Restore setupTFT
code = code[:setup_idx] + setup_code.replace('canvas.', 'tft.') + code[setup_end+1:]

# Add Cursor logic and Launcher
cursor_logic = """
extern GFXcanvas16 canvas;
extern Page curPage;
extern bool fullRedraw;
extern void applyRelays(bool, bool);

float oldCursorX = -1, oldCursorY = -1;
bool uiChanged = true;

static const uint8_t ICO_CURSOR[] PROGMEM = {
  0b10000000, 0b11000000, 0b11100000, 0b11110000,
  0b11111000, 0b11111100, 0b11111110, 0b11110000,
  0b11011000, 0b10001100, 0b00001100, 0b00000110
};

inline void eraseCursor(int x, int y) {
  if (x < 0 || y < 0) return;
  uint16_t* buf = canvas.getBuffer();
  int w = (x + 8 > 160) ? 160 - x : 8;
  if (w <= 0) return;
  for (int i=0; i<12; i++) {
    if (y+i >= 128) break;
    tft.drawRGBBitmap(x, y+i, &buf[(y+i)*160 + x], w, 1);
  }
}

inline void drawCursor() {
  int cx = (int)cursorX;
  int cy = (int)cursorY;
  int ox = (int)oldCursorX;
  int oy = (int)oldCursorY;
  
  if (uiChanged) {
    uiChanged = false;
    for (int r=0; r<12; r++) {
      if (cy+r >= 128) break;
      uint8_t bits = pgm_read_byte(&ICO_CURSOR[r]);
      for (int c=0; c<8; c++) {
        if (cx+c >= 160) break;
        if (bits & (0x80 >> c)) tft.drawPixel(cx+c, cy+r, ST77XX_WHITE);
      }
    }
    oldCursorX = cursorX; oldCursorY = cursorY;
    return;
  }
  
  if (cx != ox || cy != oy) {
    eraseCursor(ox, oy);
    for (int r=0; r<12; r++) {
      if (cy+r >= 128) break;
      uint8_t bits = pgm_read_byte(&ICO_CURSOR[r]);
      for (int c=0; c<8; c++) {
        if (cx+c >= 160) break;
        if (bits & (0x80 >> c)) tft.drawPixel(cx+c, cy+r, ST77XX_WHITE);
      }
    }
    oldCursorX = cursorX; oldCursorY = cursorY;
  }
}

inline void drawPageLauncher() {
  canvas.fillScreen(C_BG);
  canvas.fillRect(0,0,160,18,0x4A69);
  canvas.setTextSize(1); canvas.setTextColor(ST77XX_WHITE);
  canvas.setCursor(4,5); canvas.print("ESP32 OS");
  
  // Icon 1: Đồng hồ
  canvas.fillRoundRect(10, 30, 40, 40, 4, C_CARD_BG);
  canvas.setCursor(15, 60); canvas.setTextColor(ST77XX_WHITE); canvas.print("CLOCK");
  // Icon 2: Thời tiết
  canvas.fillRoundRect(60, 30, 40, 40, 4, C_CARD_BG);
  canvas.setCursor(62, 60); canvas.print("WEATHR");
  // Icon 3: Biểu đồ
  canvas.fillRoundRect(110, 30, 40, 40, 4, C_CARD_BG);
  canvas.setCursor(115, 60); canvas.print("CHART");
  // Icon 4: Relays
  canvas.fillRoundRect(60, 80, 40, 30, 4, C_CARD_BG);
  canvas.setCursor(65, 90); canvas.print("RELAY");
}

inline void drawOS() {
  if (curPage == PAGE_LAUNCHER) drawPageLauncher();
  else if (curPage == PAGE_CLOCK) drawPageClock();
  else if (curPage == PAGE_WEATHER) drawPageWeather();
  else if (curPage == PAGE_CHART) drawPageChart();
  
  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), 160, 128);
  uiChanged = true;
}

inline void handleClick(float x, float y) {
  if (curPage == PAGE_LAUNCHER) {
    if (x >= 10 && x <= 50 && y >= 30 && y <= 70) { curPage = PAGE_CLOCK; fullRedraw = true; }
    else if (x >= 60 && x <= 100 && y >= 30 && y <= 70) { curPage = PAGE_WEATHER; fullRedraw = true; }
    else if (x >= 110 && x <= 150 && y >= 30 && y <= 70) { curPage = PAGE_CHART; fullRedraw = true; }
    else if (x >= 60 && x <= 100 && y >= 80 && y <= 110) { 
       relay1 = !relay1; relay2 = relay1; applyRelays(relay1, relay2); fullRedraw = true; 
    }
  } else {
    curPage = PAGE_LAUNCHER; fullRedraw = true;
  }
}

inline void handleBack() {
  curPage = PAGE_LAUNCHER; fullRedraw = true;
}
"""

code = code.replace('// ════════════════════════════════════════════════════════════════\n// ICON 16x16 PROGMEM', cursor_logic + '\n// ════════════════════════════════════════════════════════════════\n// ICON 16x16 PROGMEM')

# Protect external variables setup
code = code.replace("extern Adafruit_ST7735 tft;", "extern Adafruit_ST7735 tft;\nextern GFXcanvas16 canvas;")

with open('e:/smart/ESP32/Code_ESP32/tft_display.h', 'w', encoding='utf-8') as f:
    f.write(code)
print('Done!')
