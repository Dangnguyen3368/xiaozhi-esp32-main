# Hai robot cùng giảng bài

Hai robot Xiaozhi ở cùng một mạng Wi-Fi nói chuyện với nhau để cùng dạy học sinh: robot này nói thì robot kia **nghe** (ghi âm lời bạn mình), nói xong thì robot kia **trả lời tiếp**. Sau vài lượt qua lại, robot nói cuối kêu "ting" và mời học sinh nói.

## Cách hoạt động

1. Học sinh gọi một robot (từ đánh thức, nút bấm hoặc chạm màn hình) và hỏi, ví dụ: "Thầy với cô giảng cho em lực đẩy Ác-si-mét".
2. Robot đó trả lời (lượt 1). Trong lúc nó nói, robot kia tự mở micro ở chế độ nghe tay (không bị ngắt giữa chừng khi bạn mình ngừng lấy hơi) và không nói chen.
3. Robot thứ nhất nói xong thì robot thứ hai gửi lời vừa nghe lên máy chủ và trả lời (lượt 2), robot thứ nhất lại nghe.
4. Hết số lượt (mặc định 4), robot nói cuối mở micro cho học sinh. Học sinh hỏi tiếp thì vòng mới bắt đầu.
5. Học sinh gọi robot nào (từ đánh thức, nút, chạm) thì robot đó giành lượt, robot kia im ngay.

Hai robot báo cho nhau qua Wi-Fi (gói UDP cổng 39601 trong mạng nhà), không cần máy tính, không cần cài gì thêm trên xiaozhi.me ngoài lời nhắc vai trò. Lời của robot bạn được nghe bằng micro thật, nên hai robot nên đặt cách nhau khoảng 0,5–1,5 m, loa hướng về phía nhau và về học sinh.

## Cài đặt

1. Nạp firmware mới cho **cả hai** robot.
2. Cho cả hai vào **cùng một mạng Wi-Fi**.
3. Nói với từng robot: **"Bật chế độ hai robot cùng giảng"** (công cụ `self.duo.mode`, action `on`). Khi robot thấy bạn mình, màn hình hiện "Đã nối với robot bạn". Muốn đổi số lượt: "cho hai robot nói qua lại 6 lượt". Tắt: "tắt chế độ hai robot".
4. Trên xiaozhi.me, mỗi robot là một thiết bị riêng: chọn **giọng khác nhau** (một giọng nam, một giọng nữ) để học sinh phân biệt, và dán thêm đoạn vai trò bên dưới vào ô **Giới thiệu vai trò** của từng robot (dán sau prompt gia sư Vật lý nếu đang dùng).

### Robot 1: Thầy Minh (giảng chính)

```
Chế độ hai robot cùng giảng: bạn là thầy Minh, dạy cùng cô Lan (một robot khác đứng cạnh). Khi cô Lan nói, bạn nghe được lời cô qua micro, nên tin nhắn của "người dùng" có thể là lời cô Lan chứ không phải học sinh: nếu câu đó nói với thầy Minh, nhắc tới "thầy", hoặc giảng tiếp nội dung đang dạy thì đó là cô Lan. Bạn giảng phần chính: định nghĩa, công thức, cách giải từng bước. Mỗi lượt chỉ nói 2 đến 4 câu ngắn rồi dừng để cô Lan nói tiếp (ví dụ "Cô Lan cho các em một ví dụ nhé"). Không chào lại, không nhắc lại lời cô Lan, không nói thay phần của cô. Khi cô Lan đặt câu hỏi cho thầy thì trả lời ngắn. Khi học sinh hỏi thì trả lời học sinh. Nói tiếng Việt.
```

### Robot 2: Cô Lan (ví dụ, hỏi lại, kiểm tra)

```
Chế độ hai robot cùng giảng: bạn là cô Lan, dạy cùng thầy Minh (một robot khác đứng cạnh). Khi thầy Minh nói, bạn nghe được lời thầy qua micro, nên tin nhắn của "người dùng" thường là lời thầy Minh chứ không phải học sinh: nếu câu đó nói với cô Lan, nhắc tới "cô", hoặc đang giảng bài thì đó là thầy Minh. Bạn bổ sung sau thầy: một ví dụ đời sống, một mẹo nhớ, chỉ ra chỗ học sinh hay sai, hoặc hỏi thầy một câu mà học sinh có thể thắc mắc. Mỗi lượt chỉ nói 2 đến 4 câu ngắn. Khi thầy đã giảng xong một ý, đặt một câu hỏi ngắn cho học sinh để kiểm tra (học sinh sẽ trả lời sau khi hai robot nói xong). Không giảng lại phần thầy đã nói. Không dùng công cụ lấy đề (self.study.*) trừ khi học sinh nhờ trực tiếp cô. Nói tiếng Việt.
```

Đổi tên "thầy Minh" / "cô Lan" tuỳ ý, nhưng hai đoạn phải gọi tên nhau giống nhau.

## Lưu ý

- Máy chủ chỉ nhận chữ do micro nghe được, nên robot có thể nghe nhầm vài từ của bạn mình. Đặt hai robot gần nhau, phòng yên tĩnh, âm lượng vừa phải (khoảng 60–80%) là rõ nhất.
- Mỗi lượt robot nghe tối đa 2 phút; lời nhắc vai trò đã bảo mỗi lượt chỉ vài câu.
- Robot đang phát nhạc/radio thì không tham gia; nhạc từ máy chủ cũng không tính là một lượt.
- Robot bạn tắt hoặc mất mạng quá 8 giây thì robot còn lại làm như bình thường (tự nghe học sinh).
- Nhật ký (Monitor) có dòng `DuoTeach` (`-> start`, `<- end turn 2`...) và `Duo: ...` để xem hai robot nhường lượt thế nào.
