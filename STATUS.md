# HitNoteDmx — status & open issues

Living development log. Updated alongside non-trivial commits so any
future Claude session (or a future you) can pick up without
re-archaeology.

Recent arc: 18 px/bar rig bump → master-dim params + 3-pane editor
redesign → spots-above-grid visualiser → velocity-driven colour fade →
utility-button relocation → vocabulary simplification (bars + pixel zones)
+ 4-column trigger menu → real-hardware DMX smoke test passed → frame-synced
strobe shutter → velocity-controllable chase tails → trigger-menu dynamics
regrouped + experimental pixel-density control → density diagonal-bias fix
(per-bar rank hash) + extended recipe bank + self-coloured Multicolor bank
(new `DynamicColorFn` routing) → flat-and-wide editor redesign + octave-
aligned MIDI remap (every section starts on a C; recipes grouped by feel
into one-octave banks; palettes relocated to C4/C6; blackout trigger note
dropped) → **transposed piano-roll trigger grid + DmxVisualizer with spots
flanking the bars + pink title strip** (1440×360). A Standalone build ships
alongside VST3.

## What this repo is

A VST3 plugin (JUCE 8) that takes **MIDI in** and produces **DMX out**
through an ENTTEC USB Pro. It carries the full recipe layer: chases,
breathes, sparkles, palette routing, mask intersection, and the whole
MIDI-note vocabulary — all run live on the audio thread.

It's the "show engine": for a song-based light show we draw MIDI clips
into the DAW, route them at `HitNoteDmx`, and the lights respond in real
time without a render step. The project is self-contained — its note
vocabulary, palette, rig and ENTTEC driver all live here.

## What's built and working

Plugin loads in Live 11/12, accepts MIDI, computes per-channel DMX
state, writes to the ENTTEC driver, paints an on-screen preview of the
rig output. End-to-end pipeline verified both on-screen and against real
ENTTEC USB Pro hardware with the 228-channel rig lit (smoke test passed).

| Layer | File(s) | State |
|-------|---------|-------|
| VST plumbing | `PluginProcessor.{h,cpp}`, `CMakeLists.txt` | Standard audio-effect shape (stereo I/O + MIDI input). `IS_MIDI_EFFECT=TRUE` did not load in Live, so we use the audio-effect shape. |
| Rig | `Rig.h` | `constexpr` 4-bar × 18-pixel + 2-spot layout (228 channels: bars DMX 1–216, spots 217/223). |
| Palette | `Palette.h` | 24-colour table, indexed by `pitch - paletteStart`. Each palette spans two octaves starting on a C: primary 72–95 (C4–B5), secondary 96–119 (C6–B7). No blackout note — `kSecondaryPaletteEnd` (120) bounds the range. |
| Held-note tracker | `MidiState.{h,cpp}` | 128-slot array indexed by pitch. O(1) noteOn/noteOff/clear, no allocations. |
| Dynamic recipes | `Recipes.{h,cpp}` | Four feel-groups, each one MIDI octave starting on a C, all function-pointer dispatch (one table per group). **Chases C0 (24–34):** chase_up/down, ping_pong, snake, sweep_up/down, diag_up/down, wave_train, expand, contract. **Breathes C1 (36–42):** sine_wave, breathe, ripple, halo, moon_rise, soft_ball, aurora. **Wild C2 (48–51):** sparkle, strobe (null slot — driver-level shutter), alt_swap, rain. **Multicolor C3 (60–64):** self-coloured `DynamicColorFn` recipes returning RGB directly (rainbow_chase, comet, vu_meter, fire, desert_breathe). `DynamicFn` carries a `tail` arg (0..1); moving-head chases render a velocity-driven comet trail via `cometBrightness` (integer tracks) / `cometBrightnessF` (continuous tracks, e.g. diagonals). Dropped in the remap: sweep_left/right + bar_chase (use the Bar selectors), kick_pulse, tide. |
| Composition | `Composition.{h,cpp}` | Bit-mask lookup tables, mask intersection across utility / static / dynamic layers, primary/secondary palette routing, spot RGBW with warm-white tint. Plus master-dim scaling and velocity-driven linear colour fade (`ColorFadeState`, see below). **Colour routing comes from the bar/zone selectors (their velocity picks primary vs secondary); brightness dynamics modulate brightness only and do not route colour** — so a chase takes the selector's colour, or primary by default. Velocity on a dynamic note instead sets its tail length. **Self-coloured recipes (100–104) are the exception:** when held, their RGB replaces the palette route for the lit pixels (multiple combine per-channel max); structural masks and brightness dynamics still gate/modulate on top, and the colour-fade stage doesn't apply to them. |
| Master dims + density | `PluginProcessor.{h,cpp}`, `Composition.cpp` | Three automatable params — LED Master Dim + Spot Master Dim + **Pixel Density** (0..1, shown as %). Dims scale RGB / spot dimmer; density is the dark-room thinning control: below 100% it blanks a fixed subset of bar pixels so fewer LEDs light without flicker and the survivors keep full brightness (gates on/off, never dims). The gate order comes from `kDensityRank` — an avalanche hash (murmur3 finalizer) of grid position, rank-normalised within each bar, so the drop order is random but every bar keeps the same lit fraction (the original single-multiply linear hash dropped pixels in diagonal stripes). All three applied inside `computeDmx` so the on-screen preview reflects them; MIDI-mappable in the host. |
| Colour fade | `Composition.{h,cpp}` | Each palette's displayed colour ramps linearly toward the winning note. Fade duration from the note's velocity (hard = instant, soft = up to 3 s) → a soft "black" palette note is a slow fade-to-black. State persists across blocks; advanced by wall-clock dt so it runs even when transport is stopped. |
| Audio-thread → GUI MIDI log | `MidiLog.{h,cpp}` | Lock-free SPSC ring (256 entries). |
| processBlock wiring | `PluginProcessor.cpp` | Pulls PPQ + BPM from `AudioPlayHead`, stamps MIDI events to beat time, runs `computeDmx()` once per block (with master-dim values + colour-fade state + block dt), pushes 228 channels via `dmx.setChannel()`. When the host transport isn't playing it advances a free-running beat clock so recipes still animate (standalone / transport-stopped preview). |
| Editor | `PluginEditor.{h,cpp}` | Flat-and-wide three-pane layout (1440×360) — left (240px): master-dim knobs (custom rotary look) + a compact MIDI log + Connect/Disconnect/Blackout + one-line ENTTEC status; middle: the *Flamingo Hitmix Lightshow* title strip (soft pink) above the visualiser; right (720px): the piano-roll trigger grid (no card title). Panes carry no header text — the cards are bare panels. 15 Hz GUI timer drains the MIDI log and mirrors the driver's strobe state onto the visualiser. Also builds as a Standalone app for DAW-free testing. |
| Trigger menu | `TriggerMenu.{h,cpp}` | **Transposed piano-roll grid** — each vocabulary section is a COLUMN (its name is the header); the 12 rows are the chromatic notes of that section's octave, **C at the bottom → B at the top**, with black-key rows shaded and a note-letter gutter on the left, so the menu lines up vertically with Ableton's piano roll. Ten columns: Spots & bars (one octave: spots 0–3, bars 4–7), Pixel zones (9 zones + Even/Odd), Chases, Breathes, Wild, Multicolor, then the palettes split low/high (Prim C4, Prim C5, Sec C6, Sec C7) so each is a full 12-row octave of swatches. Notes with no trigger render as empty slots. Cells toggle and combine; the latched set is injected into `MidiState` via lock-free atomics (`setPreviewPitches`/`applyPreview`) and previewed live. Latching a Multicolor cell counts as having a colour (no default white injected). No blackout cell — the Controls Blackout button + master-dim knobs cover that. |
| On-screen DMX preview | `DmxVisualizer.{h,cpp}` | A spot circle flanks each side of the 4 vertical bars × 18 cells (spot_l left, spot_r right, top-aligned with the bars); cells are ~2:1 and unlabelled. Pre-rendered into a single `juce::Image`, blitted by `paint()`. Re-rasterised only when the rig footprint's 8-bit fingerprint changes. Owns a dedicated strobe-flash timer (runs only while the strobe note is held) that blanks the rig to the panel background on alternate ticks at the strobe rate — preview only, not phase-locked to the real output. |
| ENTTEC driver | `EnttecProDmx.{h,cpp}` | Self-contained ENTTEC DMX USB Pro driver (IOKit discovery + POSIX termios I/O). Best-effort widget handshake, raised read timeout, 700-byte RX. Send loop runs on a `juce::HighResolutionTimer` (its own thread, off the message thread) at 40 Hz for steady frame emission. Hosts the **strobe shutter**: `setStrobeHz()` gates whole frames lit/black on the emitted-frame counter, so the strobe is exactly synced to the DMX output clock and free of audio-block jitter (10 Hz = 2-on/2-off; 20 Hz max). |

## How it runs end-to-end

```
                ┌─────────────── audio thread ────────────────┐
host MIDI in ─► processBlock ─► MidiState.noteOn/Off
                   │
                   ├─► MidiLog.push  (lock-free) ───────┐
                   │                                    │
                   └─► computeDmx(state, t, dmxValues, │
                          dims, fade, dt)               │
                            │                           │
                            └─► dmx.setChannel × 228 ──►│ DMX out (USB)
                                                        │
                ┌────────────── GUI thread (15 Hz) ─────┴────┐
                │ timer:                                      │
                │   drain MidiLog into logBuffer (no relayout)│
                │   flushLogIfDirty()  → one setText / tick   │
                │   DmxVisualizer.repaintIfChanged()          │
                │     ↳ fingerprint diff → rebuildCache       │
                │     ↳ repaint() → paint() blits the Image   │
                └─────────────────────────────────────────────┘
```

## Open issues / known rough edges

1. **GPU / compositor cost was glitchy under sustained chases.** Series
   of fixes shipped to address it, in order: dropped per-cell borders;
   `setOpaque(true)`; visualiser fingerprint diff so repaint only
   fires when state actually changes; image cache so `paint()` is one
   blit; image reuse so per-change cost is zero allocations; **batched
   log appends so the TextEditor is `setText()`'d at most once per
   timer tick** (the per-event relayout was the dominant cost as the
   log grew). Latest commit also clamps the log to 10 lines so the
   TextEditor's text never grows past trivial length. Should now be
   stable across long sessions; needs confirmation in extended play.
   Note the preview now draws **72 cells** (4 × 18) instead of 36 and
   the window is taller — re-confirm cost under sustained chases. The
   strobe-flash timer also repaints the visualiser at up to 40 Hz while
   the strobe note is held; each tick is a single blit, but confirm the
   combined cost over a long session.

2. **Colour fade — shipped (was deferred).** Rather than the offline
   `min(a.end, b.end)` crossfade (needs future end-times), the live
   model ramps each palette's displayed colour linearly toward the
   winner. Fade duration is derived from the triggering note's velocity
   (hard = instant, soft = up to `kMaxColorFadeSec` = 3 s), so a soft
   black palette note gives a slow fade-to-black. Implemented in
   `ColorFadeState` / `advanceFade` in `Composition.cpp`. Open follow-up:
   tune the 3 s ceiling and the linear-vs-curved velocity mapping once
   play-tested with a keyboard.

3. **Driver is self-contained (resolved).** `Source/EnttecProDmx.{h,cpp}`
   is owned wholly by this project — no shared-source coupling to keep in
   sync. Fixes land here and here only.

4. **Real-hardware ENTTEC confirmation — done.** Smoke-tested end-to-end
   against the ENTTEC USB Pro with the full 228-channel rig lit; output
   matches the on-screen preview. The send loop now runs on a
   `HighResolutionTimer` at 40 Hz.

5. **Strobe is a driver-level shutter (new).** Rather than a per-pixel
   recipe sampled at the audio block rate (which beat against the 40 Hz
   send and jittered), the strobe gates whole frames lit/black on the
   driver's emitted-frame counter — exactly synced to output, perfectly
   even duty, decoupled from audio. The processor only publishes "strobe
   held" (`setStrobeHz`); the menu still lists it in the dynamics block.
   `kStrobeHz` (Recipes.h) defaults to 10 Hz; 20 Hz is the hardware max.
   The on-screen flash is a separate GUI timer and is not phase-locked.

6. **Recipe vocabulary expanded + octave-grouped (was: starter set).**
   26 recipes across four feel-groups, each one MIDI octave starting on a
   C: Chases (C0), Breathes (C1), Wild (C2), Multicolor (C3). Multicolor
   recipes are self-coloured (`DynamicColorFn` returns RGB and overrides
   the palette route). Verified numerically (range, liveness, motion
   direction via ASCII frame renders) and dispatch-mapped by unit test,
   but the new recipes aren't yet judged on hardware — tuning constants
   live at the top of each recipe in `Recipes.cpp`. Five starter recipes
   were dropped in the remap (sweep_left/right + bar_chase, now done with
   Bar selectors; kick_pulse; tide).

7. **Transport-stopped animation (resolved).** When the host transport
   isn't playing, `processBlock` advances a free-running beat clock
   (continuous from the last host PPQ) so chases/breathes keep animating
   in the standalone and while stopped. When the transport plays again we
   snap back to host PPQ. Note this is a deliberate divergence from
   "freeze when paused" — recipes now always animate unless truly idle.

## Direction

Open backlog is tracked in [TODO.md](TODO.md); the strategic shape:

**Next up**
- **Compose the first songs** in Live — MIDI clips routed to `HitNoteDmx`,
  lights respond live, no render step. The vocabulary, hardware path, and
  strobe/chase-tail expressiveness are all in place for this now.
- **Visually tune the new recipe banks** on the preview and real hardware —
  speeds, band widths, hue ramps are single-constant tweaks in `Recipes.cpp`.
- **Density taste check** — the diagonal bias is fixed (per-bar rank hash);
  confirm the scatter on the physical bars and pick the default.

**Housekeeping / post-show**
- **Confirm long-session GUI stability** under sustained chases + strobe;
  optional OpenGL context only if software-compositor cost ever returns.

*Done:* trigger menu + live preview, free-running preview clock, colour
fade, master dims, 3-pane redesign, spots above the grid, 18 px/bar rig,
vocabulary simplification (bars + pixel zones), 4-column menu, real-hardware
DMX smoke test, frame-synced strobe shutter, velocity-controllable chase
tails (with dynamics decoupled from colour routing), trigger-menu dynamics
regrouped (Chases / Breathes / Wild), pixel-density control (diagonal bias
fixed via per-bar rank hash), extended recipe bank 85–99, Multicolor bank
100–104 with `DynamicColorFn` colour routing, dead colour-pitch helpers
removed.

## How to verify after a fresh checkout

```bash
cmake -S . -B build -G Xcode
cmake --build build --config Release
```

VST3 lands at `build/HitNoteDmx_artefacts/Release/VST3/HitNoteDmx.vst3`
and is also installed to `~/Library/Audio/Plug-Ins/VST3/HitNoteDmx.vst3`.

In Live: rescan VSTs (Preferences → Plug-Ins → "Rescan"), drop
`HitNoteDmx` on a MIDI-armed track, play any note. Editor's MIDI log
should show `[on ] ch1 C3  (60) vel=100` (note name + pitch number +
velocity); the on-screen rig preview should respond per the palette
table in the project README.
