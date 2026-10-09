// OpenBFME. GPL-3.0.
//
// Msvcr71Real (lane WIN-1): retail's INI real parser, MSVCR71's sscanf(text, "%f"), ported instruction by instruction so that every OS reads every INI
// real to the bits retail reads (and Windows and Linux players to the same bits as each other).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; msvcr71.dll 7.10 of the RotWK install):
//   * INI::scanReal (RW 0x42EAAD) calls sscanf(token, "%f", &value) through the IAT (0xBD05E8, format 0xBD423C) and accepts one conversion; the
//     RwGrammar float probe (RW 0x73B723) makes the same call.
//   * sscanf's %f (_input 0x7C36D493, 0x7C36D7CF-0x7C36D9D4) collects [-]digits[.digits][e[-]digits] into a buffer ('+' signs are consumed, not
//     stored; at most 349 characters) and hands it to _fassign -> _atoflt (0x7C37293E): __strgtold12 (0x7C3720A2) -> _ld12tof (0x7C372831).
//   * __strgtold12 keeps at most 25 mantissa digits (beyond 24 it bumps digit 24 when it is >= 5 and drops digit 25: a CRT quirk kept), caps the
//     exponent text at 5201, builds a 12-byte value with __mtold12 (0x7C37300F: x10 in 96 bits, normalised to an 80-bit mantissa), and scales it
//     with __multtenpow12 (0x7C37331E: three exponent bits per step, each a truncated 16x16 bit partial-product __ld12mul 0x7C3730ED with
//     round-half-even to 80 bits) over the 10^(d * 8^g) tables (generated: tools/sim/gen_msvcr71_pow10.py).
//   * _ld12cvt (0x7C3726C3, float descriptor 0x7C38F678 = { 128, -127, 24, 8, 32, 127 }) rounds the 80-bit mantissa to 24 bits UP only when the
//     round bit and a lower bit are set (an exact tie truncates), and handles subnormals by shifting the unrounded mantissa, rounding at 24 bits
//     and shifting once more (a truncation); a rounding carry in the subnormal path is lost.
// MEASURED (tests/test_win1_msvcr71_real.cpp; golden values executed from the real msvcr71.dll under Wine, native DLL forced and verified by its
// code bytes): the port agrees on 281,499 texts (grammar fuzz, random decimals, float midpoints, subnormals, overflow, 400-digit mantissas, every
// number-like token of the retail INI files). Retail edge forms: "3e" reads 3 (an exponent marker without digits is ignored), "0x10" reads the
// decimal 0, "nan" and "inf" fail; an exact tie truncates (16777219 reads 16777218), the minimum subnormal's text 1.401298464324817e-45 reads
// 0x00000002 and 1e-45 reads 0. 45 of the 6,499 number-like retail INI tokens differ from correct rounding (large integers such as 29782747).
#pragma once

#include <cstdint>

namespace Msvcr71Real
{

// sscanf(text, "%f", &out) of MSVCR71: true (one conversion, `out` assigned) or false (`out` untouched). *end (optional) is the first character
// the scan did not consume.
bool scanfFloat(const char *text, float &out, const char **end = nullptr);

// The bits of _atoflt(text) for an already collected number text ([-]digits[.digits][e[-]digits]): __strgtold12 then _ld12tof.
std::uint32_t atofltBits(const char *collected);

// Test hook: the 12 bytes of the power-of-ten entry 10^(+-digit * 8^group) as __multtenpow12 multiplies by it (group 0..4, digit 1..7).
void powerTableEntry(bool negative, int group, int digit, std::uint8_t out[12]);

} // namespace Msvcr71Real
