# HitNoteDmx — open tasks

Open backlog, renumbered. Detailed context for shipped work and
architecture lives in [STATUS.md](STATUS.md).

## Show prep

1. **DMX link resilience — auto-reconnect (RELIABILITY, do before a show)**
   — the engine is robust, but the USB driver has no recovery: in
   `EnttecProDmx::hiResTimerCallback` a single failed write calls
   `closePort()` + `connected=false` and then the timer just returns forever,
   so **one transient USB hiccup (wiggled cable, hub blip, brief stall) kills
   output until someone manually clicks Connect** — i.e. lights-out mid-show.
   Fix: on send failure keep the timer retrying `openPort()` + re-handshake
   every ~1 s instead of latching off. Also make the write non-blocking
   (`O_NONBLOCK` / write timeout, drop the frame on `EAGAIN`) so a stalled
   device can't freeze output on the last frame. ~Half-day incl. the real
   unplug/replug hardware testing, which is the bulk of it. One file, no
   architectural change.

2. **Polish the showcase content** — **Init. names** installs the embedded
   named trigger rack into the Ableton User Library; **Show clips** writes a
   set of layered demo combo clips (static looks + within-the-bar movers) to
   `~/Music/HitNoteDmx Showcase/Combos/` and opens it in Finder. Follow-ups:
   add a few longer "song section" clips; palette-routing demos (primary vs
   soft-velocity secondary); per-recipe velocity variants (e.g. a Breathe at
   low velocity to show the islands, a strobe velocity sweep). Tune clip
   count/length once auditioned.

3. **Drag a MIDI clip from the plugin into the DAW** — once a selection is
   built by clicking tiles in the trigger menu, let the user drag that latched
   set out of the plugin as a MIDI clip (the active pitches as notes) and drop
   it onto a track. Turns the menu into a quick clip builder and pairs with the
   demo-clip library (#2). JUCE `performExternalDragDropOfFiles` on a generated
   temp `.mid` is the likely path.

4. **Visually tune the recipe banks** — all four feel-group octaves are now
   full (48 recipes), implemented against numeric checks and ASCII-frame
   renders, not yet judged on real hardware. The newest fills (Wild:
   lightning/glitch/bounce/zigzag/converge/…; Multicolor: borealis, night sky,
   galaxy, nebula, plasma, police, disco, blocks, VU smooth) especially want
   an eyeball. Speeds, band widths, gaussian radii and hue ramps are all
   single-constant tweaks in `Recipes.cpp`.

5. **Pixel-density: re-roll, taste check, and C8 note controls** — the density
   gate drops pixels in a per-bar-even random order (avalanche hash, rank-
   normalised per bar; the old diagonal banding is fixed). Open work:
   - **Re-roll the thinned subset on each colour-note trigger** so the dropped
     pixels change with the colour instead of staying fixed all show (more
     life); reseed the per-bar rank from the winning colour note.
   - **Expose density + a new "soft edges (2D)" feather as note options in the
     C8 octave** (120–127), velocity = intensity — playable from MIDI like the
     other layers, alongside the existing automatable density knob.
   - Confirm the scatter looks right on the physical bars; decide the default.

## Housekeeping

6. **Re-author pixel zones natively for 18 pixels** — the 9 "zones" are a
   fossil of the old 9-pixel rig: each is authored as one zone then stretched
   2× to the physical pixels (`zone()` in `Composition.cpp`), while Even/Odd/
   Thirds are authored natively for 18. Two mental models for grouping pixels
   in the same column. Low priority (works fine) — re-author the zones as
   native 18-pixel groups for consistency when convenient.

## Recently shipped (see STATUS.md for detail)

- **Strobe rework** — moved to the root of the Wild octave (C2 = 48; sparkle/
  sparkle few shifted to 49/50). Flashes **white by default** (rig lit white via
  the white-default path so the driver shutter has something to chop; a colour/
  recipe held alongside shows through). The shutter now lights one emitted frame
  per period (shortest flash) instead of a 50% duty; **velocity sets the repeat
  rate 1–20 Hz** so only the black gap grows. Visualizer mirrors the pattern.
- **Showcase assets** — two left-pane buttons: **Init. names** installs the
  embedded named trigger rack (`Hitnotenames.adg`, via `juce_add_binary_data`)
  into the Ableton User Library; **Show clips** writes layered demo *combo*
  clips to `~/Music/HitNoteDmx Showcase/Combos/` and opens it in Finder. Clips
  are generated at runtime (`Source/Showcase.cpp`) from the bank/palette
  constants, idempotent. The rack is named by `tools/name_rack.py` (parses the
  source), with group prefixes (cp/cs/bk/sp/ba/pz/ch/br/wd/mc); `installRack()`
  always overwrites so re-clicking refreshes stale names.
- **Recipe reorder + per-bank velocity + VU meter** — all four dispatch tables
  reordered into a logical grouping (matching the menu 1:1). Velocity means a
  different thing per bank: Chases → tail, Wild → beat-division (sparkle(s) stay
  free-running), Breathes → density islands + half-speed (not ripple),
  Multicolor → speed. **VU meter** is beat-locked with velocity → gain, a fast
  per-beat release to a 2-pixel floor, and headroom so a 127 hit pins the top
  red pixel. **White default:** a palette-less bar/pixel/dynamic/strobe hold
  renders full white.
- **Velocity semantics documented** — a single table atop `Composition.cpp`
  maps the four meanings of velocity (route / tail / speed / intensity+fade).
- **Live-MIDI tile highlight** — trigger-menu tiles light up while their note
  is sounding (held in `MidiState`, MIDI or preview), via a 15 Hz snapshot
  the editor pushes to `TriggerMenu::setLiveNotes`. The menu doubles as a
  live activity display.
- **Full matrix** — every dynamics octave is now a complete chromatic set of
  12: Chases (+Snake H), Breathes (+Ripple H, Bloom, Shimmer, Sway, Drift),
  Wild (Strobe on the root, +Sparkle few, Lightning, Static, Glitch, Bounce,
  Fast ball, Zigzag, Converge), Multicolor expanded to two octaves / 24 (VU
  smooth, magma, lava, heatmap, forest, sunset, twilight, borealis, night sky,
  galaxy, nebula, storm, plasma, police, disco, blocks, candy). Pixel zones
  gained a **Thirds** comb (pixels 1,4,7,…). Blackout reintroduced over MIDI at
  C-2. All 48 recipes verified in range + alive + colourful by unit test.
- **Piano-roll editor redesign + octave-aligned MIDI remap** — window is
  now a flat 1296×360. The trigger pane is a **transposed piano-roll grid**:
  each vocabulary section is a column (name on top), 12 rows per octave
  with C at the bottom → B at the top, black-key shading + a note-letter
  gutter, so it lines up with Ableton's piano roll. Columns: Spots &
  bars, Pixel zones (9 zones + Even/Odd/Thirds), Chases, Breathes, Wild,
  Multicolor (two octaves), and the palettes split low/high (Prim C5/C6,
  Sec C7). Every section starts on a C: Spots & Bars C-2 (blackout +
  spots + bars), zones C-1, Chases C0, Breathes C1, Wild C2, Multicolor
  C3–C4, Primary C5–B6, Secondary C7. The visualiser puts the two spots
  either side of the bar grid (no labels); a pink *Flamingo Hitmix
  Lightshow* title sits above it; a narrow far-right pane holds the
  click-velocity slider. Dropped recipes: sweep L/R + bar chase, kick,
  tide.
- **Extended dynamics + Multicolor bank** — grid-aware chases (diag
  up/dn), textures (waves, expand, contract, rain, ripple, halo), slow
  ambient breathes (moon rise, soft ball, aurora) and the self-coloured
  `DynamicColorFn` Multicolor recipes (rainbow, comet, VU meter, fire,
  desert). When a Multicolor recipe is held its colour replaces the
  palette route; structural masks and brightness dynamics still
  gate/modulate on top ("rainbow + snake" = rainbow-coloured snake).
  Closes old TODO #1–#4.
- **Pixel-density diagonal-bias fix** — the linear position hash dropped
  pixels in diagonal stripes (per-bar lit counts 9/7/4/1 at 25%);
  replaced with a murmur3-finalizer avalanche hash rank-normalised per
  bar: random drop order, exact per-bar proportionality, still
  flicker-free and monotonic.
- Removed the dead `isPrimaryColorPitch` / `isSecondaryColorPitch`
  helpers in `Composition.cpp` (old TODO #6).
- **Trigger-menu dynamics regrouped** — Chases / Breathes / Wild, now
  including the new recipes, plus the live **Multicolor** group.
- Simplified BARS vocabulary (All + Bar 1–4) and PIXEL-ZONE vocabulary
  (9 single zones); freed pitches 9–11 and 21–23.
- ENTTEC USB Pro hardware smoke test — confirmed real DMX out with the
  228-channel patch.
- **Strobe** is a frame-synced global shutter in the DMX driver
  (jitter-free, decoupled from the audio block rate), mirrored on screen.
- **Velocity-controllable chase tails** — `DynamicFn` carries a `tail` arg;
  brightness dynamics don't route colour — they modulate brightness only.
