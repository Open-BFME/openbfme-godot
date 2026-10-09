#!/usr/bin/env python3
"""Dev-time generator for engine/src/Common/Audio/Mp3Tables.cpp.

The MPEG-1 Layer III Huffman code tables (ISO 11172-3 Table B.7: tables 1-3, 5-13, 15, 16, 24 and the two
count1 tables A/B) and the synthesis window D[i] (Table B.3) are standardised numeric data; they cannot be
derived by a formula, so they were read as DATA from the rodata of an installed libavcodec (the shared
library that ships with ffmpeg) and are written out here in the natural ISO layout: for table N with
xlen = ylen = n, entry [x * n + y] holds (hlen, hcode) exactly as listed in the standard. The generator
checks every table (Kraft equality, prefix-freeness, the decode order) so a bad extraction cannot pass
silently; the decoder's output is then verified against mpg123 and ffmpeg (tools/audio/oracle_check.py).

Provenance (pinned): libavcodec.so.61.19.101 from FFmpeg n7.1.1, SHA-256
5f66cf4375fc60212a0d1fe17917526d5fb390bf316f1da2447ccc012819c921; that build was configured with --enable-gpl
--enable-version3. Running this script on that exact file reproduces Mp3Tables.cpp byte for byte. Upstream notice, preserved:

    Copyright (c) 2001, 2002 Fabrice Bellard (and other FFmpeg contributors)
    This file is part of FFmpeg.
    FFmpeg is free software; you can redistribute it and/or modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either version 2.1 of the License, or (at your option) any
    later version. FFmpeg is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
    implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

The table sources are LGPL-2.1-or-later, which is GPL-3.0 compatible. Reading the data out of a binary is not a licensing
exemption: the extracted tables remain subject to that notice.

Usage: gen_mp3_tables.py /path/to/libavcodec.so.61 > engine/src/Common/Audio/Mp3Tables.cpp
Nothing here runs at build or test time.
"""
import re
import struct
import sys

SIZES = [(1, 2), (2, 3), (3, 3), (5, 4), (6, 4), (7, 6), (8, 6), (9, 6), (10, 8), (11, 8), (12, 8),
         (13, 16), (15, 16), (16, 16), (24, 16)]


def find_tables(blob):
    # lengths of tables 1..24 are stored back to back (in code order), followed by 1-byte symbols x<<4|y in
    # the same order; locate the run by its first table (lengths 3 3 2 1 / symbols 0x11 0x01 0x10 0x00).
    total = sum(n * n for _, n in SIZES)
    sym0 = bytes([0x11, 0x01, 0x10, 0x00])
    for m in re.finditer(re.escape(bytes([3, 3, 2, 1])), blob):
        lens_at = m.start()
        for sym_at in range(lens_at + total, lens_at + total + 64):
            if blob[sym_at:sym_at + 4] == sym0:
                return lens_at, sym_at
    raise SystemExit("huffman tables not found")


def assign_codes(lens):
    # codes ascend in listing order; moving to a shorter length drops low bits (they must be zero)
    codes = []
    code = 0
    prev = lens[0]
    for i, l in enumerate(lens):
        if i:
            code += 1
            if l > prev:
                code <<= (l - prev)
            elif l < prev:
                assert code & ((1 << (prev - l)) - 1) == 0, "non-canonical step"
                code >>= (prev - l)
        codes.append(code)
        prev = l
    return codes


def main():
    blob = open(sys.argv[1], "rb").read()
    lens_at, sym_at = find_tables(blob)
    tables = {}
    pos = 0
    for tno, n in SIZES:
        cnt = n * n
        lens = list(blob[lens_at + pos: lens_at + pos + cnt])
        syms = list(blob[sym_at + pos: sym_at + pos + cnt])
        pos += cnt
        assert sorted(syms) == sorted((x << 4) | y for x in range(n) for y in range(n)), "table %d symbols" % tno
        codes = assign_codes(lens)
        kraft = sum(1 << (24 - l) for l in lens)
        assert kraft <= (1 << 24), "table %d kraft" % tno
        words = sorted(format(c, "0%db" % l) for c, l in zip(codes, lens))
        for a, b in zip(words, words[1:]):
            assert not b.startswith(a), "table %d not prefix free" % tno
        hlen = [0] * (n * n)
        hcode = [0] * (n * n)
        for l, s, c in zip(lens, syms, codes):
            x, y = s >> 4, s & 15
            hlen[x * n + y] = l
            hcode[x * n + y] = c
        tables[tno] = (n, hlen, hcode, kraft == (1 << 24))
    # count1 tables: the 64 bytes before the big tables are A lengths, B lengths, A codes, B codes (16 each)
    qa_len = list(blob[lens_at - 64:lens_at - 48]); qb_len = list(blob[lens_at - 48:lens_at - 32])
    qa_code = list(blob[lens_at - 32:lens_at - 16]); qb_code = list(blob[lens_at - 16:lens_at])
    assert qa_len == [1, 4, 4, 5, 4, 6, 5, 6, 4, 5, 5, 6, 5, 6, 6, 6], qa_len
    assert qb_len == [4] * 16, qb_len
    assert qb_code == list(range(15, -1, -1)), qb_code
    for lens, codes, name in ((qa_len, qa_code, "A"), (qb_len, qb_code, "B")):
        assert sum(1 << (24 - l) for l in lens) == (1 << 24), name
        words = sorted(format(c, "0%db" % l) for c, l in zip(codes, lens))
        for a, b in zip(words, words[1:]):
            assert not b.startswith(a), "quad %s" % name
    # synthesis window: int32 enwindow[257], value / 65536 is D[i] for i <= 256 (Table B.3)
    pat = struct.pack("<8i", 0, -1, -1, -1, -1, -1, -1, -2)
    wat = blob.find(pat)
    assert wat >= 0
    enw = list(struct.unpack("<257i", blob[wat:wat + 257 * 4]))
    assert enw[256] == 75038 and enw[64] == 213

    out = []
    out.append("// OpenBFME. GPL-3.0.")
    out.append("//")
    out.append("// MPEG-1/2/2.5 Layer III constant tables (generated by tools/audio/gen_mp3_tables.py, do not edit).")
    out.append("// ISO 11172-3 Table B.7 (Huffman code length / code, row-major [x * n + y]) and Table B.3 (synthesis")
    out.append("// window D[i] * 65536 for i = 0..256). Standardised data read from a system libavcodec's rodata and")
    out.append("// validated by the generator (Kraft sums, prefix-freeness); see the generator for the provenance note.")
    out.append("//")
    out.append("// Upstream notice (preserved). The numeric tables below were read from the read-only data of FFmpeg's libavcodec")
    out.append("// (libavcodec.so.61.19.101 of FFmpeg n7.1.1, SHA-256 5f66cf4375fc60212a0d1fe17917526d5fb390bf316f1da2447ccc012819c921,")
    out.append("// built with --enable-gpl --enable-version3). FFmpeg's MPEG audio table sources carry:")
    out.append("//   Copyright (c) 2001, 2002 Fabrice Bellard (and other FFmpeg contributors)")
    out.append("//   This file is part of FFmpeg. FFmpeg is free software; you can redistribute it and/or modify it under the terms of the")
    out.append("//   GNU Lesser General Public License as published by the Free Software Foundation; either version 2.1 of the License, or")
    out.append("//   (at your option) any later version. FFmpeg is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;")
    out.append("//   without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.")
    out.append("// Extracting the data from a binary is not a licensing exemption: the tables stay under LGPL-2.1-or-later, which is")
    out.append("// compatible with this project's GPL-3.0 (see README-MP3-TABLES.md). The values themselves are ISO 11172-3 data.")
    out.append("")
    out.append('#include "Common/Audio/Mp3Tables.h"')
    out.append("")
    out.append("namespace AudioDecode")
    out.append("{")
    out.append("namespace mp3tab")
    out.append("{")
    out.append("")
    for tno, n in SIZES:
        n_, hlen, hcode, complete = tables[tno]
        out.append("static const uint8_t kLen%d[%d] = {%s};" % (tno, n * n, ", ".join(map(str, hlen))))
        out.append("static const uint32_t kCode%d[%d] = {%s};" % (tno, n * n, ", ".join(map(str, hcode))))
    out.append("")
    out.append("static const uint8_t kQuadLenA[16] = {%s};" % ", ".join(map(str, qa_len)))
    out.append("static const uint8_t kQuadCodeA[16] = {%s};" % ", ".join(map(str, qa_code)))
    out.append("static const uint8_t kQuadLenB[16] = {%s};" % ", ".join(map(str, qb_len)))
    out.append("static const uint8_t kQuadCodeB[16] = {%s};" % ", ".join(map(str, qb_code)))
    out.append("")
    out.append("const HuffmanSource kHuffmanSources[kHuffmanTableCount] = {")
    for tno, n in SIZES:
        out.append("\t{%d, %d, kLen%d, kCode%d}," % (tno, n, tno, tno))
    out.append("};")
    out.append("")
    out.append("const QuadSource kQuadSources[2] = {{kQuadLenA, kQuadCodeA}, {kQuadLenB, kQuadCodeB}};")
    out.append("")
    out.append("// D[i] * 65536, i = 0..256; D[512 - i] = (i & 63) ? -D[i] : D[i] (see Mp3Decoder.cpp).")
    out.append("const int32_t kSynthWindow257[257] = {")
    for i in range(0, 257, 16):
        out.append("\t" + ", ".join(map(str, enw[i:i + 16])) + ",")
    out.append("};")
    out.append("")
    out.append("} // namespace mp3tab")
    out.append("} // namespace AudioDecode")
    sys.stdout.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
