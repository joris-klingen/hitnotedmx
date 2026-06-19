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
into one-octave banks; palettes relocated to C5/C7; blackout trigger note
dropped) → transposed piano-roll trigger grid + DmxVisualizer with spots
flanking the bars + pink title strip → **full 48-recipe matrix + per-bank
velocity semantics** (Chases→tail, Wild→beat-division, Breathes→density
islands, Multicolor→speed; VU meter beat-locked with velocity→gain) →
white-default for recipes held without a palette colour → **Showcase assets**
(embedded named Ableton rack + runtime demo clips, via Init-names / Show-clips
buttons) → recipe reorder + `.adg` name prefixes (cp/cs/bk/sp/ba/pz/ch/br/wd/mc)
→ **strobe rework** (moved to C2 root, white by default, single-frame flash,
velocity → 1–20 Hz repeat rate) → visualizer gamma lift + per-fixture `dNNN`
DMX-address captions + tighter editor → secondary accent palette + runtime
rack naming (Python tool dropped) → **mapping v1 frozen** (`mappings/v1.tsv`
+ `mapping-tool` + `mapping-frozen` CTest drift guard) → **master/global hits**
(bump-white, bump-colour, freeze) added on the top free notes (octave 8),
mapping re-frozen at **v2** (`mappings/v2.tsv`). Window is now
1168×338. A Standalone build ships alongside VST3 — **build both targets**
(`cmake --build build`) so the DAW plugin never goes stale.

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
| VST plumbing | `PluginProcessor.{h,cpp}`, `CMakeLists.txt` | **Instrument shape** (`IS_SYNTH=TRUE`, VST3 category `Instrument`): a single stereo audio **output** bus that emits silence, no audio input, MIDI input on. It loads on a MIDI track as the instrument and receives MIDI directly. The silent audio bus is what makes Live load it — the pure MIDI-effect shape (`IS_MIDI_EFFECT=TRUE`, no audio bus) did **not** load in Live, which is why we don't use it. Changing from the old audio-effect shape moved the plugin from Live's *Audio Effects* to *Instruments*, so sessions saved against the old shape must re-add it. |
| Rig | `Rig.h` | `constexpr` 4-bar × 18-pixel + 2-spot layout (228 channels: bars DMX 1–216, spots 217/223). |
| Palette | `Palette.h` | Two separate tables. Primary `kPalette` (24 colours) spans two octaves 84–107 (C5–B6); secondary `kSecondaryPalette` (12 softer complementary accents) is one octave 108–119 (C7). `paletteColorFor(start, offset)` picks the table; `kSecondaryPaletteEnd` (120) bounds the range. |
| Held-note tracker | `MidiState.{h,cpp}` | 128-slot array indexed by pitch. O(1) noteOn/noteOff/clear, no allocations. |
| Dynamic recipes | `Recipes.{h,cpp}` | Four feel-groups, each a full chromatic octave starting on a C, all function-pointer dispatch (one table per group), 48 recipes total. Each table is ordered logically and matches the menu columns 1:1. **Chases C0 (24–35):** chase up/down, ping-pong, diag up/down, snake, snake H, spiral, waves, expand, contract, pong. **Breathes C1 (36–47):** breathe, sine, ripple, ripple H, bloom, halo, moon rise, soft ball, drift, aurora, shimmer, sway. **Wild C2 (48–59):** strobe (root C, null slot — driver-level shutter), sparkle, sparkle few, lightning, glitch, static, rain, alt swap, bounce, fast ball, zigzag, converge. **Multicolor C3–C4 (60–83, two octaves, 24):** self-coloured `DynamicColorFn` recipes returning RGB directly — rainbow, comet, VU meter, VU smooth, fire, embers, magma, lava, heatmap, ocean, forest, desert (C3); sunset, twilight, borealis, night sky, galaxy, nebula, storm, plasma, police, disco, blocks, candy (C4). `DynamicFn` carries a `tail` arg (0..1); `DynamicColorFn` carries a `param` arg (VU gain). Moving-head chases render a velocity-driven comet trail via `cometBrightness` / `cometBrightnessF`. |
| Composition | `Composition.{h,cpp}` | Bit-mask lookup tables, mask intersection across utility / static / dynamic layers, primary/secondary palette routing, spot RGBW with warm-white tint. Plus master-dim scaling and velocity-driven linear colour fade (`ColorFadeState`, see below). **Colour routing comes from the bar/zone selectors (their velocity picks primary vs secondary); brightness dynamics modulate brightness only and do not route colour** — so a chase takes the selector's colour, or primary by default. **Per-bank velocity semantics** (doc table at the top of the file): Chases → comet tail, Wild → beat-division 1/16..1/1 (except sparkle/sparkle few, free-running speed), Breathes → density "islands" + half-speed (except ripple), Multicolor → animation speed (except VU meter, beat-locked with velocity → gain). **White default:** a bar/pixel/dynamic/strobe trigger held with NO palette colour renders full white (matching click-preview). **Self-coloured Multicolor recipes (60–83) are the exception to colour routing:** when held, their RGB replaces the palette route for the lit pixels (multiple combine per-channel max); structural masks and brightness dynamics still gate/modulate on top, and the colour-fade stage doesn't apply to them. |
| Master dims + density | `PluginProcessor.{h,cpp}`, `Composition.cpp` | Three automatable params — LED Master Dim + Spot Master Dim + **Pixel Density** (0..1, shown as %). Dims scale RGB / spot dimmer; density is the dark-room thinning control: below 100% it blanks a fixed subset of bar pixels so fewer LEDs light without flicker and the survivors keep full brightness (gates on/off, never dims). The gate order comes from `kDensityRank` — an avalanche hash (murmur3 finalizer) of grid position, rank-normalised within each bar, so the drop order is random but every bar keeps the same lit fraction (the original single-multiply linear hash dropped pixels in diagonal stripes). All three applied inside `computeDmx` so the on-screen preview reflects them; MIDI-mappable in the host. |
| Master hits | `Composition.cpp`, `TriggerVocabulary.cpp` | **Global "master" controls on the top free notes (octave 8), above the palette** — not per-fixture triggers; `computeDmx` handles them as whole-rig overrides. **Bump white (120)** / **Bump color (121)** override the whole composed frame with a velocity-level flash: white, or the current primary-palette hue (white if no colour held). They override (not HTP-tint) and ignore pixel density — a bump lights everything — but still obey the master dims. **Freeze (122)** returns *before* the per-block `out.clear()`, so the previous frame is held untouched while the note is down; blackout still dominates freeze, and the beat clock keeps running so releasing resumes in time with the song. Mapping re-frozen at **v2** (`mappings/v2.tsv`); the "Master" vocabulary column (prefix `ms`) names the menu tiles + rack chains. |
| Colour fade | `Composition.{h,cpp}` | Each palette's displayed colour ramps linearly toward the winning note. Fade duration from the note's velocity (hard = instant, soft = up to 3 s) → a soft "black" palette note is a slow fade-to-black. State persists across blocks; advanced by wall-clock dt so it runs even when transport is stopped. |
| Audio-thread → GUI MIDI log | `MidiLog.{h,cpp}` | Lock-free SPSC ring (256 entries). |
| processBlock wiring | `PluginProcessor.cpp` | Pulls PPQ + BPM from `AudioPlayHead`, stamps MIDI events to beat time, runs `computeDmx()` once per block (with master-dim values + colour-fade state + block dt), pushes 228 channels via `dmx.setChannel()`. When the host transport isn't playing it advances a free-running beat clock so recipes still animate (standalone / transport-stopped preview). |
| Editor | `PluginEditor.{h,cpp}` | Flat-and-wide layout (1168×338) — left pane: master-dim knobs (custom rotary look) + a compact MIDI log + two paired-button rows (**Connect/Disconnect** toggle / Blackout, then **Init. names** / **Show clips**) + one-line ENTTEC status; middle: the *Flamingo Hitmix at Night* title strip (soft pink) above the visualiser; a narrow far-right pane holds a **click-velocity slider** (sets the velocity of clicked/previewed triggers, live) above a **drag MIDI** tile (drag the latched trigger set out as a one-bar `.mid` held chord — `MidiDragTile`); the piano-roll trigger grid fills the rest. 15 Hz GUI timer drains the MIDI log, pushes live note state to the menu, and mirrors the driver's strobe state onto the visualiser. Also builds as a Standalone app for DAW-free testing. |
| Trigger vocabulary | `TriggerVocabulary.{h,cpp}` | The **single source of truth** for the note→label map (and the palette layout). Both the trigger menu and the rack namer read it, so they can't drift. `chainName(note)` returns the prefixed chain name (cp/cs/bk/sp/ba/pz/ch/br/wd/mc, `-` for unused) used to name the Ableton rack at runtime. Colour names live as data in `Palette.h` (`kPaletteNames` / `kSecondaryPaletteNames`). |
| Mapping versioning | `TriggerVocabulary.h`, `mappings/`, `tools/MappingTool.cpp` | The note mapping is **versioned and frozen**: `vocab::kMappingVersion` (currently 1) names the live version, and every version's full note→chainName map is a snapshot in `mappings/v<N>.tsv`. The `mapping-tool` console app dumps/verifies snapshots; the `mapping-frozen` CTest fails if the live vocabulary drifts from the frozen snapshot. Freeze procedure (bump → dump → commit) is in `mappings/README.md`. Clip migration between versions (and the legacy RGB-automation import) is planned as further `mapping-tool` subcommands; the sibling `hitdesigndmx` repo consumes these snapshots for conversion. |
| Trigger menu | `TriggerMenu.{h,cpp}` | **Transposed piano-roll grid** — each vocabulary section (from `TriggerVocabulary`) is a COLUMN (its name is the header); the 12 rows are the chromatic notes of that section's octave, **C at the bottom → B at the top**, with black-key rows shaded and a note-letter gutter on the left, so the menu lines up vertically with Ableton's piano roll. Columns: Spots & bars (one octave: blackout C-2, spots, bars), Zones (9 zones + Even/Odd/Thirds), Chases, Breathes, Wild, Multicolor (two octaves under one spanning header), then the palettes split low/high (Prim C5/C6, Sec C7) so each is a full 12-row octave of swatches. Notes with no trigger render as empty slots. Cells toggle and combine; the latched set is injected into `MidiState` via lock-free atomics (`setPreviewPitches`/`applyPreview`) at the click-velocity, and previewed live. Tiles light up while their note sounds (`setLiveNotes`). Latching a Multicolor cell counts as having a colour. No blackout cell beyond the C-2 trigger — the Controls Blackout button + master-dim knobs also cover that. |
| On-screen DMX preview | `DmxVisualizer.{h,cpp}` | A spot circle flanks each side of the 4 vertical bars × 18 cells (spot_l left, spot_r right, top-aligned with the bars); cells are ~2:1, and each fixture carries a small `dNNN` start-address caption underneath. Screen brightness is gamma-lifted (`kVizGamma`) so low DMX values read as visibly lit, matching the fixtures' own dimming curve (raw values made the preview look near-black while the real lights were clearly on). Pre-rendered into a single `juce::Image`, blitted by `paint()`. Re-rasterised only when the rig footprint's 8-bit fingerprint changes. `setStrobe(hz)` runs a dedicated timer at the send rate that mirrors the driver's one-frame-lit / rest-black flash at the velocity-driven repeat rate — preview only, not phase-locked to the real output. |
| ENTTEC driver | `EnttecProDmx.{h,cpp}` | Self-contained ENTTEC DMX USB Pro driver (IOKit discovery + POSIX termios I/O). Best-effort widget handshake, raised read timeout, 700-byte RX. Send loop runs on a `juce::HighResolutionTimer` (its own thread, off the message thread) at 40 Hz for steady frame emission. **Link resilience (auto-reconnect):** after the handshake the fd goes `O_NONBLOCK`, so `sendPacket` returns 1 (sent) / 0 (would-block — drop this frame, fixtures hold last values) / −1 (hard error — device gone). A would-block never drops the link; only a hard error does, after which the same timer re-opens the (serial-stable) callout path ~1×/s until it recovers — a wiggled cable / hub blip no longer latches output off. `shouldRun` (set on first connect, cleared only by `disconnect()`) arms the loop and makes the timer the sole port owner, so a Connect click during reconnect is a no-op rather than a second-fd race; the editor's button is a **Connect/Disconnect toggle** driven by `isRunning()`. **Exclusive port:** `openPort` sets `ioctl(TIOCEXCL)` so a second instance/app gets `EBUSY` (surfaced as "ENTTEC busy …") instead of silently sharing the port and interleaving corrupt frames — released on close, so Disconnect-A → Connect-B hands off cleanly. Hosts the **strobe shutter**: `setStrobeHz()` lights exactly one emitted frame per period and blacks the rest, on the emitted-frame counter, so the strobe is exactly synced to the DMX output clock and free of audio-block jitter. `hz` is the repeat rate — the flash stays one send tick (shortest), only the black gap grows (1 Hz = 1-lit/39-black … 20 Hz = 1-lit/1-black = max). The processor maps the held note's velocity to that rate. |

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

5. **Strobe is a driver-level shutter (reworked).** Rather than a
   per-pixel recipe sampled at the audio block rate (which beat against
   the 40 Hz send and jittered), the strobe lights one emitted frame per
   period and blacks the rest, on the driver's emitted-frame counter —
   exactly synced to output, decoupled from audio. It now sits on the
   **root of the Wild octave (C2 = 48)**, flashes **white by default**
   (the processor lights the rig white via the white-default path so the
   shutter has something to chop; a colour/recipe held alongside shows
   through), and **velocity sets the repeat rate** from `kStrobeMinHz`
   (1 Hz) to `kStrobeMaxHz` (20 Hz, hardware max). The on-screen flash is
   a separate GUI timer mirroring the same pattern, not phase-locked.

6. **Recipe vocabulary — full 48-recipe matrix.** Four feel-groups, each a
   complete chromatic octave starting on a C: Chases (C0, 12), Breathes
   (C1, 12), Wild (C2, 12), Multicolor (C3–C4, 24). Multicolor recipes are
   self-coloured (`DynamicColorFn` returns RGB and overrides the palette
   route). Tables are ordered logically and match the menu columns 1:1.
   Verified numerically (range, liveness, motion via ASCII frame renders)
   and dispatch-mapped by unit test; tuning constants live at the top of
   each recipe in `Recipes.cpp`. Still wants a full eyeball on real
   hardware (TODO #4).

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
fade, master dims, spots above the grid, 18 px/bar rig, vocabulary
simplification (bars + pixel zones), real-hardware DMX smoke test,
velocity-controllable chase tails (with dynamics decoupled from colour
routing), pixel-density control (diagonal bias fixed via per-bar rank hash),
piano-roll editor redesign + octave-aligned MIDI remap, **full 48-recipe
matrix**, **per-bank velocity semantics** (tail / beat-division / density /
speed; VU meter beat-locked velocity→gain), **white default** for
palette-less holds, **Showcase assets** (embedded named rack + runtime demo
clips via Init-names / Show-clips), recipe reorder + `.adg` name prefixes,
**strobe rework** (C2 root, white default, single-frame flash, velocity →
1–20 Hz repeat rate), live-MIDI tile highlight, click-velocity slider.

## How to verify after a fresh checkout

```bash
cmake -S . -B build -G Xcode
cmake --build build --config Release   # builds BOTH the VST3 and the Standalone
ctest --test-dir build                 # mapping-frozen: live vocab matches mappings/v1.tsv
```

Always build both targets (the bare `cmake --build build` does) — building
only one leaves the other stale, so the DAW plugin can diverge from the
Standalone.

VST3 lands at `build/HitNoteDmx_artefacts/Release/VST3/HitNoteDmx.vst3`
and is also installed to `~/Library/Audio/Plug-Ins/VST3/HitNoteDmx.vst3`.

In Live: rescan VSTs (Preferences → Plug-Ins → "Rescan"), drop
`HitNoteDmx` on a MIDI-armed track, play any note. Editor's MIDI log
should show `[on ] ch1 C3  (60) vel=100` (note name + pitch number +
velocity); the on-screen rig preview should respond per the palette
table in the project README.
