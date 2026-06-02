# HitNoteDmx

MIDI-driven companion to [HitDmx](../hitdmx). A macOS VST3 plugin that
accepts MIDI input, translates incoming notes into chase / breathe /
sparkle / etc. lighting recipes at the audio rate, and writes the
resulting per-channel DMX values out to an **ENTTEC DMX USB Pro** via
the same self-contained driver that powers HitDmx.

The runtime is the same as HitDmx; the difference is what drives the
channels:

|                       | HitDmx                                                | HitNoteDmx                                                |
|-----------------------|-------------------------------------------------------|-----------------------------------------------------------|
| Input                 | Host parameter automation                              | MIDI notes + (still) host parameter automation            |
| Recipes               | None — channel value = parameter value                 | Recipes run on the audio thread; their output writes into the same 512 parameters |
| Use case              | DMXIS-style direct control, Live can automate any DMX channel | Show-time: composer writes MIDI clips, plugin runs the show |
| Co-existence          | Different plugin code, different bundle                | Different plugin code, different bundle                   |

The MIDI-note vocabulary mirrors the one defined by the Python
[hitmixmididmx](../hitmixmididmx) translator so the two stay
in sync — `lightmidi` renders a `.als` offline for cases where you'd
rather have frozen automation, and HitNoteDmx interprets the same notes
live in Live.

## Status

| Phase | What it does | State |
|-------|--------------|-------|
| Skeleton | Builds; accepts MIDI; logs note activity in the editor | **shipped** |
| Recipes | chase_up/down, ping_pong, snake, sine_wave, sparkle, breathe, sweep_up/down, strobe, kick_pulse, alt_swap | **shipped** (12 dynamics; more planned) |
| Composition | Mask intersection across utility / static / dynamic layers, primary/secondary palette routing, spot RGBW | **shipped** |
| Colour fade | Velocity-driven linear fade between palette colours (soft black note = slow fade-to-black) | **shipped** |
| Master dims | Automatable LED + spot master-dim params (MIDI-mappable) | **shipped** |
| Editor / preview | 3-pane editor (knobs + log + buttons / rig visualiser / reserved trigger pane); Standalone build for DAW-free testing | **shipped** |
| Show-time UX | Clickable test-trigger menu; save/restore of recipe-layer state | in progress |

The rig is the extended layout: **4 RGB bars × 18 pixels + 2 RGBW
spots = 228 DMX channels** (bars on DMX 1–216, spots on 217/223),
mirroring `HITMIX_EXTENDED_RIG` in `hitmixmididmx`.

## Building

Requires CMake 3.22+ and Xcode (or the Command Line Tools). JUCE is
fetched automatically via `FetchContent`. No other dependencies.

```
cmake -S . -B build -G Xcode
cmake --build build --config Release
```

The VST3 is at `build/HitNoteDmx_artefacts/Release/VST3/HitNoteDmx.vst3`,
and the build also copies it to `~/Library/Audio/Plug-Ins/VST3/`.

## Sanity-test in Live

After the plugin loads, you should be able to:

1. Add a fresh MIDI track in Live.
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

- `Source/EnttecProDmx.{h,cpp}` is a **verbatim copy** of the driver from
  `hitdmx`, with only the namespace renamed. The two should stay in
  sync; for now, fixes get applied to both. A future cleanup may pull
  this into a shared static library that both plugins link against.
- `Source/MidiLog.{h,cpp}` is a tiny SPSC ring buffer used for
  audio-thread → GUI-thread MIDI event handoff.
- The plugin namespace is `hitnotedmx`. The plugin code (`HitNoteDmxAudioProcessor`,
  `HitNoteDmxAudioProcessorEditor`) is namespaced; the
  `createPluginFilter()` entry point JUCE expects sits at file scope
  and forwards into the namespace.
