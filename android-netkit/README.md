# SHANKPIT Netkit (Android) -- the SHANKPHONE

A network toolkit for a tablet: **scan** the LAN, open an **SSH** terminal, **watch** a remote screen over **VNC** (view only). A second
Android app in this repo, separate from the shankpit-460 APK (`android/`). Source of intent: the founder's "Custom VNC Client:
Affordances and Network Map" (2026-11-23) — VS0 deliverable 0.

## It is PARENA, not Java

Founder, 2026-11-24: *"ensure its all parena based mods first dev."* So the app is a WebView around a page whose logic is PARENA compiled
to WebAssembly (`PARENA/stdlib/netkit/`, vendored build in `apps/netkit/`):

```
 ┌──────────────────────── WebView (Chromium) ────────────────────────┐
 │  nk-host.js   (PARENA → TypeScript → JS: screens, keys, plumbing)  │
 │     │  bytes only                                                  │
 │  netkit.wasm  scanner · VNC/RFB · SSH · SHA-256/512 · ChaCha20 ·   │   no malloc, zero imports,
 │  netkit-lite  Poly1305 · X25519 · Ed25519 · DES                    │   fixed memory
 │  nkterm.wasm  the xterm-style terminal                             │
 └───────────────────────────────┬────────────────────────────────────┘
                     window.NetkitNative  (this module)
 ┌───────────────────────────────┴────────────────────────────────────┐
 │ shell/  TcpHub · Prober · NetInfo · ArpTable · Resolver  (pure Java)│   what a browser sandbox cannot do:
 │ app/    MainActivity · NativeBridge · SecureStore (Keystore AES-GCM)│   raw sockets, ARP, local network,
 └─────────────────────────────────────────────────────────────────────┘   encrypted storage, clipboard
```

The Java never interprets a byte. SSH (key exchange, host-key signature check, the cipher, auth, channels) and VNC (handshake, DES auth,
Raw/CopyRect/Hextile decoding) are **written in PARENA** — no libssh2, no JSch, no JVM crypto.

### The native contract (`window.NetkitNative`; every call returns at once, results arrive as `window.netkitEvent(json)`)

`tcpOpen(id, host, port, timeoutMs)` · `tcpWrite(id, base64)` · `tcpClose(id)` → `{t:"tcp", id, e:"open"|"data"|"closed"|"error", b?, code?}`  
`probe(job, ip, port, timeoutMs)` · `ping(job, host, timeoutMs)` → `{t:"probe", job, code, ms}` · `resolve(ip)` → `{t:"name", ip, name}`  
`udpQuery(id, host, port, base64, timeoutMs)` → `{t:"udp", id, e:"data", b}` or `{t:"udp", id, e:"none"}` — one unicast UDP question to one device (the scanner's mDNS/NetBIOS name lookups; the packet is built and parsed in the wasm)  
`arpText()` · `localNets()` → JSON · `storeGet/storeSet/storeDel` (Keystore-encrypted) · `clipboardSet/clipboardGet`  
Codes: `0` connected · `1` refused (the host is up) · `2` timeout · `3` unreachable · `4` other. The page's back-button hook is `window.netkitBack()`.
The contract is documented where the page uses it: `PARENA/stdlib/netkit/host/nk_host_core.prn`.

## Build

```bash
# JDK 17+, Android SDK (platform 34, build-tools 34.0.0), Gradle 8.7
export ANDROID_HOME=~/tools/sdk
cd android-netkit
gradle :shell:test               # 19 JVM tests against real loopback sockets (TCP and UDP)
gradle :app:lintDebug :app:lintRelease   # must say "No issues found": lint warnings fail the build
gradle :app:assembleDebug        # app/build/outputs/apk/debug/app-debug.apk
gradle :app:assembleRelease      # signed with the debug key unless SHANKPIT_NETKIT_KEYSTORE* are set
node tools/verify-web-bundle.mjs ../apps/netkit
```
Env knobs: `SHANKPIT_NETKIT_VERSION_CODE`, `SHANKPIT_NETKIT_VERSION_NAME`, `SHANKPIT_NETKIT_KEYSTORE` + `_PASSWORD`, `_KEY_ALIAS`, `_KEY_PASSWORD`.
CI builds it in `release.yml` and ships it as `ShankPit_Netkit_<build>.apk` next to the other artifacts.

## Security posture

* **Credentials are never sent to a host that has not been accepted.** First contact stops at "Trust this device?" showing the SHA-256 host-key
  fingerprint; a remembered fingerprint that no longer matches fails *before* anything is sent. The server's Ed25519 signature over the key
  exchange is verified first (a server that cannot prove it owns its key fails).
* The WebView loads only the packaged `https://netkit.local` origin (other origins and navigation are blocked), under a strict CSP
  (`script-src 'self' 'wasm-unsafe-eval'`, no inline script). `NetkitNative` is reachable only by that page.
* Saved passwords are stored only if the user ticks *Remember*, AES-256-GCM encrypted with an Android Keystore key. If the Keystore is
  unavailable the value is kept unencrypted in the app-private store (and `SecureStore.encrypted()` says so) rather than lost.
* VNC is **view only by construction**: no code in the wasm or the page emits a key, pointer or clipboard message (asserted on the wire).
* Honest limits: VNC password auth is single DES (the protocol's weakness, not ours) — use it on the router network only; SSH offers only
  `curve25519-sha256` + `ssh-ed25519` + `chacha20-poly1305@openssh.com` (a server without them is refused with a plain message, never
  downgraded); no SSH public-key auth yet; no Tight/ZRLE VNC encodings; the terminal is a fixed 100×34 grid (good on a tablet, tiny on a phone).

## The garage and the arcade (2026-11-24)

The page opens on a flat-mode **garage** (the SHANKPIT CONSTRUCT hub's doctrine: every function is an object, status is a lamp on it, a cable means a live connection): scanner bench -> Devices, terminal -> SSH, monitor -> VNC, and an **arcade cabinet** that plays **BRAWLPIT** (WOTAN's WebAssembly fighter, `apps/netkit/brawlpit.wasm`, zero imports) inside the page with a multi-touch pad. All decisions are PARENA wasm (`nk_garage.prn`); the Java shell is unchanged (assets are packaged verbatim from `apps/netkit`). BRAWLPIT has no audio and no hat (no IDUNA session in the shell). Measured 2026-11-26: the release APK is 487 KB (the PNG art is ~250 KB of it). Back button: arcade/devices -> garage -> app closes. See `docs/NETKIT_NORTHSTAR.md` §6a for the CONSTRUCT port table. Untested on a device: 60 fps on a tablet WebView, multi-touch pointer capture, the extra 8 MB of memory, portrait layout.

## The SHANKPHONE and its Survival button (2026-11-26)

Founder: *"the shankpit net kit can be the shankphone and the shankphone itself can have a button for survival that launches OG shankpit as
a level and collection of mods"*. The app label is now **SHANKPHONE**, and so is the page brand. The package id is unchanged
(`industrial.einhorn.netkit`), so an installed Netkit updates in place. Garage object 5, **Survival**, opens a view that lazily loads
`survival.wasm`: the shankpit-460 PARENA engine with the CITY level and the survival mods (`PARENA/stdlib/shankpit_survival/README.md`).
It is fetched from the app's own origin and instantiated with no imports, so the CSP and the Java are untouched.

The host is `PARENA/stdlib/netkit/host/nk_host_survival.prn`. The button map, look speed and stick radius come from `nk_garage.prn`:
- the stick drives gamepad slot 0;
- dragging the picture is mouse look;
- Fire is the left mouse button, and Weapon is the wheel (owned weapons only);
- Jump, Crouch, Reload and Ability are their keys;
- a tap on the picture in a menu is a click.

At heights of 520 px or less, the header and tabs hide and the game fills the screen. Measured: release APK 650 KB. Verified:
`test_browser_garage.py` (survival leg: load, PLAY, the stick moves the view, drag-look, Fire pressed and released, Back, lamp, loop stops,
no CSP error). **Untested on a device**: frame rate on a phone WebView, real multi-finger play. **No sound yet**: the engine's sound ring is
not played.

## Clean builds (2026-11-26)

Founder: *"CLEAN BUILDS FIRST"* before every drop. The build enforces it: `javac -Xlint:all -Werror` in both modules, and Android lint with
`warningsAsErrors` + `abortOnError`. Back is handled with `OnBackInvokedCallback` on Android 13+ (predictive back), `onBackPressed` below;
cloud backup and device transfer exclude everything (`res/xml/data_extraction_rules.xml`). First built on this box 2026-11-26: JDK 21,
SDK platform 34 / build-tools 34.0.0, Gradle 8.7, AGP 8.5.2.

## Devices: names, makers, the Pi (Drop One, 2026-11-26)

The device list shows, per device: **your own name** for it (tap **Name**; kept in the encrypted store, so it survives rescans and restarts),
else the name the device or the router gives; the address; what it looks like (from its open ports); the **maker** of its network card
(from the MAC prefix: `apps/netkit/oui.bin`, 14,052 IEEE prefixes for 84 home-network makers); and chips for open SSH/VNC/RDP/SMB/web ports.
A **Raspberry Pi** gets a green **Pi** badge and goes to the top of the list (after anything you pinned) — recognised by its MAC prefix
(all nine Raspberry Pi IEEE blocks) or by the name `raspberrypi`.

Where names and MACs come from, because Android 10+ hides the ARP table from apps: reverse DNS (the router's DHCP names), **mDNS** (a
unicast reverse lookup to the device's port 5353 — Raspberry Pi OS, macOS, iOS, printers answer), and **NetBIOS** node status (port 137 —
Windows PCs and Samba also report their MAC, which is how the maker shows on a modern tablet). Priority: your label > mDNS > NetBIOS >
reverse DNS. All of it is PARENA (`PARENA/stdlib/netkit/nk_names.prn`); Java only sends one datagram and returns the first reply from
that address (`shell/Udp.java`).

Honest limits: on Android 10+ a device that answers neither mDNS nor NetBIOS has no MAC, so **no maker** (shown as nothing, never guessed);
phones and tablets use private (randomized) MACs, shown as "Private Wi-Fi address"; the maker table is curated (an unlisted maker shows as
nothing); a renamed Pi with no mDNS/NetBIOS answer and no ARP is only recognised once you name it. mDNS parsing is tested against replies
built byte-for-byte to RFC 1035/6762 and NetBIOS against RFC 1002 (Python and Node responders), **not yet against a real avahi or Windows
machine** — that is the tablet test.

## Status (see `docs/NETKIT_NORTHSTAR.md`)

Verified headlessly (488 checks in PARENA's `make test-netkit`: 402 on the modules, 65 running the generated page in Node, 21 in a real
browser; plus 16 JVM tests here): the whole stack against a real OpenSSH 9.6 `sshd`,
asyncssh and libvncserver. **Not yet run on a real tablet, the router Pi or the iMac** (the 488 figure predates the garage/arcade; those new checks, and a regenerated bundle, are described in the northstar §6a) — the APK builds and its assets are byte-identical
to the vendored bundle, but there is no Android device or emulator in the build environment.
