// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// RefPack ("EAR") decoder.
//
// Sources, in priority order:
//  * TARGET FACT (RotWK game.dat, S-001 caveat; compared by tools/retail_oracle/test_refpack_oracle.py):
//    REF_decode(dest, src, int *sizeout) at RW 0xAA17E0 (stdcall, returns the decoded length) and
//    REF_is at RW 0xAA1A00. REF_is accepts exactly the types 0x10fb, 0x11fb, 0x90fb and 0x91fb;
//    REF_decode reads the header as ZH does: flag 0x80 in the first byte = 4-byte size field, flag 0x01 =
//    a size field of that width to skip first (3 bytes: 0x11fb; 4 bytes: 0x91fb). The function is
//    byte-identical to BFME2 1.06's at 0xA8DAA0 (tools/retail_oracle/counterpart.py: equivalent).
//  * DONOR (contradicted for the header only): Open-BFME-1 BfmeRefPackDecode.cpp bfmeRefPackDecode(), whose
//    header handling is NOT RotWK's (it takes 0x15fb / 0x16fb as the 4-byte types and has no 0x90fb).
//    An earlier version of this port followed BFME1 and wrongly refused 0x90fb/0x91fb; the oracle found it.
//  * DONOR: ZH Libraries/Source/Compression/EAC/refdecode.cpp REF_decode: the same header and the same
//    command bodies as RotWK.
//  * The retail routine does not check the marker byte; this port requires 0xFB (and a listed first byte)
//    and refuses everything else instead of guessing.
//
// The retail routines have no bounds checks. This port checks every source read, every
// destination write and every back reference and reports a failure instead of reading or
// writing out of range (rule 10: no silent fallbacks).

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Header size field (the decoded length), or -1 on an unsupported type / truncated header.
// RotWK header rules (see above).
long long REF_size(const std::uint8_t *compressed, size_t compressedLen);

// Decodes a RefPack stream (the bytes after the 8-byte "EAR\0"+size envelope). On success fills
// `out` with exactly the header's decoded length and, if non-null, *consumed with the number of
// source bytes read. maxOutput bounds the header's claimed length (decompression-bomb guard).
bool REF_decode(const std::uint8_t *compressed, size_t compressedLen, std::vector<std::uint8_t> &out,
	size_t *consumed, std::string *error, size_t maxOutput = 512u * 1024u * 1024u);
