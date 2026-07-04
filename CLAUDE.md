# HitNoteDmx

MIDI-notes → DMX VST3 + Standalone (JUCE 8, macOS). **The backbone of the
hitdmx family** (sibling repos in `../`: hitlaunchdmx, hitccdmx,
hitdesigndmx) — it works well and is show-critical, so prefer small,
verified changes over restructuring.

- Architecture, per-layer state, and the full development log: [STATUS.md](STATUS.md)
- Open backlog (show-prep priorities first): [TODO.md](TODO.md)
- Note-mapping freeze/versioning procedure: [mappings/README.md](mappings/README.md)

## Finding things

When you need "where is X / what is X", read the one file that *owns* it
rather than reconstructing the answer from constants spread across files:

- **Concern → file** — the per-layer table in [STATUS.md](STATUS.md) maps every
  layer (rig, palette, recipes, driver, editor …) to its file(s). Start there.
- **What a note does / which notes are free** — [`mappings/v12.tsv`](mappings/v12.tsv),
  the current frozen 128-row `note → meaning` snapshot (highest `v<N>`; `-` = unused).
  Read it instead of tracing the `kXxxStart` note-range constants by hand.
- **Note → label / menu / rack-name logic** — `Source/TriggerVocabulary.{h,cpp}`,
  the single source of truth that both the menu and rack namer read.

## Invariants

- `Source/TriggerVocabulary.{h,cpp}` is the single source of truth for the
  note→name map; menu and rack namer both read it. Never hardcode a note
  number elsewhere.
- A change to what a note *means* requires the mapping freeze procedure
  (bump `vocab::kMappingVersion`, dump `mappings/v<N>.tsv` with
  `mapping-tool`). The `mapping-frozen` CTest catches forgotten freezes.
- `computeDmx()` runs on the audio thread: no allocations, no locks.
- Always build both targets (plain `cmake --build build` does) so the VST3
  and Standalone never diverge.

## Build & verify

```sh
cmake -S . -B build
cmake --build build -j8
ctest --test-dir build
```

## Releasing to the show machine

The show machine installs from the Dropbox folder
`~/Library/CloudStorage/Dropbox/Music/Hitmix/9_tech/hitnotedmx_installer`
(payload: `HitNoteDmx.vst3` + `HitNoteDmx.app` + `install.command`; script
source lives in [installer/install.command](installer/install.command)).
**Refresh that folder only on request** — "release to the show machine" =
build + `ctest`, then copy the two artefacts and the script into it. On the
target machine, double-clicking `install.command` copies the plugin to
`~/Library/Audio/Plug-Ins/VST3` and the standalone to `/Applications`, and
strips the quarantine flag (the binaries are ad-hoc signed).
