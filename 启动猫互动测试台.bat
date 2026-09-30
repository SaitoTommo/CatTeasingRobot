@echo off
setlocal
pushd "%~dp0" || (
  echo Cannot open the test lab directory.
  pause
  exit /b 1
)

where node.exe >nul 2>&1 || (
  echo Node.js was not found. Install Node.js or add it to PATH, then try again.
  pause
  exit /b 1
)

call :check_lab
if not errorlevel 1 goto open_lab
call :check_old_lab
if not errorlevel 1 (
  echo An older cat interaction lab server is using port 8765.
  echo Close its server window, then run this BAT file again.
  pause
  exit /b 1
)

echo Starting the cat interaction lab...
start "Cat interaction lab server" cmd /k "node server.mjs"

for /L %%I in (1,1,20) do (
  call :check_lab
  if not errorlevel 1 goto open_lab
  timeout /t 1 /nobreak >nul
)

echo The lab did not start. Check the server window for the error.
pause
exit /b 1

:open_lab
echo Opening http://localhost:8765/
start "" "http://localhost:8765/"
echo Keep the server window open while testing. Close it to stop the lab.
popd
exit /b 0

:check_lab
powershell -NoProfile -Command "try { $r = Invoke-WebRequest -Uri 'http://127.0.0.1:8765/' -UseBasicParsing -TimeoutSec 1; $m = Invoke-WebRequest -Uri 'http://127.0.0.1:8765/robot-observations.mjs' -UseBasicParsing -TimeoutSec 1; if ($r.StatusCode -eq 200 -and $r.Content.Contains('connectSerial') -and $m.StatusCode -eq 200) { exit 0 } } catch {}; exit 1" >nul 2>&1
exit /b %errorlevel%

:check_old_lab
powershell -NoProfile -Command "try { $r = Invoke-WebRequest -Uri 'http://127.0.0.1:8765/' -UseBasicParsing -TimeoutSec 1; if ($r.StatusCode -eq 200 -and $r.Content.Contains('connectSerial')) { exit 0 } } catch {}; exit 1" >nul 2>&1
exit /b %errorlevel%
