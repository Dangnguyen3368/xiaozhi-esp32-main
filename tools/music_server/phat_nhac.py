"""Phát nhạc MP3 trong một thư mục trên máy tính cho robot Xiaozhi.

Chạy: python phat_nhac.py [thư mục nhạc]   (mặc định: thư mục chứa file này)

Robot nghe được máy tính qua tín hiệu UDP phát mỗi 2 giây ở cổng 8766, rồi mở
http://<máy tính>:8765/stream: các bài MP3 trong thư mục được nối liền thành
một luồng như đài radio, phát ngẫu nhiên và lặp lại. /next chuyển bài.
Chỉ dùng thư viện có sẵn của Python 3, không cần cài thêm gì.
"""

import os
import random
import socket
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HTTP_PORT = 8765
DISCOVERY_PORT = 8766
# Không cần tự giới hạn tốc độ: robot đọc tới đâu thì TCP mới cho gửi tới đó.
CHUNK = 4096

MUSIC_DIR = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__)))

state_lock = threading.Lock()
stream_generation = 0  # mỗi lần robot mở luồng mới thì luồng cũ dừng lại
skip_requested = False
now_playing = ""


def list_songs():
    songs = []
    for root, _dirs, files in os.walk(MUSIC_DIR):
        for name in files:
            if name.lower().endswith(".mp3"):
                songs.append(os.path.join(root, name))
    return songs


def skip_id3(f):
    """Bỏ phần thẻ ID3v2 (ảnh bìa, tên bài) ở đầu file để robot không phải đọc qua."""
    header = f.read(10)
    if len(header) == 10 and header[:3] == b"ID3":
        size = (header[6] << 21) | (header[7] << 14) | (header[8] << 7) | header[9]
        f.seek(10 + size)
    else:
        f.seek(0)


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def log_message(self, fmt, *args):
        pass

    def do_GET(self):
        global stream_generation, skip_requested, now_playing
        if self.path.startswith("/next"):
            with state_lock:
                skip_requested = True
            self.reply(200, "ok")
            return
        if self.path.startswith("/now"):
            self.reply(200, now_playing)
            return
        if self.path.startswith("/ping"):
            self.reply(200, "XIAOZHI_MUSIC")
            return
        if not self.path.startswith("/stream"):
            self.reply(404, "not found")
            return

        with state_lock:
            stream_generation += 1
            my_generation = stream_generation
            skip_requested = False
        print(f"Robot {self.client_address[0]} bắt đầu nghe nhạc")
        self.send_response(200)
        self.send_header("Content-Type", "audio/mpeg")
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        try:
            while my_generation == stream_generation:
                songs = list_songs()
                if not songs:
                    print(f"Chưa có file .mp3 nào trong {MUSIC_DIR}")
                    time.sleep(5)
                    continue
                random.shuffle(songs)
                for path in songs:
                    if my_generation != stream_generation:
                        break
                    if not self.send_song(path, my_generation):
                        return
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass
        print("Robot đã ngắt nhạc")

    def send_song(self, path, my_generation):
        global skip_requested, now_playing
        title = os.path.splitext(os.path.basename(path))[0]
        now_playing = title
        print(f"Đang phát: {title}")
        with open(path, "rb") as f:
            skip_id3(f)
            while True:
                with state_lock:
                    if my_generation != stream_generation:
                        return True
                    if skip_requested:
                        skip_requested = False
                        return True
                data = f.read(CHUNK)
                if not data:
                    return True
                self.wfile.write(data)

    def reply(self, code, text):
        body = text.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def local_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))  # không gửi gì, chỉ để biết card mạng đang dùng
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


def announce():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    message = f"XIAOZHI_MUSIC {HTTP_PORT}".encode()
    while True:
        ip = local_ip()
        targets = ["255.255.255.255"]
        if ip.count(".") == 3 and not ip.startswith("127."):
            targets.append(ip.rsplit(".", 1)[0] + ".255")
            try:
                sock.bind((ip, 0))
            except OSError:
                pass
        for target in targets:
            try:
                sock.sendto(message, (target, DISCOVERY_PORT))
            except OSError:
                pass
        time.sleep(2)


def main():
    if not os.path.isdir(MUSIC_DIR):
        print(f"Không thấy thư mục {MUSIC_DIR}")
        sys.exit(1)
    count = len(list_songs())
    threading.Thread(target=announce, daemon=True).start()
    server = ThreadingHTTPServer(("0.0.0.0", HTTP_PORT), Handler)
    print("=== Máy phát nhạc cho robot ===")
    print(f"Thư mục nhạc: {MUSIC_DIR} ({count} bài .mp3)")
    print(f"Địa chỉ: http://{local_ip()}:{HTTP_PORT}/stream")
    print("Trên robot: Menu > Nhạc máy tính, hoặc nói 'mở nhạc trong máy tính'.")
    print("Nếu Windows hỏi tường lửa: tích 'Private networks' rồi bấm Allow.")
    print("Đóng cửa sổ này để tắt.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
