@echo off
call "%~dp0pxa.cmd" device %*
exit /b %errorlevel%
