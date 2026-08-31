import os
from PIL import Image

def generate_c_array(image_path, output_path, width=160, height=128):
    img = Image.open(image_path).convert('RGB')
    
    img_ratio = img.width / img.height
    screen_ratio = width / height
    
    if img_ratio > screen_ratio:
        new_w = width
        new_h = int(width / img_ratio)
    else:
        new_h = height
        new_w = int(height * img_ratio)
        
    img = img.resize((new_w, new_h), Image.LANCZOS)
    
    bg = Image.new('RGB', (width, height), (0, 0, 0))
    offset = ((width - new_w) // 2, (height - new_h) // 2)
    bg.paste(img, offset)
    
    c_str = '#include "lvgl.h"\n\n'
    c_str += '#ifndef LV_ATTRIBUTE_MEM_ALIGN\n#define LV_ATTRIBUTE_MEM_ALIGN\n#endif\n'
    c_str += 'const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST uint8_t binex_logo_map[] = {\n'
    
    pixels = bg.load()
    for y in range(height):
        for x in range(width):
            r, g, b = pixels[x, y]
            # RGB565
            c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            # Standard little endian
            # lvgl expects [b0, b1] where c = b0 + (b1 << 8)
            # Let's output low byte then high byte
            c_str += f"0x{c & 0xFF:02x}, 0x{(c >> 8) & 0xFF:02x}, "
        c_str += '\n'
        
    c_str += '};\n\n'
    
    c_str += 'const lv_image_dsc_t binex_logo = {\n'
    c_str += '  .header = {\n'
    c_str += '    .magic = LV_IMAGE_HEADER_MAGIC,\n'
    c_str += '    .cf = LV_COLOR_FORMAT_RGB565,\n'
    c_str += '    .flags = 0,\n'
    c_str += f'    .w = {width},\n'
    c_str += f'    .h = {height},\n'
    c_str += f'    .stride = {width * 2},\n'
    c_str += '    .reserved_2 = 0,\n'
    c_str += '  },\n'
    c_str += f'  .data_size = {width * height * 2},\n'
    c_str += '  .data = binex_logo_map,\n'
    c_str += '};\n'
    
    with open(output_path, 'w') as f:
        f.write(c_str)
        
if __name__ == "__main__":
    generate_c_array(r'C:\Users\ongth\.gemini\antigravity-ide\brain\fc6c70c1-e9b1-4739-8705-8e81e1095ac4\media__1786334493228.png', r'e:\smart\ESP32\Code_ESP32\binex_logo.c')
