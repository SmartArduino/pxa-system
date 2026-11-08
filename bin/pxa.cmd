@echo off
setlocal
set "PXA_ROOT=%~dp0.."
set "PYTHONPATH=%PXA_ROOT%\tools;%PYTHONPATH%"
set "PYTHONDONTWRITEBYTECODE=1"
set "PYTHONUTF8=1"
set "PATH=%PXA_ROOT%\runtime\bin;%PATH%"
if not defined PYTHON if exist "%PXA_ROOT%\runtime\python\python.exe" set "PYTHON=%PXA_ROOT%\runtime\python\python.exe"
if defined PYTHON (
  "%PYTHON%" -X utf8 -m pxa_tools %*
) else (
  python -X utf8 -m pxa_tools %*
)
exit /b %errorlevel%
