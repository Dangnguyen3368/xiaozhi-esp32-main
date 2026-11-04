"""Generates the Vietnamese voice clips the robot uses to read bank transfer amounts.

Run: pip install edge-tts && python tools/money_voice/gen.py
Needs ffmpeg. Writes main/assets/money_vi/<name>.ogg (Opus, 16 kHz mono, like the other sounds).
"""

import asyncio
import os
import subprocess
import sys
import tempfile

import edge_tts

VOICE = "vi-VN-HoaiMyNeural"
OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "main", "assets", "money_vi")

# File name (ASCII) -> words. Order does not matter.
CLIPS = {
    "da_nhan": "Đã nhận",
    "dong": "đồng",
    "khong": "không",
    "mot": "một",
    "hai": "hai",
    "ba": "ba",
    "bon": "bốn",
    "nam": "năm",
    "sau": "sáu",
    "bay": "bảy",
    "tam": "tám",
    "chin": "chín",
    "muoi": "mười",
    "muoi2": "mươi",
    "mot2": "mốt",
    "lam": "lăm",
    "linh": "linh",
    "tram": "trăm",
    "nghin": "nghìn",
    "trieu": "triệu",
    "ty": "tỷ",
    "bien_dong": "Tài khoản vừa có biến động",
}


async def make(name, text, tmp):
    mp3 = os.path.join(tmp, name + ".mp3")
    await edge_tts.Communicate(text, VOICE, rate="+10%").save(mp3)
    ogg = os.path.join(OUT_DIR, name + ".ogg")
    # Trim leading/trailing silence so the words join smoothly, then encode like
    # scripts/mp3_to_ogg.sh does.
    subprocess.run([
        "ffmpeg", "-y", "-loglevel", "error", "-i", mp3,
        "-af", "silenceremove=start_periods=1:start_threshold=-45dB,areverse,"
               "silenceremove=start_periods=1:start_threshold=-45dB,areverse,apad=pad_dur=0.04",
        "-c:a", "libopus", "-b:a", "16k", "-ac", "1", "-ar", "16000", "-frame_duration", "60", ogg,
    ], check=True)
    print(f"{name}: {text} -> {os.path.getsize(ogg)} bytes")


async def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        for name, text in CLIPS.items():
            await make(name, text, tmp)


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
