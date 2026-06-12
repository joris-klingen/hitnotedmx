#!/usr/bin/env python3
"""Name the chains of the HitNote DMX trigger rack (.adg) from the live vocabulary.

The Ableton MIDI Effect Rack `Hitnotenames.adg` has 128 chains — one per MIDI
note 0..127 — each gated to a single key. This tool reads the trigger
vocabulary straight out of the plugin source (the SAME labels the on-screen
trigger menu draws) and writes each chain's name to match the note it listens
for, so the rack in Ableton mirrors HitNote DMX exactly.

Source of truth (no hand-kept copy here — parsed at run time):
  * Source/Recipes.h  — bank pitch-start constants (Chases/Breathes/Wild/Multicolor)
  * Source/Palette.h  — palette pitch starts + the 24 colour names
  * Source/TriggerMenu.cpp::buildModel() — the per-tile label lists

A chain is matched to a name by its ACTUAL key zone (ZoneSettings/KeyRange/Min),
not by its position in the file, so reordered banks still land correctly. Notes
with no trigger (the gaps at 9..11 and 120..127) are left blank.

Edits are surgical: only the `<Name Value="..."/>` strings change; every other
byte of the XML is preserved, then re-gzipped to a valid .adg. A .bak copy of
the original is written next to the target before it is overwritten.

Usage:
    python3 tools/name_rack.py [path/to/Rack.adg] [--out OTHER.adg] [--dry-run]

With no path it defaults to the user's installed preset.
"""
from __future__ import annotations

import argparse
import gzip
import html
import re
import shutil
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "Source"

DEFAULT_ADG = Path.home() / (
    "Music/Ableton/User Library/Presets/MIDI Effects/"
    "MIDI Effect Rack/Hitnotenames.adg"
)


# --------------------------------------------------------------------------- #
#  Parse the plugin source for the note -> name vocabulary
# --------------------------------------------------------------------------- #
def parse_int_constants(*files: Path) -> dict[str, int]:
    """Collect `inline constexpr int kFoo = 123;` definitions from headers."""
    consts: dict[str, int] = {}
    pat = re.compile(r"\bconstexpr\s+int\s+(k\w+)\s*=\s*([^;]+);")
    for f in files:
        for name, expr in pat.findall(f.read_text()):
            consts[name] = _eval_int(expr, consts)
    return consts


def _eval_int(expr: str, consts: dict[str, int]) -> int:
    """Evaluate a tiny C++ int expression made of literals, known ks, + - * /."""
    expr = expr.split("//")[0].strip()

    def repl(m: re.Match) -> str:
        k = m.group(0)
        return str(consts[k]) if k in consts else k

    expr = re.sub(r"k\w+", repl, expr)
    if not re.fullmatch(r"[\d\s+\-*/()]+", expr):
        raise ValueError(f"unsafe/unknown constant expression: {expr!r}")
    return int(eval(expr))  # noqa: S307 — input restricted to the regex above


def parse_palette_names(palette_h: Path, marker: str) -> list[str]:
    """Pull the colour names from a palette table's trailing comments.

    `marker` is the array opener, e.g. "kPalette {{" or "kSecondaryPalette {{".
    Each row looks like:  `{ 1.000f, 0.000f, 0.000f },  //  1  Red`
    """
    text = palette_h.read_text()
    start = text.index(marker)
    body = text[start : start + text[start:].index("}}")]  # just this table
    names: list[str] = []
    for m in re.finditer(r"//[ \t]*\d+[ \t]+([^\n]+)", body):  # one row, one line
        name = m.group(1).strip()
        # Drop a parenthetical qualifier like "Warm white (palette)".
        name = re.sub(r"\s*\(.*?\)\s*$", "", name).strip()
        names.append(name)
    return names


# Short 2-letter prefix per group, so the named chains cluster and read
# clearly in Ableton (e.g. "cp Red" vs "cs Red" vs "mc Rainbow").
COLUMN_PREFIX = {
    "Zones": "pz",
    "Chases": "ch",
    "Breathes": "br",
    "Wild": "wd",
    "Multicolor": "mc",
}


def _prefix(column_title: str, label: str) -> str:
    """The group prefix for a tile, given its column title and label."""
    if column_title == "Spots & bars":  # one column mixes blackout / spots / bars
        if label == "Blackout":
            return "bk"
        if label.startswith("Spot"):
            return "sp"
        if label.startswith("Bar"):
            return "ba"
        return ""
    return COLUMN_PREFIX.get(column_title, "")


def parse_label_columns(menu_cpp: Path, consts: dict[str, int]) -> dict[int, str]:
    """Extract trigCol(...) calls from buildModel and map note -> prefixed label."""
    text = menu_cpp.read_text()
    body = text[text.index("buildModel") :]
    notes: dict[int, str] = {}

    # trigCol ("Title", <octaveStartExpr>, { "a", "b", ... })
    call = re.compile(
        r"trigCol\s*\(\s*\"([^\"]*)\"\s*,\s*([^,]+?)\s*,\s*\{(.*?)\}\s*\)",
        re.DOTALL,
    )
    for title, start_expr, labels_blob in call.findall(body):
        start = _eval_int(start_expr, consts)
        labels = re.findall(r'"((?:[^"\\]|\\.)*)"', labels_blob)
        for i, lab in enumerate(labels):
            lab = _unescape_cpp(lab)
            pre = _prefix(title, lab)
            notes[start + i] = f"{pre} {lab}" if pre else lab
    return notes


def _unescape_cpp(s: str) -> str:
    return s.replace('\\"', '"').replace("\\\\", "\\")


def build_note_names() -> dict[int, str]:
    """Full note -> chain-name map, mirroring the trigger menu vocabulary."""
    consts = parse_int_constants(SRC / "Recipes.h", SRC / "Palette.h")
    # Locals that TriggerMenu.cpp defines for its own columns.
    consts.setdefault("kSpotBarOctave", 0)
    consts.setdefault("kPixelStaticStart", 12)

    names = parse_label_columns(SRC / "TriggerMenu.cpp", consts)

    # Palette columns: colour names, prefixed "cp" (primary) / "cs" (secondary)
    # so the two clusters stay distinguishable in Ableton's list. The two
    # palettes are separate tables (the secondary is its own accent set).
    primary = parse_palette_names(SRC / "Palette.h", "kPalette {{")
    secondary = parse_palette_names(SRC / "Palette.h", "kSecondaryPalette {{")
    prim = consts["kPrimaryPaletteStart"]
    sec = consts["kSecondaryPaletteStart"]
    sec_size = consts.get("kSecondaryPaletteSize", 12)
    for o in range(len(primary)):  # primary spans all 24 colours
        names[prim + o] = f"cp {primary[o]}"
    for o in range(min(sec_size, len(secondary))):
        names[sec + o] = f"cs {secondary[o]}"

    return names


# --------------------------------------------------------------------------- #
#  Patch the .adg
# --------------------------------------------------------------------------- #
BRANCH_RE = re.compile(r"<MidiEffectBranchPreset\b.*?</MidiEffectBranchPreset>", re.DOTALL)
NAME_RE = re.compile(r'(<Name Value=")(.*?)("\s*/>)')
KEYMIN_RE = re.compile(r"<KeyRange>\s*<Min Value=\"(\d+)\"")

# Unused notes get a hyphen rather than an empty name — Ableton renders an
# empty chain name as its default "Chain" label, so a "-" reads as "unused".
UNUSED_NAME = "-"


def patch_xml(xml: str, names: dict[int, str]) -> tuple[str, list[tuple[int, str]]]:
    """Set each branch's Name from its KeyRange.Min. Returns (xml, applied)."""
    applied: list[tuple[int, str]] = []

    def fix_branch(bm: re.Match) -> str:
        block = bm.group(0)
        km = KEYMIN_RE.search(block)
        if not km:
            return block
        note = int(km.group(1))
        label = names.get(note, UNUSED_NAME)
        applied.append((note, label))

        def set_name(nm: re.Match) -> str:
            return nm.group(1) + html.escape(label, quote=True) + nm.group(3)

        return NAME_RE.sub(set_name, block, count=1)

    return BRANCH_RE.sub(fix_branch, xml), applied


# --------------------------------------------------------------------------- #
def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("adg", nargs="?", type=Path, default=DEFAULT_ADG, help="rack .adg to name (default: installed preset)")
    ap.add_argument("--out", type=Path, help="write here instead of overwriting in place")
    ap.add_argument("--dry-run", action="store_true", help="print the mapping, write nothing")
    args = ap.parse_args()

    if not args.adg.exists():
        print(f"error: {args.adg} not found", file=sys.stderr)
        return 1

    names = build_note_names()
    xml = gzip.decompress(args.adg.read_bytes()).decode("utf-8")
    patched, applied = patch_xml(xml, names)

    applied.sort()
    named = [(n, l) for n, l in applied if l and l != UNUSED_NAME]
    print(f"{len(applied)} chains, {len(named)} named, {len(applied) - len(named)} unused:\n")
    for note, label in applied:
        print(f"  note {note:>3}  {label if label != UNUSED_NAME else '·'}")

    if args.dry_run:
        print("\n(dry run — nothing written)")
        return 0

    out = args.out or args.adg
    if out == args.adg:
        bak = args.adg.with_suffix(args.adg.suffix + ".bak")
        shutil.copy2(args.adg, bak)
        print(f"\nbackup: {bak}")
    out.write_bytes(gzip.compress(patched.encode("utf-8")))
    print(f"wrote:  {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
