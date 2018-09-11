@echo off

rem Copy required files...
@copy /y "%~dp0matrix.scr" "%windir%\system32\matrix.scr"

if errorlevel 1 goto error

if not exist "%windir%\system32\matrix.ini" (
	@copy /y "%~dp0matrix.ini" "%windir%\system32\matrix.ini"
)

rem Open screesaver dialog...
rundll32.exe shell32.dll,Control_RunDLL desk.cpl,ScreenSaver,@ScreenSaver

exit

:error
echo ERROR: Please run this script with admin rights...
pause
