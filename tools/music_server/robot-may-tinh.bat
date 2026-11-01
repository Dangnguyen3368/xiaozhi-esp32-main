@echo off
chcp 65001 >nul
rem Chuong trinh may tinh cho robot: phat nhac MP3 trong thu muc nay va cho robot dieu khien may.
cd /d "%~dp0"
where py >nul 2>nul && (py -3 robot_may_tinh.py "%~dp0." & goto :end)
where python >nul 2>nul && (python robot_may_tinh.py "%~dp0." & goto :end)
if exist "C:\Espressif\tools\python\v6.0.3\venv\Scripts\python.exe" (
  "C:\Espressif\tools\python\v6.0.3\venv\Scripts\python.exe" robot_may_tinh.py "%~dp0."
  goto :end
)
echo Khong tim thay Python. Cai Python 3 tu python.org roi chay lai.
:end
pause
