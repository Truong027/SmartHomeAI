import os

html_path = r'e:\smart\ESP32\Code_ESP32\data\index.html'
header_path = r'e:\smart\ESP32\Code_ESP32\web_ui.h'

with open(html_path, 'r', encoding='utf-8') as f:
    html_content = f.read()

header_content = '#ifndef WEB_UI_H\n#define WEB_UI_H\n\nconst char index_html[] PROGMEM = \n'
for line in html_content.splitlines():
    escaped_line = line.replace('\\', '\\\\').replace('"', '\\"')
    header_content += f'"{escaped_line}\\n"\n'
header_content += ';\n\n#endif\n'

with open(header_path, 'w', encoding='utf-8') as f:
    f.write(header_content)
