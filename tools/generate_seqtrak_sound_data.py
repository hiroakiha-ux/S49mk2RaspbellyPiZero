#!/usr/bin/env python3
"""Generate the C++ preset table used by the LCD Sound List screen."""

import json
import sys


def cpp_string(value):
    return json.dumps(value, ensure_ascii=True)


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: generate_seqtrak_sound_data.py INPUT.json OUTPUT.h")
    with open(sys.argv[1], encoding="utf-8") as source:
        data = json.load(source)

    with open(sys.argv[2], "w", encoding="utf-8") as output:
        output.write("// Generated from SEQTRAK_data_list_En_D0.pdf.\n")
        output.write("// Regenerate with tools/parse_seqtrak_sound_list.py and this script.\n")
        output.write("#pragma once\n\n#include <cstdint>\n\n")
        output.write("namespace seqtrak {\n\n")
        output.write("struct SoundPreset {\n")
        output.write("  uint16_t number;\n  const char* name;\n  const char* category;\n};\n\n")
        for track_type in ("Drum", "Synth", "DX", "SAMPLER"):
            entries = data[track_type]["entries"]
            symbol = "Sampler" if track_type == "SAMPLER" else track_type
            output.write(f"inline constexpr SoundPreset k{symbol}SoundPresets[] = {{\n")
            for number, name, category in entries:
                output.write(
                    f"    {{{number}, {cpp_string(name)}, {cpp_string(category)}}},\n")
            output.write("};\n\n")
        output.write("}  // namespace seqtrak\n")


if __name__ == "__main__":
    main()
