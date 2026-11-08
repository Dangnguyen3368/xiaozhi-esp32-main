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

### GPS không kết nối?

- Chỉ bản build **Bread Compact Wi-Fi + LCD** mới có chế độ trên xe.
- Robot tự thử cả hai dây (GPIO 5 và GPIO 4) và các tốc độ baud 9600, 38400, 4800, 115200, 57600, 19200, nên cắm ngược TX/RX vẫn chạy.
- Mở Serial Monitor, tìm dòng `GpsReader`:
  - `GPS module found: module TX on GPIO 5, 9600 baud` là đã kết nối.
  - `No GPS data on GPIO 5 or 4` là không có dữ liệu: kiểm tra VCC, GND (chung GND với robot) và dây TX.
  - `... bytes, ... bad lines, no valid NMEA` là có dữ liệu nhưng sai tốc độ hoặc dây chập chờn.
- Đèn trên module chỉ nháy khi **đã bắt được vệ tinh**; chưa bắt được thì đèn tắt, đó là bình thường.

### Không có mạng

GPS, tốc độ, cảnh báo camera (danh sách có sẵn + camera tự đánh dấu) và cảnh báo quá tốc độ theo số bạn cài vẫn chạy và robot vẫn **nói** cảnh báo, kể cả khi robot không vào được Wi-Fi và đang ở chế độ cài Wi-Fi. Biển tốc độ theo đường, kẹt xe, bản đồ và nói chuyện với robot cần internet.

Trên xe, robot vào mạng bằng điểm phát Wi-Fi của điện thoại: thêm hotspot đó vào danh sách Wi-Fi của robot như mạng nhà.

## Robot làm gì

- **Camera:** còn khoảng 500 m thì robot nói "Phía trước có camera giám sát, chú ý tốc độ" và hiện khoảng cách. Có sẵn 49 camera từ OpenStreetMap (ở Việt Nam còn thiếu nhiều). Đi qua camera thì nói "đánh dấu camera ở đây" (có thể kèm "tốc độ 60") để robot nhớ.
- **Biển tốc độ của đường:** khi có GPS, có key TomTom và xe đang chạy, robot hỏi TomTom tốc độ tối đa của đoạn đường đang đi (khoảng mỗi 250 m, ít nhất mỗi phút một lần, không quá 1.500 lần một ngày để không hết lượt miễn phí). Biển trên màn hình đổi theo đường, đổi biển thì robot hiện "Tốc độ tối đa 50 km/h - tên đường". Đường nào TomTom chưa có dữ liệu thì robot dùng số "Tốc độ tối đa của bạn" trong trang cài đặt.
- **Quá tốc độ:** robot lấy số nhỏ nhất trong: biển của đường, tốc độ ghi ở camera gần đó, và tốc độ tối đa bạn tự đặt (0 = chỉ theo biển). Chạy quá số đó hơn 3 km/h trong 3 giây thì robot nhắc, tối đa 30 giây một lần.
- **Tắc phía trước:** 2 phút một lần robot hỏi TomTom đoạn đường cách khoảng 1 km phía trước; xe ở đó chạy dưới 45% tốc độ lúc vắng thì robot báo.
- **Hỏi bằng giọng nói:** "đi đến trường mất bao lâu", "đường về nhà có tắc không", "đoạn này có tắc không".
- **Báo đường buổi sáng:** đến giờ đặt sẵn, robot tính đường từ "Từ" đến "Đến"; chậm hơn bình thường từ số phút bạn đặt thì nói "nên đi sớm", không thì "thông thoáng".
- Robot **không** dẫn đường từng ngã rẽ, không biết biển báo khác ngoài tốc độ, không tra phạt nguội.

## Màn hình lái xe

Bấm nút **Xe** ở góc trái dưới màn hình, hoặc nói "mở màn hình lái xe" / "mở bản đồ". Khi xe chạy trên 15 km/h được 5 giây, màn hình này tự mở (mỗi chuyến một lần). Chạm vào màn hình để đổi giữa hai trang, nút **X** để đóng.

- **Trang tốc độ:** số tốc độ thật to (chuyển đỏ khi quá tốc độ), biển giới hạn tốc độ, khoảng cách tới camera kèm thanh đếm lùi, và dòng báo tắc đường gần nhất.
- **Trang bản đồ:** bản đồ TomTom quanh xe (cập nhật 2 giây một lần), mũi tên xanh là xe, chấm cam là camera, nút **+ / −** để phóng to hoặc thu nhỏ. Chưa có GPS thì bản đồ hiện quanh địa điểm "Nhà", chấm đỏ là chỗ đó. Bản đồ tốn khoảng 5–10 MB 4G mỗi giờ.

Cắm module GPS vào thì robot nói "Đã kết nối GPS, đang tìm vệ tinh"; bắt được vệ tinh thì nói "Đã bắt được tín hiệu GPS". Rút dây ra thì màn hình báo "Mất kết nối GPS".

## Cài đặt

Nói "cài đặt dẫn đường" (hoặc gọi `self.nav.settings` với action `setup`): màn hình hiện mã QR, điện thoại cùng mạng quét để mở `http://<IP robot>:8081/xe`.

1. **Key TomTom** (miễn phí 2.500 lượt/ngày): đăng ký ở developer.tomtom.com → Dashboard → Keys, chép key dán vào trang. Không có key thì camera và tốc độ vẫn chạy, chỉ mất phần tắc đường.
2. **Địa điểm:** mỗi dòng `Tên | địa chỉ` hoặc `Tên | vĩ độ, kinh độ` (Google Maps: nhấn giữ một điểm rồi chép số).
3. **Báo đường buổi sáng:** giờ, Từ, Đến (tên trong danh sách địa điểm), số phút chậm thì nhắc.

## Không có module GPS

iPhone gửi vị trí bằng Phím tắt: "Lấy vị trí hiện tại" → "Lấy nội dung của URL" `http://<IP robot>:8081/xe/vitri?lat=<Vĩ độ>&lon=<Kinh độ>`. Robot dùng vị trí này 10 phút cho các câu hỏi tắc đường. Cảnh báo camera và tốc độ cần module GPS.

## Giọng nói

Các câu cảnh báo nằm trong `main/nav/nav_voice_data.cc`, tạo bằng `python tools/nav_voice/gen.py` (Edge TTS, giọng Hoài My). Sửa câu trong file đó rồi chạy lại.
