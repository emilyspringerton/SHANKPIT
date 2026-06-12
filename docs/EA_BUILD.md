# SHANKPIT — Early Access Build Guide

**Build:** Milestone 5 EA (FPS core only)
**Date:** 2026-06-12
**Price point:** $9.99 USD EA

---

## What's Included

| File | Description |
|------|-------------|
| `shank_go_server` / `shank_go_server.exe` | Headless Go matchmaker + scene server (UDP :6969) |
| `shank_lobby` / `shank_lobby.exe` | C game client with OpenGL rendering |
| `README.txt` | This file |

---

## Requirements (Linux)

- **SDL2** runtime libraries: `libsdl2-2.0-0`
- **OpenGL / GLU**: usually provided by your graphics driver
- **glibc 2.17+** (any Linux from ~2014 onwards)

Install on Debian/Ubuntu:
```bash
sudo apt-get install libsdl2-2.0-0
```

Install on Fedora/RHEL:
```bash
sudo dnf install SDL2
```

## Requirements (Windows)

- Windows 10 64-bit or later
- Place `SDL2.dll` in the same folder as `shank_lobby.exe`
  (SDL2 runtime available at https://github.com/libsdl-org/SDL/releases)

---

## Running a 4-Player Session

### Host machine

```bash
# 1. Start the scene server (listens on UDP :6969)
./shank_go_server

# 2. Start your own client (connects to localhost)
./shank_lobby --host 127.0.0.1 --port 6969
```

### Player machines

```bash
# Replace HOST_IP with the host's LAN/internet IP address
./shank_lobby --host HOST_IP --port 6969
```

The server accepts up to 4 concurrent players. No Steam or account required for LAN play.
Internet play requires the host to forward UDP port 6969 on their router, or use a VPN like
Tailscale / ZeroTier.

---

## Building from Source

```bash
# Clone the repo
git clone https://github.com/emilyspringerton/SHANKPIT.git
cd SHANKPIT

# Build everything (requires gcc, SDL2-dev, libGL-dev)
make ea

# Output: dist/ea/shank_lobby + dist/ea/shank_go_server
```

Cross-compile for Windows (requires mingw-w64):
```bash
make ea-windows
# Output: dist/ea-windows/shank_lobby.exe + dist/ea-windows/shank_go_server.exe
```

---

## Controls

| Key / Action | Effect |
|---|---|
| WASD | Move |
| Mouse | Aim |
| Left click | Shoot |
| Right click | Zoom |
| Space | Jump |
| Ctrl | Crouch (landing speed boost) |
| E | Use / interact with portal |
| Tab | Scoreboard |

---

## Current EA Status (Milestone 5)

- FPS core: server-authoritative movement, hitscan combat, turbo movement
- Multi-scene worlds: 8 scenes connected by portals (fully traversable)
- Cross-scene attack prevention: cannot shoot players in other scenes
- Team deathmatch + CTF
- Bot support (neural net)
- Retro neon-brutalist rendering

**Not in EA (Milestone 7):** Dragonfly persistent world, BedWars, destructible terrain.

---

## Known Issues / EA Caveats

- Windows build requires SDL2.dll placed alongside the executable
- No dedicated server browser yet — share IP out of band
- Bots use a placeholder AI in scenes without a configured neural net
- Serverctl (ncurses admin UI) is Linux-only and not included in the EA package

---

## Contact

Questions, bugs, feedback: emilyspringerton@gmail.com

Steam page coming soon (Milestone 6 — pending Steam Direct account).
