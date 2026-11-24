# SHANKPIT Netkit (Android)

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
`arpText()` · `localNets()` → JSON · `storeGet/storeSet/storeDel` (Keystore-encrypted) · `clipboardSet/clipboardGet`  
Codes: `0` connected · `1` refused (the host is up) · `2` timeout · `3` unreachable · `4` other. The page's back-button hook is `window.netkitBack()`.
The contract is documented where the page uses it: `PARENA/stdlib/netkit/host/nk_host_core.prn`.

## Build

```bash
# JDK 17+, Android SDK (platform 34, build-tools 34.0.0), Gradle 8.7
export ANDROID_HOME=~/tools/sdk
cd android-netkit
gradle :shell:test               # 16 JVM tests against real loopback sockets
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

## Status (see `docs/NETKIT_NORTHSTAR.md`)

Verified headlessly (488 checks in PARENA's `make test-netkit`: 402 on the modules, 65 running the generated page in Node, 21 in a real
browser; plus 16 JVM tests here): the whole stack against a real OpenSSH 9.6 `sshd`,
asyncssh and libvncserver. **Not yet run on a real tablet, the router Pi or the iMac** — the APK builds and its assets are byte-identical
to the vendored bundle, but there is no Android device or emulator in the build environment.
