@echo off
echo Upload script for bin and to drive D:\homebrew
echo ========================================

REM Check if source files exist
if not exist "homebrew.bin" (
    echo ERROR: homebrew.bin not found in current directory.
    goto :error
)

REM Check if target drive is available
if not exist "D:\" (
    echo ERROR: Drive D:\ not found or not accessible.
    goto :error
)

echo Files found. Beginning upload to D:\ drive...

REM Copy files to D:\ drive
copy "homebrew.bin" "D:\homebrew" /Y
if %ERRORLEVEL% neq 0 (
    echo ERROR: Failed to copy homebrew.bin to D:\ drive.
    goto :error
)
echo Successfully copied homebrew.bin to D:\ drive.

echo ========================================
echo Upload completed successfully!
goto :end

:error
echo ========================================
echo Upload failed. Please check the errors above.
exit /b 1

:end
echo Press any key to exit...
pause > nul
exit /b 0