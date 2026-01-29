@echo off
setlocal

REM ============================================================================
REM SETUP PATHS
REM %~dp0 is the directory where this script sits (Project Root)
REM ============================================================================
set "PROJECT_ROOT=%~dp0"
set "KTX_SOURCE=%PROJECT_ROOT%KerberosRenderer/vendor\ktx\lib"
set "KTX_BUILD=%PROJECT_ROOT%KerberosRenderer/vendor\ktx\lib\build"

echo [INFO] Project Root: %PROJECT_ROOT%
echo [INFO] KTX Source:   %KTX_SOURCE%
echo [INFO] KTX Build:    %KTX_BUILD%

REM ============================================================================
REM STEP 1: CMAKE CONFIGURATION
REM ============================================================================
echo.
echo [INFO] Configuring KTX CMake...
cmake -S "%KTX_SOURCE%" -B "%KTX_BUILD%" -D BUILD_SHARED_LIBS=OFF
if %errorlevel% neq 0 (
    echo [ERROR] CMake Configuration failed.
    pause
    exit /b %errorlevel%
)

REM ============================================================================
REM STEP 2: BUILD DEBUG
REM ============================================================================
echo.
echo [INFO] Building KTX (Debug)...
cmake --build "%KTX_BUILD%" --config Debug
if %errorlevel% neq 0 (
    echo [ERROR] Debug Build failed.
    pause
    exit /b %errorlevel%
)

REM ============================================================================
REM STEP 3: BUILD RELEASE
REM ============================================================================
echo.
echo [INFO] Building KTX (Release)...
cmake --build "%KTX_BUILD%" --config Release
if %errorlevel% neq 0 (
    echo [ERROR] Release Build failed.
    pause
    exit /b %errorlevel%
)

echo.
echo [SUCCESS] KTX libraries built successfully.
pause