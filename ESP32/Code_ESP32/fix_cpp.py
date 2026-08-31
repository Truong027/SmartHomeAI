import os

path = 'e:/smart/ESP32/Code_ESP32/Code_ESP32.ino'
with open(path, 'r', encoding='utf-8') as f:
    content = f.read()

new_content = content.replace('"/ESP32_AI_Hub/relays/relay1"', 'FIREBASE_NODE "/relays/relay1"')
new_content = new_content.replace('"/ESP32_AI_Hub/relays/relay2"', 'FIREBASE_NODE "/relays/relay2"')
new_content = new_content.replace('"/ESP32_AI_Hub/sensors"', 'FIREBASE_NODE "/sensors"')
new_content = new_content.replace('"/ESP32_AI_Hub"', 'FIREBASE_NODE')

# Add define at the top
if '#define FIREBASE_NODE' not in new_content:
    new_content = '#define FIREBASE_NODE "/ESP32_AI_Hub"\n' + new_content

if new_content != content:
    with open(path, 'w', encoding='utf-8') as f:
        f.write(new_content)
    print('Updated Code_ESP32.ino')
