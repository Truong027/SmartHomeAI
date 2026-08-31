import re

with open(r'e:\smart\smart_home_app\lib\smart_energy_screen.dart', 'r', encoding='utf-8') as f:
    content = f.read()

content = content.replace(
    'Text("Công su?t dang tiêu th? (T?c th?i)"',
    'Expanded(child: Text("Công su?t dang tiêu th? (T?c th?i)"'
)
content = content.replace(
    'const Text("U?c tính ti?n di?n EVN"',
    'Expanded(child: const Text("U?c tính ti?n di?n EVN"'
)

# Fix 2: 'Công Su?t T?ng Thi?t B? Relay'
old_row2 = '''                  Row(
                    mainAxisAlignment: MainAxisAlignment.spaceBetween,
                    children: [
                      Text("Công Su?t T?ng Thi?t B? Relay", style: TextStyle(color: AppThemeColors.textPrimary(context), fontWeight: FontWeight.bold, fontSize: 16)),
                      Text("Ch?m d? s?a Watt W", style: TextStyle(color: AppThemeColors.textMuted(context), fontSize: 11)),
                    ],
                  ),'''
new_row2 = '''                  Row(
                    mainAxisAlignment: MainAxisAlignment.spaceBetween,
                    children: [
                      Expanded(child: Text("Công Su?t T?ng Thi?t B? Relay", style: TextStyle(color: AppThemeColors.textPrimary(context), fontWeight: FontWeight.bold, fontSize: 16))),
                      Text("Ch?m d? s?a Watt W", style: TextStyle(color: AppThemeColors.textMuted(context), fontSize: 11)),
                    ],
                  ),'''

if old_row2 in content:
    content = content.replace(old_row2, new_row2)

# Fix 3: 'L?ch S? Tiêu Th? Ði?n T? Ð?ng (5Phút/L?n)'
old_row3 = '''                  Row(
                    mainAxisAlignment: MainAxisAlignment.spaceBetween,
                    children: [
                      Text("L?ch S? Tiêu Th? Ði?n T? Ð?ng (5Phút/L?n)", style: TextStyle(color: AppThemeColors.textPrimary(context), fontWeight: FontWeight.bold, fontSize: 16)),
                      Container('''
new_row3 = '''                  Row(
                    mainAxisAlignment: MainAxisAlignment.spaceBetween,
                    children: [
                      Expanded(child: Text("L?ch S? Tiêu Th? Ði?n T? Ð?ng (5Phút/L?n)", style: TextStyle(color: AppThemeColors.textPrimary(context), fontWeight: FontWeight.bold, fontSize: 16))),
                      const SizedBox(width: 8),
                      Container('''

if old_row3 in content:
    content = content.replace(old_row3, new_row3)

with open(r'e:\smart\smart_home_app\lib\smart_energy_screen.dart', 'w', encoding='utf-8') as f:
    f.write(content)
