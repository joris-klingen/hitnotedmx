# HitNoteDmx — status & open issues

Living development log. Updated alongside non-trivial commits so any
future Claude session (or a future you) can pick up without
re-archaeology.

Latest commit referenced: `3e98d70` (utility buttons → bottom-left).
Recent arc: 18 px/bar rig bump → master-dim params + 3-pane editor
redesign → spots-above-grid visualiser → velocity-driven colour fade →
utility-button relocation. A Standalone build now ships alongside VST3.

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
rig output. End-to-end pipeline verified visually with Live's keyboard
input (real ENTTEC hardware not yet tested in this round, see open
issue #4).

| Layer | File(s) | State |
|-------|---------|-------|
| VST plumbing | `PluginProcessor.{h,cpp}`, `CMakeLists.txt` | Standard audio-effect shape (stereo I/O + MIDI input). `IS_MIDI_EFFECT=TRUE` did not load in Live, so we use the audio-effect shape. |
| Rig | `Rig.h` | `constexpr` 4-bar × 18-pixel + 2-spot layout (228 channels: bars DMX 1–216, spots 217/223). |
| Palette | `Palette.h` | 24-colour table, indexed by `pitch - paletteStart`. |
| Held-note tracker | `MidiState.{h,cpp}` | 128-slot array indexed by pitch. O(1) noteOn/noteOff/clear, no allocations. |
| Dynamic recipes | `Recipes.{h,cpp}` | 12 recipes (chase_up/down, ping_pong, snake, sine_wave, sparkle, breathe, sweep_up/down, strobe, kick_pulse, alt_swap). Function-pointer dispatch from pitch. |
| Composition | `Composition.{h,cpp}` | Bit-mask lookup tables, mask intersection across utility / static / dynamic layers, primary/secondary palette routing, spot RGBW with warm-white tint. Plus master-dim scaling and velocity-driven linear colour fade (`ColorFadeState`, see below). |
| Master dims | `PluginProcessor.{h,cpp}` | Two automatable params — LED Master Dim + Spot Master Dim (0..1, shown as %). Applied inside `computeDmx` so output and on-screen preview both reflect them; MIDI-mappable in the host. |
| Colour fade | `Composition.{h,cpp}` | Each palette's displayed colour ramps linearly toward the winning note. Fade duration from the note's velocity (hard = instant, soft = up to 3 s) → a soft "black" palette note is a slow fade-to-black. State persists across blocks; advanced by wall-clock dt so it runs even when transport is stopped. |
| Audio-thread → GUI MIDI log | `MidiLog.{h,cpp}` | Lock-free SPSC ring (256 entries). |
| processBlock wiring | `PluginProcessor.cpp` | Pulls PPQ + BPM from `AudioPlayHead`, stamps MIDI events to beat time, runs `computeDmx()` once per block (with master-dim values + colour-fade state + block dt), pushes 228 channels via `dmx.setChannel()`. When the host transport isn't playing it advances a free-running beat clock so recipes still animate (standalone / transport-stopped preview). |
| Editor | `PluginEditor.{h,cpp}` | Three-pane layout — left: master-dim knobs (custom rotary look) + MIDI log + Connect/Disconnect/Blackout + ENTTEC status; middle: visualiser; right: the clickable trigger menu. Also builds as a Standalone app for DAW-free testing. |
| Trigger menu | `TriggerMenu.{h,cpp}` | Multi-column, non-scrolling reference of the whole vocabulary, each cell labelled with its MIDI note name. Cells toggle and combine; the latched set is injected into `MidiState` via lock-free atomics (`setPreviewPitches`/`applyPreview`) and previewed live. |
| On-screen DMX preview | `DmxVisualizer.{h,cpp}` | 2 spot circles on top, then 4 vertical bars × 18 cells. Pre-rendered into a single `juce::Image`, blitted by `paint()`. Re-rasterised only when the rig footprint's 8-bit fingerprint changes. |
| ENTTEC driver | `EnttecProDmx.{h,cpp}` | Self-contained ENTTEC DMX USB Pro driver (IOKit discovery + POSIX termios I/O). Best-effort widget handshake, raised read timeout, 700-byte RX. |

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
   the window is taller — re-confirm cost under sustained chases.

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

4. **No real-hardware ENTTEC confirmation yet** in this development
   round. Visualiser matches the recipe semantics; the channels are
   being pushed to the driver but the USB widget hasn't been re-tested
   end-to-end with actual lights since the recipe layer landed. First
   item on the list before composing.

5. **Recipe vocabulary is the starter set.** 12 dynamics is enough to
   exercise the plumbing but a real 30-minute show wants more — rain
   variants, ripples, wave-trains, halos, expand/contract, edge-only,
   etc. The plan is LLM-design-then-encode: use Claude to draft
   per-pixel functions, port the good ones to C++ alongside the
   existing 12.

6. **Transport-stopped animation (resolved).** When the host transport
   isn't playing, `processBlock` advances a free-running beat clock
   (continuous from the last host PPQ) so chases/breathes keep animating
   in the standalone and while stopped. When the transport plays again we
   snap back to host PPQ. Note this is a deliberate divergence from
   "freeze when paused" — recipes now always animate unless truly idle.

## Direction

Tracked in the session task list; copied here for durability.

**Features in flight**
- **Simplify the BARS + PIXEL-ZONE vocabularies** (tasks #13/#14) — keep
  "all" + per-bar and per-zone single notes; drop the combo notes (easy
  to play as multiple notes). This is now the project's own vocabulary —
  no external parity to preserve.
- **Trigger menu: 4 columns + inline grey note name** (task #15).
- **Velocity-controllable chase tails** (task #5) — needs velocity
  threaded into the `DynamicFn` signature (recipes currently get none).
- **Pixel-density reduction** (task #3, experimental) — a master control
  that thins active LEDs while keeping full-brightness flashes; spike
  first to judge whether it looks good.

**Show prep**
1. **Confirm real DMX out** through the ENTTEC USB Pro with the rig lit,
   incl. the new 228-channel patch (task #6). Smoke test before composing.
2. **Compose the first songs** in Live — MIDI clips routed to
   `HitNoteDmx`, lights respond live, no render step.
3. **Expand the recipe library** (task #7) as gaps appear — LLM-drafted,
   ported to C++ alongside the existing 12.

**Housekeeping / post-show**
- **Confirm long-session GUI stability** (task #10); optional OpenGL
  context only if software-compositor cost ever returns.

*Done:* trigger menu + live preview, free-running preview clock, colour
fade, master dims, 3-pane redesign, spots above the grid, 18 px/bar rig.

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
