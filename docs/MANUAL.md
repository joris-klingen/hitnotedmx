# HitNoteDmx — manual for Ableton users

HitNoteDmx is an **instrument that plays lights**. Put it on a MIDI track, and
every note you hold switches on a *layer* of the light show — which bars,
which pixels, what motion, what colour. Held notes combine, and **velocity is
the parameter** of each layer. The DMX goes out live through an ENTTEC DMX
USB Pro; there is no render step. If you can draw a clip in Live, you can
program a light show.

> Note names follow Ableton: **C3 = MIDI 60**, so the bottom note is C-2 (0)
> and the top is G8 (127). The full frozen note map is
> [`mappings/v13.tsv`](../mappings/v13.tsv).

---

## 1. Setup in Live

1. **Install.** Double-click `install.command` from the installer folder: it
   copies `HitNoteDmx.vst3` to `~/Library/Audio/Plug-Ins/VST3` and
   `HitNoteDmx.app` (Standalone) to `/Applications`. In Live: *Settings →
   Plug-Ins → Rescan*.
2. **Load it as an instrument.** It's in the browser under *Plug-Ins* and is
   an **instrument** (it outputs silent audio), so drop it on a **MIDI track**.
3. **Connect the ENTTEC.** Click **Connect USB**; the status line shows the
   widget. Only one HitNoteDmx instance (or app) can own the ENTTEC at a time —
   a second one reports *busy*. The link reconnects by itself after a cable
   wiggle.
4. **Get note names in the piano roll.** Click **Init. names**: this installs
   the *Hitnotenames* MIDI Effect Rack into your User Library (*MIDI
   Effects → MIDI Effect Rack*). Drop it **in front of** HitNoteDmx and the
   piano roll shows every trigger's name.
5. **Demo clips.** Click the folder button (**Show clips**): layered example
   clips are written to `~/Music/HitNoteDmx Showcase/` and opened in Finder.
   Drag any onto the track.
6. **Grid.** The default rig is **4 bars × 18 pixels** plus 2 RGBW spots. Set
   another shape (up to 8 bars, 32 pixels) in the grid fields + **Set grid**.
   DMX patch: spot L = ch 1–6, spot R = 7–12, bars from ch 13 (3 ch/pixel,
   bar after bar, pixel 1 at the bottom).

**Timing.** Everything is locked to Live's tempo and song position. With the
transport stopped the looks keep moving on a free-running clock, and on
transport start the motion re-locks to the song, so a loop always restarts the
same way.

---

## 2. The keyboard

Every section starts on a C, so each one is one octave of the piano roll.

| Notes | Section | What's there (C → B) |
|---|---|---|
| **C-2** (0) | Blackout | Kills everything, spots included |
| C#-2 – E-2 (1–4) | Spots | Spot L WW · Spot L col · Spot R WW · Spot R col |
| F-2 – G#-2 (5–8) | Bars | Left · Mid left · Mid right · Right |
| A-2, A#-2 (9, 10) | Fades | From black · To black |
| **C-1** (12–23) | Zones | Zone 1–9 (bottom → top) · Even · Odd · Thirds |
| **C0** (24–35) | Chases | Chase · Ripple · Ping-pong · Diag · Radar · Snake · Theater · Spiral · Waves · Expand · Contract · Chase H |
| **C1** (36–47) | Breathes | Tide · Sine · Pond · Gyre · Bloom · Halo · Moon rise · Soft ball · Drift · Aurora · Shimmer · Glow |
| **C2** (48–59) | Wild | Strobe · Sparkle · Sparkle few · Lightning · Glitch · Fountain · Rain · Waterfalls · Bounce · Fast ball · Pong · Burst |
| **C3–B4** (60–83) | Multicolor | Rainbow · Comet · VU meter · VU smooth · Fire · Embers · Magma · Lava · Heatmap · Ocean · Forest · Desert · Sunset · Twilight · Borealis · Night sky · Galaxy · Nebula · Storm · Plasma · Police · Disco · Velvet · Rouge |
| **C5–B6** (84–107) | Primary colours | Black · Red · Orange-red · Orange · Amber · Yellow · Lime · Green · Mint · Teal · Cyan · Sky · Blue · Royal · Indigo · Violet · Purple · Magenta · Pink · Hot pink · Crimson · Warm white · Cool white · Lavender |
| **C7** (108–119) | Secondary colours | Warm white · Coral · Gold · Chartreuse · Jade · Aqua · Azure · Periwinkle · Lavender · Orchid · Rose · Cool white |
| **C8–G8** (120–127) | Master | Bump · Release · Crossfade · Freeze · Reverse · Flip · Spread · Speed |

The **bar selectors are positional**: on a 4-bar rig they're simply bars 1–4.
On wider grids *Left* and *Right* own the outer bar(s) and the two *Mid*
selectors share the rest.

---

## 3. How notes combine

Think of three **mask layers** plus a **colour**:

```
lit pixels  =  Bars  ∩  Zones  ∩  Motion          (a layer with no note held = everything)
colour      =  primary colour, or the secondary colour where a zone asks for it
```

- **Bars** and **Zones** say *where*. Several bars or zones together = their
  union. Bars ∩ zones = the intersection: *Left + Right + Zone 1–3* lights the
  bottom of the two outer bars.
- **Motion** (Chases, Breathes, Wild) says *how it moves*. Several motions
  together are layered (the brightest wins per pixel).
- **Colour.** The latest-held primary colour wins. A **zone's velocity** picks
  which palette its pixels use: **≥ 64 → primary, < 64 → secondary**.
- **No colour held = white.** Bars, zones or a motion on their own play white,
  so you can sketch motion first and colour it later.
- **Multicolor** looks bring their own colour: while one is held it replaces the
  palette on the lit pixels (masks and motion still apply on top).
- **Black** (C5) is a real colour: a soft *Black* note fades the look out.
- **Spots** are separate RGBW fixtures: *WW* = warm white, *col* = the current
  **secondary** colour.

**Example clip** — a red chase climbing the two outer bars, with a soft cyan
accent at the top:

| Note | Length | Velocity | Meaning |
|---|---|---|---|
| F-2 *Left*, G#-2 *Right* | whole clip | 127 | outer bars, full brightness |
| C0 *Chase* | whole clip | 90 | chase with a medium tail |
| C#5 *Red* | whole clip | 127 | primary = red, instant |
| G#-1 *Zone 9* | whole clip | 40 | top zone uses the secondary colour |
| F7 *Aqua* | whole clip | 127 | secondary = aqua |

---

## 4. Velocity — what it does per layer

| Layer | Velocity sets |
|---|---|
| **Bars** | brightness ceiling of those bars (soft = dim) |
| **Zones** | colour route: ≥ 64 primary, < 64 secondary |
| **Chases** | **tail length** (soft = single-pixel head, hard = long comet). Never the speed. |
| **Breathes** | **depth**: 127 = the clean shape, softer = dimmed into a drifting patchy wash |
| **Wild** | **speed, on the beat grid**: very soft = 1 per bar … very hard = 4 per beat. *Sparkle / Sparkle few* run free; **Strobe** = flash rate 1–20 Hz |
| **Multicolor** | **speed**, smooth: soft ≈ ⅕×, mid = normal, hard ≈ 2×. *VU meter* and *Disco* stay on the beat; velocity = level |
| **Palette colours** | **fade-in time**: hard = instant, soft = up to 3 s glide. Brightness is never velocity |
| **From / To black** | fade time: 127 = instant … 1 = one bar. *From black* at velocity **1** = hold black |
| **Master notes** | see below |

Default speeds: **chases ~one pass per beat**, **breathes one cycle per 4
bars**. Spots, Blackout, Freeze, Reverse and Flip ignore velocity.

---

## 5. Master notes (C8 – G8)

Whole-rig controls. They also sit in the editor's **master grid** (left pane):
*Bump* is momentary there, the rest latch.

| Note | Name | Behaviour | Velocity |
|---|---|---|---|
| C8 (120) | **Bump** | Flash toward white (or the primary colour), fires on the **note start** and decays by itself — length doesn't matter | brightness |
| C#8 (121) | **Release** | Sets Bump's decay | 1/16 note (soft) … 1 bar (hard); without it 1/8 note |
| D8 (122) | **Crossfade** | Look changes on the bars glide instead of snapping (long values also motion-blur) | fade length 1/16 note … 1 bar |
| D#8 (123) | **Freeze** | Holds the current frame and pauses the motion; release continues seamlessly. Bump still punches through | – |
| E8 (124) | **Reverse** | Chases/breathes run backwards from where they are | – |
| F8 (125) | **Flip** | Mirrors direction instantly (up ↔ down, left ↔ right) | – |
| F#8 (126) | **Spread** | De-syncs the bars from each other | offset 0 … 1 beat |
| G8 (127) | **Speed** | Global speed for every look | ladder below |

**Speed ladder** (powers of two, so everything stays on the bar grid):

| Velocity | 1–14 | 15–28 | 29–42 | 43–56 | 57–70 | 71–127 |
|---|---|---|---|---|---|---|
| Speed | ¼× | ½× | 1× | 2× | 4× | 8× |
| A breathe loops in | 16 bars | 8 bars | 4 bars | 2 bars | 1 bar | ½ bar |

Not holding *Speed* = 1×. While Speed is held, **Wild** velocity stops
setting speed and picks the colour route instead (≥ 64 primary). Chases keep
velocity = tail.

---

## 6. The editor

- **Left pane:** **LED DIM**, **SPOT DIM** and **DENSITY** knobs; a MIDI log;
  **Connect USB / Disconnect**, **Blackout**, **Init. names**; the grid fields
  + **Set grid**, with a **per-bar dim** cell (1–10) under it for each bar.
- **Centre:** the rig preview, mirroring the DMX output. Cells that are
  selected but have nothing lighting them show as grey outlines.
- **Right:** the **trigger menu**, laid out like Live's piano roll (one column
  per section, C at the bottom, black keys shaded). **Click tiles to latch and
  audition** them; latched tiles combine like held notes. Tiles light up when
  their note is playing.
- **Far right:** **VEL**, the velocity for clicked tiles; the **drag MIDI**
  tile, which drags the latched set out as a one-bar clip (design a look by
  clicking, then drop it into Live); **Show clips**.

The three knobs are the plugin's **only automatable parameters**. Automate them
or MIDI-map them in Live like any device parameter. **DENSITY** thins out the
lit pixels in a fixed random order (for dark rooms) without dimming the
survivors.

---

## 7. Working tips

- **One look = one clip.** Hold the structural notes (bars, zones, colour,
  motion) for the whole clip. Use short notes only for accents (Bump, Strobe).
- **Edit velocity, not more notes.** Tail, depth, speed, colour route and fades
  all live in the velocity lane.
- **Split layers over tracks.** Put colour, motion and accents on separate MIDI
  tracks and route them into the HitNoteDmx track (*MIDI To* → the HitNoteDmx
  track, and set that track's *Monitor* to *In*). Session clips then mix looks
  live.
- **Smooth scene changes:** hold **Crossfade** across the change, or use soft
  palette velocities so colours glide.
- **Endings:** *To black* (or a soft *Black* colour note) fades out; **C-2
  Blackout** is the hard kill.
- **Standalone app:** the same engine with no DAW, useful for designing looks
  with the trigger menu and checking the rig.
