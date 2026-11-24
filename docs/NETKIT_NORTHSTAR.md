# SHANKPIT Netkit — NORTHSTAR

**Golden doc: `NETKIT-NORTH`.** Status as of 2026-11-24 (founder real-time, S635 / NETKIT-0). Source of intent: the founder's
*"Custom VNC Client: Affordances and Network Map"* (2026-11-23) — reproduced in the repo's backlog (`EMILY/BACKLOG.md` § 635) and summarized below.

## 1. What it is

One Android app on the tablet that finds devices on the network, shows what each one is, and drives them: an **SSH terminal**, a
**view-only VNC screen**, a scanner with Ping and Port-check tools. It is built on the SHANKPIT control surface and designed so that the same
modules can later run in AR / on a Quest (Osaka Garage doctrine). It is a **second app in this repo**, separate from the shankpit-460 APK.

The network it lives on (founder's map): Android tablet (controller) · Android phone (internet source) · iMac on Windows 10 running TightVNC
(the machine being controlled) · Raspberry Pi 3 router (64-bit Ubuntu; the only door to the internet) · Tap Pi (packet capture, later) ·
Pi Zero W pager (later) · Meta Quest (later). **Internet rule:** the router Pi is the only way out; the tablet needs only the local network.

## 2. The founder's direction: PARENA first

> *"ensure its all parena based mods first dev"* — 2026-11-24, redirecting a hand-written-Java build already under way.

Consistent with root `CLAUDE.md` ("Core Deps Are PARENA-First") and the roadmap rule *"each new capability is a PARENA module, never baked into
the app core"*. So:

* **Logic is PARENA** (`PARENA/stdlib/netkit/`), compiled to WebAssembly with the repo's web-game conventions: no malloc, **zero imports**,
  fixed memory. A module cannot touch a socket, a clock or the DOM.
* **The page host is PARENA too** (`stdlib/netkit/host/`, → TypeScript → the one generated `nk-host.js`). No hand-written JavaScript.
* **Java is a socket pipe** (`android-netkit/shell`, `app`): raw TCP, probes, local network, ARP, encrypted storage, clipboard. It never
  interprets a byte.
* The only hand-written non-PARENA assets are markup and styling (`apps/netkit/index.html`, `netkit.css`).

Where PARENA could not yet express something, **that gap was the first task** (root rule) — and it was closed in PARENA, not routed around:
no I64 → X25519 and Ed25519 over 32 limbs of 8 bits in I32 words and SHA-512 over I32 pairs; no terminal fit for SSH → a new terminal
(a fork of GFD's MUD console, which does local line editing); no VNC at all → written from the RFC. The one stopgap that was considered
(JSch/libssh2 for SSH) was **not needed** and is not in the product.

## 3. Architecture

```
 WebView ── nk-host.js ──────────────── bytes ──────────────  wasm modules (one instance per session)
   (PARENA→TS→JS)                                              netkit.wasm       scanner · RFB · SSH · crypto
                                                               netkit-lite.wasm  same code, 8 MB (scan, SSH)
                                                               nkterm.wasm       terminal
        │ window.NetkitNative / window.netkitEvent(json)
 Java shell: TcpHub · Prober · NetInfo · ArpTable · Resolver · SecureStore (Keystore AES-GCM) · clipboard
```
Sans-IO throughout: a module owns *what to say and what it means*; the page host moves bytes; the shell owns the sockets. The wasm memory
map and state words are in `PARENA/stdlib/netkit/nk_mem.prn`; the native contract is in `host/nk_host_core.prn` and `android-netkit/README.md`.

## 4. What exists, and how it was verified

All of it runs headlessly, and every piece is checked against something this repo did not write.

| piece | PARENA module | verified against |
|---|---|---|
| Scanner brain: CIDR plan (clamped to /22, loudly), job enumeration, probe-result merge (an RST proves a host is up), `/proc/net/arp`, "what is this device", ordering | `nk_scan.prn` | 62 checks; **found a real bug in the browser-level run** (sweep with own address excluded could not finish) |
| VNC (RFB 3.3/3.7/3.8, None + VNC auth, Raw/CopyRect/Hextile/DesktopSize), **view only by construction** | `nk_rfb.prn`, `nk_des.prn` | 97 scripted checks (every case fed whole / byte-wise / random chunks) + **real libvncserver** (TightVNC's lineage): real auth, its encoders, live resize, final screen **byte-for-byte equal** |
| SSH-2 client: `curve25519-sha256`, `ssh-ed25519`, `chacha20-poly1305@openssh.com`, none→password / keyboard-interactive, pty+shell, window-change, flow control both ways | `nk_ssh_wire.prn`, `nk_ssh.prn` | 61 checks vs **asyncssh** + 14 vs **real OpenSSH 9.6 `sshd`** (run unprivileged with `libnss_wrapper`) |
| Crypto, all PARENA: SHA-256, SHA-512, ChaCha20, Poly1305, X25519, Ed25519 verify, DES | `nk_crypto`, `nk_sha512`, `nk_x25519`, `nk_ed25519`, `nk_des` | RFC vectors **and** hashlib / pyca-cryptography / pycryptodome on random inputs (incl. carry extremes, malleability, low-order points) |
| Terminal: xterm key encodings, ICH/DCH/ECH, IL/DL, scroll regions, alt screen, DEC graphics, replies | `term/nk_term.prn` | 82 checks (pyte differential where pyte is right; hand-derived where it is not) |
| The page: Devices, connect + saved, "Trust this device?", Terminal, view-only Screen, Back | `host/*.prn` | 65 checks in Node vs real SSH + VNC servers; **21 in headless Chromium** with the app's CSP (canvas **pixel-identical** to an independent re-render; VNC image byte-for-byte) |
| The shell | `android-netkit/shell` | 16 JVM tests against real loopback sockets |
| The APK | `android-netkit/app` | builds (116 KB release); assets byte-identical to `apps/netkit` |

`make test-netkit` in PARENA runs it all (CI: `.github/workflows/netkit.yml`).

### Security properties that are tested, not just claimed
* **No credential leaves for an unaccepted host.** First contact stops at the fingerprint dialog; a pinned fingerprint that no longer matches
  fails before auth — asserted on the wire (`only KEXINIT, ECDH_INIT, NEWKEYS were sent`) and by the *server's own* login counters.
* A server that cannot prove it owns its host key (forged Ed25519 signature) fails before NEWKEYS.
* A flipped bit anywhere in an encrypted packet (length, body, tag) fails the connection.
* A server without chacha20 / curve25519 / ed25519 is refused in plain words — **never downgraded**.
* VNC never sends a key, pointer or clipboard message (parsed off the real wire).

## 5. VS0 definition-of-done vs reality

| VS0 criterion (founder's doc) | status |
|---|---|
| Scan the network, show a device list with name and address | **built, verified** (loopback + fixtures). Not run on a real LAN. |
| Tap a device → SSH or VNC depending on which answers | **built, verified** |
| SSH: terminal view, extra keys (Ctrl/Esc/Tab/arrows), copy and paste, saved connections | **built, verified** (soft-keyboard path is written but **untested on a device**) |
| VNC: view the remote screen, saved password, no mouse/keyboard | **built, verified**; not against TightVNC-for-Windows itself |
| Clear errors: unreachable / port closed / wrong password | **built, verified** (plain-words text lives in PARENA) |
| **Done when:** tablet gets a shell prompt on the router Pi with no screen attached; one tap on the iMac shows its desktop | **protocol-verified against real OpenSSH 9.6 and libvncserver; NOT yet demonstrated on the real tablet / Pi / iMac** (no device or emulator in the build environment) |
| Deliverable 1: `setup.bat` installing TightVNC on the iMac | **not started** |
| Deliverable 2: router Pi on 64-bit Ubuntu (AP + tethering + DHCP script) | **not started** |

## 6. Known limits (named, not hidden)

* No SSH public-key auth, no other ciphers/kex (OpenSSH has defaulted to this set for a decade; an old Dropbear is refused). Password expiry
  at first Ubuntu login is reported as "password expired" — set the password via the SD-card config as the plan intends.
* VNC: no Tight/ZRLE (needs zlib/JPEG in PARENA — its own module), no cursor-shape or clipboard, single-DES auth (the protocol's weakness; use
  it only on the router network).
* Terminal: fixed 100×34 grid, ASCII + box drawing + blocks only (other characters show a placeholder), no mouse reporting, no wide characters.
  Tablet-sized; unreadably small on a phone.
* Scanner: TCP-connect sweep of 8 ports, ≤ /22; names via reverse DNS only (no NetBIOS/mDNS); `/proc/net/arp` is blocked on Android 10+.
* Not built from the founder's doc: packet capture + Tap Pi, pager/Pi Zero W alerts, stream deck, AR/Quest, the garage doctrine, stages 2–6.

## 7. Roadmap (the founder's stages, as PARENA modules)

1. **VS0 toolkit** — this. Remaining: the two scripts (iMac `setup.bat`, router-Pi setup), a real-device pass, VNC Tight/ZRLE.
2. **First modules** — SSH and VNC are already PARENA modules; what remains is the *module interface* question below.
3. **Streaming platform** — stream deck, packet capture (`pcap` in PARENA exists: `stdlib/pentest/pcap.prn`), pager alerts.
4. **Game engine / 5. creation tools / 6. collaboration** — the founder's longer roadmap; none of it started here.

## 8. Open questions (carried from the founder's doc, plus what building it raised)

* Founder's: is the pager alert UDP broadcast? Should router Pi and Tap Pi ever link? Does the tablet have internet on its own? Which Quest
  model? What goes on the first stream-deck page? Is the iMac's Wi-Fi 2.4 GHz, and is the phone hotspot name/password chosen? Which Android
  version is the tablet on? What is the module interface (what may a PARENA module ask the app core to do)?
* New: should the per-session instances stay (simple, isolated) or share one? What is the right terminal size for a phone (scroll vs scale)?
  Should the shell grow NetBIOS/mDNS name lookup so Windows PCs show their real names? Does the iMac run TightVNC with a password set (this
  app refuses a password-less *required* server and tells the user)?
