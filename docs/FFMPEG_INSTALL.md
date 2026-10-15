# Installing ffmpeg (needed only for STREAM OUT)

SHANKPIT does **not** ship ffmpeg (card #618). F2 / pause menu -> STREAM OUT pipes the program feed into
`ffmpeg`, which the game looks up **on your `PATH`** (command name `ffmpeg`, must have **libx264** -- every build
below does). Nothing else in the game needs it. The encoder is a labeled stopgap; a PARENA encoder replaces it later.

**Check first** -- open a *new* terminal and run `ffmpeg -version`. If it prints a version and
`--enable-libx264` appears in the configuration line, you are done.

## Windows (robust, step by step)

### Option A -- winget (Windows 10 1809+/11, simplest)
1. Press **Win**, type `PowerShell`, open **Windows PowerShell** (admin not required).
2. `winget install --id Gyan.FFmpeg -e`  (accept the source agreement if asked).
3. **Close PowerShell and open a new one** (PATH is read at start). Run `ffmpeg -version`.
4. If `winget` is "not recognized": update *App Installer* from the Microsoft Store, or use Option B.

### Option B -- manual zip + PATH (works everywhere, no package manager)
1. Download a **GPL "full" or "essentials" win64** build: <https://www.gyan.dev/ffmpeg/builds/> (`ffmpeg-release-essentials.zip`)
   or <https://github.com/BtbN/FFmpeg-Builds/releases> (`ffmpeg-master-latest-win64-gpl.zip`).
2. Right-click the zip -> **Extract All...** to `C:\ffmpeg` so that `C:\ffmpeg\bin\ffmpeg.exe` exists
   (the zip has a versioned inner folder; move its contents up so there is no extra nesting).
   Avoid paths with spaces, and do not run it from inside the zip.
3. Add to PATH: **Win** -> type `environment variables` -> **Edit the system environment variables** ->
   **Environment Variables...** -> under *User variables* select **Path** -> **Edit** -> **New** -> `C:\ffmpeg\bin` -> **OK** x3.
   (PowerShell alternative, current user only:
   `[Environment]::SetEnvironmentVariable('Path', $env:Path + ';C:\ffmpeg\bin', 'User')`)
4. **Close every terminal and restart ShankPit.exe** -- running programs never see a changed PATH.
5. Verify in a new terminal: `ffmpeg -version` and `where ffmpeg` (should print `C:\ffmpeg\bin\ffmpeg.exe`).

### Windows troubleshooting
| Symptom | Fix |
|---|---|
| Game says `STREAM NEEDS FFMPEG ON PATH` | PATH not updated for the game: fully quit ShankPit, launch it again (launching from a terminal opened *before* the PATH change still has the old PATH). |
| `'ffmpeg' is not recognized` in a new terminal | Folder added was wrong -- it must be the one containing `ffmpeg.exe` (`...\bin`), not its parent. |
| SmartScreen / "blocked" on first run | Right-click `ffmpeg.exe` -> Properties -> **Unblock**, or run once from a terminal. |
| Antivirus quarantines it | Re-download from the official links above; add `C:\ffmpeg` to exclusions. |
| Two ffmpegs installed | `where ffmpeg` lists them in PATH order; the first wins. Remove the stale one. |

## Linux
| Distro | Command |
|---|---|
| Debian / Ubuntu / Mint | `sudo apt update && sudo apt install -y ffmpeg` |
| Fedora | enable RPM Fusion, then `sudo dnf install -y ffmpeg` |
| Arch / Manjaro | `sudo pacman -S ffmpeg` |
| openSUSE | `sudo zypper install ffmpeg` (Packman repo) |
| Any (static build) | download `ffmpeg-release-amd64-static.tar.xz` from <https://johnvansickle.com/ffmpeg/>, extract, `sudo cp ffmpeg /usr/local/bin/` |

Verify: `ffmpeg -version | head -1 && command -v ffmpeg`. No restart needed beyond a new shell for the game launch.

## macOS (not a supported client, for completeness)
`brew install ffmpeg`.

## Using something else
`SHANKPIT_STREAM_SINK_CMD` replaces ffmpeg with any program that reads raw rgb24 on stdin -- then ffmpeg is not needed.
