# HitNoteDmx

**Play your light show like an instrument.** HitNoteDmx is a macOS audio
plugin (VST3 + Standalone, JUCE 8) that turns MIDI notes into live DMX — draw
clips in your DAW, route them at the plugin, and the rig responds in real time
with no offline render step. It talks to an **ENTTEC DMX USB Pro** through a
self-contained driver (IOKit discovery + POSIX serial I/O — no FTDI SDK, no
external dependencies).

It also runs as a **Standalone** app so you can design and preview a show
without a DAW at all.

## What it does

- **Note → light, at the audio rate.** Every note is a trigger: structural
  selectors (which bars / pixels / spots), palette colours, and a deep bank of
  motion recipes — chases, breathes, wild bursts, and self-coloured effects.
- **Piano-roll trigger menu.** The editor's right pane is a transposed
  piano-roll grid: each part of the vocabulary is a column, twelve chromatic
  rows run C (bottom) → B (top), and black-key rows are shaded — so it lines up
  visually with the piano roll in your DAW. Click any tile to audition it live;
  tiles also light up when their note is actually playing.
- **Velocity is expressive.** Depending on the layer, a note's velocity sets
  the colour route (hard = primary, soft = secondary accent), the comet-tail
  length of a chase, the beat-division or animation speed of an effect, the
  density of a breathe, a VU meter's gain, the strobe's repeat rate, or a
  palette's brightness and fade time.
- **Live preview + master controls.** An on-screen rig visualiser mirrors the
  output; LED-dim, Spot-dim and Pixel-Density knobs (all host-automatable /
  MIDI-mappable) ride the whole rig.

## The rig

**4 RGB bars × 18 pixels + 2 RGBW spots = 228 DMX channels** (bars on DMX
1–216, spots on 217 / 223), defined in `Source/Rig.h`.

## The note map

Every section starts on a C, so each column of the menu is one octave of your
keyboard (C3 = MIDI 60, Ableton convention):

| Octave | Notes | Section |
|--------|-------|---------|
| C-2 | 0–8 | **Blackout** (C-2) + Spots (L/R, warm-white & colour) + Bars 1–4 |
| C-1 | 12–23 | **Pixel zones** — 9 zones + Even / Odd / Thirds combs |
| C0 | 24–35 | **Chases** — chase, ping-pong, snake(s), sweeps, diagonals, waves, expand/contract |
| C1 | 36–47 | **Breathes** — sine, ripple(s), halo, moon rise, soft ball, aurora, bloom, shimmer, sway, drift |
| C2 | 48–59 | **Wild** — strobe (on the root C), sparkle(s), lightning, glitch, static, rain, bounce, fast ball, zigzag, converge … |
| C3–C4 | 60–83 | **Multicolor** — self-coloured effects: rainbow, comet, VU, fire, magma, ocean, forest, borealis, night sky, galaxy, plasma, police, disco … |
| C5–C6 | 84–107 | **Primary palette** — 24 colours |
| C7 | 108–119 | **Secondary palette** — accent colours |

Recipes are pure functions of beat-time, bar and pixel, so they stay locked to
the song's tempo (and keep animating on a free-running clock when the transport
is stopped). Strobe sits on the root of the Wild octave (C2): it's a
frame-synced shutter in the driver itself — one lit frame per period, so it
never beats against the audio block rate — flashing white by default, with
velocity setting the repeat rate from 1 Hz up to the 20 Hz hardware max.

## Building

Requires CMake 3.22+ and Xcode (or the Command Line Tools). JUCE is fetched
automatically via `FetchContent`.

```sh
cmake -S . -B build -G Xcode
cmake --build build --config Release
```

The VST3 lands at `build/HitNoteDmx_artefacts/Release/VST3/HitNoteDmx.vst3` and
is also copied to `~/Library/Audio/Plug-Ins/VST3/`. The Standalone app is at
`build/HitNoteDmx_artefacts/Standalone/HitNoteDmx.app`.

## Using it

1. Drop `HitNoteDmx` on a MIDI-armed track in your DAW (or launch the
   Standalone app).
2. Click **Connect USB** to bind the ENTTEC widget — the status line shows what
   was found.
3. Click tiles in the trigger menu to audition the vocabulary, or play / draw
   MIDI notes from the octave map above. The visualiser mirrors the rig.

To set up your DAW for naming and demo content, the left pane has two
buttons: **Init. names** installs a named MIDI Effect Rack
(`Hitnotenames.adg`) into your Ableton User Library so the piano roll shows
every trigger's name, and **Show clips** writes a set of layered demo MIDI
clips to `~/Music/HitNoteDmx Showcase/` and opens it in Finder for drag-and-drop.

## Under the hood

- `Source/Recipes.{h,cpp}` — the recipe banks (brightness `DynamicFn`,
  self-coloured `DynamicColorFn`) and their pitch dispatch.
- `Source/Composition.{h,cpp}` — `computeDmx()`: the single-pass compositor that
  intersects the structural masks, routes palette colour, runs the recipes, and
  applies the master dims. The velocity-semantics table lives at the top.
- `Source/EnttecProDmx.{h,cpp}` — the self-contained ENTTEC driver; the send
  loop runs on its own high-priority timer at 40 Hz.
- `Source/TriggerMenu.{h,cpp}` — the piano-roll menu and live preview.
- `Source/TriggerVocabulary.{h,cpp}` — single source of truth for the
  note→name map; the mapping is versioned (`vocab::kMappingVersion`) and each
  version is frozen as `mappings/v<N>.tsv`, guarded by the `mapping-frozen`
  CTest (see `mappings/README.md`).
- The plugin namespace is `hitnotedmx`; JUCE's `createPluginFilter()` entry
  point sits at file scope and forwards in.

## Family

HitNoteDmx is the backbone of the hitdmx family:
[hitlaunchdmx](https://github.com/joris-klingen/hitlaunchdmx) (standalone
Launchpad-triggered ambient scenes),
[hitccdmx](https://github.com/joris-klingen/hitccdmx) (raw CC-style DMX
channel VST), and
[hitdesigndmx](https://github.com/joris-klingen/hitdesigndmx) (clip design +
conversion of legacy lighting clips into this plugin's note vocabulary).
