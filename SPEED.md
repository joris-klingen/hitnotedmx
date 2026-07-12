# Speed — how fast the looks move

Every animated look in HitNoteDmx runs off the song's tempo, so it always
stays in time with the music. How *fast* a look moves is set by three things:

1. its **built-in default speed** (different per group of looks),
2. **how hard you play the note** (velocity), and
3. the **Master Speed** note, which stretches or squeezes *everything* at once.

"Velocity" below just means how hard the note is played — soft = low, hard = high.

---

## 1. Default speeds, by group

| Group | What it does | Default speed |
|---|---|---|
| **Chases** | Moving heads / sweeps (Chase, Ripple, Diag, Snake…) | Fast — about one pass per beat |
| **Breathes** | Slow ambient shapes (Tide, Pond, Gyre, Bloom, Drift, Moon rise…) | Slow — **one cycle every 4 bars** |
| **Wild** | Energetic / chaotic (Bounce, Rain, Lightning, Pong…) | Set by velocity (see below) |
| **Multicolor** | Self-coloured looks (Fire, Ocean, Rainbow, Disco…) | Set by velocity (see below) |

The Breathes group is deliberately the slowest — it's meant to drift gently in
the background of quiet sections. A single Breathe now takes four bars to
complete one loop.

---

## 2. What velocity does — it depends on the group

Playing a note harder does **not** always mean "faster". It means different
things per group:

| Group | Harder note (higher velocity) = |
|---|---|
| **Chases** | **Longer trail** behind the moving head — not faster. Soft = a single pixel, hard = a long comet tail. |
| **Breathes** | **Fuller, cleaner** look. Soft notes dim the shape into a patchy wash; full velocity leaves it untouched. Speed is unchanged. |
| **Wild** | **Faster**, snapped to musical divisions — see the ladder below. |
| **Multicolor** | **Faster**, smoothly. Soft ≈ ⅕ speed, medium (around half-velocity) = normal, hard ≈ 2× speed. |

**Wild speed ladder** (snaps to note values so it stays locked to the beat):

| How hard you play | Speed |
|---|---|
| Very soft | one move per whole bar (slowest) |
| Soft | one per half-bar |
| Medium | one per beat |
| Hard | two per beat |
| Very hard | four per beat (fastest) |

**A few exceptions** (these ignore the velocity-speed rule):

- **Sparkle / Sparkle few** (in Wild) run continuously, like the Multicolor looks.
- **VU meter** and **Disco** (in Multicolor) are always locked to the beat; on
  those, velocity sets the *level / range*, not the speed.
- **Strobe** (in Wild): velocity sets the flash rate — from slow flashes at a
  soft touch up to the hardware's fastest strobe at full velocity.

---

## 3. Master Speed — one knob for everything

The very top note of the keyboard (the last "Master" tile, **Speed**) is a
global speed control. **Hold it down** and its velocity picks how fast *every*
look runs, all at once. It multiplies on top of each look's own speed, so the
relationships between looks stay intact — it just slows the whole show down or
speeds it up.

There are six steps. Play the Speed note softer for slower, harder for faster:

| How hard you hold Speed | Everything runs | A Breathe then loops in |
|---|---|---|
| Very soft | ¼ speed | **16 bars** (slowest, deepest) |
| Soft | ½ speed | 8 bars |
| Medium (default feel) | normal | 4 bars |
| Firm | 2× speed | 2 bars |
| Hard | 4× speed | 1 bar |
| Very hard | 8× speed | half a bar |

**Not holding the Speed note = normal speed** (the defaults in section 1).

> While Speed is held, the Chase and Wild looks stop using velocity for their
> trail / speed and instead use it to pick the colour (harder = primary colour,
> softer = secondary). The Master Speed note is doing the timing for them.

---

### Quick reference

- **Breathes** are the slow ones — 4 bars per loop by default, up to 16 bars at the slowest Master Speed.
- **Chases** velocity = trail length. **Breathes** velocity = fullness. **Wild / Multicolor** velocity = speed.
- **Master Speed** (top note): hold it, and its velocity scales the whole show from ¼× to 8×.
