# HitNoteDmx — open tasks

Open backlog, renumbered. Detailed context for shipped work and
architecture lives in [STATUS.md](STATUS.md).

## Show prep

1. **Recipe-bank refinement (Disco + tuning pass)** — the recipes are tuned on
   hardware and read well. Remaining items:
   - **Disco — time it to the beat.** Today it free-runs; lock its colour
     switches / motion to the beat grid so it pulses in time with the song.
   - **VU-meter low-level feel:** only the *lowest* LED should dim at very low
     signal (today a fixed 2-pixel floor with a fast per-beat release).
   - **Eyeball the v7 recipe pass on hardware:** the new/changed recipes —
     Theater, Tide, Smooth shimmer, Stutter, Velvet, Rouge, plus the bloom /
     moon-rise / ripple-H / converge / desert / sunset / storm tweaks and the
     wider Speed-note slow end (0.125× = 8-beat chases) — were dialled on the
     preview; confirm on the bars and fine-tune the single constants in
     `Recipes.cpp`. (Blocks was retired in favour of Velvet.)
   Speeds, band widths, gaussian radii and hue ramps are single-constant tweaks
   in `Recipes.cpp`. See #2 for the automated quality net (`recipe-range`).

2. **Render tool — recipe quality net, clip preview, hitdesigndmx integration**
   — an offline render tool (sibling to `mapping-tool`) reusing the real
   `computeDmx` engine so it can never drift, with a **headless rig rasteriser
   factored out of `DmxVisualizer`** (one drawing path → preview, golden images
   and clip renders all match). *(The cheap half — a finite/in-range guard over
   recipes + `computeDmx` — already shipped as the `recipe-range` CTest
   (`tools/RecipeCheck.cpp`); what remains below is the **look** net: golden
   images + clip GIFs.)* Three uses:
   - **Vocabulary filmstrips (golden + preview)** — sample each recipe/trigger in
     isolation → **contact-sheet PNGs** + a manifest (note → image + metadata),
     emitted as a *versioned artifact* tied to `vocab::kMappingVersion` (same
     dump-and-consume contract as `mappings/v<N>.tsv`). Doubles as a CTest
     **golden-image regression guard** (a recipe's output changing trips a diff;
     optional motion / colour metrics: frame-to-frame delta, coverage, hue
     spread; optional **LLM-as-judge** for fuzzy triage) and as the hitdesigndmx
     trigger-preview source.
   - **Clip render, on demand (the design tool calls it)** — a CLI such as
     `render-clip <in.mid> --out <out.gif|out_dir> [--fps N] [--bpm N]`: parse the
     `.mid` (`juce::MidiFile`), drive `MidiState` + the beat clock over the clip's
     duration, rasterise per frame, and emit an **animated GIF** (full motion —
     best for a temporal clip) and/or a filmstrip contact sheet. Lets hitdesigndmx
     preview *designed clips* and converted legacy sets without Live or hardware.
     Deterministic (recipes are beat-time + hash driven), so renders reproduce.
     GIF via a small vendored public-domain encoder, or emit a PNG frame-sequence
     and let the Python side assemble the GIF with Pillow.
   - **Why a file/CLI contract, not shared code** — repos differ in language
     (C++ vs Python) and hitdesigndmx already hand-mirrors the mapping
     (`vocab/hitnote_v1.py`) for conversion but can't reproduce the recipe
     visuals; the authoritative C++ engine renders once, the Python tool consumes
     the result. **Pin before either side depends on it:** the CLI signature, the
     manifest schema, and the image/GIF layout.

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
   - Confirm the scatter looks right on the physical bars; decide the default.

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

## Recently shipped (see STATUS.md for detail)

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
  Closes old TODO #1–#4.
- **Pixel-density diagonal-bias fix** — the linear position hash dropped
  pixels in diagonal stripes (per-bar lit counts 9/7/4/1 at 25%);
  replaced with a murmur3-finalizer avalanche hash rank-normalised per
  bar: random drop order, exact per-bar proportionality, still
  flicker-free and monotonic.
- Removed the dead `isPrimaryColorPitch` / `isSecondaryColorPitch`
  helpers in `Composition.cpp` (old TODO #6).
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
