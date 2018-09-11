@echo off

if exist "%windir%\system32\matrix.scr" (
	del /f /q "%windir%\system32\matrix.scr"
	if errorlevel 1 goto error
)

if exist "%windir%\system32\matrix.ini" (
	del /f /q "%windir%\system32\matrix.ini"
	if errorlevel 1 goto error
)

exit

:error
echo ERROR: Please run this script with admin rights...
pause
