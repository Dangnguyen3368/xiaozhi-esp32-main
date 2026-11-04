# Chế độ trên xe: GPS, camera, tốc độ, tắc đường

Chỉ có trên bản màn màu (board **Bread Compact Wi-Fi + LCD**).

## Lắp module GPS (GY-NEO6MV2)

| Module GPS | Robot |
|---|---|
| VCC | 3V3 (hoặc 5V) |
| GND | GND |
| TX | GPIO 5 |
| RX | GPIO 4 |

Muốn dùng chân khác thì sửa `NAV_GPS_RX_PIN` / `NAV_GPS_TX_PIN` trong `main/boards/bread-compact-wifi-lcd/config.h`.

Ăng-ten phải nhìn thấy trời (đặt sát kính lái). Lần đầu mất 1–5 phút mới bắt được vệ tinh; trong nhà thường không bắt được. Bắt được thì robot nói "Đã bắt được tín hiệu GPS" và góc trái màn hình hiện tốc độ.

Trên xe, robot vào mạng bằng điểm phát Wi-Fi của điện thoại: thêm hotspot đó vào danh sách Wi-Fi của robot như mạng nhà.

## Robot làm gì

- **Camera:** còn khoảng 500 m thì robot nói "Phía trước có camera giám sát, chú ý tốc độ" và hiện khoảng cách. Có sẵn 49 camera từ OpenStreetMap (ở Việt Nam còn thiếu nhiều). Đi qua camera thì nói "đánh dấu camera ở đây" (có thể kèm "tốc độ 60") để robot nhớ.
- **Quá tốc độ:** chạy quá giới hạn bạn đặt (hoặc giới hạn ghi ở camera gần đó) hơn 3 km/h trong 3 giây thì robot nhắc, tối đa 30 giây một lần.
- **Tắc phía trước:** 2 phút một lần robot hỏi TomTom đoạn đường cách khoảng 1 km phía trước; xe ở đó chạy dưới 45% tốc độ lúc vắng thì robot báo.
- **Hỏi bằng giọng nói:** "đi đến trường mất bao lâu", "đường về nhà có tắc không", "đoạn này có tắc không".
- **Báo đường buổi sáng:** đến giờ đặt sẵn, robot tính đường từ "Từ" đến "Đến"; chậm hơn bình thường từ số phút bạn đặt thì nói "nên đi sớm", không thì "thông thoáng".
- Robot **không** dẫn đường từng ngã rẽ, không biết biển báo khác ngoài tốc độ, không tra phạt nguội.

## Cài đặt

Nói "cài đặt dẫn đường" (hoặc gọi `self.nav.setup`): màn hình hiện mã QR, điện thoại cùng mạng quét để mở `http://<IP robot>:8081/xe`.

1. **Key TomTom** (miễn phí 2.500 lượt/ngày): đăng ký ở developer.tomtom.com → Dashboard → Keys, chép key dán vào trang. Không có key thì camera và tốc độ vẫn chạy, chỉ mất phần tắc đường.
2. **Địa điểm:** mỗi dòng `Tên | địa chỉ` hoặc `Tên | vĩ độ, kinh độ` (Google Maps: nhấn giữ một điểm rồi chép số).
3. **Báo đường buổi sáng:** giờ, Từ, Đến (tên trong danh sách địa điểm), số phút chậm thì nhắc.

## Không có module GPS

iPhone gửi vị trí bằng Phím tắt: "Lấy vị trí hiện tại" → "Lấy nội dung của URL" `http://<IP robot>:8081/xe/vitri?lat=<Vĩ độ>&lon=<Kinh độ>`. Robot dùng vị trí này 10 phút cho các câu hỏi tắc đường. Cảnh báo camera và tốc độ cần module GPS.

## Giọng nói

Các câu cảnh báo nằm trong `main/nav/nav_voice_data.cc`, tạo bằng `python tools/nav_voice/gen.py` (Edge TTS, giọng Hoài My). Sửa câu trong file đó rồi chạy lại.
