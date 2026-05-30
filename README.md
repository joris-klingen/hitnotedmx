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
| Skeleton | Builds; accepts MIDI; logs note activity in the editor | **shipped (this commit)** |
| Recipes | Implements chase_up, chase_down, ping_pong, …; writes to channel parameters | TODO |
| Composition | Mask intersection across utility / static / dynamic layers, primary/secondary palette routing | TODO |
| Show-time UX | Sensible "what's on right now" view, save/restore of the recipe layer state | TODO |

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
3. Open the editor — you should see the title, connect/disconnect/blackout
   buttons, the ENTTEC status line, and an empty MIDI log.
4. Play any MIDI note (computer keyboard works) → the log should print
   `[on ]  ch1  C3   vel=N` and `[off]  ch1  C3   vel=N`.

That confirms the audio thread is receiving MIDI and the lock-free
MidiLog ring is being drained by the GUI timer. No DMX is driven yet
in this commit.

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
