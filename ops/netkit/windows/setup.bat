@echo off
rem ============================================================================
rem NETKIT Deliverable 1 - TightVNC server setup for the iMac (Windows 10 Home)
rem Run from the external drive:  D:\setup.bat   (double-click, or run in cmd)
rem
rem Does, idempotently:
rem   1. self-elevates (UAC) if not already administrator
rem   2. reads setup.conf (next to this file) for settings and password
rem   3. removes any existing TightVNC (MSI) and, by default, its saved settings
rem   4. installs the TightVNC MSI found next to this file (else downloads it)
rem      as a Windows service with VNC password authentication
rem   5. opens a Windows Firewall rule for the VNC TCP port
rem   6. makes the tvnserver service auto-start and verifies it is running
rem   7. writes vnc-log.txt next to this file (timestamps, versions, service
rem      state, listening port, IPv4 addresses, MAC addresses)
rem
rem Honest status: STOPGAP. This is a shell script around a third-party binary
rem (TightVNC) because the VNC server is an OS service. The VNC client is
rem PARENA-first in SHANKPIT/android-netkit. See README.md in this folder.
rem Written without access to Windows: not executed on a real machine yet.
rem
rem Passwords are never hardcoded here. Do not use a double quote character in
rem a password, and do not end it with a backslash.
rem ============================================================================
setlocal EnableExtensions DisableDelayedExpansion

set "SCRIPT_VERSION=1"
set "HERE=%~dp0"
set "SELF=%~f0"
set "ARGS=_ %*"
set "SVC=tvnserver"
set "FAILS=0"
set "WARNS=0"
set "NOPAUSE="
set "ELEVATED="
if not "%ARGS:/nopause=%"=="%ARGS%" set "NOPAUSE=1"
if not "%ARGS:elevated=%"=="%ARGS%" set "ELEVATED=1"

rem ---- log file: next to the script, else %TEMP% when the drive is read-only
set "LOG=%HERE%vnc-log.txt"
verify >nul
(>>"%LOG%" echo.) 2>nul
if errorlevel 1 set "LOG=%TEMP%\vnc-log.txt"

call :log "=============================================================="
call :log "NETKIT VNC setup.bat v%SCRIPT_VERSION% starting"
call :log "script: %SELF%"
call :log "log file: %LOG%"

rem ---- 1. administrator check and self-elevation
fltmc >nul 2>&1
if not errorlevel 1 goto :admin_ok
if defined ELEVATED goto :elev_failed
call :log "Not administrator. Asking Windows for elevation (UAC prompt)."
echo.
echo A Windows permission prompt will appear. Click Yes to continue.
powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath '%SELF%' -ArgumentList 'elevated %ARGS:_ =%' -Verb RunAs"
if errorlevel 1 goto :elev_failed
call :log "Elevated copy launched in a new window; this window can be closed."
exit /b 0

:elev_failed
call :log "FAILED: administrator rights are required and could not be obtained."
call :log "Right-click setup.bat and choose Run as administrator, or accept the UAC prompt."
set "FAILS=1"
goto :finish

:admin_ok
call :log "Running as administrator."
cd /d "%HERE%" 2>nul

rem ---- system info
ver >>"%LOG%"
set "ARCH=32bit"
if /i "%PROCESSOR_ARCHITECTURE%"=="AMD64" set "ARCH=64bit"
if /i "%PROCESSOR_ARCHITEW6432%"=="AMD64" set "ARCH=64bit"
call :log "computer: %COMPUTERNAME%  user: %USERNAME%  cpu arch: %PROCESSOR_ARCHITECTURE%  msi arch: %ARCH%"

rem ---- 2. defaults then setup.conf
set "VNC_PORT=5900"
set "VNC_PASSWORD_FILE=vnc-password.txt"
set "FW_PROFILE=any"
set "FW_REMOTEIP=localsubnet"
set "NETWORK_PRIVATE=0"
set "KEEP_AWAKE=1"
set "RESET_SETTINGS=1"
set "MSI_URL="
set "MSI_FILE="
set "MSI_SHA256="
set "VNC_PASSWORD="
set "VNC_VIEWONLY_PASSWORD="
set "VNC_CONTROL_PASSWORD="
set "PW_SOURCE="

set "CONF=%HERE%setup.conf"
if not exist "%CONF%" call :log "No setup.conf found (optional); using defaults."
if exist "%CONF%" call :log "Reading %CONF%"
if exist "%CONF%" for /f "usebackq eol=# tokens=1* delims==" %%A in ("%CONF%") do set "CFG_%%A=%%B"
rem an empty KEY= line leaves the for variable unexpanded; treat that as unset
if exist "%CONF%" for /f "usebackq tokens=1* delims==" %%V in (`set CFG_ 2^>nul`) do if "%%W"=="%%B" set "%%V="

if defined CFG_VNC_PASSWORD set "VNC_PASSWORD=%CFG_VNC_PASSWORD%"
if defined CFG_VNC_VIEWONLY_PASSWORD set "VNC_VIEWONLY_PASSWORD=%CFG_VNC_VIEWONLY_PASSWORD%"
if defined CFG_VNC_CONTROL_PASSWORD set "VNC_CONTROL_PASSWORD=%CFG_VNC_CONTROL_PASSWORD%"
if defined CFG_VNC_PASSWORD_FILE set "VNC_PASSWORD_FILE=%CFG_VNC_PASSWORD_FILE%"
if defined CFG_VNC_PORT set "VNC_PORT=%CFG_VNC_PORT%"
if defined CFG_MSI_FILE set "MSI_FILE=%CFG_MSI_FILE%"
if defined CFG_MSI_URL set "MSI_URL=%CFG_MSI_URL%"
if defined CFG_MSI_SHA256 set "MSI_SHA256=%CFG_MSI_SHA256%"
if defined CFG_FW_PROFILE set "FW_PROFILE=%CFG_FW_PROFILE%"
if defined CFG_FW_REMOTEIP set "FW_REMOTEIP=%CFG_FW_REMOTEIP%"
if defined CFG_NETWORK_PRIVATE set "NETWORK_PRIVATE=%CFG_NETWORK_PRIVATE%"
if defined CFG_KEEP_AWAKE set "KEEP_AWAKE=%CFG_KEEP_AWAKE%"
if defined CFG_RESET_SETTINGS set "RESET_SETTINGS=%CFG_RESET_SETTINGS%"
if defined VNC_PASSWORD set "PW_SOURCE=setup.conf"

rem ---- password: conf, else one-line file on the drive, else prompt
if defined VNC_PASSWORD goto :pw_have
set "PWF=%VNC_PASSWORD_FILE%"
if not exist "%PWF%" set "PWF=%HERE%%VNC_PASSWORD_FILE%"
if exist "%PWF%" set /p "VNC_PASSWORD="<"%PWF%"
if defined VNC_PASSWORD set "PW_SOURCE=file %VNC_PASSWORD_FILE%"
if defined VNC_PASSWORD goto :pw_have
call :log "No password in setup.conf or %VNC_PASSWORD_FILE%; prompting."
for /f "usebackq delims=" %%P in (`powershell -NoProfile -Command "$s=Read-Host 'Enter the VNC password (only the first 8 characters are used)' -AsSecureString; [Runtime.InteropServices.Marshal]::PtrToStringAuto([Runtime.InteropServices.Marshal]::SecureStringToBSTR($s))"`) do set "VNC_PASSWORD=%%P"
if not defined VNC_PASSWORD set /p "VNC_PASSWORD=Enter the VNC password (first 8 characters are used): "
if defined VNC_PASSWORD set "PW_SOURCE=prompt"
:pw_have
if not defined VNC_PASSWORD goto :pw_missing
if not "%VNC_PASSWORD:~8%"=="" call :log "WARNING: VNC authentication only uses 8 characters; password truncated to 8."
if not "%VNC_PASSWORD:~8%"=="" set /a WARNS+=1
set "VNC_PASSWORD=%VNC_PASSWORD:~0,8%"
if "%VNC_PASSWORD:~5,1%"=="" call :log "WARNING: password is shorter than 6 characters."
if defined VNC_VIEWONLY_PASSWORD set "VNC_VIEWONLY_PASSWORD=%VNC_VIEWONLY_PASSWORD:~0,8%"
if not defined VNC_CONTROL_PASSWORD set "VNC_CONTROL_PASSWORD=%VNC_PASSWORD%"
set "VNC_CONTROL_PASSWORD=%VNC_CONTROL_PASSWORD:~0,8%"
call :log "password source: %PW_SOURCE% (value never logged)"
if defined VNC_VIEWONLY_PASSWORD call :log "a separate view-only password is configured (value never logged)"
goto :pw_done
:pw_missing
call :log "FAILED: no VNC password. Put VNC_PASSWORD in setup.conf, or one line in vnc-password.txt."
set "FAILS=1"
goto :finish
:pw_done
call :log "settings: port=%VNC_PORT% firewall profile=%FW_PROFILE% remoteip=%FW_REMOTEIP% reset=%RESET_SETTINGS% keep_awake=%KEEP_AWAKE%"

rem ---- locate or download the MSI
set "MSI="
if defined MSI_FILE if exist "%MSI_FILE%" set "MSI=%MSI_FILE%"
if defined MSI_FILE if not defined MSI if exist "%HERE%%MSI_FILE%" set "MSI=%HERE%%MSI_FILE%"
if not defined MSI for %%F in ("%HERE%tightvnc-*-gpl-setup-%ARCH%.msi") do set "MSI=%%~fF"
if defined MSI goto :msi_found
if /i "%MSI_URL%"=="none" goto :msi_missing
if not defined MSI_URL set "MSI_URL=https://www.tightvnc.com/download/2.8.85/tightvnc-2.8.85-gpl-setup-%ARCH%.msi"
call :log "No MSI next to the script. Trying download: %MSI_URL%"
set "DLDEST=%HERE%tightvnc-dl-gpl-setup-%ARCH%.msi"
if /i "%LOG%"=="%TEMP%\vnc-log.txt" set "DLDEST=%TEMP%\tightvnc-dl-gpl-setup-%ARCH%.msi"
powershell -NoProfile -ExecutionPolicy Bypass -Command "try{[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12;(New-Object Net.WebClient).DownloadFile($env:MSI_URL,$env:DLDEST);exit 0}catch{Write-Output ('download error: '+$_.Exception.Message);exit 1}" >>"%LOG%" 2>&1
if not errorlevel 1 if exist "%DLDEST%" goto :dl_ok
del "%DLDEST%" >nul 2>&1
goto :msi_missing
:dl_ok
set "MSI=%DLDEST%"
call :log "Downloaded to %MSI%"
goto :msi_found

:msi_missing
call :log "FAILED: no TightVNC MSI. Copy tightvnc-VERSION-gpl-setup-%ARCH%.msi from"
call :log "https://www.tightvnc.com/download.php onto the drive next to setup.bat and re-run."
set "FAILS=1"
goto :finish

:msi_found
call :log "MSI: %MSI%"
set "MSI_HASH="
for /f "usebackq delims=" %%H in (`powershell -NoProfile -Command "(Get-FileHash -LiteralPath $env:MSI -Algorithm SHA256).Hash"`) do set "MSI_HASH=%%H"
call :log "MSI sha256: %MSI_HASH%"
if defined MSI_SHA256 if /i not "%MSI_HASH%"=="%MSI_SHA256%" goto :msi_hash_bad
powershell -NoProfile -Command "$s=Get-AuthenticodeSignature -LiteralPath $env:MSI; Write-Output ('MSI signature: '+$s.Status+' signer: '+$s.SignerCertificate.Subject)" >>"%LOG%" 2>&1
goto :msi_hash_ok
:msi_hash_bad
call :log "FAILED: MSI sha256 does not match MSI_SHA256 in setup.conf. Refusing to install."
set "FAILS=1"
goto :finish
:msi_hash_ok

rem ---- 3. remove any existing TightVNC (idempotent re-run)
call :log "Checking for an existing TightVNC install."
sc query %SVC% >nul 2>&1
if not errorlevel 1 call :log "Existing %SVC% service found; stopping it."
if not errorlevel 1 net stop %SVC% >>"%LOG%" 2>&1
taskkill /f /im tvnserver.exe >nul 2>&1
for /f "usebackq delims=" %%G in (`powershell -NoProfile -Command "Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*','HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*' -ErrorAction SilentlyContinue | Where-Object { $_.DisplayName -like 'TightVNC*' -and $_.PSChildName -like '{*}' } | ForEach-Object { $_.PSChildName }"`) do call :uninstall_guid "%%G"
if /i not "%RESET_SETTINGS%"=="1" goto :reset_skip
call :log "Clearing saved TightVNC server settings (RESET_SETTINGS=1)."
reg delete "HKLM\SOFTWARE\TightVNC\Server" /f /reg:64 >nul 2>&1
reg delete "HKLM\SOFTWARE\TightVNC\Server" /f /reg:32 >nul 2>&1
:reset_skip

rem ---- 4. install (retry while another installer holds the lock, code 1618)
set "MSILOG=%TEMP%\tightvnc-msi.log"
set "INST_TRY=0"
:install_try
set /a INST_TRY+=1
del "%MSILOG%" >nul 2>&1
call :log "Installing TightVNC server (attempt %INST_TRY%)."
if defined VNC_VIEWONLY_PASSWORD goto :install_vo
start "" /wait msiexec /i "%MSI%" /qn /norestart /L*v "%MSILOG%" ADDLOCAL=Server SERVER_REGISTER_AS_SERVICE=1 SERVER_ADD_FIREWALL_EXCEPTION=0 SET_USEVNCAUTHENTICATION=1 VALUE_OF_USEVNCAUTHENTICATION=1 SET_PASSWORD=1 VALUE_OF_PASSWORD="%VNC_PASSWORD%" SET_USECONTROLAUTHENTICATION=1 VALUE_OF_USECONTROLAUTHENTICATION=1 SET_CONTROLPASSWORD=1 VALUE_OF_CONTROLPASSWORD="%VNC_CONTROL_PASSWORD%" SET_RFBPORT=1 VALUE_OF_RFBPORT=%VNC_PORT% SET_ACCEPTRFBCONNECTIONS=1 VALUE_OF_ACCEPTRFBCONNECTIONS=1
set "RC=%errorlevel%"
goto :install_done
:install_vo
start "" /wait msiexec /i "%MSI%" /qn /norestart /L*v "%MSILOG%" ADDLOCAL=Server SERVER_REGISTER_AS_SERVICE=1 SERVER_ADD_FIREWALL_EXCEPTION=0 SET_USEVNCAUTHENTICATION=1 VALUE_OF_USEVNCAUTHENTICATION=1 SET_PASSWORD=1 VALUE_OF_PASSWORD="%VNC_PASSWORD%" SET_VIEWONLYPASSWORD=1 VALUE_OF_VIEWONLYPASSWORD="%VNC_VIEWONLY_PASSWORD%" SET_USECONTROLAUTHENTICATION=1 VALUE_OF_USECONTROLAUTHENTICATION=1 SET_CONTROLPASSWORD=1 VALUE_OF_CONTROLPASSWORD="%VNC_CONTROL_PASSWORD%" SET_RFBPORT=1 VALUE_OF_RFBPORT=%VNC_PORT% SET_ACCEPTRFBCONNECTIONS=1 VALUE_OF_ACCEPTRFBCONNECTIONS=1
set "RC=%errorlevel%"
:install_done
call :log "msiexec exit code: %RC%"
if "%RC%"=="1618" if %INST_TRY% LSS 4 call :log "Another installation is in progress; waiting 20 seconds."
if "%RC%"=="1618" if %INST_TRY% LSS 4 ping -n 21 127.0.0.1 >nul
if "%RC%"=="1618" if %INST_TRY% LSS 4 goto :install_try
if "%RC%"=="0" goto :install_ok
if "%RC%"=="3010" goto :install_ok
if "%RC%"=="1641" goto :install_ok
call :log "FAILED: TightVNC MSI install failed (code %RC%). Redacted tail of the MSI log follows."
powershell -NoProfile -Command "$t=Get-Content -LiteralPath $env:MSILOG -ErrorAction SilentlyContinue | Select-Object -Last 120; foreach($s in @($env:VNC_PASSWORD,$env:VNC_VIEWONLY_PASSWORD,$env:VNC_CONTROL_PASSWORD)){ if($s){ $t=$t -replace [regex]::Escape($s),'********' } }; $t | Add-Content -LiteralPath $env:LOG" 2>&1
del "%MSILOG%" >nul 2>&1
set "FAILS=1"
goto :finish
:install_ok
del "%MSILOG%" >nul 2>&1
if "%RC%"=="3010" call :log "NOTE: Windows reports a reboot is recommended (3010)."
call :log "TightVNC installed."

rem ---- 5. firewall
call :log "Configuring Windows Firewall rule for TCP %VNC_PORT%."
netsh advfirewall firewall delete rule name="NetKit VNC TCP %VNC_PORT%" >nul 2>&1
netsh advfirewall firewall add rule name="NetKit VNC TCP %VNC_PORT%" dir=in action=allow protocol=TCP localport=%VNC_PORT% profile=%FW_PROFILE% remoteip=%FW_REMOTEIP% >>"%LOG%" 2>&1
if errorlevel 1 goto :fw_bad
call :log "Firewall rule added: NetKit VNC TCP %VNC_PORT% (profile %FW_PROFILE%, remote %FW_REMOTEIP%)."
goto :fw_done
:fw_bad
call :fail "could not add the firewall rule (is the Windows Firewall service running?)"
:fw_done

rem ---- network profile (log it; optionally flip Public to Private)
call :log "Network profiles (Public blocks inbound unless the rule above covers it):"
powershell -NoProfile -Command "Get-NetConnectionProfile | Format-Table Name,InterfaceAlias,NetworkCategory -AutoSize | Out-String -Width 200" >>"%LOG%" 2>&1
if not "%NETWORK_PRIVATE%"=="1" goto :netprofile_done
call :log "NETWORK_PRIVATE=1: setting Public connections to Private."
powershell -NoProfile -Command "Get-NetConnectionProfile | Where-Object { $_.NetworkCategory -eq 'Public' } | Set-NetConnectionProfile -NetworkCategory Private" >>"%LOG%" 2>&1
:netprofile_done

rem ---- keep the machine awake on mains power so the tablet can always connect
if not "%KEEP_AWAKE%"=="1" goto :awake_done
powercfg /change standby-timeout-ac 0 >>"%LOG%" 2>&1
powercfg /change hibernate-timeout-ac 0 >>"%LOG%" 2>&1
call :log "Sleep and hibernate on AC power set to never (KEEP_AWAKE=1)."
:awake_done

rem ---- 6. service: auto start, restart on failure, running
sc config %SVC% start= auto >>"%LOG%" 2>&1
sc failure %SVC% reset= 86400 actions= restart/5000/restart/5000/restart/5000 >>"%LOG%" 2>&1
set "TRIES=0"
:svc_poll
sc query %SVC% 2>nul | find "RUNNING" >nul
if not errorlevel 1 goto :svc_up
set /a TRIES+=1
if %TRIES% GEQ 15 goto :svc_down
if "%TRIES%"=="1" net start %SVC% >>"%LOG%" 2>&1
ping -n 3 127.0.0.1 >nul
goto :svc_poll
:svc_down
call :fail "service %SVC% is not RUNNING after 15 checks"
goto :svc_report
:svc_up
call :log "Service %SVC%: RUNNING"
:svc_report
sc query %SVC% >>"%LOG%" 2>&1
sc qc %SVC% >>"%LOG%" 2>&1
sc qc %SVC% 2>nul | find "AUTO_START" >nul
if errorlevel 1 goto :auto_bad
call :log "Service %SVC%: start type AUTO_START"
goto :auto_done
:auto_bad
call :fail "service %SVC% start type is not AUTO_START"
:auto_done
set "PWSAVED="
reg query "HKLM\SOFTWARE\TightVNC\Server" /v Password /reg:64 >nul 2>&1
if not errorlevel 1 set "PWSAVED=1"
reg query "HKLM\SOFTWARE\TightVNC\Server" /v Password /reg:32 >nul 2>&1
if not errorlevel 1 set "PWSAVED=1"
if defined PWSAVED call :log "Registry: a VNC password is stored for the server (value not read or logged)."
if not defined PWSAVED call :log "WARNING: no stored VNC password found in HKLM\SOFTWARE\TightVNC\Server. The MSI property names may be wrong; test with a wrong password from the tablet."
if not defined PWSAVED set /a WARNS+=1
for %%P in ("%ProgramFiles%\TightVNC\tvnserver.exe" "%ProgramFiles(x86)%\TightVNC\tvnserver.exe") do if exist %%P call :exe_version "%%~P"

rem ---- 7. verification
call :log "Verifying listening port TCP %VNC_PORT%."
ping -n 3 127.0.0.1 >nul
netstat -ano -p tcp | findstr /R /C:":%VNC_PORT%  *0.0.0.0:0 " >>"%LOG%"
netstat -ano -p tcp | findstr /R /C:":%VNC_PORT%  *0.0.0.0:0 " >nul
if errorlevel 1 goto :port_bad
call :log "Port TCP %VNC_PORT%: LISTENING"
goto :port_done
:port_bad
call :fail "nothing is listening on TCP %VNC_PORT%"
:port_done

set "BANNER="
for /f "usebackq delims=" %%B in (`powershell -NoProfile -Command "try{$h=(Get-NetIPAddress -AddressFamily IPv4 | Where-Object {$_.IPAddress -notlike '127.*' -and $_.IPAddress -notlike '169.254.*'} | Select-Object -First 1).IPAddress; if(-not $h){'SKIP no non-loopback IPv4 address yet'; exit}; $c=New-Object Net.Sockets.TcpClient; $c.Connect($h,[int]$env:VNC_PORT); $s=$c.GetStream(); $s.ReadTimeout=4000; $b=New-Object byte[] 12; $n=$s.Read($b,0,12); $c.Close(); [Text.Encoding]::ASCII.GetString($b,0,$n).Trim()}catch{'ERR '+($_.Exception.Message -replace '[^A-Za-z0-9 .:_-]','')}"`) do set "BANNER=%%B"
call :log "RFB banner probe result: %BANNER%"
if /i "%BANNER:~0,4%"=="RFB " call :log "RFB handshake banner received: the server speaks the VNC protocol."
if /i "%BANNER:~0,4%"=="SKIP" set /a WARNS+=1
if /i "%BANNER:~0,3%"=="ERR" call :log "WARNING: RFB banner probe failed (port is listening, so this may be harmless)."
if /i "%BANNER:~0,3%"=="ERR" set /a WARNS+=1
if not defined BANNER call :log "WARNING: RFB banner probe returned nothing."
if not defined BANNER set /a WARNS+=1

call :log "Firewall rules mentioning VNC (for the record):"
netsh advfirewall firewall show rule name=all dir=in 2>nul | findstr /I /C:"Rule Name" | findstr /I "vnc" >>"%LOG%"
call :log "Network adapters and addresses:"
ipconfig /all >>"%LOG%" 2>&1
call :log "MAC addresses (give the iMac WLAN MAC to the router Pi config):"
getmac /v >>"%LOG%" 2>&1
for /f "usebackq delims=" %%I in (`powershell -NoProfile -Command "Get-NetIPAddress -AddressFamily IPv4 | Where-Object {$_.IPAddress -notlike '127.*'} | ForEach-Object { $_.IPAddress + ' (' + $_.InterfaceAlias + ')' }"`) do call :log "IPv4 address: %%I  - tablet connects to this address, port %VNC_PORT%"

:finish
call :log "--------------------------------------------------------------"
if not "%FAILS%"=="0" goto :result_bad
call :log "RESULT: OK (warnings: %WARNS%). Service running, port listening."
call :log "On the tablet open NetKit, VNC, enter an IPv4 address from above and port %VNC_PORT%."
goto :result_end
:result_bad
call :log "RESULT: FAILED (%FAILS% problem(s)). See the lines above and the MSI notes in README.md."
:result_end
echo.
echo Log written to: %LOG%
if not defined NOPAUSE pause
if "%FAILS%"=="0" exit /b 0
exit /b 1

rem ============================== subroutines ==================================

:log
echo [%date% %time%] %~1
>>"%LOG%" echo [%date% %time%] %~1
exit /b 0

:fail
set /a FAILS+=1
call :log "PROBLEM: %~1"
exit /b 0

:uninstall_guid
call :log "Uninstalling existing TightVNC %~1"
start "" /wait msiexec /x %~1 /qn /norestart
call :log "uninstall exit code: %errorlevel%"
exit /b 0

:exe_version
for /f "usebackq delims=" %%V in (`powershell -NoProfile -Command "(Get-Item -LiteralPath '%~1').VersionInfo.FileVersion"`) do call :log "tvnserver.exe: %~1 version %%V"
exit /b 0
