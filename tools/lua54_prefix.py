#!/usr/bin/env python3
"""Regenerate Lua 5.4 ABI prefixes without editing upstream source."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
source = root / 'engine/thirdparty/lua-5.4.9/src'
text = '\n'.join(p.read_text() for p in sorted(source.iterdir()) if p.suffix in ('.h', '.c'))
macros = set(re.findall(r'^\s*#\s*define\s+(lua\w+)', text, re.M))
# Optional configuration hooks must remain undefined so upstream default branches run.
configured = set(re.findall(r'defined\s*\(\s*(lua\w+)\s*\)', text))
configured.update(re.findall(r'^\s*#\s*ifn?def\s+(lua\w+)', text, re.M))
symbols = sorted(set(re.findall(r'\b(lua\w+)\b', text)) - macros - configured)
header = root / 'engine/src/Libraries/Lua54/prefix.h'
header.write_text('// Prefix Lua 5.4 identifiers so the retail Lua 4 VM can coexist. Generated from upstream tokens.\n#pragma once\n'
                  + ''.join(f'#define {s} obf54_{s}\n' for s in symbols))
