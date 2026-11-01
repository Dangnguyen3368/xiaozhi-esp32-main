# Robot Xiaozhi – bản tiếng Việt có màn cảm ứng và gia sư Vật lý

Bản này được làm từ [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) (giấy phép MIT) và thêm vài chức năng cho robot chạy ESP32-S3 lắp trên breadboard.

## Chức năng thêm vào

- **Màn LCD cảm ứng 2,8"** (ILI9341 Non-IPS, cảm ứng XPT2046). Chạm vào màn để bắt đầu hoặc dừng nói chuyện. Bấm nút **Menu** ở góc dưới bên phải (hoặc giữ tay khoảng 1 giây) để mở menu: Luyện thi, Ôn bài sai, Hẹn giờ học, Công thức, Tiến độ, Mở/Tắt nhạc, Wi-Fi, Cập nhật.
- **Chọn Wi-Fi ngay trên màn**: Menu → Wi-Fi, robot quét mạng, chọn tên mạng, gõ mật khẩu bằng bàn phím trên màn, robot lưu lại và khởi động lại. Không cần điện thoại.
- Dưới mỗi bài hiện trên màn có nút **Gợi ý**, **Lời giải**, **Bài khác** và **Đóng**.
- **Hẹn giờ học Pomodoro**: 25 phút học, 5 phút nghỉ (đổi được bằng lời), có đồng hồ đếm ngược trên màn và nhạc nhẹ khi học.
- **Ôn bài sai theo lịch**: bài làm sai được hỏi lại sau 1, 2, 4 rồi 8 ngày. Có thể hẹn giờ để robot tự nhắc ôn mỗi ngày ("Nhắc em ôn bài lúc 8 giờ tối").
- **Thẻ công thức** Cơ, Nhiệt, Điện, Quang hiện trên màn, và **biểu đồ tiến độ** từng học sinh.
- **Cập nhật firmware qua Wi-Fi**: mỗi lần code trên nhánh `main` thay đổi, GitHub Actions tự build và đăng bản phát hành. Nói "cập nhật phần mềm" hoặc bấm **Cập nhật** trong menu, robot sẽ tải bản mới và tự khởi động lại.
- **Gia sư Vật lý luyện thi vào 10 chuyên** (ôn theo đề Lam Sơn, Thanh Hóa). Có 314 bài: 192 bài tự soạn chia 3 mức, cộng 122 bài lấy từ đề thi thật của các tỉnh (D01–D122). Tối đa 6 học sinh, robot lưu tiến độ từng em, có chế độ ôn bài sai và thi thử. Mỗi bài đều hiện lên màn **bảng tóm tắt đại lượng**, bài nào có hình vẽ thì hiện thêm hình.
- **Radio nhạc Internet** (3 kênh nhạc không lời của SomaFM: chill, ambient, space; luồng MP3), có **sóng nhạc** chạy trên màn khi phát nhạc hoặc radio.
- **Nhắc việc** (self.reminder.*).
- Cho bản dùng màn OLED: **mặt Mochi** với sóng nhạc toàn màn.

## Phần cứng

| Bộ phận | Chân GPIO |
|---|---|
| Mic I2S (song công, dùng chung clock với loa) | WS 12, SCK 11, DIN 14 |
| Loa I2S | DOUT 13 (dùng chung WS/BCLK với mic) |
| LCD ILI9341 (SPI) | MOSI 18, SCK 17, MISO 15, CS 10, DC 3, RST 46, đèn nền 16 |
| Cảm ứng XPT2046 | CS 9, IRQ 8 (dùng chung SPI với LCD) |
| Nút | BOOT 0, chạm 47, tăng âm lượng 40, giảm âm lượng 39 |
| LED | 48 |

Cần ESP32-S3 có **16 MB flash và PSRAM** (ví dụ N16R8). Chân và thông số cân chỉnh cảm ứng nằm trong `main/boards/bread-compact-wifi-lcd/config.h`. Nếu chạm bị lệch, mở log và xem dòng `Touch raw` khi chạm 4 góc, rồi sửa `TOUCH_RAW_*_MIN/MAX`.

## Build và nạp

1. Cài **ESP-IDF v6.0.1 trở lên** (dễ nhất là cài extension ESP-IDF trong VS Code).
2. Tải code về:
   ```
   git clone https://github.com/Dangnguyen3368/xiaozhi-esp32-main.git
   ```
3. Mở thư mục trong VS Code, chọn target **esp32s3**.
4. Bấm **Build**. Board "Bread Compact Wi-Fi + LCD", màn ILI9341 Non-IPS và tiếng Việt đã được đặt sẵn trong `sdkconfig.defaults.esp32s3`, nên không cần vào menuconfig. Lần build đầu sẽ tự tải thư viện vào `managed_components`, mất vài phút.
5. Cắm cáp, chọn cổng COM, bấm **Flash**.

Nếu trước đó đã build với board khác, xóa file `sdkconfig` (hoặc chạy **Full Clean**) để các giá trị mặc định có hiệu lực.

## Cài đặt sau khi nạp

1. Bật robot. Nó phát Wi-Fi tên `Xiaozhi-xxxx`: kết nối vào rồi nhập Wi-Fi nhà mình.
2. Robot đọc một mã gồm 6 số. Vào [xiaozhi.me](https://xiaozhi.me), thêm thiết bị bằng mã đó.
3. Để dùng gia sư Vật lý: mở [docs/vi/prompt-gia-su-vat-ly.md](docs/vi/prompt-gia-su-vat-ly.md), chép đoạn prompt dán vào ô **Giới thiệu vai trò** trên xiaozhi.me, bấm Lưu rồi khởi động lại robot.
4. Gọi robot bằng câu "Nǐ hǎo Xiǎozhì", rồi nói chẳng hạn:
   - "Cho anh luyện thi Vật lý", "Lấy bài D119", "Thi thử phần điện"
   - "Mở radio" (hoặc "mở kênh ambient"), rồi chạm màn hoặc gọi robot để tắt
   - "Nhắc anh 9 giờ tối học bài"
   - "Hẹn giờ học 25 phút", "Cho em xem công thức phần điện", "Em học đến đâu rồi"
   - "Kiểm tra cập nhật phần mềm"

## Cập nhật qua Wi-Fi

Bản firmware đầu tiên có chức năng này phải nạp bằng cáp như bình thường. Từ đó trở đi:

1. Merge code vào `main`. Workflow **Release OTA firmware** (tab Actions) build khoảng 10 phút rồi đăng bản phát hành có `version.txt`, `xiaozhi-bread-compact-wifi-lcd.bin` (để robot tải) và `merged-binary-...bin` (để nạp bằng cáp khi cần).
2. Bảo robot "cập nhật phần mềm" hoặc bấm **Cập nhật** trong menu. Robot so phiên bản của nó với bản trên GitHub, nếu mới hơn thì tải về và khởi động lại.

Bản build trên GitHub dùng cấu hình mặc định của repo (board LCD, ILI9341 Non-IPS, tiếng Việt). Nếu mất điện lúc đang cập nhật, robot vẫn chạy bản cũ vì firmware có hai phân vùng ứng dụng.

## Nghe nhạc trong máy tính

Robot phát được nhạc MP3 của mày (nhạc Việt cũng được) từ máy tính cùng mạng Wi-Fi.

1. Tạo thư mục, ví dụ `C:\ESPCode\nhac`, rồi chép `phat_nhac.py` và `phat-nhac.bat` từ `tools/music_server/` vào đó.
2. Bỏ các file `.mp3` vào thư mục đó (có thư mục con cũng được).
3. Bấm đúp `phat-nhac.bat`. Nếu Windows hỏi tường lửa thì tích "Private networks" rồi bấm Allow. Để cửa sổ đó mở trong lúc nghe.
4. Trên robot: Menu > **Nhạc máy tính** (bấm lại lần nữa là **Bài tiếp**), hoặc nói "mở nhạc trong máy tính". Tắt nhạc bằng nút Tắt nhạc hoặc nói "tắt nhạc".

Robot tự tìm máy tính qua tín hiệu UDP cổng 8766; nhạc đi qua HTTP cổng 8765. Bài được phát ngẫu nhiên, hết thì lặp lại.

## Sửa ngân hàng câu hỏi

Đề bài, đáp án, gợi ý, lời giải và bảng tóm tắt đại lượng nằm trong `tools/physics-bank/*.py`. Hình vẽ nằm trong `tools/physics-bank/fig/b_<mã>.png`. Sau khi sửa, chạy:

```
cd tools/physics-bank
pip install pillow
python build.py
python gen_cc.py
```

Lệnh `build.py` kiểm tra lại đáp án của mọi bài. Lệnh `gen_cc.py` ghi lại file `main/study/physics_bank_data.cc` và lưu ảnh xem trước từng màn hình vào `fig/g_<mã>.png`. Bài mới chỉ được **thêm vào cuối**, vì tiến độ học sinh lưu theo thứ tự bài.

## Ghi chú kỹ thuật

- Mic và loa dùng I2S song công, mic chạy theo clock của loa. Đừng bao giờ tắt kênh phát (TX) trong lúc mic đang dùng, nếu tắt thì mic sẽ câm cho đến khi khởi động lại.
- Radio chỉ phát được luồng MP3. Các kênh VOV/FM dùng HLS+AAC nên chưa phát được.
- Firmware chiếm khoảng 3,6 MB trên phân vùng ứng dụng 4 MB (`partitions/v2/16m.csv`).
