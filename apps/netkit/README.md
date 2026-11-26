# apps/netkit — the SHANKPIT Netkit page (vendored build output + markup)

What the Android app (`android-netkit/`) loads into its WebView, and what runs in any browser:

| file | what | where it comes from |
|---|---|---|
| `index.html`, `netkit.css` | markup and styling — **no logic, no inline script** | hand-written, here |
| `nk-host.js` | the page host: screens, dialogs, key handling, byte plumbing | **generated**: `PARENA/stdlib/netkit/host/*.prn` → TypeScript → JavaScript |
| `netkit.wasm` (64 MB memory) | scanner + VNC (RFB) + SSH + crypto; one instance per VNC session | **generated**: `PARENA/stdlib/netkit/nk_*.prn` → LLVM → wasm32 |
| `netkit-lite.wasm` (8 MB) | the same code with less memory: the scanner, and one instance per SSH session | same build, `NETKIT_MEM=8388608` |
| `nkterm.wasm` (8 MB) | the SSH terminal emulator | **generated**: `PARENA/stdlib/netkit/term/nk_term.prn` |
| `brawlpit.wasm` (8 MB, zero imports) | the arcade cabinet's game: BRAWLPIT, loaded lazily as a second module | WOTAN's committed web build (`WOTAN/BRAWLPIT/brawlpit.wasm`, from `PARENA/stdlib/brawlpit/bp_*.prn`), copied unchanged (sha256 in `PROVENANCE`) |
| `duck.png`, `sprites.png` | BRAWLPIT's art atlases (640x340 fighter frames, 512x256 portraits); optional — without them the engine draws its fallback look | copied from `WOTAN/BRAWLPIT` |

The page opens on the **garage** (flat-mode CONSTRUCT hub: scanner bench, terminal, monitor, arcade cabinet, each with a status light; cables only for live sessions). Layout, lights, words, tap routing, the back button and BRAWLPIT's touch-key map are `PARENA/stdlib/netkit/nk_garage.prn` (wasm); see `docs/NETKIT_NORTHSTAR.md` §6a for what was and was not ported from CONSTRUCT and why.

There is no hand-written JavaScript in this product (founder, 2026-11-24: "ensure its all parena based mods first dev"; same rule as
shankpit-460 and BRAWLPIT web). Every `.wasm` imports nothing and has fixed memory — they cannot touch a socket, a clock or the DOM; the
only platform they need is the Android shell's `NetkitNative` (see `android-netkit/README.md`).

## Regenerate

```bash
# needs the LLVM 18 toolchain (opt, llc, wasm-ld) and node 24+; see PARENA/scripts/build_netkit_wasm.sh
cd PARENA && scripts/build_netkit_web.sh /tmp/netkit-web
cp /tmp/netkit-web/{nk-host.js,netkit.wasm,netkit-lite.wasm,nkterm.wasm,brawlpit.wasm,duck.png,sprites.png} ../SHANKPIT/apps/netkit/
# (then refresh the sha256 lines in PROVENANCE)
node ../SHANKPIT/android-netkit/tools/verify-web-bundle.mjs ../SHANKPIT/apps/netkit   # imports, memory, exports, ids, no inline script
```

Provenance of the committed build: PARENA `main` at the commit named in `PROVENANCE`.

**STATUS (2026-11-26): the bundle is current.** `nk-host.js`, `netkit.wasm` and `netkit-lite.wasm` were regenerated from the garage/arcade source (PARENA `nk_garage.prn`, `nk_host_garage.prn`, `nk_host_arcade.prn`) with LLVM 18.1.3 + node 24; the same build reproduced `brawlpit.wasm`, `nkterm.wasm` and the PNGs byte for byte. `verify-web-bundle.mjs` passes. Run headless: PARENA `scripts/test_netkit.sh` legs (garage IR 45, garage wasm 45, rfb 97, scan 62, ssh interop 61, term 82, x25519 31, crypto 14, des 12, ed25519 16, rfb vs libvncserver 13, the generated page in Node vs a real SSH + VNC server 94). Not run: the two headless-Chromium legs (no playwright here), the OpenSSH leg (no libnss_wrapper), the APK build (no Android SDK) and any real device.

## Try it in a desktop browser

The page needs `window.NetkitNative`; without it the Devices screen still loads but can reach nothing. `PARENA/tests/netkit/browser_bridge.py`
is a stand-in (an in-page shim over a WebSocket-to-TCP bridge) used by `test_browser_e2e.py`, which drives this exact page in headless
Chromium with the same Content-Security-Policy the app sets.
