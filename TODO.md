# HitNoteDmx — open tasks

Open backlog, grouped by kind. Shipped work, architecture and the full
changelog live in [STATUS.md](STATUS.md). Item numbers are stable — other
entries (and code comments) cross-reference them.

## Show prep — recipe tuning

1. **Recipe-bank refinement** — the **v9 pass shipped** (see the Changelog in
   STATUS.md): new **Comets** + **Rotor** chases, **Waterfalls** (was Stutter),
   **Pong** moved into Wild, **Burst**/**Glow** renames + tuning, **Disco**
   beat-lock, and the **VU-meter / Moon rise / Tide** tuning — all behind the one
   v9 freeze. Single-constant tweaks (speeds, band widths, gaussian radii, hue
   ramps) live in `Recipes.cpp`; see #2 for the automated quality net
   (`recipe-range`). **Remaining:**
   - **Eyeball the v9 pass on the bars** and fine-tune the single constants —
     especially Radar (now a thin rotating spoke — confirm it reads as a radar
     sweep on the bars vs the stretched grid) and Waterfalls (reads a touch
     sparkly — smooth toward a continuous sheet if wanted).

## Tooling & tests

2. **Render tool — recipe quality net, clip preview, hitdesigndmx integration**
   — an offline render tool (sibling to `mapping-tool`) reusing the real
   `computeDmx` engine so it can never drift, with a **headless rig rasteriser
   factored out of `DmxVisualizer`** (one drawing path → preview, golden images
   and clip renders all match). *(The cheap half — a finite/in-range guard over
   recipes + `computeDmx` — already shipped as the `recipe-range` CTest
   (`tools/RecipeCheck.cpp`); the static film-strip / contact-sheet previews +
   `.mid` clip render shipped as `recipe-render` (`tools/RecipeRender.cpp`,
   modes `strip`/`sheet`/`stats`/`clip`/`contact`). What remains below is the
   **look** net: golden images + animated GIFs.)* Three uses:
   - **Vocabulary filmstrips (golden + preview)** — sample each recipe/trigger in
     isolation → **contact-sheet PNGs** + a manifest (note → image + metadata),
     emitted as a *versioned artifact* tied to `vocab::kMappingVersion` (same
     dump-and-consume contract as `mappings/v<N>.tsv`). Doubles as a CTest
     **golden-image regression guard** (a recipe's output changing trips a diff;
     optional motion / colour metrics: frame-to-frame delta, coverage, hue
     spread; optional **LLM-as-judge** for fuzzy triage) and as the hitdesigndmx
     trigger-preview source. (Render rig-pixels-only / no text labels so font &
     anti-aliasing differences can't cause false diffs.)
   - **Clip render → animated GIF** — `recipe-render clip` already renders a
     `.mid` as a static film strip; add a true **animated GIF** (full motion —
     best for a temporal clip) so hitdesigndmx can preview *designed clips* and
     converted legacy sets as they actually move, without Live or hardware.
     Deterministic (recipes are beat-time + hash driven), so renders reproduce.
     GIF via a small vendored public-domain encoder, or emit a PNG frame-sequence
     and let the Python side assemble the GIF with Pillow.
   - **Why a file/CLI contract, not shared code** — repos differ in language
     (C++ vs Python) and hitdesigndmx already hand-mirrors the mapping
     (`vocab/hitnote_v1.py`) for conversion but can't reproduce the recipe
     visuals; the authoritative C++ engine renders once, the Python tool consumes
     the result. **Pin before either side depends on it:** the CLI signature, the
     manifest schema, and the image/GIF layout.

8. **Clip remigrator (`mapping-tool` subcommand)** — convert MIDI clips authored
   against an old mapping version to a newer one. **MIDI-note based, not
   chainName based:** carry an explicit per-transition note → note remap table
   (drop / keep / move), authored at each freeze. Name-based matching is too
   fragile here — versions routinely rename and *repurpose notes in place*, so a
   chainName diff silently loses them. The v7 → v8 remap is the case in point:
   122 (To black) → 10, 123 (From black) → 9, while 122/123/125 were *reused* for
   Crossfade / free / Reverse and 120/121/24/27 were renamed in place
   (Bump white→Bump, Bump color→Release, Chase up→Chase, Diag up→Diag) — none of
   which a name match could follow. Lands as the `convert --from v<N>` subcommand
   in `mappings/README.md` (whose current text still describes the *name*-based
   plan — supersede it), reusing the frozen `mappings/v<N>.tsv` snapshots. Open:
   compose multi-step hops (v6→v8) vs one table per adjacent pair; what to do
   with dropped notes (warn / strip / leave); whether a dropped directional note
   (e.g. old Chase dn) auto-rewrites to its base + a Reverse note.

## Feature backlog — playable notes / behaviours

3. **Pixel-density: re-roll + soft-edges note** *(later)* — the density gate
   drops pixels in a per-bar-even random order (avalanche hash, rank-normalised
   per bar; the old diagonal banding is fixed). Open work:
   - **Re-roll the thinned subset on each colour-note trigger** so the dropped
     pixels change with the colour instead of staying fixed all show (more
     life); reseed the per-bar rank from the winning colour note.
   - **Expose density + a new "soft edges (2D)" feather as playable notes**
     (velocity = intensity), alongside the existing automatable density knob.
     Placement note: octave 8 is now full of master controls (120–125 + 127
     Speed); only F#8 (126) is free there, so these need a home — 126, or
     another octave.

4. **Chase blend mode — "on top" vs mask (toggle note, top octave)** *(later)* —
   today a held brightness dynamic (chase/breathe/wild) acts as a **mask**: lit
   pixels are the *intersection* of the bar/zone/dynamic layers, so a chase
   carves its moving shape OUT of the colour wash (the dynamic value multiplies
   the per-pixel brightness in `computeDmx`'s bar compose). Add a **playable
   toggle note** (a free note — see #3 on placement; octave 8 is now master
   controls) that switches the dynamic layer to **additive / on-top**: the
   base wash stays full and the chase *adds* its lit pixels over it instead of
   gating them (a `max`/add at the dynamic-mask step instead of the multiply).
   Default stays mask — the current behaviour. Useful with the layering workflow
   (multicolor wash + a chase riding on top without darkening the rest).

5. **Crossfade control note** *(discuss implementation first)* — a note that
   crossfades (primary↔secondary, or wash↔dynamic — semantics TBD) and how it
   composes with the existing layers. Decide note placement (octave 8 is nearly
   full — see #3), velocity meaning, and compositing before building. (The
   speed half of this idea shipped as the G8 global-speed note.)

6. **Auto-program / hands-free mode** *(discuss first)* — for nights when there's
   no time to play the rig live. A **semi-active generative sequence** that cycles
   chase / block / breathe on its own — enough motion to feel alive, not a busy
   show. Plus a minimal fallback for the most rushed case: **solid single-colour
   mode** where the note **velocity picks the colour**. Open: what arms it (a
   master note / a button), how busy "semi-active" should be, whether it follows
   the beat clock, and how it yields the moment live triggers come in.

## Bigger / longer-term

7. **Abstract the rig so it can change (add fixtures)** — today the rig is
   hardcoded everywhere: `Rig.h` fixes 4 bars × 18 pixels + 2 spots and the
   228-channel patch, the note vocabulary and recipes assume exactly that
   geometry (bar/zone masks are 18-bit, spots are a fixed pair), and the
   visualiser draws that exact layout. Adding or changing fixtures means edits
   in many places. At some point elevate to a **data-driven rig model** — a rig
   description (fixture types, counts, DMX start addresses, per-fixture
   pixel/channel layout) that `computeDmx`, the recipes, the vocabulary and the
   visualiser all read from, so a new rig is a config change, not a code change.
   Big, cross-cutting refactor with real show-compat implications (the mapping
   would likely need to become rig-aware / re-versioned), so it's not show-prep
   — scope it deliberately when the rig actually needs to grow.
