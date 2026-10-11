@echo off
rem Instaluje Markdown Live do Notepad++ (wymaga zamknietego Notepad++ i uprawnien administratora).
setlocal
set "SRC=%~dp0build\MarkdownLive"
set "DST=%ProgramFiles%\Notepad++\plugins\MarkdownLive"

if not exist "%SRC%\MarkdownLive.dll" (
  echo Brak zbudowanej wtyczki: %SRC%
  pause & exit /b 1
)
tasklist /FI "IMAGENAME eq notepad++.exe" 2>nul | find /I "notepad++.exe" >nul && (
  echo Notepad++ jest uruchomiony. Zapisz prace, zamknij go i uruchom instalacje ponownie.
  pause & exit /b 1
)
net session >nul 2>&1 || (
  powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
  exit /b
)

robocopy "%SRC%" "%DST%" /MIR /NFL /NDL /NJH /NJS >nul
if %ERRORLEVEL% GEQ 8 (
  echo Blad kopiowania do %DST%
  pause & exit /b 1
)
echo Zainstalowano: %DST%
echo Uruchom Notepad++ i otworz plik .md - podglad otworzy sie sam (Ctrl+Alt+M przelacza).
pause
