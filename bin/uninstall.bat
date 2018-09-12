@echo off

reg add hklm /f>nul 2>&1

if errorlevel 1 (

	echo ERROR: Please run this script with admin rights...
	pause


) else (

	if exist "%windir%\system32\matrix.scr" (
		del /f /q "%windir%\system32\matrix.scr"
	)

	if exist "%windir%\system32\matrix.ini" (
		del /f /q "%windir%\system32\matrix.ini"
	)

)
