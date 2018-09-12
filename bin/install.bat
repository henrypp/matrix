@echo off

set "MATRIX_PATH=%~dp0matrix.scr"
set "MATRIX_PATH_DEST=%windir%\system32\matrix.scr"

if not exist "%MATRIX_PATH%" (
	echo ERROR: "matrix.scr" not found.
	pause
) else (
	reg add hklm /f>nul 2>&1

	if errorlevel 1 (
		echo ERROR: Please run this script with admin rights...
		pause
	) else (
		rem Copy required files...
		copy /y %MATRIX_PATH% %MATRIX_PATH_DEST%

		rem Set as default screensaver...
		if exist %MATRIX_PATH_DEST% (
			reg add "HKCU\Control Panel\Desktop" /v "ScreenSaveActive" /t REG_SZ /d "1" /f
			reg add "HKCU\Control Panel\Desktop" /v "SCRNSAVE.EXE" /t REG_SZ /d "%MATRIX_PATH_DEST%" /f
		)

		rem Open screesaver dialog...
		rem rundll32.exe shell32.dll,Control_RunDLL desk.cpl,ScreenSaver,@ScreenSaver
	)
)
