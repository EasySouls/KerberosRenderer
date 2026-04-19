@echo off
REM Change the current directory to the directory where the batch file is located
CD /D %~dp0
python Setup.py
PAUSE