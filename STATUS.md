# HitNoteDmx — status & open issues

Living development log. Updated alongside non-trivial commits so any
future Claude session (or a future you) can pick up without
re-archaeology.

Recent arc: 18 px/bar rig bump → master-dim params + 3-pane editor
redesign → spots-above-grid visualiser → velocity-driven colour fade →
utility-button relocation → vocabulary simplification (bars + pixel zones)
+ 4-column trigger menu → real-hardware DMX smoke test passed → frame-synced
strobe shutter → velocity-controllable chase tails → trigger-menu dynamics
regrouped + experimental pixel-density control. A Standalone build ships
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
| Palette | `Palette.h` | 24-colour table, indexed by `pitch - paletteStart`. |
| Held-note tracker | `MidiState.{h,cpp}` | 128-slot array indexed by pitch. O(1) noteOn/noteOff/clear, no allocations. |
| Dynamic recipes | `Recipes.{h,cpp}` | 12-slot vocabulary. 11 per-pixel recipes (chase_up/down, ping_pong, snake, sine_wave, sparkle, breathe, sweep_up/down, kick_pulse, alt_swap) via function-pointer dispatch; the strobe slot (pitch 33) is null here — it's a driver-level shutter (see below). `DynamicFn` carries a `tail` arg (0..1); the moving-head chases render a velocity-driven comet trail via `cometBrightness`. |
| Composition | `Composition.{h,cpp}` | Bit-mask lookup tables, mask intersection across utility / static / dynamic layers, primary/secondary palette routing, spot RGBW with warm-white tint. Plus master-dim scaling and velocity-driven linear colour fade (`ColorFadeState`, see below). **Colour routing comes from the bar/zone selectors (their velocity picks primary vs secondary); the dynamic layer modulates brightness only and does not route colour** — so a chase takes the selector's colour, or primary by default. Velocity on a dynamic note instead sets its tail length. |
| Master dims + density | `PluginProcessor.{h,cpp}`, `Composition.cpp` | Three automatable params — LED Master Dim + Spot Master Dim + **Pixel Density** (0..1, shown as %). Dims scale RGB / spot dimmer; density is an experimental dark-room thinning control: a stable per-pixel position hash (`pixelDensityHash`) blanks a fixed subset of bar pixels below 100%, so fewer LEDs light without flicker and the survivors keep full brightness (gates on/off, never dims). All three applied inside `computeDmx` so the on-screen preview reflects them; MIDI-mappable in the host. |
| Colour fade | `Composition.{h,cpp}` | Each palette's displayed colour ramps linearly toward the winning note. Fade duration from the note's velocity (hard = instant, soft = up to 3 s) → a soft "black" palette note is a slow fade-to-black. State persists across blocks; advanced by wall-clock dt so it runs even when transport is stopped. |
| Audio-thread → GUI MIDI log | `MidiLog.{h,cpp}` | Lock-free SPSC ring (256 entries). |
| processBlock wiring | `PluginProcessor.cpp` | Pulls PPQ + BPM from `AudioPlayHead`, stamps MIDI events to beat time, runs `computeDmx()` once per block (with master-dim values + colour-fade state + block dt), pushes 228 channels via `dmx.setChannel()`. When the host transport isn't playing it advances a free-running beat clock so recipes still animate (standalone / transport-stopped preview). |
| Editor | `PluginEditor.{h,cpp}` | Three-pane layout — left: master-dim knobs (custom rotary look) + MIDI log + Connect/Disconnect/Blackout + ENTTEC status; middle: visualiser; right: the clickable trigger menu. 15 Hz GUI timer drains the MIDI log and mirrors the driver's strobe state onto the visualiser. Also builds as a Standalone app for DAW-free testing. |
| Trigger menu | `TriggerMenu.{h,cpp}` | 4-column, non-scrolling reference of the whole vocabulary; each cell shows its label plus the MIDI note name in grey on the same line. Cells toggle and combine; the latched set is injected into `MidiState` via lock-free atomics (`setPreviewPitches`/`applyPreview`) and previewed live. Dynamics are grouped by feel — **Chases / Breathes / Wild** — with a **Multicolor** slot reserved for the future self-coloured recipes. |
| On-screen DMX preview | `DmxVisualizer.{h,cpp}` | 2 spot circles on top, then 4 vertical bars × 18 cells. Pre-rendered into a single `juce::Image`, blitted by `paint()`. Re-rasterised only when the rig footprint's 8-bit fingerprint changes. Owns a dedicated strobe-flash timer (runs only while the strobe note is held) that blanks the rig to the panel background on alternate ticks at the strobe rate — preview only, not phase-locked to the real output. |
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

6. **Recipe vocabulary is the starter set.** 12 dynamics is enough to
   exercise the plumbing but a real 30-minute show wants more — rain
   variants, ripples, wave-trains, halos, expand/contract, edge-only,
   etc. The plan is LLM-design-then-encode: use Claude to draft
   per-pixel functions, port the good ones to C++. Moving-head additions
   can reuse the `tail` arg + `cometBrightness` for velocity trails.

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
- **Expand the recipe library** as gaps appear — LLM-drafted, ported to C++
  alongside the existing set. New moving-head recipes reuse the `tail` arg.
- **Judge / tune the pixel-density spike** — a master DENSITY knob now thins
  the bar LEDs via a stable position hash (preview-visible). If kept,
  follow-ups are tuning the scatter (hash vs every-Nth) and the default.

**Housekeeping / post-show**
- **Confirm long-session GUI stability** under sustained chases + strobe;
  optional OpenGL context only if software-compositor cost ever returns.
- Remove the dead `isPrimaryColorPitch` / `isSecondaryColorPitch` helpers.

*Done:* trigger menu + live preview, free-running preview clock, colour
fade, master dims, 3-pane redesign, spots above the grid, 18 px/bar rig,
vocabulary simplification (bars + pixel zones), 4-column menu, real-hardware
DMX smoke test, frame-synced strobe shutter, velocity-controllable chase
tails (with dynamics decoupled from colour routing), trigger-menu dynamics
regrouped (Chases / Breathes / Wild), experimental pixel-density control.

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
