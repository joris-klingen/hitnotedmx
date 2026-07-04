# HitNoteDmx — status & development log

Living development log + architecture reference, updated alongside
non-trivial commits so any future session (or a future you) can pick up
without re-archaeology. The per-layer table below is the current-state
reference; the **Changelog** at the bottom is how it got there.

**Open backlog lives in [TODO.md](TODO.md).** Current focus: compose the
first songs in Live (the vocabulary, hardware path and expressiveness are all
in place — it has already driven a live show) and the render-tool "look" net
(TODO #2). The plugin ships as **both a VST3 and a Standalone — build both
targets** (`cmake --build build`) so the DAW plugin never goes stale.

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
| VST plumbing | `PluginProcessor.{h,cpp}`, `CMakeLists.txt` | **Instrument shape** (`IS_SYNTH=TRUE`, VST3 category `Instrument`): a single stereo audio **output** bus that emits silence, no audio input, MIDI input on. It loads on a MIDI track as the instrument and receives MIDI directly. The silent audio bus is what makes Live load it — the pure MIDI-effect shape (`IS_MIDI_EFFECT=TRUE`, no audio bus) did **not** load in Live, which is why we don't use it. Changing from the old audio-effect shape moved the plugin from Live's *Audio Effects* to *Instruments*, so sessions saved against the old shape must re-add it. **Automatable params are now ONLY the three master controls** (LED dim / spot dim / pixel density). The old 512 per-DMX-channel `chN` params (a fossil of the pre-vocabulary RGB-automation era) were **removed**: `processBlock` rewrites all 228 rig channels from the recipe engine every block, so any host-automated/manual `chN` value was clobbered ~40–90×/s — the params did nothing for the rig while bloating saved sessions and the host automation list. Old sessions still load (APVTS ignores the stale `chN` nodes; the master params keep their string IDs). |
| Rig | `Rig.h` | **Runtime grid shape** (`Rig` POD: cols × rows, default 4 × 18) + 2 spots **pinned at the bottom of the universe** so a re-shape never re-patches them: spot_l DMX 1–6, spot_r 7–12, bars contiguous from ch 13 (3 ch/pixel, column-major). Caps: cols ≤ 8, rows ≤ 32 (masks stay uint32), cols×rows ≤ 166 (universe budget). Default 4×18 = 228 channels (bars 13–228). |
| Grid apply | `Composition.{h,cpp}`, `PluginProcessor.{h,cpp}`, `PluginEditor.cpp` | The editor's **grid section** (cols × rows + *Set grid*, above the utility buttons) calls `setGridShape`, which packs the pair into ONE atomic (`gridRequest`, never tears) and stamps `gridCols`/`gridRows` **properties** on the APVTS state tree (persisted with the session; deliberately NOT host-automatable — automating bar count would live-repatch DMX mid-song). `processBlock` step 0 applies a changed request: updates `grid.rig`, calls `GridState::rebuild()` (noexcept, allocation-free — pixel-zone masks: pixel p → zone `((p-1)*9)/rows`, reproducing the original 2-pixel pairs at 18 rows; Even/Odd/Thirds combs modular; density rank re-ranked per bar), resets bump/xfade state and zeroes all 512 driver channels once so a shrink leaves no stale bytes lit. All grid-dependent buffers (`SelectionMask`, xfade, visualiser fingerprint) are preallocated at the 8×32 max, so applying never allocates. **Bar selectors are positional** — Left / Mid left / Mid right / Right (pitches 5–8): the ends are **exclusive** (Left owns the outermost left bar(s), Right the outermost right, no other selector touches them; 1 bar each up to 6 cols, 2 from 7 — `e = max(1, (cols+1)/4)`), the mids split the bars in between half-and-half and **share the centre bar** when that count is odd (see `selectorCoversBar`). Identical to the old per-bar mapping at 4 cols; 2 cols leaves the mids empty, 1 col collapses Left = Right. |
| Palette | `Palette.h` | Two separate tables. Primary `kPalette` (24 colours) spans two octaves 84–107 (C5–B6); secondary `kSecondaryPalette` (12 softer complementary accents) is one octave 108–119 (C7). `paletteColorFor(start, offset)` picks the table; `kSecondaryPaletteEnd` (120) bounds the range. |
| Held-note tracker | `MidiState.{h,cpp}` | 128-slot array indexed by pitch. O(1) noteOn/noteOff/clear, no allocations. |
| Dynamic recipes | `Recipes.{h,cpp}` | Four feel-groups, each a full chromatic octave starting on a C, all function-pointer dispatch (one table per group), 48 recipes total. Each table is ordered logically and matches the menu columns 1:1. **Chases C0 (24–35):** chase, comets, ping-pong, diag, radar, snake, theater, spiral, waves, expand, contract, fountain (direction is the global Flip/Reverse notes now, not up/dn variants). **Breathes C1 (36–47):** tide, sine, ripple, ripple H, bloom, halo, moon rise, soft ball, drift, aurora, shimmer, glow. **Wild C2 (48–59):** strobe (root C, null slot — driver-level shutter), sparkle, sparkle few, lightning, glitch, static, rain, waterfalls, bounce, fast ball, pong (beat-synced here), burst. **Multicolor C3–C4 (60–83, two octaves, 24):** self-coloured `DynamicColorFn` recipes returning RGB directly — rainbow, comet, VU meter, VU smooth, fire, embers, magma, lava, heatmap, ocean, forest, desert (C3); sunset, twilight, borealis, night sky, galaxy, nebula, storm, plasma, police, disco, velvet, rouge (C4). `DynamicFn` carries a `tail` arg (0..1); `DynamicColorFn` carries a `param` arg (VU gain). Moving-head chases render a velocity-driven comet trail via `cometBrightness` / `cometBrightnessF`. |
| Composition | `Composition.{h,cpp}` | Bit-mask lookup tables, mask intersection across utility / static / dynamic layers, primary/secondary palette routing, spot RGBW with warm-white tint. Plus master-dim scaling and velocity-driven linear colour fade (`ColorFadeState`, see below). **Colour routing comes from the pixel-zone selectors (their velocity picks primary vs secondary); bar selectors set each bar's brightness ceiling (velocity / 127) and no longer route colour; brightness dynamics modulate brightness only** — so a chase takes the zone's colour, or primary by default. **Palette colour notes carry no brightness of their own — velocity sets only the fade duration, so a soft note rises slowly to full colour.** **Per-bank velocity semantics** (doc table at the top of the file): Chases → comet tail, Wild → beat-division 1/16..1/1 (except sparkle/sparkle few, free-running speed), Breathes → density "islands" + half-speed (except ripple), Multicolor → animation speed (except VU meter, beat-locked with velocity → gain). **White default:** a bar/pixel/dynamic/strobe trigger held with NO palette colour renders full white (matching click-preview). **Self-coloured Multicolor recipes (60–83) are the exception to colour routing:** when held, their RGB replaces the palette route for the lit pixels (multiple combine per-channel max); structural masks and brightness dynamics still gate/modulate on top, and the colour-fade stage doesn't apply to them. |
| Master dims + density | `PluginProcessor.{h,cpp}`, `Composition.cpp` | Three automatable params — LED Master Dim + Spot Master Dim + **Pixel Density** (0..1, shown as %). Dims scale RGB / spot dimmer; density is the dark-room thinning control: below 100% it blanks a fixed subset of bar pixels so fewer LEDs light without flicker and the survivors keep full brightness (gates on/off, never dims). The gate order comes from `kDensityRank` — an avalanche hash (murmur3 finalizer) of grid position, rank-normalised within each bar, so the drop order is random but every bar keeps the same lit fraction (the original single-multiply linear hash dropped pixels in diagonal stripes). All three applied inside `computeDmx` so the on-screen preview reflects them; MIDI-mappable in the host. |
| Master controls | `Composition.cpp`, `TriggerVocabulary.cpp`, `PluginEditor.cpp` | **Whole-rig overrides** — not per-fixture triggers; `computeDmx` handles them in section 9, state in `BumpState`, advanced in beat-time. **Master octave 8:** **Bump (120)** flashes the frame toward white, or the current primary hue if one is held (velocity = brightness), **zero sustain** — the flash fires on each note's **onset** and **decays back to the scene regardless of hold**, so only the note start matters. Its release length is the **Release (121)** note's velocity (1/16 note at vel 1 → 1 bar at vel 127; absent = 1/8 note). **Crossfade (122)** slews the displayed **bar** frame toward the composed scene so look changes glide instead of snapping (velocity = fade length, 1/16 note … 1 bar; absent = instant; bars only — spots stay snappy; bumps punch through; at long lengths it also motion-blurs the recipes, by design). **Freeze (123)** holds the previous frame *and pauses the animation clock* (`animBeats` advances only on non-frozen blocks), so releasing it resumes seamlessly; blackout dominates freeze. **Reverse (124)** runs the chases/breathes phase clock *backward* so they **retrace from the current state** (a temporal reverse; Wild/Multicolor keep absolute beat time so their beat-locks hold; phase kept ≥ 0 since recipes wrap on non-negative t). **Flip (125)** mirrors recipe direction by sampling each recipe at mirrored grid coords (`bar→nBars-1-bar`, `pixel→nPix+1-pixel`) — an instant *spatial* flip; most movers turn around with no per-recipe code (symmetric/rotational ones no-op). **Spread (126)** phase-offsets each bar's recipe clock (velocity = amount, 0 … 1 beat) so the four bars de-sync. **Speed (G8 / 127)** is the global recipe-speed multiplier (exponential, vel 64 = 1×, ~0.25×–4×); while held, chase/wild velocity picks the palette route instead of tail/beat-speed. **Bar octave −2 (beside the bar selectors):** **From black (9, A-2)** / **To black (10, A#-2)** — the master fade pair (they're bar-level masks, so they live here): to-black snaps to the scene on onset then falls to black; from-black snaps to black then rises — **except at velocity 1, where it holds full black** (a "stay black" sentinel). Both reset to the scene when the note ends (per-note via start beat), glide at their *own* note's velocity (127 = instant, 0 = one bar), and **exclude the spots** (only blackout C-2 darkens spots). The octave-8 controls live in the **editor's left-pane master grid** — a 2×4 grid whose **top row is the frame controls** (Bump, Release, Crossfade, Freeze) and **bottom row the motion modifiers** (Reverse, Flip, Spread, Speed); only **Bump** is momentary, the rest latch; to/from-black moved into the Spots & bars menu column. Mapping frozen at **v9** (`mappings/v9.tsv`); the "Master" vocabulary column (prefix `ms`) names the tiles + rack chains (to/from-black keep their `ms` chain-name across the move so clips migrate by chainName). |
| Colour fade | `Composition.{h,cpp}` | Each palette's displayed colour ramps linearly toward the winning note. Fade duration from the note's velocity (hard = instant, soft = up to 3 s) → a soft "black" palette note is a slow fade-to-black. State persists across blocks; advanced by wall-clock dt so it runs even when transport is stopped. |
| Audio-thread → GUI MIDI log | `MidiLog.{h,cpp}` | Lock-free SPSC ring (256 entries). |
| processBlock wiring | `PluginProcessor.cpp` | Pulls PPQ + BPM from `AudioPlayHead`, stamps MIDI events to beat time, runs `computeDmx()` once per block (with master-dim values + colour-fade state + block dt), pushes 228 channels via `dmx.setChannel()`. When the host transport isn't playing it advances a free-running beat clock so recipes still animate (standalone / transport-stopped preview). |
| Editor | `PluginEditor.{h,cpp}` | Flat-and-wide layout (1168×338) — left pane: master-dim knobs (custom rotary look) + a compact MIDI log + two paired-button rows (**Connect/Disconnect** toggle / Blackout, then **Init. names** / **Show clips**) + one-line ENTTEC status; middle: the *Flamingo Hitmix at Night* title strip (soft pink) above the visualiser; a narrow far-right pane holds a **click-velocity slider** (sets the velocity of clicked/previewed triggers, live) above a **drag MIDI** tile (drag the latched trigger set out as a one-bar `.mid` held chord — `MidiDragTile`); the piano-roll trigger grid fills the rest. 15 Hz GUI timer drains the MIDI log, pushes live note state to the menu, and mirrors the driver's strobe state onto the visualiser. Also builds as a Standalone app for DAW-free testing. |
| Trigger vocabulary | `TriggerVocabulary.{h,cpp}` | The **single source of truth** for the note→label map (and the palette layout). Both the trigger menu and the rack namer read it, so they can't drift. `chainName(note)` returns the prefixed chain name (cp/cs/bk/sp/ba/pz/ch/br/wd/mc, `-` for unused) used to name the Ableton rack at runtime. Colour names live as data in `Palette.h` (`kPaletteNames` / `kSecondaryPaletteNames`). |
| Mapping versioning | `TriggerVocabulary.h`, `mappings/`, `tools/MappingTool.cpp` | The note mapping is **versioned and frozen**: `vocab::kMappingVersion` (currently 9) names the live version, and every version's full note→chainName map is a snapshot in `mappings/v<N>.tsv`. The `mapping-tool` console app dumps/verifies snapshots; the `mapping-frozen` CTest fails if the live vocabulary drifts from the frozen snapshot. Freeze procedure (bump → dump → commit) is in `mappings/README.md`. Clip migration between versions (and the legacy RGB-automation import) is planned as further `mapping-tool` subcommands; the sibling `hitdesigndmx` repo consumes these snapshots for conversion. |
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

## Design decisions & gotchas

Non-obvious calls a future session could get wrong. The per-layer table
above covers the rest; resolved-issue history lives in the Changelog + git.

- **Transport-stopped animation is deliberate, not a bug.** When the host
  transport isn't playing, `processBlock` advances a free-running beat clock
  (continuous from the last host PPQ) so chases/breathes keep animating in the
  Standalone and while stopped; it snaps back to host PPQ when playback
  resumes. This is a chosen divergence from "freeze when paused" — don't
  "fix" it back.
- **The plugin must be an instrument, not a MIDI effect.** `IS_SYNTH=TRUE`
  with a silent stereo output bus is the shape that makes Live load it; the
  pure `IS_MIDI_EFFECT` shape did **not** load. Don't "simplify" the silent
  audio bus away. (Sessions saved against the old audio-effect shape must
  re-add the plugin as an instrument.)
- **GUI runs on the software compositor.** It has held up across live shows
  under sustained chases + the 40 Hz strobe-flash repaint, but an OpenGL
  context is the escape hatch if that cost ever regresses (the visualiser is
  already a single cached blit gated on a fingerprint diff).
- **`computeDmx()` is on the audio thread** — no allocations, no locks (the
  offline tools call the very same function, so a render never drifts from the
  rig). Persistent envelopes (`ColorFadeState`, `BumpState`) live across blocks
  rather than allocating per call.

## How to verify after a fresh checkout

```bash
cmake -S . -B build -G Xcode
cmake --build build --config Release   # builds BOTH the VST3 and the Standalone
ctest --test-dir build                 # mapping-frozen + recipe-range (see below)
```

Two CTests run:
- **mapping-frozen** — live vocab matches `mappings/v<N>.tsv` (drift guard).
- **recipe-range** — drives the real recipes + `computeDmx` over a beat /
  velocity / density / grid-shape sweep (4×18, 6×24, 2×32, 8×1) and fails on
  any non-finite or out-of-range output
  (`tools/RecipeCheck.cpp`). A cheap engine regression net; the golden-image
  diff (recipe *look*, not just range) is still the bigger render tool in
  TODO #2.

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

## Changelog

Reverse-chronological, newest first — the single record of shipped work
(the per-layer table above is the current-state reference).

- **Parametric grid shape (mapping v10, TODO 7a)** — the rig geometry is a
  RUNTIME setting: a `Rig` POD (cols × rows, default 4 × 18) replaces the
  `constexpr` 4×18 tables. **DMX re-addressed** — the spots are pinned at the
  bottom of the universe (spot_l 1–6, spot_r 7–12) so re-shaping never
  re-patches them; bars run contiguously from ch 13 (the physical rig needs a
  one-time re-patch even at 4×18: bars 1/55/109/163 → 13/67/121/175, spots
  217/223 → 1/7). Bounds: cols ≤ 8, rows ≤ 32 (pixel masks stay `uint32`),
  cols×rows ≤ 166. The editor gains a small **grid section** (cols × rows +
  *Set grid*) above the utility buttons (the MIDI log shrank to fit); the
  shape persists as `gridCols`/`gridRows` state-tree properties (not
  host-automatable). Handoff is one packed atomic; the audio thread rebuilds
  the derived tables in `GridState::rebuild()` (allocation-free, all buffers
  preallocated at 8×32) and zeroes the full universe once per apply so a
  shrink can't leave stale channels lit. **Bar selectors renamed** Bar 1–4 →
  **Left / Mid left / Mid right / Right** with positional semantics —
  Left/Right own the outermost bar(s) exclusively (1 each up to 6 cols, 2
  from 7), the mids split the bars in between and share the centre bar on
  odd counts (identical at 4 cols); zone selectors generalise as bottom-up row spans
  (`zone(p) = ((p-1)*9)/rows`, the original pairs at 18 rows); the recipes
  were already parametric (`nBars`/`nPix` args) and needed no changes.
  Visualiser draws the live shape (adaptive bar width, max-size fingerprint +
  shape bytes); `recipe-check` now sweeps `computeDmx` at 4×18, 6×24, 2×32
  and 8×1. Frozen as **`mappings/v10.tsv`** — pitches 5–8 are unchanged, so
  existing clips keep working; re-run *Init. names* to refresh rack chain
  names.

- **Recipe-bank refinement pass (mapping v9)** — new recipes, renames + a
  reshuffle, and tuning, all behind one freeze. **New chases** filling the slots
  freed by dropping the up/dn pairs: **Comets (25)** — three comet heads climb at
  once, velocity morphs three dots → continuous wash; **Radar (28)** — a thin
  (~1-px) spoke sweeping around the grid centre with a velocity-driven trail
  (experimental on the tall-narrow rig). **Reshuffle:** Stutter → **Waterfalls**
  (55, a downward cascade of thin streaks); **Pong** moved Chases-35 → Wild-58
  (beat-synced now, was Zigzag); renames **Converge → Burst** (59, a real
  centre-out circle in cell units — narrow sides saturate before the top) and
  **Smooth shimmer → Glow** (47, +shimmer +spatial gradient). **Fountain (35)**
  fills the slot Pong vacated — water jets rising from the bottom, fading toward
  the top. **Tuning:** Spiral now defaults up (like the other chases); Disco beat-locked
  (pulses on the song beat, like VU); VU-meter low-level now a single dimming
  floor LED (was a 2-px block); Moon rise narrower + a per-bar diagonal lean;
  Tide gained a per-bar diagonal wave.
  Eyeballed via `recipe-render`; `recipe-range` + `mapping-frozen` green; both
  targets build.
- **Master-section remap + new FX-modifier notes (mapping v8)** — informed by
  a pass over lighting-console literature (the canonical Speed/Size/Spread/
  Direction FX-modifier family + busking toolkit). **To/from-black moved** out
  of the master octave to the Spots & bars octave (A-2 / A#-2) beside the bar
  selectors — they're bar-level masks (keep their `ms` chain-name so clips
  migrate). **Bump white + Bump color merged** into one zero-sustain **Bump**
  (120, white or primary-if-held); **Release** (121) now sets the bump release
  length (1/16 note … 1 bar, default 1/8); the old generic Release note is gone.
  **Four new master notes:** **Crossfade** (122, bar-frame slew so look changes
  glide), **Reverse** (124, *temporal* — runs the chases/breathes phase clock
  backward so they retrace from the current state; Wild/Multicolor keep absolute
  beat time), **Flip** (125, *spatial* — mirrored-coord sampling, instant
  direction flip, no per-recipe code), **Spread** (126, per-bar phase offset).
  **All up/dn recipe pairs removed** — Chase/Diag are single now (default up;
  notes 25/28 freed) and Flip/Reverse handle direction. The 8 master notes are
  ordered into two grid rows — frame (Bump, Release, Crossfade, Freeze) / motion
  (Reverse, Flip, Spread, Speed); B-2 (11) left free. One mapping freeze
  (v7 → v8). `recipe-range` + `mapping-frozen` green; both targets
  build.
- **Master notes — zero-sustain bumps + from-black "hold black"** — bump
  white/colour now fire on note ONSET and decay regardless of hold length
  (zero sustain), so only the note start matters and they're easier to
  MIDI-program; from-black at velocity 1 holds full black while held (lay
  black down first, then reveal with a higher-velocity note). `Composition.cpp`
  section 9 / `BumpState`; no vocabulary change.
- **recipe-render — clip + contact modes** — the offline render tool can now
  parse a `.mid` and drive it through the real `computeDmx`, rendering a whole
  combination clip as a film strip (`clip`) or surveying a folder one row per
  clip (`contact`). The design tool (hitdesigndmx) authors the `.mid`,
  hitnotedmx renders it — no engine re-implementation, no drift. (Strobe, a
  driver-level shutter, is the one thing not reproduced offline.)
- **Master / global controls (octave 8) + global speed** — a left-pane master
  grid of whole-rig controls (`computeDmx` section 9, state in `BumpState`):
  **bump-white / bump-color** (momentary flash, velocity = brightness, instant
  attack + a velocity-set release tail back to the scene); **to/from-black**
  (a fade pair — to-black auto-releases, from-black is an instant-black reveal;
  glide at their own note velocity; bars only, spots ignore it — only blackout
  C-2 darkens spots); **Release** (bump-tail rate); **Freeze** (holds the frame
  *and pauses the animation clock*, so it resumes seamlessly); **Speed (G8)**
  (global recipe-speed multiplier; while held, chase/wild velocity picks the
  palette route). Tiles styled like menu cells, bump/fade tiles momentary,
  Release/Freeze/Speed latch. Mapping re-frozen **v2 → v6**; "Master" vocab
  column (prefix `ms`). Built + `mapping-frozen` green.
- **Pixel zones re-authored natively for 18 pixels** — the nine zones used a
  `zone(z) = bit(2z-1)|bit(2z)` helper (a fossil of the old 9-pixel rig) while
  Even/Odd/Thirds were native; now all of `kPixelStaticMask` is authored in real
  18-pixel terms via a `span(first,last)` helper (zones = contiguous pairs 1-2 …
  17-18). Behaviour-preserving — `static_assert`s pin the masks byte-identical to
  the old definitions, so note meanings are unchanged (no mapping bump).
- **Plugin reshaped as an instrument** — was an audio effect with MIDI input;
  now `IS_SYNTH=TRUE` with a silent stereo output bus, no audio input, VST3
  category `Instrument`. Loads on a MIDI track *as the instrument* and gets MIDI
  directly. The silent audio bus is the trick that makes Live load it (the pure
  `IS_MIDI_EFFECT` shape didn't). Verified in Live + on the rig. Note: sessions
  saved against the old audio-effect shape must re-add it (Fx → Instrument).
- **DMX link resilience — auto-reconnect + toggle + exclusive port** — a
  transient USB hiccup no longer latches output off. The send fd is `O_NONBLOCK`
  after the handshake, so `sendPacket` distinguishes sent / would-block (drop one
  frame, fixtures hold) / hard error; only a hard error drops the link, after
  which the same timer re-opens the (serial-stable) callout path ~1×/s until it
  recovers. `shouldRun` makes the timer the sole port owner; the editor button is
  a **Connect/Disconnect toggle**. `ioctl(TIOCEXCL)` claims the port so a second
  instance/app gets a clear "ENTTEC busy" instead of silently sharing it. The
  editor logs drop/recovery edges and updates the status line. Verified with real
  unplug/replug/wiggle and two instances on the ENTTEC + 228-channel rig.
- **Drag a MIDI clip from the plugin into the DAW** — latch a set by clicking
  tiles in the trigger menu, then drag the far-right **drag MIDI** tile out to
  Finder / Ableton to drop a one-bar `.mid` of the latched notes as a held chord
  (`MidiDragTile` in `PluginEditor.cpp`, `performExternalDragDropOfFiles` on a
  temp `HitNoteDmx clip.mid`). Follow-ups: configurable length; name the file
  from the selection.
- **Mapping v1 frozen + mapping-tool** — the note mapping is versioned
  (`vocab::kMappingVersion`); each version's note→chainName map is a snapshot
  in `mappings/v<N>.tsv`, dumped/verified by the `mapping-tool` console app
  and guarded by the `mapping-frozen` CTest. Freeze procedure in
  `mappings/README.md`. Clip migration between versions (and legacy
  RGB-automation import) planned as further subcommands; sibling repo
  **hitdesigndmx** converts legacy sets against these snapshots.
- **Visualizer polish** — preview brightness is gamma-lifted (`kVizGamma`) so
  low DMX levels read as visibly lit, matching the fixtures' own dimming
  curve; each fixture shows a small `dNNN` start-address caption; editor
  tightened to 1168×338.
- **Strobe rework** — moved to the root of the Wild octave (C2 = 48; sparkle/
  sparkle few shifted to 49/50). Flashes **white by default** (rig lit white via
  the white-default path so the driver shutter has something to chop; a colour/
  recipe held alongside shows through). The shutter now lights one emitted frame
  per period (shortest flash) instead of a 50% duty; **velocity sets the repeat
  rate 1–20 Hz** so only the black gap grows. Visualizer mirrors the pattern.
- **Showcase assets** — two left-pane buttons: **Init. names** installs the
  trigger rack (`Hitnotenames.adg`, embedded via `juce_add_binary_data`) into
  the Ableton User Library; **Show clips** writes layered demo *combo* clips to
  `~/Music/HitNoteDmx Showcase/Combos/` and opens it in Finder. Clips are
  generated at runtime (`Source/Showcase.cpp`), idempotent. The rack's 128
  chains are named **at runtime** from the shared `TriggerVocabulary` (group
  prefixes cp/cs/bk/sp/ba/pz/ch/br/wd/mc, `-` for unused) — the embedded .adg
  is just a key-gated template, so the names can never drift from the plugin
  and there is no offline tool. `installRack()` always overwrites.
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
  C-2. All 48 recipes guarded in range + finite by the `recipe-range` CTest
  (`tools/RecipeCheck.cpp`); "alive + colourful" is eyeballed via `recipe-render`.
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
- **Pixel-density diagonal-bias fix** — the linear position hash dropped
  pixels in diagonal stripes (per-bar lit counts 9/7/4/1 at 25%);
  replaced with a murmur3-finalizer avalanche hash rank-normalised per
  bar: random drop order, exact per-bar proportionality, still
  flicker-free and monotonic.
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
