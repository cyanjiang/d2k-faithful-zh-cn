@echo off
chcp 65001 >nul
cd /d "%~dp0"
if exist "d2k.exe" (
    start "" "d2k.exe" --display-setup
) else if exist "bin\d2k.exe" (
    start "" "bin\d2k.exe" --display-setup
) else (
    echo 找不到 d2k.exe，请把本文件放在游戏可执行文件所在目录。
    pause
)
