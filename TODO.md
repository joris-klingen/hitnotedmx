# HitNoteDmx — open tasks

Open backlog, renumbered. Detailed context for shipped work and
architecture lives in [STATUS.md](STATUS.md).

## Show prep

1. **Ship with ~100 demo MIDI clips** — a bundled library of clips that
   showcase the vocabulary so a new user has ready-made examples to drop on
   a `HitNoteDmx` track: one clip per recipe (each chase/breathe/wild/
   multicolor), palette-routing demos (primary vs soft-velocity secondary),
   structural combos (bars × zones × chases), spot moves, strobe, and a few
   longer "song section" arrangements that layer several at once. Decide the
   format (`.mid` files vs an Ableton `.als`/rack) and where they live in the
   repo; document how to load them in the README.

2. **Drag a MIDI clip from the plugin into the DAW** — once a selection is
   built by clicking tiles in the trigger menu, let the user drag that latched
   set out of the plugin as a MIDI clip (the active pitches as notes) and drop
   it onto a track. Turns the menu into a quick clip builder and pairs with the
   demo-clip library (#1). JUCE `performExternalDragDropOfFiles` on a generated
   temp `.mid` is the likely path.

3. **Visually tune the recipe banks** — all four feel-group octaves are now
   full (48 recipes), implemented against numeric checks and ASCII-frame
   renders, not yet judged on real hardware. The newest fills (Wild:
   lightning/glitch/bounce/zigzag/…; Multicolor: night sky, skyline, embers,
   plasma, ocean, nebula, smooth VU) especially want an eyeball. Speeds,
   band widths, gaussian radii and hue ramps are all single-constant tweaks
   in `Recipes.cpp`.

4. **Pixel-density: re-roll, taste check, and C8 note controls** — the density
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

5. **Re-author pixel zones natively for 18 pixels** — the 9 "zones" are a
   fossil of the old 9-pixel rig: each is authored as one zone then stretched
   2× to the physical pixels (`zone()` in `Composition.cpp`), while Even/Odd/
   Thirds are authored natively for 18. Two mental models for grouping pixels
   in the same column. Low priority (works fine) — re-author the zones as
   native 18-pixel groups for consistency when convenient.

## Recently shipped (see STATUS.md for detail)

- **Velocity semantics documented** — a single table atop `Composition.cpp`
  maps the four meanings of velocity (route / tail / speed / intensity+fade).
- **Live-MIDI tile highlight** — trigger-menu tiles light up while their note
  is sounding (held in `MidiState`, MIDI or preview), via a 15 Hz snapshot
  the editor pushes to `TriggerMenu::setLiveNotes`. The menu doubles as a
  live activity display.
- **Full matrix** — every dynamics octave is now a complete chromatic set of
  12: Chases (+Snake H), Breathes (+Ripple H, Bloom, Shimmer, Sway, Drift),
  Wild (+Lightning, Stutter, Static, Glitch, Bounce, Strobe R, Beat,
  Zigzag), Multicolor (+VU smooth, Night sky, Skyline, Embers, Plasma,
  Ocean, Nebula). Pixel zones gained a **Thirds** comb (pixels 1,4,7,…).
  Blackout reintroduced over MIDI at C-2. All 48 recipes verified in range +
  alive + colourful by unit test.
- **Piano-roll editor redesign + octave-aligned MIDI remap** — window is
  now a flat 1160×360. The right pane is a **transposed piano-roll grid**:
  each vocabulary section is a column (name on top), 12 rows per octave
  with C at the bottom → B at the top, black-key shading + a note-letter
  gutter, so it lines up with Ableton's piano roll. Ten columns: Spots &
  bars, Pixel zones (9 zones + Even/Odd), Chases, Breathes, Wild,
  Multicolor, and the palettes split low/high (Prim C4/C5, Sec C6/C7).
  Every section starts on a C: Spots & Bars C-2 (all-bars dropped),
  zones C-1, Chases C0, Breathes C1, Wild C2, Multicolor C3, Primary
  C4–B5, Secondary C6–B7. The visualiser now puts the two spots either
  side of the bar grid (no labels); a pink *Flamingo Hitmix Lightshow*
  title sits above it. Blackout trigger note removed (Controls button +
  master dims cover it). Dropped recipes: sweep L/R + bar chase, kick,
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
