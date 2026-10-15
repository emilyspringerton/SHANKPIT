# TUTORIAL_PROGRAMMABLE

Level for the programmable-widget system (NOCK `edu_widgets` -> EduVM).

Run: `bin/shank_lobby --level examples/programmable/TUTORIAL_PROGRAMMABLE.json` (local game; the Orb is local-only).

A pressure plate (register 1) sits at z=8; three red gates (registers 2, 3, 4) block the corridor at z=16/24/32.
Stand on the plate, walk to a gate and press **E** (or `` ` ``) to open the Architect's Orb on the *Gate Sequencer*
slot, **F7** compile, **F8** run. With the plate held the loop opens every gate; off the plate it opens none.
The script runs once per F8 and gate state persists, so: step on the plate, run it, walk through the open gates.
Step off the plate and run it again and nothing changes (an already-open gate stays open; `set_state("gate_1", 0)` closes it).
For a harder variant, write your own loop that closes gates behind you.

Script surface: `set_state("id", v)`, `set_reg(reg, v)`, `read_sensor(reg)`, `read_state("id")`. Widget fields:
`entity_id`, `machine_type` (1 gate, 2 bridge, 3 portal, 4 crate, 5 plate/sensor), `initial_state`,
`memory_address` (unique register), `x/y/z`, `radius` (plate reach), `box_index` (wall this widget removes when state=1).
