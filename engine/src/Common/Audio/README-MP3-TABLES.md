# Provenance and licence of Mp3Tables.cpp

`Mp3Tables.cpp` holds the MPEG-1 Layer III Huffman code tables (ISO 11172-3 Table B.7) and the synthesis
window D[i] (Table B.3). These are standardised numeric data that cannot be derived by a formula.

## Source

The values were extracted as DATA from the read-only rodata of one installed FFmpeg library by
`tools/audio/gen_mp3_tables.py`, which validates every table (Kraft equality, prefix-freeness, decode order):

| item | value |
| --- | --- |
| file | `libavcodec.so.61.19.101` |
| FFmpeg release | n7.1.1 |
| SHA-256 | `5f66cf4375fc60212a0d1fe17917526d5fb390bf316f1da2447ccc012819c921` |
| binary configuration | built with `--enable-gpl --enable-version3` (among others) |

`python3 tools/audio/gen_mp3_tables.py <that file> > engine/src/Common/Audio/Mp3Tables.cpp` reproduces the
committed file byte for byte (checked when this note was written).

## Licence

The upstream table sources are LGPL-2.1-or-later (FFmpeg: "Copyright (c) 2001, 2002 Fabrice Bellard" and other
FFmpeg contributors; "This file is part of FFmpeg. FFmpeg is free software; you can redistribute it and/or modify it
under the terms of the GNU Lesser General Public License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version."). LGPL-2.1-or-later is compatible with this
project's GPL-3.0. The installed binary was built with `--enable-gpl --enable-version3`, which says nothing about the
table sources, which remain LGPL-2.1-or-later; extracting data from a compiled binary is not a licensing exemption, so
the upstream notice is kept in `gen_mp3_tables.py` and in the header of the generated `Mp3Tables.cpp`, and any
redistribution of the tables must keep it. The values themselves are the ISO 11172-3 numbers.

No libavcodec source code is copied; the decoder in `Mp3Decoder.cpp` is original. Its output is checked against
mpg123 and ffmpeg by `tools/audio/oracle_check.py` (dev-time only, nothing runs at build or test time).
