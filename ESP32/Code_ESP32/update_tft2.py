import re

with open('e:/smart/ESP32/Code_ESP32/tft_display.h', 'r', encoding='utf-8') as f:
    code = f.read()

# Add drawStatusBar and new Launcher
new_launcher = '''
inline void drawStatusBar() {
  canvas.fillRect(0, 0, 160, 14, C_HDR1_BG);
  canvas.setTextSize(1);
  canvas.setTextColor(ST77XX_WHITE); // Wifi/Time always white for visibility
  
  // Wifi Icon (4 bars)
  if (WiFi.status() == WL_CONNECTED) {
    long rssi = WiFi.RSSI();
    int bars = 1;
    if (rssi > -60) bars = 4;
    else if (rssi > -70) bars = 3;
    else if (rssi > -80) bars = 2;
    for (int i=0; i<4; i++) {
      int h = 3 + i*2;
      uint16_t col = (i < bars) ? C_OK : C_DIM;
      canvas.fillRect(2 + i*3, 10 - h, 2, h, col);
    }
  } else {
    canvas.setCursor(2, 4);
    canvas.setTextColor(C_ERR);
    canvas.print("x");
  }
  
  // App Title
  canvas.setCursor(25, 4);
  canvas.setTextColor(ST77XX_WHITE);
  if (curPage == PAGE_LAUNCHER) canvas.print("SMART OS");
  else if (curPage == PAGE_CLOCK) canvas.print("CLOCK");
  else if (curPage == PAGE_WEATHER) canvas.print("WEATHER");
  else if (curPage == PAGE_CHART) canvas.print("CHART");
  else if (curPage == PAGE_SETTINGS) canvas.print("SETTINGS");
  
  // Time
  canvas.setTextColor(C_GOLD);
  canvas.setCursor(125, 4);
  canvas.print(hhmmText);
}

inline void drawPageLauncher() {
  canvas.fillScreen(C_BG);
  drawStatusBar();
  
  // Lưới 3x2, bắt đầu từ y=20
  // Hàng 1
  canvas.fillRoundRect(10, 20, 40, 40, 4, C_CARD_BG);
  canvas.setCursor(15, 50); canvas.setTextColor(C_TEXT); canvas.print("CLOCK");
  
  canvas.fillRoundRect(60, 20, 40, 40, 4, C_CARD_BG);
  canvas.setCursor(62, 50); canvas.print("WEATHR");
  
  canvas.fillRoundRect(110, 20, 40, 40, 4, C_CARD_BG);
  canvas.setCursor(115, 50); canvas.print("CHART");
  
  // Hàng 2
  canvas.fillRoundRect(10, 70, 40, 40, 4, (relay1 || relay2) ? C_OK : C_CARD_BG);
  canvas.setCursor(15, 100); canvas.setTextColor(C_TEXT); canvas.print("RELAY");
  
  canvas.fillRoundRect(60, 70, 40, 40, 4, C_CARD_BG);
  canvas.setCursor(65, 100); canvas.print("SETTING");
  
  canvas.fillRoundRect(110, 70, 40, 40, 4, C_CARD_BG);
  canvas.setCursor(115, 100); canvas.print("REBOOT");
}

inline void drawPageSettings() {
  canvas.fillScreen(C_BG);
  drawStatusBar();
  
  canvas.setTextSize(1);
  canvas.setTextColor(C_TEXT);
  canvas.setCursor(10, 20); canvas.print("System Settings");
  
  // Nút Theme
  canvas.fillRoundRect(10, 35, 140, 25, 4, C_CARD_BG);
  canvas.setCursor(15, 43); canvas.print("Change Theme");
  
  // Nút Reset WiFi
  canvas.fillRoundRect(10, 65, 140, 25, 4, C_CARD_BG);
  canvas.setCursor(15, 73); canvas.setTextColor(C_WARN); canvas.print("Reset WiFi");
  
  // Nút Info
  canvas.fillRoundRect(10, 95, 140, 25, 4, C_CARD_BG);
  canvas.setCursor(15, 103); canvas.setTextColor(C_TEXT); 
  canvas.print("IP:"); canvas.print(WiFi.localIP().toString());
}
'''

# Find bounds of drawPageLauncher in code
start_idx = code.find('inline void drawPageLauncher()')
end_idx = code.find('inline void drawOS()', start_idx)

code = code[:start_idx] + new_launcher + '\n' + code[end_idx:]

# Update drawOS
drawos_old = '''inline void drawOS() {
  if (curPage == PAGE_LAUNCHER) drawPageLauncher();
  else if (curPage == PAGE_CLOCK) drawPageClock();
  else if (curPage == PAGE_WEATHER) drawPageWeather();
  else if (curPage == PAGE_CHART) drawPageChart();
  
  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), 160, 128);
  uiChanged = true;
}'''

drawos_new = '''inline void drawOS() {
  if (curPage == PAGE_LAUNCHER) drawPageLauncher();
  else if (curPage == PAGE_CLOCK) drawPageClock();
  else if (curPage == PAGE_WEATHER) drawPageWeather();
  else if (curPage == PAGE_CHART) drawPageChart();
  else if (curPage == PAGE_SETTINGS) drawPageSettings();
  
  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), 160, 128);
  uiChanged = true;
}'''
code = code.replace(drawos_old, drawos_new)

# Update handleClick
handleclick_old = '''inline void handleClick(float x, float y) {
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
}'''

handleclick_new = '''inline void handleClick(float x, float y) {
  if (curPage == PAGE_LAUNCHER) {
    if (y >= 20 && y <= 60) {
      if (x >= 10 && x <= 50) { curPage = PAGE_CLOCK; fullRedraw = true; }
      else if (x >= 60 && x <= 100) { curPage = PAGE_WEATHER; fullRedraw = true; }
      else if (x >= 110 && x <= 150) { curPage = PAGE_CHART; fullRedraw = true; }
    } else if (y >= 70 && y <= 110) {
      if (x >= 10 && x <= 50) { relay1 = !relay1; relay2 = relay1; applyRelays(relay1, relay2); fullRedraw = true; }
      else if (x >= 60 && x <= 100) { curPage = PAGE_SETTINGS; fullRedraw = true; }
      else if (x >= 110 && x <= 150) { ESP.restart(); }
    }
  } else if (curPage == PAGE_SETTINGS) {
    if (y >= 35 && y <= 60) {
      // Toggle Theme
      if (C_BG == 0x0000) { C_BG = 0xFFFF; C_CARD_BG = 0xDF7D; C_TEXT = 0x0000; }
      else { C_BG = 0x0000; C_CARD_BG = 0x2104; C_TEXT = 0xFFFF; }
      fullRedraw = true;
    } else if (y >= 65 && y <= 90) {
      // Reset WiFi
      WiFiManager wm; wm.resetSettings(); ESP.restart();
    } else {
      curPage = PAGE_LAUNCHER; fullRedraw = true;
    }
  } else {
    curPage = PAGE_LAUNCHER; fullRedraw = true;
  }
}'''
code = code.replace(handleclick_old, handleclick_new)

with open('e:/smart/ESP32/Code_ESP32/tft_display.h', 'w', encoding='utf-8') as f:
    f.write(code)

with open('e:/smart/ESP32/Code_ESP32/Code_ESP32.ino', 'r', encoding='utf-8') as f:
    ino = f.read()

ino = ino.replace('enum Page { PAGE_LAUNCHER=0, PAGE_CLOCK=1, PAGE_WEATHER=2, PAGE_CHART=3 };', 'enum Page { PAGE_LAUNCHER=0, PAGE_CLOCK=1, PAGE_WEATHER=2, PAGE_CHART=3, PAGE_SETTINGS=4 };')

# We need to remove the header draw code in drawPageClock, drawPageWeather and drawPageChart since drawStatusBar handles it.
# Actually, those functions are in tft_display.h! Let's modify tft_display.h again.

print('Step 1 Done!')
