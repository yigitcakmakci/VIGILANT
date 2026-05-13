@echo off
setlocal

:: ============================================================
::  VIGILANT - Standalone Export Script
::  Release build dosyalarini "export" klasorune kopyalar.
::  Kullanim: export.bat
:: ============================================================

set SCRIPT_DIR=%~dp0
set EXPORT_DIR=%SCRIPT_DIR%export
set RELEASE_DIR=%SCRIPT_DIR%x64\Release

echo.
echo ============================================
echo   VIGILANT - Export Araci
echo ============================================
echo.

:: Release build kontrolu
if not exist "%RELEASE_DIR%\VIGILANT.exe" (
	echo [HATA] Release build bulunamadi: %RELEASE_DIR%\VIGILANT.exe
	echo        Once Visual Studio'da Release ^| x64 modunda build yapin.
	echo.
	pause
	exit /b 1
)

:: Export klasorunu olustur
if not exist "%EXPORT_DIR%" mkdir "%EXPORT_DIR%"

:: EXE kopyala
echo [1/3] VIGILANT.exe kopyalaniyor...
copy /Y "%RELEASE_DIR%\VIGILANT.exe" "%EXPORT_DIR%\" >nul

:: WebView2Loader.dll kopyala
if exist "%RELEASE_DIR%\WebView2Loader.dll" (
	echo [2/3] WebView2Loader.dll kopyalaniyor...
	copy /Y "%RELEASE_DIR%\WebView2Loader.dll" "%EXPORT_DIR%\" >nul
) else (
	echo [2/3] WebView2Loader.dll bulunamadi, atlaniyor.
)

:: ============================================================
::  OPSIYONEL: vc_redist.x64.exe indir
::  Proje /MT ile derlendigi icin genelde gerekmez.
::  Ancak bazi sistemlerde sorun yasanirsa asagidaki satirin
::  basindaki :: isaretlerini kaldirip tekrar calistirin.
:: ============================================================
:: echo [OPS] vc_redist.x64.exe indiriliyor...
:: powershell -Command "Invoke-WebRequest -Uri 'https://aka.ms/vs/17/release/vc_redist.x64.exe' -OutFile '%EXPORT_DIR%\vc_redist.x64.exe'"

:: ============================================================
::  OPSIYONEL: WebView2 Runtime Bootstrapper indir
::  Hedef bilgisayarda WebView2 yuklu degilse bu dosyayi
::  calistirarak kurulabilir.
:: ============================================================
:: echo [OPS] MicrosoftEdgeWebview2Setup.exe indiriliyor...
:: powershell -Command "Invoke-WebRequest -Uri 'https://go.microsoft.com/fwlink/p/?LinkId=2124703' -OutFile '%EXPORT_DIR%\MicrosoftEdgeWebview2Setup.exe'"

echo [3/3] README.txt kopyalaniyor...
if exist "%SCRIPT_DIR%export\README.txt" (
	echo        README.txt zaten mevcut.
) else (
	echo        README.txt bulunamadi.
)

echo.
echo ============================================
echo   Export tamamlandi!
echo   Konum: %EXPORT_DIR%
echo ============================================
echo.
echo   Dosyalar:
dir /B "%EXPORT_DIR%"
echo.

pause
