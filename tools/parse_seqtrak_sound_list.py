#!/usr/bin/env python3
"""Parses SEQTRAK_data_list_En_D0.pdf's "Sound List" tables (Drum/Synth/DX/
SAMPLER preset No./Name/Category) from `pdftotext -raw` output.

-raw mode (not -layout) puts each table row on its own line in correct
reading order for this particular 2-column table layout, e.g.:
    1 Tight Punchy Kick 1 Kick
    2 Tight Punchy Kick 2 Kick
which -layout mode garbles by interleaving the two printed columns.

Usage (from the repo root):
    pdftotext -raw 資料/SEQTRAK_data_list_En_D0.pdf /tmp/seqtrak_raw.txt
    python3 tools/parse_seqtrak_sound_list.py /tmp/seqtrak_raw.txt [out.json]

Prints a per-track-type summary (entry count, unmatched lines, non-
sequential number gaps, category boundaries) so extraction problems are
visible immediately rather than silently producing wrong data, then writes
the full parsed entries (No./Name/Category per track type) as JSON.
"""
import re
import sys
import json

CATEGORIES = {
    "Drum": ["Kick", "Snare", "Rim", "Clap", "Snap", "Closed HiHat",
             "Open HiHat", "Shaker / Tambourine", "Ride", "Crash", "Tom",
             "Bell", "Conga / Bongo", "World", "SFX"],
    "Synth": ["Bass", "Synth Lead", "Piano", "Keyboard", "Organ", "Pad",
              "Strings", "Brass", "Woodwind", "Guitar", "World", "Mallet",
              "Bell", "Rhythmic", "SFX"],
    "DX": ["Bass", "Synth Lead", "Piano", "Keyboard", "Organ", "Pad",
           "Strings", "Brass", "Woodwind", "Guitar", "World", "Mallet",
           "Bell", "Rhythmic", "SFX"],
    "SAMPLER": ["Vocal Count", "Vocal Phrase / Chant", "Singing Vocal",
                "Robotic Vocal / Effect", "Riser", "Laser / Sci-Fi",
                "Impact", "Noise / Distorted Sound", "Ambient / Soundscape",
                "SFX", "Scratch", "Nature / Animals",
                "Hit / Stab / Musical Instrument Sound", "Percussion",
                "Recorded Sound"],
}

SECTION_HEADERS = {
    "Drum": "Drum Sound",
    "Synth": "Synth Sound",
    "DX": "DX Sound",
    "SAMPLER": "SAMPLER Sound",
}


def find_section_bounds(lines):
    starts = {}
    for i, line in enumerate(lines):
        stripped = line.strip()
        for track_type, header in SECTION_HEADERS.items():
            if stripped == header and track_type not in starts:
                starts[track_type] = i
    order = ["Drum", "Synth", "DX", "SAMPLER"]
    bounds = {}
    for idx, track_type in enumerate(order):
        start = starts[track_type] + 1
        if idx + 1 < len(order):
            end = starts[order[idx + 1]]
        else:
            # SAMPLER section ends where "Wave List" begins (merged with a
            # page-footer line in raw mode, e.g. "33 SEQTRAK Data ListWave
            # List").
            end = None
            for i in range(start, len(lines)):
                if ("SEQTRAK Data ListWave List" in lines[i] or
                        lines[i].strip() == "Wave List"):
                    end = i
                    break
            if end is None:
                end = len(lines)
        bounds[track_type] = (start, end)
    return bounds


def parse_section(lines, track_type):
    cats = sorted(CATEGORIES[track_type], key=len, reverse=True)
    entries = []
    i = 0
    n = len(lines)
    while i < n:
        line = lines[i].strip()
        i += 1
        if not line:
            continue
        if line == "No. Name Category":
            continue
        if re.match(r'^\d+\s+SEQTRAK Data List', line):
            continue
        m = re.match(r'^(\d+)\s+(.*)$', line)
        if not m:
            continue
        num = int(m.group(1))
        rest = m.group(2).strip()

        matched_cat = None
        for cat in cats:
            if rest == cat or rest.endswith(' ' + cat):
                matched_cat = cat
                name = rest[: -len(cat)].strip() if rest != cat else ''
                break
        if matched_cat is not None:
            entries.append((num, name, matched_cat))
            continue

        # Special case: long categories that wrap onto following line(s)
        # (only "Hit / Stab / Musical Instrument Sound" observed so far).
        name = rest
        lookahead = []
        j = i
        while j < n and len(lookahead) < 2:
            nxt = lines[j].strip()
            if not nxt or nxt == "No. Name Category" or re.match(
                    r'^\d+\s+SEQTRAK Data List', nxt):
                j += 1
                continue
            if re.match(r'^\d+\s', nxt):
                break
            lookahead.append(nxt)
            j += 1
        joined = ' '.join(lookahead)
        found = False
        for cat in cats:
            if joined == cat or joined.startswith(cat):
                entries.append((num, name, cat))
                i = j
                found = True
                break
        if not found:
            entries.append((num, name, None))  # unmatched, flagged below
    return entries


def category_boundaries(entries):
    boundaries = []
    prev_cat = None
    for num, name, cat in entries:
        if cat != prev_cat:
            boundaries.append((num, cat))
            prev_cat = cat
    return boundaries


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else '/tmp/seqtrak_raw.txt'
    with open(path, encoding='utf-8', errors='replace') as f:
        # Strip form-feed (page break) characters: pdftotext -raw sometimes
        # emits them mid-line (e.g. "...Data List\fWave List"), which would
        # otherwise silently break substring matches across a page boundary.
        lines = [line.replace('\f', '') for line in f.readlines()]

    bounds = find_section_bounds(lines)
    result = {}
    for track_type, (start, end) in bounds.items():
        entries = parse_section(lines[start:end], track_type)
        unmatched = [e for e in entries if e[2] is None]
        # sequential-number sanity check
        nums = [e[0] for e in entries]
        gaps = []
        for a, b in zip(nums, nums[1:]):
            if b != a + 1:
                gaps.append((a, b))
        result[track_type] = {
            "count": len(entries),
            "max_no": max(nums) if nums else None,
            "unmatched": unmatched,
            "non_sequential_gaps": gaps,
            "category_boundaries": category_boundaries(entries),
            "entries": entries,
        }

    for track_type, data in result.items():
        print(f"== {track_type} ==")
        print(f"  entries parsed: {data['count']}, max No.: {data['max_no']}")
        print(f"  unmatched lines: {len(data['unmatched'])}")
        for u in data['unmatched'][:10]:
            print(f"    UNMATCHED: {u}")
        print(f"  non-sequential gaps: {data['non_sequential_gaps'][:10]}")
        print(f"  category boundaries:")
        for num, cat in data['category_boundaries']:
            print(f"    {num:>5}  {cat}")
        print()

    out_path = sys.argv[2] if len(sys.argv) > 2 else '/tmp/seqtrak_sound_list.json'
    with open(out_path, 'w', encoding='utf-8') as f:
        json.dump(result, f, ensure_ascii=False, indent=1)
    print(f"Wrote full parsed data to {out_path}")


if __name__ == '__main__':
    main()
