# NETKIT Deliverable 1 - TightVNC on the iMac (Windows 10 Home)

`setup.bat` turns the iMac (booted into Windows 10 Home) into a VNC server that the
NetKit tablet app (`SHANKPIT/android-netkit`) can watch. One file, run from the
external drive, safe to run again.

**Status: written but NOT yet run on Windows.** It was authored on Linux with no
Windows machine, no `cmd.exe`, no TightVNC. It passes the static linter
(`python3 lint_bat.py`) and nothing else. See "What is unverified" below. The
real acceptance test is the founder running it on the iMac and reading
`vnc-log.txt`.

**Stopgap label (monorepo "Core Deps Are PARENA-First" rule):** this is a shell
script around a third-party binary (TightVNC server) because a VNC server is an OS
service, not product logic. The NetKit VNC *client* is the PARENA-first part. A
PARENA-native RFB server is not planned here; this is the honest stopgap.

## Drive layout

```
D:\
  setup.bat                                   <- double-click this
  setup.conf                                  <- optional settings (copy of setup.conf.example)
  vnc-password.txt                            <- optional, one line: the VNC password
  tightvnc-2.8.85-gpl-setup-64bit.msi         <- the installer (offline path)
  vnc-log.txt                                 <- written by the script, appended each run
```

Copy `setup.bat` (and `setup.conf.example` as `setup.conf`) onto the drive root.
The drive letter does not matter; everything is relative to the script.

## Running it with no screen (keyboard only) - the tablet writes the drive

The iMac has a keyboard but no working screen, and nothing else here can write a drive. The Netkit tablet app does it:
plug the USB drive into the tablet (USB-C), open the **USB** tab, **Choose USB drive**, then **VNC kit for a PC with no
screen**. Type the VNC password and tap **Write the kit to the drive**. It writes three files onto the drive:

- `setup.bat` - this script, byte for byte (the app ships a copy);
- `setup.conf` - `VNC_PASSWORD=...`, `SPEAK=1`, `NOPAUSE=1`, `KEEP_AWAKE=1`, and `NETWORK_PRIVATE` as you chose;
- `NETKIT.TXT` - the steps below, and the marker the launcher line looks for.

Eject the drive on the tablet, plug it into the iMac, then, signed in as an administrator:

1. Wait 10 seconds. Press Windows+R (Apple keyboard: Command+R). Wait 2 seconds.
2. Type this line exactly (it finds whichever drive letter holds `NETKIT.TXT`):

   ```
   powershell -ep bypass -c "Get-PSDrive -PSProvider FileSystem|%{if(Test-Path($_.Root+'NETKIT.TXT')){& ($_.Root+'setup.bat') /nopause}}"
   ```
3. Press Ctrl+Shift+Enter (runs it as administrator), wait 3 seconds, press Alt+Y (Yes to the UAC prompt).
4. Listen. With `SPEAK=1` the script talks through Windows speech (a beep if speech is missing):
   "Netkit setup started", "Downloading the VNC server", "Installing the VNC server", "Opening the firewall", then
   **"VNC is ready. Address 192 dot 168 dot ... Port 5900"** (said twice), or **"Setup failed. Plug the drive into the
   tablet and read the v n c log."** If you only pressed Enter, it asks for elevation itself and says "Press Alt Y".
5. Check from the tablet: Devices, Scan (the iMac shows 5900 open), or plug the drive back in and open `vnc-log.txt` in
   the USB tab (a log opens at its last line).

The tablet editor also edits any text file on the drive (`setup.conf`, your own `.bat`), keeping each file's own line
ends (a `.bat` stays CRLF). Delete `setup.conf` when you are done: it holds the password.

## Getting the installer (offline vs download)

Offline (preferred): download once from <https://www.tightvnc.com/download.php> the
"Installer for Windows (64-bit)" `tightvnc-<version>-gpl-setup-64bit.msi` (and the
`-32bit` one if the Windows is 32-bit) and put it next to `setup.bat`. The script
globs `tightvnc-*-gpl-setup-<arch>.msi`. Keep only one version per arch.

If no MSI is found it tries to download
`https://www.tightvnc.com/download/2.8.85/tightvnc-2.8.85-gpl-setup-<arch>.msi`
with PowerShell (TLS 1.2) into the drive folder (so the next run is offline). The
URL is unverified; override with `MSI_URL` in `setup.conf`. On failure the log
says exactly which file to fetch by hand. After the first successful run the log
prints the MSI SHA-256; paste it into `MSI_SHA256` to pin it.

## Settings

All in `setup.conf` (see `setup.conf.example`, every key documented there). Nothing
secret lives in the script. Password resolution order: `VNC_PASSWORD` in
`setup.conf`, else the first line of `vnc-password.txt`, else a hidden prompt.

- VNC password authentication (RFB security type 2) is single DES and **only the
  first 8 characters count**. The script truncates to 8 and warns in the log.
- Optional `VNC_VIEWONLY_PASSWORD` gives a second password that can only watch.
  NetKit is view-only already; use the view-only password on the tablet if you
  want the tablet never to be able to control the iMac even if the app changed.
- Passwords must not contain a double quote or end with a backslash (cmd/msiexec
  quoting). The password appears briefly on the msiexec command line (visible to
  other local administrators only) and in the MSI verbose log, which the script
  keeps in `%TEMP%`, redacts on failure and deletes.
- Treat the drive as sensitive if `setup.conf` or `vnc-password.txt` is on it.

## What it does (in order)

1. Self-elevates through a UAC prompt (`fltmc` test; relaunches itself with
   `Start-Process -Verb RunAs` and an `elevated` marker so it can never loop). A
   Standard (non-admin) account will be asked for admin credentials.
2. Reads settings and the password. Logs the source, never the value.
3. Picks the MSI by architecture (`AMD64`/`PROCESSOR_ARCHITEW6432` means 64-bit).
   Logs its SHA-256 and Authenticode status; refuses to install on a pin mismatch.
4. Idempotent reset: stops `tvnserver`, uninstalls any existing TightVNC found in
   the Uninstall registry keys (`msiexec /x {GUID}`), and (default
   `RESET_SETTINGS=1`) deletes `HKLM\SOFTWARE\TightVNC\Server` so the result is
   exactly what the script configures.
5. Installs with documented MSI properties:
   `ADDLOCAL=Server SERVER_REGISTER_AS_SERVICE=1 SERVER_ADD_FIREWALL_EXCEPTION=0
   SET_USEVNCAUTHENTICATION=1
   VALUE_OF_USEVNCAUTHENTICATION=1 SET_PASSWORD=1 VALUE_OF_PASSWORD=...
   [SET_VIEWONLYPASSWORD=1 VALUE_OF_VIEWONLYPASSWORD=...]
   SET_USECONTROLAUTHENTICATION=1 VALUE_OF_USECONTROLAUTHENTICATION=1
   SET_CONTROLPASSWORD=1 VALUE_OF_CONTROLPASSWORD=... SET_RFBPORT=1
   VALUE_OF_RFBPORT=5900 SET_ACCEPTRFBCONNECTIONS=1
   VALUE_OF_ACCEPTRFBCONNECTIONS=1` with `/qn /norestart`. `SERVER_ADD_FIREWALL_EXCEPTION=0` stops the MSI adding its own program-wide
   exception that would bypass step 6's `localsubnet` scope. Retries (up to 4x, 20 s
   apart) on exit 1618 (another installer running). 0, 3010 and 1641 count as success.
6. Firewall: deletes then adds an inbound allow rule `NetKit VNC TCP 5900` for TCP
   5900, profile `any`, remote scope `localsubnet` (so even a Public-classified
   Wi-Fi accepts only the local subnet).
7. Logs the Windows network profiles; with `NETWORK_PRIVATE=1` flips Public to Private.
8. `KEEP_AWAKE=1` (default): sleep/hibernate on AC set to never, otherwise the tablet
   finds a sleeping iMac.
9. Service: `sc config tvnserver start= auto`, restart-on-failure, `net start`,
   polls up to ~30 s for `RUNNING`.
10. Verifies: service RUNNING, `AUTO_START`, a `Password` value exists under
    `HKLM\SOFTWARE\TightVNC\Server` (warning if not: the MSI properties were probably
    ignored), `netstat` shows a listener on `:5900` (matched by foreign address
    `0.0.0.0:0`, so it works on any Windows language), and reads the 12-byte RFB banner
    (`RFB 003.008`) over the machine's LAN address. A failed banner probe is only a
    warning (the port check is the hard check).
11. Dumps `ipconfig /all`, `getmac /v` and a per-IPv4 summary into the log. **The
    iMac's WLAN MAC in this log is exactly what Deliverable 2 (router Pi) needs
    for its fixed-address reservation.**

Exit code 0 means every check passed; 1 means at least one `PROBLEM:` line is in the
log. The window stays open (`pause`) unless you pass `/nopause`.

## Success looks like (the "done" bar)

`vnc-log.txt` ends with lines like:

```
[..] Service tvnserver: RUNNING
[..] Service tvnserver: start type AUTO_START
[..] Port TCP 5900: LISTENING
[..] RFB handshake banner received: the server speaks the VNC protocol.
[..] IPv4 address: 192.168.50.20 (Wi-Fi)  - tablet connects to this address, port 5900
[..] RESULT: OK (warnings: 0). Service running, port listening.
```

Then on the tablet: NetKit, VNC, that IPv4 address, port 5900, the password. The
tablet must be on the same network (the router Pi's Wi-Fi from Deliverable 2, or
any common LAN).

## Network profile guidance

Windows labels each network Public, Private or Domain. A new Wi-Fi network is often
Public, and the built-in inbound rules then block most things. The rule this script
adds applies to all profiles but only from the local subnet, so VNC works on a
Public network too. Still recommended: set the router Pi's network to Private
(Settings, Network and Internet, Wi-Fi, the network, Network profile: Private), or
set `NETWORK_PRIVATE=1`. Never expose 5900 to the internet: VNC DES auth is weak.

## Compatibility with the NetKit client

NetKit's VNC client speaks RFB with VNC (DES challenge) authentication and the
Raw, CopyRect and Hextile encodings, view only. TightVNC server offers VNC
authentication and picks an encoding from whatever the client lists in
`SetEncodings`, so a client that lists only those three works. Items to confirm
on first real run (not verified): the server accepts security type 2 from this
client, the client picks it over TightVNC's own "Tight" type 16, and the desktop
appears (see unverified list).

## Can Windows file sharing (port 445) do this from the tablet instead?

Not on its own. File sharing only moves files. To start the VNC server or open the firewall remotely, a client must also sign in
as an **administrator** and use Windows' service-control interface over the same port. That is how PsExec works.

Windows 10 Home blocks that by default for every administrator account except the built-in `Administrator`. It strips admin
rights from network sign-ins by local accounts ("UAC remote restrictions"; the `LocalAccountTokenFilterPolicy` registry value).
Turning that off needs someone at the iMac anyway.

So the plan stays: run `setup.bat` once at the iMac. After that, VNC needs nothing on the iMac.

What port 445 *does* give without any password is the PC's own description of itself. The Netkit SMB probe (PARENA
`stdlib/netkit/nk_smb.prn`; from a Linux box, `PARENA/scripts/netkit_smb_probe.sh <imac-ip>`) shows:
- the computer name and workgroup;
- the Windows release and build;
- the SMB version and the signing and encryption settings;
- the PC's clock.

That confirms you have the right machine before you walk over to it. With a Windows account on the iMac, the tablet's **Files** button
(or `PARENA/scripts/netkit_smb_browse.sh`) lists its shares and folders and previews text files, read only. Copying files onto a share and
remote start (EMILY KIT-SMB S3) are not built: remote start only works under the conditions above, and needs a decision first.

## Re-running, repairing, uninstalling

Just run it again; every step is reset-and-redo (the VNC connection drops for
a few seconds). To uninstall: Settings, Apps, TightVNC, Uninstall; delete the
firewall rule `NetKit VNC TCP 5900` (`netsh advfirewall firewall delete rule
name="NetKit VNC TCP 5900"`).

## Troubleshooting

- `FAILED: no TightVNC MSI`: put the MSI next to `setup.bat` (see above).
- `msiexec exit code: 1603`: read the redacted MSI tail the script appended to the log.
  Usual causes: another install running, antivirus blocking, bad password characters.
- Service RUNNING but the tablet cannot see it: check the iMac and tablet are on the
  same subnet, the log's `PROBLEM:` lines, and that `RFB banner probe` says `RFB `.
  The probe says `SKIP` if the iMac has no non-loopback address yet (not joined to
  the router Pi yet): that is a warning, not a failure. TightVNC rejects loopback
  connections by default, so the script probes the LAN address, not 127.0.0.1.
- Black screen when nobody is logged in: the service shows the login screen; log in
  on the iMac.
- Log lands in `%TEMP%\vnc-log.txt` instead of the drive: the drive was read-only.

## Linting without Windows

```
python3 lint_bat.py            # lints setup.bat
python3 lint_bat.py other.bat
```

`lint_bat.py` checks CRLF endings, ASCII only, no BOM, duplicate or missing goto/call
labels, single-percent for variables, `%errorlevel%` or `echo ... )` inside parenthesised
blocks, unbalanced parentheses, unquoted `set`, literal passwords (and literal
`VALUE_OF_*PASSWORD`), `timeout` under redirected stdin, bare `exit`, `if %VAR%==`, and `< > | &` inside
`call :log`/`:fail` messages (they are re-parsed unquoted, so `->` creates a file).
It is a pitfall detector, not a batch interpreter. `setup.bat` deliberately avoids
parenthesised blocks and delayed expansion entirely so most of those pitfalls cannot occur.
`.gitattributes` forces `*.bat` to CRLF in git; if you edit on Linux, keep CRLF
(`sed -i 's/\r$//; s/$/\r/' setup.bat`).

## What is unverified (honest list)

Nothing here has been executed on Windows. Specifically unverified:

1. That the script runs end to end on Windows 10 Home (cmd parsing of the long
   `start /wait msiexec` lines, the `for /f` with embedded PowerShell, `%ProgramFiles(x86)%`
   in a `for` list). Static lint only.
2. The exact TightVNC MSI property names (`SET_*`/`VALUE_OF_*`, `SERVER_REGISTER_AS_SERVICE`,
   `ADDLOCAL=Server`) are from memory of TightVNC's "Installing from the command line"
   page, not checked against the shipped MSI. If a name is wrong, msiexec silently
   ignores it and the server would run with no password configured (the RFB probe
   would still pass); verify by connecting from the tablet with a wrong password.
3. That the default download URL / version `2.8.85` exists.
4. That the MSI installer does not itself add a broader firewall exception that
   bypasses `remoteip=localsubnet` (the log lists every firewall rule mentioning VNC).
5. That the MSI is Authenticode-signed (only logged, never enforced).
6. That NetKit's client completes the RFB handshake against TightVNC 2.8.x and shows
   the desktop (Raw/CopyRect/Hextile only). Not tested against a real TightVNC;
   NetKit's own tests used a mock RFB server.
7. The PowerShell snippets (RFB banner probe, `Get-NetIPAddress`, `Get-FileHash`,
   `Get-NetConnectionProfile`, secure-string prompt) are untested on Windows PowerShell 5.1.
8. UAC relaunch from an external drive (`Start-Process -Verb RunAs` on a `.bat`).
9. That `getmac`, `sc failure` and `powercfg` calls behave as written on Home edition.
10. Localised Windows: the `RUNNING` and `AUTO_START` matches assume `sc` prints its
    state tokens untranslated (believed true, untested). The listener check no longer
    depends on the word `LISTENING`.
11. `SERVER_ADD_FIREWALL_EXCEPTION`, like the other property names, is from memory; if it
    is wrong the MSI's own exception may still exist (the log lists rules mentioning VNC).
12. The spoken steps (System.Speech under Windows PowerShell 5.1), the Win+R launcher line, Ctrl+Shift+Enter
    elevation from the Run box and Alt+Y on the UAC prompt: all believed right for Windows 10, none run here.
13. `set CFG_` / `%%B` handling of empty `KEY=` lines in setup.conf is based on documented
    `for /f` behaviour, not run.
