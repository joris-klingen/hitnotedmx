# HitNoteDmx — open tasks

Open backlog, renumbered. Detailed context for shipped work and
architecture lives in [STATUS.md](STATUS.md).

## Trigger menu & vocabulary

1. **Simplify the BARS vocabulary** — keep just "All bars" + one note per
   individual bar (Bar 1–4) = 5 notes. Drop the bundle selectors (1+2,
   3+4, 1+4); those combos are trivially played as multiple single-bar
   notes. Touches `kBarSelectorMask`/`kBarSelStart` in `Composition.cpp`
   (pitch 4–11 → 4–8) and the `TriggerMenu` "Bars" block. Frees pitches
   9–11. Project-owned vocabulary — no external parity to preserve.

2. **Simplify the PIXEL-ZONE vocabulary** — keep one note per single zone
   and drop the combo masks (Zones 2-3, 1-3, 4-6, 7-9, Ends 1&9, Odd,
   2,5,8). Decide the single-zone set (18 px grouped into 9 zones today
   via `zone()` — likely 9 single-zone notes). Touches `kPixelStaticMask`
   + range/bit-width in `Composition.cpp` and the menu "Pixel zones"
   block. Re-confirm the zone→pixel mapping afterward.

3. **Trigger menu: 4 columns + inline note name** — lay text-trigger cells
   in 4 columns (was 3). Put the button name and its MIDI note on the
   *same* line: name in a slightly bigger font, note name to the right in
   grey (instead of stacked). May widen the right pane a touch (wider
   window is fine). Verify no clipping and still no scroll.

## Features

4. **Velocity-controllable chase tails** — chases get a trail whose length
   tracks note velocity (soft = long tail, hard = short/instant).
   Architectural: `DynamicFn` currently receives no velocity — thread a
   velocity/tail arg (or params struct) into the recipe call, then add
   decay to chase_up/down, ping_pong, snake, sweep_up/down. No
   audio-thread allocation.

5. **Pixel-density reduction (experimental)** — a master control that thins
   how many LEDs are lit while keeping full-brightness flashes (for dark
   rooms). Needs a *stable* per-pixel gate (hash or every-Nth) so it
   doesn't flicker frame-to-frame. Spike first to judge whether it looks
   good before full build-out.

## Show prep

6. **ENTTEC USB Pro hardware smoke test** — confirm real DMX out with the
   rig lit, including the new 228-channel patch (bars 1–216, spots
   217/223). Do this before composing cues.

7. **Expand the dynamic recipe library** — beyond the current 12: rain,
   ripples, wave-trains, halos, expand/contract, edge-only, etc. Draft
   per-pixel functions, port the good ones to C++. Overlaps with #4
   (recipe internals).

## Housekeeping

8. **Confirm long-session GUI stability** — the preview now draws 72 cells
   (4 × 18) in a larger window; re-confirm compositor cost is fine under
   sustained chases over a long session. Optional OpenGL context only if
   software-compositor cost ever returns.
