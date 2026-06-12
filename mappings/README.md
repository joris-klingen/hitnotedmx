# Mapping snapshots

Frozen snapshots of the trigger note mapping, one per version. Each
`v<N>.tsv` is 128 rows of `note<TAB>chainName` — the meaning of every MIDI
note at mapping version `N`.

The current version is `vocab::kMappingVersion` in
[`Source/TriggerVocabulary.h`](../Source/TriggerVocabulary.h). The chain name
(`bk Blackout`, `wd Strobe`, `cp Red`, `-` for unused …) is a **stable
semantic key**: it identifies what a note *does*, independent of which note it
sits on. That's what makes clip migration possible — a converter maps an old
note → its chainName → the new note carrying that same chainName, so note
moves and recolours are handled with no special-casing.

## Why this exists

Clips authored against one mapping break when a later version changes what a
note means. To migrate them you need the *old* map to diff against the new
one. The plugin code only knows "current", so each version is frozen here as
data the migration tool can load directly (no checking out old source).

## Freeze procedure — when you change a note's MEANING

A "meaning change" = a trigger moves to a different note, a colour is
recoloured, or a label is repurposed. (Pure additions to unused notes don't
strictly need a bump, but bumping is cheap and unambiguous.)

1. Bump `kMappingVersion` in `Source/TriggerVocabulary.h` (e.g. 1 → 2).
2. Rebuild and freeze the new snapshot:
   ```sh
   cmake --build build --target mapping-tool
   ./build/mapping-tool_artefacts/mapping-tool dump > mappings/v2.tsv
   ```
3. Commit the new `mappings/v<N>.tsv` alongside the vocabulary change.

The `mapping-frozen` CTest (`mapping-tool check mappings`) fails if the live
vocabulary doesn't match the snapshot for the current version — so a mapping
change you forgot to freeze (or made by accident) is caught at test time.

## Tool

`mapping-tool` (built from `tools/MappingTool.cpp`, links the shared
`TriggerVocabulary`):

| command | does |
|---|---|
| `dump` | print the current snapshot to stdout |
| `version` | print `kMappingVersion` |
| `check <dir>` | compare the live map to `<dir>/v<version>.tsv`; non-zero on drift |

The clip migration converter (`convert --from v<N>`) and the legacy
RGB-automation importer will land here as further subcommands.
