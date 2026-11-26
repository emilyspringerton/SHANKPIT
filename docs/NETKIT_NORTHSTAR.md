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
* Not built from the founder's doc: packet capture + Tap Pi, pager/Pi Zero W alerts, stream deck, AR/Quest, stages 2–6. (The garage doctrine's flat mode IS built since 2026-11-24, see §6a; the physical/AR side is not.)

## 6a. The garage: CONSTRUCT affordances in the Netkit page (flat mode) — built 2026-11-24, source complete, bundle regeneration pending

Founder real-time (BACKLOG SECTION 639): *"ensure CONSTRUCT affordances are available in the SHANKPIT android build (not the shankpit-460 build) -- all wasm based, all parena mods based."* CONSTRUCT is the SHANKPIT hub scene (`TYLER_SCENE_CONSTRUCT`, `apps/lobby/src/main.c`). Its doctrine (Osaka Garage): **every affordance is an object; flat mode on a tablet means you tap the same objects; status is a light on the object; a cable means a connection.** The Netkit page now opens on a **garage** built exactly that way:

| object | does | light | cable |
|---|---|---|---|
| scanner bench | opens the device list (the scanner already runs on boot) | off never scanned / amber scanning / green devices found / red scanned, nothing found | — |
| terminal | the SSH view; with no session it opens the device list with a hint to start one | off / amber connecting / green live | bench–terminal, drawn only while the SSH session is live |
| monitor | the VNC view, same rule | same | bench–monitor, only while the VNC session is live |
| arcade cabinet | **BRAWLPIT inside the page** (canvas + touch pad + a clear *Back to garage*) | off not loaded / amber loading / green ready / red could not load | — |

Everything that decides is PARENA wasm (`PARENA/stdlib/netkit/nk_garage.prn`: pure scalar functions, no memory state — layout in permille of the floor, 4-in-a-row landscape / 2×2 portrait, hit test, lights, words, cables, tap → view, the back-button ladder, touch button → BRAWLPIT key code). The page host (`nk_host_garage.prn`, `nk_host_arcade.prn`, generated to `nk-host.js`) creates the button objects, positions them from the wasm's numbers, draws cables, and moves bytes. **BRAWLPIT** is WOTAN's web build (`WOTAN/BRAWLPIT/brawlpit.wasm`, from `PARENA/stdlib/brawlpit/bp_*.prn`): zero imports, 8 MB fixed memory, no allocator — so it obeys the Netkit CSP (`script-src 'self' 'wasm-unsafe-eval'`, `connect-src 'self'`) unchanged and needs no host-provided imports. It is instantiated lazily (first time you open the cabinet) with an empty import object, as a second module beside the netkit ones; its frame loop runs only while the arcade is on screen; held keys are released on leaving, blur and visibility change. **Not used:** `assets/levels/brawlpit/brawlpit_level.wasm` (the lobby's level build) imports eight `env.reflux_host_*` functions — it would need a PARENA-generated REFLUX host and breaks the zero-import rule here. **Deliberately dropped from BRAWLPIT's own host:** the Hat Store flow (needs IDUNA, which the Android shell has no session for, and `connect-src 'self'` forbids it; the game runs with no hat) and fullscreen. No audio exists in the web build. Gamepad polling is ported behind a feature check; untested.

### CONSTRUCT port table — no silent omissions

| CONSTRUCT affordance (where) | here | why |
|---|---|---|
| Netkit's own functions: scanner, terminal, monitor | **ported** as the three objects above | they ARE Netkit's functions; nothing to port from the 3D hub |
| Arcade cabinet (SECTION 638, ARCADE-0..2, in the desktop lobby) | **ported** as the cabinet object | same BRAWLPIT game; hosted in-page instead of through the level manager |
| The 3D CONSTRUCT level, the Duck hero, groove combat | not ported | GL/C engine content; Netkit is a 2D WebView page |
| Architect's Orb (EduVM scripting terminal, `draw_orb_*`) and the machine bay (crate rail, gate, bridge, portal ring) | not ported | EduVM is a C package (`packages/education`) plus a NOCK-snippet fetch from okemily.com, which `connect-src 'self'` forbids; not a network tool. A PARENA port of EduVM is separate future work |
| Programmable NOCK widgets (`lobby_edu_widgets_init`) | not ported | same dependency on EduVM and on loading custom levels |
| Phone (23 `BP_APP_*` apps: messages, map, camera, notes, lab, cargo, skills, GFD, IDUNA device-auth, ARPANET, PELLEGRINO browser, DEADWEIGHT, ...) | not ported | game UI for the 3D lobby, not network tools; GFD / IDUNA / PELLEGRINO need network and credentials the shell does not offer |
| LAB stations (splice bench, centrifuge, PCR, clone vat, sample fridge, console, war terminal) | not ported | scene-bound BIG_O game systems |
| Garage-scene portals (vox, tanker, dust, poo-poo, Telecrystal ring / `portal_resolve_destination`) | not ported | scene-bound; they switch 3D scenes, which do not exist here |
| EMPIRE troop placement, kits, food/CARGO | not ported | game systems |
| Lobby APPS page (DEADWEIGHT, PITVIPER, IDUNA.GAME, REDGARDEN, EDITOR.GAME, ZOMBIES, PELLEGRINO, NOCK) | not ported | each spawns a native binary; an Android WebView cannot launch them |

No greyed-out placeholder objects were added for the unported rows: a light that does not mean something true would break the doctrine, so the omissions are recorded here instead.

**Verification state (honest, 2026-11-26, after Drop One):** the PARENA garage logic is verified on the compiler's LLVM IR (45 checks) and on the linked wasm (45). The bundle is regenerated and vendored (`verify-web-bundle.mjs` passes, now including `oui.bin`). The generated page passes its Node end-to-end leg against a real SSH server, libvncserver and real UDP name responders (108 checks), and — first time — both headless-Chromium legs under the app's CSP: garage + in-page BRAWLPIT 18, devices/SSH/VNC 25. The APK builds here (javac + lint warnings are errors). Still unrun: any real-device run (NETKIT-3).

## 6b. Drop One: names, makers, the Pi (2026-11-26, EMILY SECTION 646)

Founder's tablet-kit plan, Drop One: *"Look up each device's hardware address prefix to show the manufacturer ... Show IP address, hostname
when available, and which common ports are open ... Highlight the Pi ... Allow saving and naming devices. Done when: you can open the app,
scan, and see the Pi labeled by name."* Preceded by "CLEAN BUILDS FIRST": the APK now builds with javac and lint warnings as errors.

| piece | where | verified |
|---|---|---|
| maker table: 14,052 IEEE MA-L prefixes → 84 makers, binary-searched in wasm | `PARENA/stdlib/netkit/nk_oui_gen.prn` (curate + pack, PARENA wasm), `stdlib/netkit/data/oui_brands.tsv`, `apps/netkit/oui.bin` | every prefix round-trips (`PARENA/tests/netkit/names_spec.prn`, SCARAB) |
| private (randomized) MAC detection | `nk_names.prn` | names_spec.prn |
| mDNS reverse lookup (unicast legacy query to :5353) — build + parse, compression, hostile packets | `nk_names.prn` | RFC-built replies in Python + a real Node UDP responder in the page e2e; **not** against real avahi (it could not run in this sandbox) |
| NetBIOS node status (:137) — name + MAC → maker without ARP | `nk_names.prn` | RFC 1002-built replies; real Node UDP responder in the page e2e |
| name ranks (label > mDNS > NetBIOS > reverse DNS), `.local`/`.lan`/… suffixes dropped | `nk_names.prn` | names_spec.prn |
| the Pi: all nine Raspberry Pi blocks (three were missing) or the name `raspberrypi`; badge + first in the list | `nk_scan.prn`, `nk_host_scan.prn`, `netkit.css` | names_spec.prn, page e2e |
| user labels: Name dialog, kept in the Keystore-encrypted store, survive rescans, follow a device from IP key to MAC key | `nk_host_scan.prn`, `index.html` | page e2e (108 checks) |
| one UDP question / one answer from that address | `android-netkit/shell/Udp.java`, `NativeBridge.udpQuery` | 3 JVM tests on real loopback sockets |

Founder, same day: *"ALL PARENA BASED MODS BASED DEVELOPMENT".* Audit of Drop One: the only logic outside PARENA was the first maker-table
generator (Python). It is now `nk_oui_gen.prn` (wasm) with a PARENA->TS node host, proven byte-identical and the Python deleted (PARENA
e74853f). What remains outside PARENA is platform only: `index.html`/`netkit.css` (markup, no logic) and the Java shell (sockets, UDP,
storage, clipboard; `Udp.java` sends one datagram and returns the first reply from that address). Tests stay in Python/Node, as before.

Still unproven: a real tablet on the real network (the "done when"), avahi and Windows as real responders, and how many devices on the
founder's network answer neither protocol (those keep no maker on Android 10+).

## 6c. Windows file sharing: Files (2026-11-26, EMILY KIT-SMB)

Founder: *"windows file sharing is open on 445 can we add tools to the netkit for exploring those capabilities? parena first mods first"*,
then *"ok the netkit doesnt do anything per the windows file sharing yet can we get that working so we can explore what files are being
shared? parena mods based"*. A device answering on 445 gets a **Files** button. You sign in with a Windows account (`DOMAIN\user` works,
and empty fields try without an account), then see its shares, open folders, and preview text files. It is **read only by construction**:
the wasm has no code that writes, renames or deletes, and every request the specs see is parsed to prove it.

| piece | where | verified |
|---|---|---|
| credential-free probe: SMB2 NEGOTIATE (2.0.2..3.1.1) + the NTLM challenge: names, workgroup/domain, Windows build, dialect, signing, cipher, clock | `PARENA/stdlib/netkit/nk_smb.prn`, CLI `scripts/netkit_smb_probe.sh` | `smb_spec.prn` 59 specs incl. a recorded Samba 4.19 reply; live Samba |
| MD4, MD5, HMAC-MD5, HMAC-SHA256 | `nk_md.prn` | `md_spec.prn` 23 specs, RFC 1320/1321/2202/4231 + MS-NLMP vectors |
| sign-in: NTLMv2 + MIC in SPNEGO with mechListMIC; SMB 2.1 HMAC-SHA256 signing of every request, server signatures checked; anonymous | `nk_smb_files.prn` | MS-NLMP 4.2.4 literals; replays of signed sessions recorded from **real Samba 4.19 and impacket**; a changed reply byte ends the session |
| shares (IPC$, srvsvc DCE/RPC NetrShareEnumAll, multi-fragment), folders (QUERY_DIRECTORY, all pages, 2048-entry cap), text preview (60 KB) | `nk_smb_files.prn` | `smb_files_spec.prn` 49 specs; live Samba (folder, preview, wrong password) and impacket (shares) |
| the Files screen, sign-in dialog, saved connections, Back/Up | `PARENA/stdlib/netkit/host/nk_host_files.prn`, `apps/netkit/index.html` | headless Chromium against impacket on port 445 (17 checks, one-off: page legs move to PARENA drivers in KIT-T T4) |

Named limits: the browse session speaks SMB **2.1**, because 3.x signing needs AES-CMAC and an SP800-108 KDF that PARENA does not have yet.
Every current Windows still accepts 2.1, including Windows 11 24H2, which requires signing. A share set to "encrypt data" will refuse.
The user name is upper-cased as ASCII only. The share list could not be checked against Samba here, because its RPC helper would not run
unprivileged; impacket served that part. **Not yet run against a real Windows PC.** Starting VNC remotely over SMB (svcctl) is not built:
Windows 10 Home blocks admin sign-ins over the network (UAC remote restrictions), so `setup.bat` is still run once at the iMac.

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
  ~~Should the shell grow NetBIOS/mDNS name lookup so Windows PCs show their real names?~~ Done (§6b). Does the iMac run TightVNC with a password set (this
  app refuses a password-less *required* server and tells the user)?
