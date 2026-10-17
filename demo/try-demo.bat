@echo off
rem Runs CMS Portal with the sample accounts and records in the demo-data folder.
cd /d "%~dp0"
cms.exe --data demo-data
pause
