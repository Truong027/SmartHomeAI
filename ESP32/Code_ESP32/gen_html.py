import os

with open('data/index.html', 'r', encoding='utf-8') as f:
    html = f.read()

html_escaped = html.replace('\\', '\\\\').replace('\"', '\\\"').replace('\n', '\\n\"\n\"')

cpp_code = f'''#ifndef WEB_UI_H
#define WEB_UI_H

const char index_html[] PROGMEM = \"{html_escaped}\";

#endif
'''

with open('web_ui.h', 'w', encoding='utf-8') as f:
    f.write(cpp_code)
