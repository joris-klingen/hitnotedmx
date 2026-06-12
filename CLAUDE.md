# HitNoteDmx

MIDI-notes → DMX VST3 + Standalone (JUCE 8, macOS). **The backbone of the
hitdmx family** (sibling repos in `../`: hitlaunchdmx, hitccdmx,
hitdesigndmx) — it works well and is show-critical, so prefer small,
verified changes over restructuring.

- Architecture, per-layer state, and the full development log: [STATUS.md](STATUS.md)
- Open backlog (show-prep priorities first): [TODO.md](TODO.md)
- Note-mapping freeze/versioning procedure: [mappings/README.md](mappings/README.md)

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
