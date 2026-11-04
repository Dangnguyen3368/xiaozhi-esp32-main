# Biểu tượng ứng dụng (font_app_icons_30.c)

Font biểu tượng 30 px cho lưới ứng dụng và thanh dock trên màn hình chính (bảng TFT).
Lấy từ Material Symbols Rounded (giấy phép Apache 2.0), gói npm `material-symbols`.

Tạo lại sau khi thêm biểu tượng (thêm mã vào `main/boards/bread-compact-wifi-lcd/app_icons.h`):

```
pip install fonttools brotli pillow
npm pack material-symbols && tar xzf material-symbols-*.tgz
python -c "from fontTools.ttLib import TTFont; f=TTFont('package/material-symbols-rounded.woff2'); f.flavor=None; f.save('msr.ttf')"
python tools/app_icons/mkfont.py msr.ttf 30 font_app_icons_30 main/boards/bread-compact-wifi-lcd/font_app_icons_30.c e0c9,e80c,e042,e24a,f190,e425,e412,e03e,e030,e31e,e84f,eff7,ebcc,f172,e63e,e429,e88e,f2cd,e5c3,e855
```
