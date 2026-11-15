@echo off
chcp 65001 >nul
rem Сборка krendtrademonitor: MSVC из VS 18 Build Tools + Qt 5.12.12 (msvc2017_64).
rem Готовый файл с DLL Qt рядом: build-rel\krendtrademonitor.exe
rem Файл в UTF-8 с концами строк CRLF; chcp 65001 — чтобы cmd правильно читал русские строки.
setlocal
set QTDIR=C:\prog\cpp\qt\forVS2019\5.12.12\msvc2017_64
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
if not exist build-rel\CMakeCache.txt (
  cmake -S . -B build-rel -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=%QTDIR% || exit /b 1
)
cmake --build build-rel || exit /b 1
"%QTDIR%\bin\windeployqt.exe" --release --no-translations --no-compiler-runtime build-rel\krendtrademonitor.exe >nul || exit /b 1
echo.
echo OK: build-rel\krendtrademonitor.exe
