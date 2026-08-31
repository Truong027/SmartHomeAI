
def mk_icon(pixels):
    b = []
    for y in range(24):
        for x in range(0, 24, 8):
            byte = 0
            for bit in range(8):
                if x+bit < 24 and pixels[y][x+bit] == '#':
                    byte |= (1 << (7 - bit))
            b.append(f'0x{byte:02X}')
    return ', '.join(b)

# Generate simple icons (24x24)
empty = [' ' * 24] * 24

# CLOCK
clock = []
for y in range(24):
    row = ''
    for x in range(24):
        dx, dy = x-11.5, y-11.5
        d = (dx*dx + dy*dy)**0.5
        if 9.5 < d < 11.5: row += '#'
        elif 3 < d < 9 and x == 11 and y < 12: row += '#'
        elif 2 < d < 6 and y == 11 and x > 11: row += '#'
        else: row += ' '
    clock.append(row)

# WEATHER
weather = []
for y in range(24):
    row = ''
    for x in range(24):
        if 8<y<16 and 4<x<20: row += '#'
        elif 5<y<12 and 7<x<15: row += '#'
        else: row += ' '
    weather.append(row)

# CHART
chart = []
for y in range(24):
    row = ''
    for x in range(24):
        if x==2 or y==21: row += '#'
        elif x==6 and 10<y<21: row += '#'
        elif x==12 and 14<y<21: row += '#'
        elif x==18 and 6<y<21: row += '#'
        else: row += ' '
    chart.append(row)

# RELAY (lightbulb/switch)
relay = []
for y in range(24):
    row = ''
    for x in range(24):
        if 4<x<19 and 6<y<14: row += '#'
        elif 9<x<14 and 14<=y<18: row += '#'
        else: row += ' '
    relay.append(row)

# SETTINGS (gear)
settings = []
for y in range(24):
    row = ''
    for x in range(24):
        dx, dy = x-11.5, y-11.5
        d = (dx*dx + dy*dy)**0.5
        if 4 < d < 8: row += '#'
        elif (x%8==0 or y%8==0) and d<11: row += '#'
        else: row += ' '
    settings.append(row)

# REBOOT
reboot = []
for y in range(24):
    row = ''
    for x in range(24):
        dx, dy = x-11.5, y-11.5
        d = (dx*dx + dy*dy)**0.5
        if 7 < d < 10 and not (x>11 and y<11): row += '#'
        elif x>10 and x<14 and y<5: row += '#'
        else: row += ' '
    reboot.append(row)

out = '#ifndef ICONS_H\n#define ICONS_H\n#include <pgmspace.h>\n\n'
out += f'const unsigned char icon_clock[] PROGMEM = {{ {mk_icon(clock)} }};\n'
out += f'const unsigned char icon_weather[] PROGMEM = {{ {mk_icon(weather)} }};\n'
out += f'const unsigned char icon_chart[] PROGMEM = {{ {mk_icon(chart)} }};\n'
out += f'const unsigned char icon_relay[] PROGMEM = {{ {mk_icon(relay)} }};\n'
out += f'const unsigned char icon_settings[] PROGMEM = {{ {mk_icon(settings)} }};\n'
out += f'const unsigned char icon_reboot[] PROGMEM = {{ {mk_icon(reboot)} }};\n'
out += '#endif\n'

with open('icons.h', 'w') as f:
    f.write(out)
print('Icons generated!')

