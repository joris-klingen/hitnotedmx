# HitNoteDmx

A macOS VST3 plugin that turns MIDI notes into a live light show. It
accepts MIDI input, translates incoming notes into chase / breathe /
sparkle / etc. lighting recipes at the audio rate, and writes the
resulting per-channel DMX values out to an **ENTTEC DMX USB Pro** via a
self-contained driver (IOKit discovery + POSIX serial I/O, no external
dependencies).

Workflow: draw MIDI clips on a track in your DAW, route them to
`HitNoteDmx`, and the lights respond in real time — no offline render
step. It also runs as a **Standalone** app for previewing without a DAW.

The note vocabulary (spot triggers, bar selectors, pixel zones, dynamic
recipes, two palette ranges, blackout) is the plugin's own; the in-editor
**trigger menu** documents every note and lets you click to preview it.

## Status

| Phase | What it does | State |
|-------|--------------|-------|
| Skeleton | Builds; accepts MIDI; logs note activity in the editor | **shipped** |
| Recipes | chase_up/down, ping_pong, snake, sine_wave, sparkle, breathe, sweep_up/down, strobe, kick_pulse, alt_swap | **shipped** (12 dynamics; more planned) |
| Composition | Mask intersection across utility / static / dynamic layers, primary/secondary palette routing, spot RGBW | **shipped** |
| Colour fade | Velocity-driven linear fade between palette colours (soft black note = slow fade-to-black) | **shipped** |
| Master dims | Automatable LED + spot master-dim params (MIDI-mappable) | **shipped** |
| Editor / preview | 3-pane editor (knobs + log + buttons / rig visualiser / clickable trigger menu); Standalone build for DAW-free testing | **shipped** |
| Trigger menu | Clickable, toggle-and-combine reference of every MIDI note, previewed live on the visualiser | **shipped** |
| Show-time UX | Save/restore of recipe-layer state | in progress |

The rig: **4 RGB bars × 18 pixels + 2 RGBW spots = 228 DMX channels**
(bars on DMX 1–216, spots on 217/223), defined in `Rig.h`.

## Building

Requires CMake 3.22+ and Xcode (or the Command Line Tools). JUCE is
fetched automatically via `FetchContent`. No other dependencies.

```
cmake -S . -B build -G Xcode
cmake --build build --config Release
```

The VST3 is at `build/HitNoteDmx_artefacts/Release/VST3/HitNoteDmx.vst3`,
and the build also copies it to `~/Library/Audio/Plug-Ins/VST3/`.

## Sanity-test

After the plugin loads, you should be able to:

1. Add a fresh MIDI track in your DAW.
2. Insert `HitNoteDmx` on it.
3. Open the editor — left pane shows the master-dim knobs, MIDI log, and
   the connect/disconnect/blackout buttons + ENTTEC status; the middle
   pane shows the rig visualiser.
4. Play any MIDI note (computer keyboard works) → the log prints
   `[on ]  ch1  C3   vel=N`, and the on-screen rig responds per the
   MIDI vocabulary (palette colours, bar/pixel selects, chases, spots).

That confirms the audio thread is receiving MIDI, the lock-free MidiLog
ring is drained by the GUI timer, and `computeDmx` is driving the rig.
For a quick look without a DAW, launch the **Standalone** build at
`build/HitNoteDmx_artefacts/Standalone/HitNoteDmx.app`.

## Notes on code organisation

- `Source/EnttecProDmx.{h,cpp}` is the self-contained ENTTEC DMX USB Pro
  driver — IOKit for device discovery, POSIX termios for serial I/O, no
  external dependencies.
- `Source/MidiLog.{h,cpp}` is a tiny SPSC ring buffer used for
  audio-thread → GUI-thread MIDI event handoff.
- The plugin namespace is `hitnotedmx`. The plugin code (`HitNoteDmxAudioProcessor`,
  `HitNoteDmxAudioProcessorEditor`) is namespaced; the
  `createPluginFilter()` entry point JUCE expects sits at file scope
  and forwards into the namespace.
