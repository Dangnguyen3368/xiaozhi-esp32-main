# Bản tiếng Pháp (gia sư tiếng Pháp)

Bản firmware thứ hai của robot, build từ **cùng code** với bản thường nhưng bật tùy chọn
`CONFIG_XIAOZHI_FRENCH_EDITION` ("French tutor edition" trong menuconfig → Xiaozhi Assistant).

| | Bản thường | Bản tiếng Pháp |
|---|---|---|
| Luyện thi Vật lý (ngân hàng đề, thẻ công thức, ôn bài sai) | có | **không** |
| Radio Internet, nhạc từ máy tính | có | **không** |
| Học tiếng Pháp: 36 bài theo "Le nouveau Taxi ! 1", thẻ ôn từ | không | **có** |
| Màn hình chính, lịch, thời tiết, chế độ trên xe, máy tính, ảnh, ngân hàng, hai robot, Pomodoro | có | có |
| Menu, thông báo, giọng offline | tiếng Việt | tiếng Việt |
| Cập nhật qua Wi-Fi (GitHub) | `xiaozhi-<board>.bin` | `xiaozhi-<board>-fr.bin` |

Khi tùy chọn tắt (mặc định), bản thường build y như trước.

## Build bản tiếng Pháp trên máy (thư mục riêng)

`scripts/build.py` ghi đè file `sdkconfig` trong thư mục, nên bản tiếng Pháp nên nằm ở **một thư
mục riêng**, để thư mục bản thường (E:\ESPCode\xiaozhi-esp32-main) giữ nguyên cấu hình.

Mở **IDF_v6.1_Powershell** rồi chạy:

```powershell
cd E:\ESPCode
git clone https://github.com/Dangnguyen3368/xiaozhi-esp32-main.git xiaozhi-phap
cd xiaozhi-phap
git checkout main            # hoặc nhánh đang thử
python scripts/build.py bread-compact-wifi-lcd --name bread-compact-wifi-lcd-fr --language vi-VN
idf.py -p COM3 flash
```

- Robot màn OLED (không màn màu): thay hai chữ `bread-compact-wifi-lcd` bằng `bread-compact-wifi`
  (`--name bread-compact-wifi-fr`).
- **Robot 2** (chip flash lạ, bootloader riêng): KHÔNG dùng `idf.py flash`; chỉ nạp phần ứng dụng:
  `idf.py -p COM3 app-flash`.
- Lần sau cập nhật: `git pull` rồi chạy lại dòng `python scripts/build.py ...`.

Nếu muốn dùng VS Code ESP-IDF: mở thư mục `xiaozhi-phap`, chạy dòng `python scripts/build.py ...`
một lần (tạo sdkconfig bản Pháp), sau đó Build / Flash bằng nút của VS Code như thường.

## Trên xiaozhi.me

Robot tiếng Pháp dùng prompt riêng: [prompt-gia-su-tieng-phap.md](prompt-gia-su-tieng-phap.md).

## Sửa bài học

Nội dung 36 bài ở `tools/french-course/lessons.py` (từ, mẫu câu, ngữ pháp). Sửa xong chạy
`python tools/french-course/gen.py` để tạo lại `main/french/french_lessons_data.cc`. Giữ nguyên
thứ tự bài: robot lưu tiến độ theo số bài.
