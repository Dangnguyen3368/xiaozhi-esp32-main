"""Chương trình chạy trên máy tính cho robot Xiaozhi: phát nhạc MP3 và điều khiển máy tính.

Chạy: python robot_may_tinh.py [thư mục nhạc]   (mặc định: thư mục chứa file này)

Điều khiển máy tính: robot gõ chữ mày đọc vào Word, Excel..., bấm phím và mở vài ứng dụng.
Chụp đề: điện thoại cùng Wi-Fi mở http://<máy tính>:8765/chup, chụp ảnh gửi lên, robot lấy
ảnh đó nhờ AI đọc đề và giảng bài.
Chỉ robot đã ghép mã (mã 4 số hiện trong cửa sổ này) mới điều khiển được.

Robot nghe được máy tính qua tín hiệu UDP phát mỗi 2 giây ở cổng 8766, rồi mở
http://<máy tính>:8765/stream: các bài MP3 trong thư mục được nối liền thành
một luồng như đài radio, phát ngẫu nhiên và lặp lại. /next chuyển bài.
Chỉ dùng thư viện có sẵn của Python 3, không cần cài thêm gì.
"""

import json
import os
import random
import secrets
import socket
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HTTP_PORT = 8765
DISCOVERY_PORT = 8766
# Không cần tự giới hạn tốc độ: robot đọc tới đâu thì TCP mới cho gửi tới đó.
CHUNK = 4096

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
TOKEN_FILE = os.path.join(SCRIPT_DIR, "robot_token.txt")
MAX_PAIR_ATTEMPTS = 5
MUSIC_DIR = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__)))

state_lock = threading.Lock()
stream_generation = 0  # mỗi lần robot mở luồng mới thì luồng cũ dừng lại
skip_requested = False
now_playing = ""


# ---------------------------------------------------------------- ảnh chụp từ điện thoại

MAX_PHOTO_BYTES = 4 * 1024 * 1024
PHOTO_MAX_AGE = 30 * 60  # giây: ảnh cũ hơn thì robot coi như chưa có ảnh mới
latest_photo = None  # (thời điểm, bytes JPEG)

PHONE_PAGE = """<!doctype html>
<html lang="vi"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Chụp đề cho robot</title>
<style>
body{font-family:system-ui,sans-serif;margin:0;padding:16px;background:#f4f6fb;color:#1c2333;text-align:center}
h1{font-size:22px;margin:8px 0 4px}
p{margin:6px 0;color:#4a5468}
label{display:block;margin:18px auto;max-width:360px;padding:22px;border-radius:16px;background:#2f6fed;
color:#fff;font-size:22px;font-weight:600}
input{display:none}
img{max-width:100%;max-height:55vh;border-radius:12px;margin-top:12px}
#st{font-size:18px;font-weight:600;margin-top:12px}
</style></head><body>
<h1>Chụp đề cho robot</h1>
<p>Chụp rõ, đủ sáng, đề nằm ngang màn hình.</p>
<label>&#128247; Chụp đề<input id="f" type="file" accept="image/*" capture="environment"></label>
<div id="st"></div><img id="pv" alt="">
<script>
const st=document.getElementById('st'),pv=document.getElementById('pv');
document.getElementById('f').onchange=e=>{
  const file=e.target.files[0]; if(!file) return;
  st.textContent='Đang thu nhỏ ảnh...';
  const img=new Image();
  img.onload=()=>{
    const k=Math.min(1,1280/Math.max(img.width,img.height));
    const c=document.createElement('canvas');
    c.width=Math.round(img.width*k); c.height=Math.round(img.height*k);
    c.getContext('2d').drawImage(img,0,0,c.width,c.height);
    pv.src=c.toDataURL('image/jpeg',0.8);
    c.toBlob(b=>{
      st.textContent='Đang gửi...';
      fetch('/photo',{method:'POST',headers:{'Content-Type':'image/jpeg'},body:b})
        .then(r=>r.ok?r.text():Promise.reject(r.status))
        .then(()=>{st.textContent='Đã gửi! Giờ nói với robot: "giải bài vừa chụp".'})
        .catch(x=>{st.textContent='Gửi lỗi ('+x+'). Thử lại.'});
    },'image/jpeg',0.85);
    URL.revokeObjectURL(img.src);
  };
  img.onerror=()=>{st.textContent='Không đọc được ảnh này.'};
  img.src=URL.createObjectURL(file);
};
</script></body></html>"""


# ---------------------------------------------------------------- ghép robot

def load_tokens():
    try:
        with open(TOKEN_FILE, encoding="utf-8") as f:
            return {line.strip() for line in f if line.strip()}
    except OSError:
        return set()


tokens = load_tokens()
pair_code = f"{secrets.randbelow(10000):04d}"
pair_attempts = 0


def try_pair(code):
    global pair_code, pair_attempts
    with state_lock:
        if pair_attempts >= MAX_PAIR_ATTEMPTS:
            return None, "Sai mã quá nhiều lần. Tắt rồi mở lại robot-may-tinh.bat để ghép lại."
        if not secrets.compare_digest(str(code).strip(), pair_code):
            pair_attempts += 1
            return None, "Sai mã ghép. Đọc đúng 4 số trong cửa sổ trên máy tính."
        token = secrets.token_hex(16)
        tokens.add(token)
        with open(TOKEN_FILE, "a", encoding="utf-8") as f:
            f.write(token + "\n")
        pair_code = f"{secrets.randbelow(10000):04d}"
        pair_attempts = 0
    print(f"Đã ghép robot. Mã ghép mới (cho robot khác): {pair_code}")
    return token, None


# ---------------------------------------------------------------- điều khiển bàn phím (Windows)

if os.name == "nt":
    import ctypes
    from ctypes import wintypes

    ULONG_PTR = ctypes.c_size_t

    class MOUSEINPUT(ctypes.Structure):
        _fields_ = [("dx", wintypes.LONG), ("dy", wintypes.LONG), ("mouseData", wintypes.DWORD),
                    ("dwFlags", wintypes.DWORD), ("time", wintypes.DWORD), ("dwExtraInfo", ULONG_PTR)]

    class KEYBDINPUT(ctypes.Structure):
        _fields_ = [("wVk", wintypes.WORD), ("wScan", wintypes.WORD), ("dwFlags", wintypes.DWORD),
                    ("time", wintypes.DWORD), ("dwExtraInfo", ULONG_PTR)]

    class HARDWAREINPUT(ctypes.Structure):
        _fields_ = [("uMsg", wintypes.DWORD), ("wParamL", wintypes.WORD), ("wParamH", wintypes.WORD)]

    class _INPUTUNION(ctypes.Union):
        _fields_ = [("mi", MOUSEINPUT), ("ki", KEYBDINPUT), ("hi", HARDWAREINPUT)]

    class INPUT(ctypes.Structure):
        _fields_ = [("type", wintypes.DWORD), ("union", _INPUTUNION)]

    INPUT_KEYBOARD = 1
    KEYEVENTF_KEYUP = 0x0002
    KEYEVENTF_UNICODE = 0x0004
    user32 = ctypes.WinDLL("user32", use_last_error=True)
    user32.SendInput.argtypes = (wintypes.UINT, ctypes.POINTER(INPUT), ctypes.c_int)
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    user32.OpenClipboard.argtypes = (wintypes.HWND,)
    user32.OpenClipboard.restype = wintypes.BOOL
    user32.CloseClipboard.restype = wintypes.BOOL
    user32.EmptyClipboard.restype = wintypes.BOOL
    user32.GetClipboardData.argtypes = (wintypes.UINT,)
    user32.GetClipboardData.restype = wintypes.HANDLE
    user32.SetClipboardData.argtypes = (wintypes.UINT, wintypes.HANDLE)
    user32.SetClipboardData.restype = wintypes.HANDLE
    kernel32.GlobalAlloc.argtypes = (wintypes.UINT, ctypes.c_size_t)
    kernel32.GlobalAlloc.restype = wintypes.HGLOBAL
    kernel32.GlobalLock.argtypes = (wintypes.HGLOBAL,)
    kernel32.GlobalLock.restype = ctypes.c_void_p
    kernel32.GlobalUnlock.argtypes = (wintypes.HGLOBAL,)
    kernel32.GlobalFree.argtypes = (wintypes.HGLOBAL,)
    CF_UNICODETEXT = 13
    GMEM_MOVEABLE = 0x0002

    def _open_clipboard():
        for _ in range(10):  # một chương trình khác có thể đang giữ clipboard
            if user32.OpenClipboard(None):
                return True
            time.sleep(0.05)
        return False

    def get_clipboard_text():
        if not _open_clipboard():
            return None
        try:
            handle = user32.GetClipboardData(CF_UNICODETEXT)
            if not handle:
                return None
            pointer = kernel32.GlobalLock(handle)
            if not pointer:
                return None
            try:
                return ctypes.wstring_at(pointer)
            finally:
                kernel32.GlobalUnlock(handle)
        finally:
            user32.CloseClipboard()

    def set_clipboard_text(text):
        data = ctypes.create_unicode_buffer(text)
        size = ctypes.sizeof(data)
        handle = kernel32.GlobalAlloc(GMEM_MOVEABLE, size)
        if not handle:
            return False
        pointer = kernel32.GlobalLock(handle)
        if not pointer:
            kernel32.GlobalFree(handle)
            return False
        ctypes.memmove(pointer, data, size)
        kernel32.GlobalUnlock(handle)
        if not _open_clipboard():
            kernel32.GlobalFree(handle)
            return False
        try:
            user32.EmptyClipboard()
            if not user32.SetClipboardData(CF_UNICODETEXT, handle):
                kernel32.GlobalFree(handle)
                return False
            return True  # Windows giữ vùng nhớ từ đây
        finally:
            user32.CloseClipboard()

    def _send(inputs):
        array = (INPUT * len(inputs))(*inputs)
        user32.SendInput(len(inputs), array, ctypes.sizeof(INPUT))

    def _key_input(vk, up=False):
        return INPUT(type=INPUT_KEYBOARD,
                     union=_INPUTUNION(ki=KEYBDINPUT(wVk=vk, wScan=0, dwFlags=KEYEVENTF_KEYUP if up else 0,
                                                     time=0, dwExtraInfo=0)))

    def _unicode_input(unit, up=False):
        flags = KEYEVENTF_UNICODE | (KEYEVENTF_KEYUP if up else 0)
        return INPUT(type=INPUT_KEYBOARD,
                     union=_INPUTUNION(ki=KEYBDINPUT(wVk=0, wScan=unit, dwFlags=flags, time=0, dwExtraInfo=0)))
else:
    def _send(inputs):
        raise RuntimeError("Điều khiển máy tính chỉ chạy trên Windows.")

    def get_clipboard_text():
        return None

    def set_clipboard_text(text):
        return False

    def _key_input(vk, up=False):
        return None

    def _unicode_input(unit, up=False):
        return None


VK = {
    "enter": 0x0D, "tab": 0x09, "backspace": 0x08, "delete": 0x2E, "del": 0x2E, "esc": 0x1B,
    "escape": 0x1B, "space": 0x20, "up": 0x26, "down": 0x28, "left": 0x25, "right": 0x27,
    "home": 0x24, "end": 0x23, "pageup": 0x21, "pagedown": 0x22, "win": 0x5B, "ctrl": 0x11,
    "control": 0x11, "alt": 0x12, "shift": 0x10,
}
for _i in range(1, 13):
    VK[f"f{_i}"] = 0x6F + _i
MODIFIERS = {"ctrl", "control", "alt", "shift", "win"}


def type_text(text):
    """Dán qua clipboard: đúng từng chữ, nhanh, không bị Unikey/EVKey sửa dấu hay rơi chữ.
    Clipboard cũ được trả lại sau khi dán. Nếu không dùng được clipboard thì gõ từng phím."""
    if os.name == "nt":
        old = get_clipboard_text()
        if set_clipboard_text(text.replace("\r\n", "\n").replace("\n", "\r\n")):
            press_keys("ctrl+v")
            time.sleep(0.4)  # chờ ứng dụng dán xong rồi mới trả clipboard cũ
            if old is not None:
                set_clipboard_text(old)
            return
    type_keys(text)


def type_keys(text):
    inputs = []
    for ch in text:
        if ch == "\n":
            inputs += [_key_input(VK["enter"]), _key_input(VK["enter"], True)]
        elif ch == "\t":
            inputs += [_key_input(VK["tab"]), _key_input(VK["tab"], True)]
        elif ch == "\r":
            continue
        else:
            data = ch.encode("utf-16-le")
            for i in range(0, len(data), 2):
                unit = int.from_bytes(data[i:i + 2], "little")
                inputs += [_unicode_input(unit), _unicode_input(unit, True)]
    if inputs:
        _send(inputs)


def press_keys(spec):
    """'ctrl+s' hoặc nhiều tổ hợp cách nhau bằng dấu cách: 'tab tab enter'."""
    for combo in spec.lower().split():
        parts = [p for p in combo.split("+") if p]
        codes = []
        for part in parts:
            if part in VK:
                codes.append(VK[part])
            elif len(part) == 1 and part.isalnum():
                codes.append(ord(part.upper()))
            else:
                raise ValueError(f"Không biết phím '{part}'.")
        if not codes:
            continue
        inputs = [_key_input(c) for c in codes] + [_key_input(c, True) for c in reversed(codes)]
        _send(inputs)
        time.sleep(0.05)


APPS = {
    "word": "winword", "excel": "excel", "powerpoint": "powerpnt", "notepad": "notepad",
    "calculator": "calc", "calc": "calc", "browser": "https://www.google.com",
}


def open_app(name):
    target = APPS.get(name.strip().lower())
    if target is None:
        raise ValueError("Chỉ mở được: " + ", ".join(sorted(set(APPS))))
    if os.name != "nt":
        raise RuntimeError("Điều khiển máy tính chỉ chạy trên Windows.")
    subprocess.Popen(["cmd", "/c", "start", "", target], shell=False)


def run_control(action, text):
    if action == "type":
        type_text(text)
        print(f"Gõ: {text}")
        return f"Typed {len(text)} characters."
    if action == "key":
        press_keys(text)
        print(f"Phím: {text}")
        return "Keys pressed."
    if action == "open":
        open_app(text)
        print(f"Mở: {text}")
        return f"Opening {text}."
    raise ValueError("Use action type, key or open.")


# ---------------------------------------------------------------- nhạc

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
        if self.path.startswith("/chup"):
            self.reply(200, PHONE_PAGE, "text/html; charset=utf-8")
            return
        if self.path.startswith("/photo"):
            self.send_photo()
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

    def send_photo(self):
        if self.headers.get("X-Robot-Token", "") not in tokens:
            self.reply_json(403, {"error": "Robot is not paired."})
            return
        with state_lock:
            photo = latest_photo
        if photo is None or time.time() - photo[0] > PHOTO_MAX_AGE:
            self.reply_json(404, {"error": "Chưa có ảnh mới."})
            return
        print(f"Robot {self.client_address[0]} lấy ảnh ({len(photo[1]) // 1024} KB)")
        self.send_response(200)
        self.send_header("Content-Type", "image/jpeg")
        self.send_header("Content-Length", str(len(photo[1])))
        self.end_headers()
        self.wfile.write(photo[1])

    def receive_photo(self, length):
        global latest_photo
        if length <= 0 or length > MAX_PHOTO_BYTES:
            self.reply(413, "Ảnh quá lớn.")
            return
        data = self.rfile.read(length)
        if not data.startswith(b"\xff\xd8"):
            self.reply(400, "Không phải ảnh JPEG.")
            return
        with state_lock:
            latest_photo = (time.time(), data)
        print(f"Đã nhận ảnh từ điện thoại {self.client_address[0]} ({len(data) // 1024} KB). "
              "Nói với robot: 'giải bài vừa chụp'.")
        self.reply(200, "ok")

    def do_POST(self):
        length = int(self.headers.get("Content-Length") or 0)
        if self.path.startswith("/photo"):
            self.receive_photo(length)
            return
        if length > 16384:
            self.reply_json(413, {"error": "Request too large."})
            return
        try:
            data = json.loads(self.rfile.read(length) or b"{}")
        except ValueError:
            self.reply_json(400, {"error": "Bad JSON."})
            return
        if self.path.startswith("/pair"):
            token, error = try_pair(data.get("code", ""))
            if error:
                self.reply_json(403, {"error": error})
            else:
                self.reply_json(200, {"token": token})
            return
        if not self.path.startswith("/control"):
            self.reply_json(404, {"error": "Not found."})
            return
        if self.headers.get("X-Robot-Token", "") not in tokens:
            self.reply_json(403, {"error": "Robot is not paired. Ask the user for the 4-digit code shown in "
                                           "the robot-may-tinh window, then use action pair."})
            return
        try:
            message = run_control(str(data.get("action", "")), str(data.get("text", "")))
        except (ValueError, RuntimeError, OSError) as e:
            self.reply_json(400, {"error": str(e)})
            return
        self.reply_json(200, {"ok": True, "message": message})

    def reply_json(self, code, obj):
        self.reply(code, json.dumps(obj, ensure_ascii=False), "application/json; charset=utf-8")

    def reply(self, code, text, content_type="text/plain; charset=utf-8"):
        body = text.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", content_type)
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
    print("=== Robot - máy tính ===")
    print(f"Thư mục nhạc: {MUSIC_DIR} ({count} bài .mp3)")
    print(f"Địa chỉ: http://{local_ip()}:{HTTP_PORT}/stream")
    print("Nghe nhạc: trên robot vào Menu > Nhạc máy tính, hoặc nói 'mở nhạc trong máy tính'.")
    if tokens:
        print(f"Điều khiển máy tính: đã ghép {len(tokens)} robot.")
    print(f"Mã ghép robot: {pair_code}  (nói với robot: 'ghép máy tính, mã {pair_code}')")
    print(f"Chụp đề bằng điện thoại (cùng Wi-Fi): mở http://{local_ip()}:{HTTP_PORT}/chup")
    print("Nếu Windows hỏi tường lửa: tích 'Private networks' rồi bấm Allow.")
    print("Đóng cửa sổ này để tắt.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
