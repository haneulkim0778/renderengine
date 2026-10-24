"""Embed validated SPIR-V words so executable relocation needs no shader files."""

from pathlib import Path
import struct
import sys

source, destination, name = sys.argv[1:]
data = Path(source).read_bytes()
if len(data) % 4 or len(data) < 20:
    raise ValueError("invalid SPIR-V word stream")
words = struct.unpack(f"<{len(data) // 4}I", data)
if words[0] != 0x07230203 or words[1] > 0x00010500:
    raise ValueError("expected SPIR-V at most 1.5")
target = Path(destination)
target.parent.mkdir(parents=True, exist_ok=True)
target.write_text("#pragma once\n#include <cstdint>\ninline constexpr std::uint32_t "
                  + name + "[] = {\n" + ",\n".join(f"0x{word:08x}" for word in words)
                  + "\n};\n", encoding="utf-8")
