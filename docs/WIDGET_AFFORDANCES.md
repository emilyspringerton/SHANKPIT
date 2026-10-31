# Widget affordances: how to make a thing in the world do something

This is the contract to build against. If you are an agent asked to "bring the lab equipment to life",
"add a security terminal", "make an elevator", or "add a light switch", you do not write a new system.
You place widgets, wire them, and (optionally) drive them from an EduVM script.

Everything below already exists and is tested. Nothing here needs a new renderer.

## 1. The wire format (level JSON, `edu_widgets`)

```json
{
  "entity_id": "lever_a",
  "machine_type": 6,
  "memory_address": 10,
  "x": 0, "y": 0, "z": 0,
  "radius": 3,
  "input_pin": -1,
  "gate_pin": -1,
  "delay_ticks": 0,
  "box_index": -1
}
```

| key | meaning |
|---|---|
| `entity_id` | the script-visible name (`set_state("lever_a", 1)`), max 31 chars, must be unique |
| `machine_type` | what the node **is**, see the table below |
| `memory_address` | the EduVM register its live value is mirrored into; `-1` = not scripted |
| `x,y,z` / `radius` | where it stands and how close a player must be for the F prompt |
| `input_pin` / `gate_pin` | index **into this same array** of the widget feeding it; `-1` = none |
| `delay_ticks` | a DELAY's own length in 60 Hz ticks; `0` = the default half-second relay |
| `box_index` | for geometry machines: which authored box it raises/lowers; `-1` = register only |

**Pins must point at lower indices.** The circuit is evaluated once per tick in authoring order, so a
node reads values already computed this tick. A pin pointing forward (or at itself, or past the end)
is dropped to `-1` at load: a mis-wired level degrades to "that switch does nothing", never to a
feedback loop.

## 2. The five kinds

| `machine_type` | kind | behaves as |
|---|---|---|
| `6` | LEVER | a light switch. F toggles it, it stays. 0 or 15, no wiring needed. |
| `5` | PLATE | a pressure plate. The **host** writes it from bodies standing on it; a script cannot forge one. |
| `7` | DELAY | a relay. Output follows input `delay_ticks` later, **both edges**. |
| `8` | TRANSISTOR | a real AND. Passes its input only while its `gate_pin` is powered. |
| `1`–`4` | MACHINE | a motor: gate, bridge, portal, crate, terminal, lift car. Follows its input. |

Power is one integer, **0..15**, the same tier range DEADWEIGHT's cables use. A whole level is a byte
per node, and any node can drive any other.

## 3. Recipes

**Light switch** — one LEVER at the wall, `memory_address` if a script cares. Done.

**Security door** — PLATE (`radius` 2–3) → TRANSISTOR → MACHINE(2), with a second LEVER wired to the
transistor's `gate_pin` as the "unlocked" switch. Step on the plate with the gate off and nothing
happens: that is the whole point of the transistor.

**Elevator** — LEVER (call button) → DELAY (`delay_ticks` 45, so the car does not lurch) →
MACHINE(2) with `box_index` set to the platform. An elevator is literally a widget of widgets: the
lever is the button, the delay is the door-closing pause, the machine is the car.

**Lab centrifuge ("the spiny thing")** — PLATE → MACHINE(1) with `memory_address` 14. The warp drives
the spin from the register; the panel below sets the RPM.

**Terminal** — MACHINE(1) with a `memory_address` and no `box_index`. F opens its panel (section 4);
ESC closes it and hands the world back.

## 4. The F interface (already implemented in `packages/simulation/widget_circuit.h`)

- `widget_circuit_focus(c, x, y, z, busy, dt_ms)` → the nearest in-reach widget, or `-1`. `busy`
  (panel open, in a vehicle, cutscene) suppresses the prompt. The prompt holds its last focus for
  ~250 ms so it does not strobe as you walk along a shelf of levers.
- `widget_circuit_use(c)` → on a LEVER, toggles and stays in the world; on anything with a register,
  opens its **PANEL** (`WIDGET_MODE_PANEL`) — this is the "custom programmed affordance" handoff.
- `widget_circuit_panel_key(c, key, panel_max)` → UP/DOWN dial a setpoint by 500 per press, clamped to
  `0..panel_max`; USE/LEFT/RIGHT are consumed and do nothing yet (reserved for tabs); BACK closes and
  returns `1`. Keys in the world are never swallowed by a closed panel.
- `widget_circuit_reg_value(c, reg)` → what a host copies into EduVM, or `-1` if no node owns it.

## 5. EduVM

Every node mirrors its live value into `reg_value` each tick, and a panel writes that same value. So a
script and the physical circuit never hold two copies of the world:

```pascal
// slot 8: poll the plate, hold the gate open while someone is on it
while (1) {
    if (read_sensor(13) > 0) { set_reg(10, 1); } else { set_reg(10, 0); }
    sleep(1);
}
```

Widget values are written by the host with `edu_widget_host_set`, so a script can read a sensor but
cannot forge one. That asymmetry is deliberate: a security door that a script can claim is open is not
a door.

## 6. What is NOT built (do not pretend otherwise)

- **No drawing.** There is no prompt quad, no panel rendering, no lever/centrifuge mesh. The circuit is
  headless; a renderer pass reads `mode`, `pressed`, `focused` and `reg_value` and draws what it likes.
- **Not in the game loop yet.** `widget_circuit_tick` is driven by `make test-widget-circuit`. Wiring it
  to a scene (load on level load, tick once per frame, feed plates from player positions, install the
  PARENA rules at startup) is the next step.
- **No editor UI for wiring.** The NOCK/level-editor affordance is a table of pins in the JSON; adding
  a visual wire tool in IDUNA's editor or `tools/nock_editor.html` is a separate slice.
- **No back-EMF / overload.** DEADWEIGHT's circuit-burn mechanics are not modelled: power is a plain
  0..15 integer with no heat, no fuses, no cascading failure.
