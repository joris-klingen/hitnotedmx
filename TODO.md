# HitNoteDmx — open tasks

Open backlog, renumbered. Detailed context for shipped work and
architecture lives in [STATUS.md](STATUS.md).

## Features

1. **Pixel-density reduction (experimental)** — a master control that thins
   how many LEDs are lit while keeping full-brightness flashes (for dark
   rooms). Needs a *stable* per-pixel gate (hash or every-Nth) so it
   doesn't flicker frame-to-frame. Spike first to judge whether it looks
   good before full build-out.

2. **Reorganise the trigger-menu groups** — the right panel lumps every
   recipe under one "Dynamics" header. Split it into clearer groups:
   **Chases**, **Breathes**, **Multicolor** (the future self-coloured
   recipes, #5), and **Wild** (strobe, sparkle). Menu-organisation change
   in `TriggerMenu.cpp` (new `rowsBlock`s) — pitches can stay as-is, though
   the regrouping may motivate reordering the dynamic pitch range later so
   each group is contiguous.

## Show prep

3. **Expand the dynamic recipe library** — beyond the current 12: rain,
   ripples, wave-trains, halos, expand/contract, edge-only, etc. Draft
   per-pixel functions, port the good ones to C++. The chase-tail plumbing
   (velocity → `tail` arg in `DynamicFn`) is now in place for any new
   moving-head recipes to reuse.

4. **Grid-aware chases (more directions)** — today's chases run mostly
   vertical (per-bar up/down) plus snake. Add motion that uses the full
   4×18 grid: **horizontal** (sweep across the bars at a fixed pixel row),
   **diagonal**, wipes, etc. No signature change needed — recipes already
   receive `barIdx`, `pixel`, `nBars`, `nPix`; horizontal is driven by
   `barIdx`, diagonal by combining `barIdx` + `pixel`. The `tail`/
   `cometBrightness` plumbing applies to these too.

5. **Full-colour chases (self-coloured recipes)** — recipes that carry their
   own distinct colours rather than taking the palette route: rainbow
   chases, a comet, a VU meter (green → yellow → red up the bar), desert
   breathe, etc. **Architectural:** today `DynamicFn` returns a single
   brightness and colour comes from the palette/selector routing in
   `Composition.cpp`. Self-coloured recipes need to emit RGB — add a
   parallel recipe kind (e.g. `DynamicColorFn` returning RGB, or have the
   recipe write straight to the pixel colour) and a routing path that lets
   the dynamic layer override the palette colour when the held recipe is a
   coloured one. Decide how these compose with the colour-fade stage.

6. **Calming "ambient" breathes for quiet sections** — very slow, smooth
   movements across the grid for soft passages: a moon rise (a soft glow
   climbing the bars), a soft ball that drifts slowly, a slowly rotating
   square, etc. Emphasis on gentle, sub-bar-rate motion and smooth
   brightness ramps (no hard edges). Some of these are 2-D shapes over the
   4×18 grid, so they need bar+pixel geometry (they already get `barIdx`,
   `pixel`, `nBars`, `nPix`); a few may want their own colour (overlaps
   with #5).

## Housekeeping

7. **Confirm long-session GUI stability** — the preview now draws 72 cells
   (4 × 18) in a larger window, plus a strobe-flash timer that repaints the
   visualiser at up to 40 Hz while the strobe note is held. Re-confirm
   compositor cost is fine under sustained chases + strobe over a long
   session. Optional OpenGL context only if software-compositor cost ever
   returns.

8. **Remove dead colour-pitch helpers** — `isPrimaryColorPitch` /
   `isSecondaryColorPitch` in `Composition.cpp` are unused (compiler warns).
   Trivial cleanup.

## Recently shipped (see STATUS.md for detail)

- Simplified BARS vocabulary (All + Bar 1–4) and PIXEL-ZONE vocabulary
  (9 single zones); freed pitches 9–11 and 21–23.
- Trigger menu: 4 columns, inline label + grey note name.
- ENTTEC USB Pro hardware smoke test — confirmed real DMX out with the
  228-channel patch.
- **Strobe** is now a frame-synced global shutter in the DMX driver
  (jitter-free, decoupled from the audio block rate), mirrored on screen.
- **Velocity-controllable chase tails** — `DynamicFn` carries a `tail` arg;
  chases (chase_up/down, ping_pong, snake) trail by velocity. Dynamics no
  longer route colour (that's the bar/zone selectors' job) — they modulate
  brightness only.
