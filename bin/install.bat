@echo off

copy /y /v "%~dp0matrix.scr" "%windir%\system32\matrix.scr"
copy /y /v "%~dp0matrix.ini" "%windir%\system32\matrix.ini"

pause
