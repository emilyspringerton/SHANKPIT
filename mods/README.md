# SHANKPIT Mods (v1)

This folder contains example mods and notes on the v1 mod API.

## Manifest format

Each mod lives in its own folder with a `mod.json` manifest. Example:

```json
{
  "id": "hello_world",
  "name": "Hello World Mod",
  "version": "0.1.0",
  "api_version": "1.0",
  "type": "native",
  "entry": "hello_world.so",
  "priority": 100,
  "capabilities": ["console", "hooks"],
  "author": "modder",
  "description": "Logs greeting and adds a console command."
}
```

## Hook API

See `include/mod_api.h` for hook enums, payloads, and the `mod_api_t` function table passed to `mod_init`.

## Examples

### `hello_world` (native)
* Logs a greeting on startup.
* Registers `hw_hello` console command.

### `health_tweak` (native)
* Increases player health on spawn using the entity spawn hook.

### `timed_message` (native)
* Prints a message every few seconds on the frame hook.

### `asset_override` (python)
* Placeholder script showing how a Python mod could respond to asset hooks.

## Notes

* Native mods are C shared libraries (`.so`) built against `include/mod_api.h`.
* Script mods are staged for Python support; v1 uses manifest metadata only.
* The engine keeps compatibility with legacy `.lua` mods in the root `mods/` directory.
