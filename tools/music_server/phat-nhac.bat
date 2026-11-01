@echo off
chcp 65001 >nul
rem Phat nhac MP3 trong thu muc nay cho robot. Bo file .mp3 vao cung thu muc voi file nay.
cd /d "%~dp0"
where py >nul 2>nul && (py -3 phat_nhac.py "%~dp0." & goto :end)
where python >nul 2>nul && (python phat_nhac.py "%~dp0." & goto :end)
if exist "C:\Espressif\tools\python\v6.0.3\venv\Scripts\python.exe" (
  "C:\Espressif\tools\python\v6.0.3\venv\Scripts\python.exe" phat_nhac.py "%~dp0."
  goto :end
)
echo Khong tim thay Python. Cai Python 3 tu python.org roi chay lai.
:end
pause
