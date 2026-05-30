# HitNoteDmx — status & open issues

Living development log. Updated alongside non-trivial commits so any
future Claude session (or a future you) can pick up without
re-archaeology.

Latest commit referenced: `a1a724a` (MIDI-log batching) plus the
follow-up to trim the log to 10 lines.

## What this repo is

Macros VST3 plugin built on JUCE 8 that takes **MIDI in** and produces
**DMX out** through an ENTTEC USB Pro. Sibling to
[`../hitdmx`](../hitdmx) — that one's the bare 512-parameter DMX VST
the host automates directly. This one carries the recipe layer:
chases, breathes, sparkles, palette routing, mask intersection, the
whole MIDI-note vocabulary defined by `../hitmixmididmx`'s offline
translator.

The relationship between the three artifacts:

| | Repo | Plugin | Role |
|---|---|---|---|
| 1 | `hitdmx`       | `HitDmx`      | Bare 512-channel parameter VST (no MIDI). |
| 2 | `hitmixmididmx`| —             | Python `lightmidi` CLI that converts MIDI clips to DMX automation in an `.als`. |
| 3 | `hitnotedmx`   | `HitNoteDmx`  | MIDI VST that runs the recipe vocabulary live. Same driver as HitDmx. |

`hitnotedmx` (#3) is the "show engine"; for a 30-minute song-based
light show we draw MIDI clips into Ableton, route them at `HitNoteDmx`,
and lights respond in real time without a render step.

## What's built and working

Plugin loads in Live 11/12, accepts MIDI, computes per-channel DMX
state, writes to the ENTTEC driver, paints an on-screen preview of the
rig output. End-to-end pipeline verified visually with Live's keyboard
input (real ENTTEC hardware not yet tested in this round, see open
issue #4).

| Layer | File(s) | State |
|-------|---------|-------|
| VST plumbing | `PluginProcessor.{h,cpp}`, `CMakeLists.txt` | Standard audio-effect shape (stereo I/O + MIDI input). `IS_MIDI_EFFECT=TRUE` did not load in Live; the working shape mirrors HitDmx. |
| Rig | `Rig.h` | `constexpr` 4-bar × 9-pixel + 2-spot layout (120 channels). Matches `HITMIX_EXTENDED_RIG` in `lightmidi/fixtures_extended.py`. |
| Palette | `Palette.h` | Verbatim 24-color table from `midi_to_dmx.py:PALETTE`. |
| Held-note tracker | `MidiState.{h,cpp}` | 128-slot array indexed by pitch. O(1) noteOn/noteOff/clear, no allocations. |
| Dynamic recipes | `Recipes.{h,cpp}` | All 12 ports (chase_up/down, ping_pong, snake, sine_wave, sparkle, breathe, sweep_up/down, strobe, kick_pulse, alt_swap). Function-pointer dispatch from pitch. |
| Composition | `Composition.{h,cpp}` | Port of `_compute_state`. Bit-mask lookup tables, mask intersection across utility / static / dynamic layers, primary/secondary palette routing, spot RGBW with warm-white tint. |
| Audio-thread → GUI MIDI log | `MidiLog.{h,cpp}` | Lock-free SPSC ring (256 entries). |
| processBlock wiring | `PluginProcessor.cpp` | Pulls PPQ + BPM from `AudioPlayHead`, stamps MIDI events to beat time, runs `computeDmx()` once per block, pushes 120 channels via `dmx.setChannel()`. Falls back to PPQ=0 / BPM=120 when transport isn't running (static layers still work; periodic recipes freeze). |
| Editor | `PluginEditor.{h,cpp}` | Connect / Disconnect / Blackout buttons + ENTTEC status + a compact MIDI log + the visualiser. |
| On-screen DMX preview | `DmxVisualizer.{h,cpp}` | 4 vertical bars × 9 cells + 2 spot circles. Pre-rendered into a single `juce::Image`, blitted by `paint()`. Re-rasterised only when the rig footprint's 8-bit fingerprint changes. |
| ENTTEC driver | `EnttecProDmx.{h,cpp}` | Mirror copy from `hitdmx`, namespace renamed to `hitnotedmx`. Best-effort widget handshake, raised read timeout, 700-byte RX. |

## How it runs end-to-end

```
                ┌─────────────── audio thread ────────────────┐
host MIDI in ─► processBlock ─► MidiState.noteOn/Off
                   │
                   ├─► MidiLog.push  (lock-free) ───────┐
                   │                                    │
                   └─► computeDmx(state, t, dmxValues)  │
                            │                           │
                            └─► dmx.setChannel × 120 ──►│ DMX out (USB)
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

2. **Colour crossfade deferred.** The Python translator linearly
   crossfades the two most-recent overlapping colour notes across
   `min(a.end, b.end)`. The live VST doesn't know future end times so
   v1 just picks the most-recently-started note. Configurable
   fade-in window (`currentBeat − startBeat` clamped to a knob) is the
   natural extension; flagged in `Composition.cpp`.

3. **Driver duplication.** `Source/EnttecProDmx.{h,cpp}` is a verbatim
   copy from `hitdmx`, namespace-renamed. The connect-robustness fix
   (`commit 1f9b783` here / `58b080f` in hitdmx) had to be applied
   twice. Future cleanup: pull the driver into a shared static-library
   repo that both `hitdmx` and `hitnotedmx` `FetchContent`.

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

6. **Live's "transport stopped" UX is sharp.** `processBlock` falls
   back to PPQ=0 / BPM=120 when no `AudioPlayHead::PositionInfo` is
   available. Periodic recipes therefore freeze at t=0 when transport
   is stopped while static layers (bar selectors, pixel statics, spot
   triggers, palette colour) keep responding. That matches how
   `lightmidi`'s rendered automation behaves when paused, so it's
   probably the right behaviour — but worth flagging so it isn't
   read as a bug.

## Direction

1. **Confirm real DMX out** through the ENTTEC USB Pro with the rig
   actually lit. Smoke test before composing anything.
2. **Compose the first songs of the show** in Live with the iteration
   loop tight: MIDI clips on a track routed to `HitNoteDmx`, lights
   respond live, no render step. Vocabulary gaps surface naturally as
   we go.
3. **Expand the recipe library** as those gaps appear — LLM-drafted,
   ported to C++ alongside the existing 12.
4. **Re-add colour crossfade** once it actually matters for a cue
   (deferred from v1).
5. **DRY the ENTTEC driver** into a shared static library after the
   show ships; tolerable mirror policy until then.
6. **Optional** OpenGL context attached to the editor — only if the
   software compositor cost ever comes back. Not on the critical
   path.

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
