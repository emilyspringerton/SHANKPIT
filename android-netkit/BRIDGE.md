# Java <-> web bridge contract (VS0)

Front end = static web app in `app/src/main/assets/web/` (served from virtual origin
`https://netkit.local/`, same pattern as `android/`'s MainActivity). Back end = Java.
Calls are JSON strings. No ids are reused within a session.

## JS -> Java: `window.NetkitNative` (a @JavascriptInterface object; every method returns void, results come back as events)

| method | args | effect |
|---|---|---|
| `scanStart(cidrOrEmpty)` | string | start a LAN scan ("" = auto-detect) |
| `scanCancel()` | | |
| `probe(id, host, port)` | int id, string, int | TCP port check -> `probe` event |
| `ping(id, host)` | | -> `probe` event |
| `sshOpen(id, jsonParams)` | `{host,port,user,password,cols,rows}` | open shell -> `ssh` events |
| `sshWrite(id, b64)` | base64 bytes | keystrokes to the remote |
| `sshResize(id, cols, rows)` | | |
| `sshClose(id)` | | |
| `vncOpen(id, host, port, password)` | | view-only session -> `vnc` events |
| `vncClose(id)` | | |
| `storeGet(key)` -> string (sync, "" if absent) | | saved connections live in app-private SharedPreferences |
| `storeSet(key, value)` | | |
| `info()` -> string (sync JSON) | | `{version, localCidrs:[...]}` |

## Java -> JS: `window.netkit.onEvent(obj)` (Java calls `evaluateJavascript("window.netkit&&window.netkit.onEvent("+json+")")` on the UI thread)

Every event has `type`. Others:

- `{type:"device", ip, mac, name, guess, ports:[..]}` (repeat per ip = update), `{type:"scanDone", hosts:N}`, `{type:"scanError", kind, message}`
- `{type:"probe", id, open:bool, kind:"UNREACHABLE|PORT_CLOSED|TIMEOUT|...|null", message, millis}`
- `{type:"ssh", id, event:"open"}` once shell is up; `{type:"ssh", id, event:"data", b64}`; `{type:"ssh", id, event:"closed", reason}`; `{type:"ssh", id, event:"error", kind, message}` (open failed)
- `{type:"vnc", id, event:"size", w, h, name}`; `{type:"vnc", id, event:"rect", x, y, w, h, b64}` where b64 = **RGBA** bytes, w*h*4 (alpha 255), row-major; `{type:"vnc", id, event:"closed", reason}`; `{type:"vnc", id, event:"error", kind, message}`

`kind` strings are exactly the `ErrorKind` enum names. The UI maps them to plain words:
UNREACHABLE "Can't reach that device", PORT_CLOSED "Nothing is listening on that port",
TIMEOUT "No answer (asleep, or blocked?)", AUTH_FAILED "Wrong username or password".

Saved-connection store keys used by the UI: `connections` (JSON array of `{name,host,port,proto:"ssh"|"vnc",user,password}`), `pinned` (JSON array of ip/mac strings).
