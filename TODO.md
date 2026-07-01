# HitNoteDmx — open tasks

Open backlog, grouped by kind. Shipped work, architecture and the full
changelog live in [STATUS.md](STATUS.md). Item numbers are stable — other
entries (and code comments) cross-reference them, so removed items leave a
gap rather than renumbering.

## Tooling & tests

2. **Render tool — golden-image regression net + hitdesigndmx preview** — the
   render tool itself is **done**: `recipe-render` ships `strip` / `sheet` /
   `stats` (per-recipe previews) plus `clip` / `contact`, which **parse a `.mid`
   through the real `computeDmx`** — so MIDI clip input already works, and
   `recipe-range` (`tools/RecipeCheck.cpp`) guards finite / in-range output.
   Animated-GIF output was considered and **dropped** on purpose — a film strip
   shows every frame at once, which is what you want when judging a recipe.
   **Remaining (optional):** a **golden-image regression guard** — sample each
   recipe / trigger in isolation into **contact-sheet PNGs** + a manifest (note →
   image + metadata), emitted as a *versioned artifact* tied to
   `vocab::kMappingVersion` (same dump-and-consume contract as
   `mappings/v<N>.tsv`). It doubles as a CTest **diff guard** (a recipe's output
   changing trips a diff; optional motion / colour metrics) and as the
   hitdesigndmx trigger-preview source. Render rig-pixels-only / no text so font &
   anti-aliasing differences can't cause false diffs. Needs a **headless rig
   rasteriser factored out of `DmxVisualizer`** so preview, golden images and clip
   renders share one drawing path.

8. **Clip remigrator (`mapping-tool` subcommand)** — convert MIDI clips authored
   against an old mapping version to a newer one. **MIDI-note based, not
   chainName based:** carry an explicit per-transition note → note remap table
   (drop / keep / move), authored at each freeze. Name matching is too fragile
   here — versions routinely rename and *repurpose notes in place*, so a chainName
   diff silently loses them (the v7 → v8 remap is the case in point; see the v8
   changelog in STATUS.md). Lands as `convert --from v<N>`, reusing the frozen
   `mappings/v<N>.tsv` snapshots, and supersedes the *name*-based plan still
   described in `mappings/README.md`. Open: compose multi-step hops (v6→v8) vs one
   table per adjacent pair; dropped-note policy (warn / strip / leave); whether a
   dropped directional note auto-rewrites to its base + a Reverse / Flip note.

## Feature backlog — playable notes / behaviours

3. **Pixel density → dynamic soft shimmer (knob only)** — today density is a
   static hard gate: below 100% it blanks a fixed per-bar subset of pixels
   (avalanche-hash rank order) and the survivors keep full brightness — it gates
   on/off, never dims, and the thinned set never moves. **Rework it into a
   dynamic, slowly-drifting soft mask:** a smooth, slowly-moving raster/shimmer
   that gently *reduces* brightness (soft dimming, not a hard on/off gate) so
   density reads as a living texture instead of a fixed thinning. **Knob-only** —
   no playable note version; the automatable Pixel Density param stays the sole
   control, it just drives depth/coverage of the moving soft mask. Keep it
   flicker-free and cheap (still inside `computeDmx`, no allocations).

6. **Auto-program / hands-free mode** *(discuss first)* — for nights when there's
   no time to play the rig live. A **semi-active generative sequence** that cycles
   chase / block / breathe on its own — enough motion to feel alive, not a busy
   show. Plus a minimal fallback for the most rushed case: **solid single-colour
   mode** where the note **velocity picks the colour**. **Arm it on B-2 (pitch 11)**
   — the one free note in the Spots & bars octave. Open: how busy "semi-active"
   should be, whether it follows the beat clock, and how it yields the moment live
   triggers come in.

9. **MIDI panic / stuck-note recovery** — `processBlock` only handles note-on /
   note-off, so a dropped note-off (a MIDI hiccup, a clip edit mid-hold, a track
   disarmed while a note sounds) leaves that light **stuck on** with no automatic
   recovery — the only escape is the manual Blackout button. Handle the standard
   MIDI panic messages — **All Notes Off (CC 123)** and **All Sound Off (CC 120)**
   — by clearing `liveMidi` (leave the click-preview state alone), so a host/pedal
   panic clears stuck lights without killing the rig. Small and self-contained
   (`PluginProcessor.cpp` block loop). Optional extra: treat a long transport gap
   as a re-sync.

## Bigger / longer-term — rig flexibility

7a. **Parametric grid shape (bar count / pixels)** — today the geometry is fixed
    at 4 bars × 18 pixels: `Rig.h` hardcodes it, the bar/zone masks are 18-bit,
    and the visualiser draws exactly that. First step toward a flexible rig is to
    let the **grid shape vary** — e.g. 6 bars, or 4 bars at a different pixel
    count — driven from a small set of rig constants that `computeDmx`, the
    recipes, the masks and the visualiser all read, so a different bar/pixel count
    is a config change rather than edits in many files. (The mapping likely has to
    become grid-aware / re-versioned once the mask widths change.)

7b. **Full data-driven rig model** — the general case of 7a: a rig description
    (fixture types, counts, DMX start addresses, per-fixture pixel/channel layout,
    spot pairs) that every layer reads, so an arbitrary new rig — not just a
    different grid — is a config change, not a code change. Big, cross-cutting
    refactor with real show-compat implications (rig-aware mapping / re-versioning),
    so scope it deliberately when the rig actually needs to grow.

10. **Resizable / pop-out visualiser** — the editor is a fixed 1168×338 and the
    rig preview is sized to fit alongside the controls. Make the window resizable
    (or give the visualiser a detachable / full-window view) so it can scale up
    for front-of-house "is the rig doing what I think" glances. **Most valuable
    once the rig grows** (see 7a/7b) — a bigger grid needs more pixels on screen
    than the current fixed layout affords, so pair it with the rig-shape work.
