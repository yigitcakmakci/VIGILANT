@echo off
REM ═══════════════════════════════════════════════════════════════════════
REM  InterviewHandler C++ Test — Build & Run
REM
REM  Usage: run from VIGILANT\tests\ or project root
REM    tests\build_and_run_tests.bat
REM
REM  Requires: VS 2022+ x64 Native Tools Command Prompt
REM            or vcvarsall.bat amd64 called beforehand
REM ═══════════════════════════════════════════════════════════════════════

pushd "%~dp0"

echo.
echo === Building InterviewHandler Tests ===
echo.

call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvarsall.bat" amd64 >nul 2>&1

cl /std:c++20 /EHsc /MT /DNOMINMAX ^
   /I"..\include" /I"..\vendor\sqlite" /I"..\resources" ^
   InterviewHandlerTest.cpp ..\src\AI\InterviewHandler.cpp ^
   /FeInterviewHandlerTest.exe /link /SUBSYSTEM:CONSOLE

if %ERRORLEVEL% neq 0 (
    echo.
    echo BUILD FAILED
    popd
    exit /b 1
)

echo.
echo === Running Tests ===
echo.

InterviewHandlerTest.exe

popd
exit /b %ERRORLEVEL%
