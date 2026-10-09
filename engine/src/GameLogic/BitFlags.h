// OpenBFME. GPL-3.0.
//
// Name-indexed bit masks and their INI parsers (the RotWK `BitFlags<N>::parse`). Lane HORDE-1: needed
// by the contain module data (ObjectStatusOfContained, KindOf masks, model conditions, RankInfo weapon
// conditions); written generically so other lanes can reuse it.
//
// TARGET FACTS (RotWK game.dat; S-001 caveat):
//   * the per-instantiation parse functions are RW 0x7B1B4C (ObjectStatus, 128 bits, names RW 0xD8AFF0),
//     0x6C949F (weapon conditions, 128 bits, names 0xDA1328), 0x65621C (KindOf, 224 bits, names
//     0xDA0E68), 0x4B8B37 (model conditions, 608 bits, names 0xD9FAD8). All four are the same loop over
//     different per-token handlers (RW 0x73543E, 0x6C9013, 0x655B0B, 0x4B5E05):
//       loop: token = getNextTokenOrNull; macro = preprocessMacro(token)
//         plain token  -> handler(token); a `false` ends the whole parse
//         macro token  -> the macro's value is split on " \n\r\t" and each piece goes to the handler;
//                         a `false` ends that macro's pieces ONLY and the line continues (retail quirk)
//       handler(token): "NONE" (stricmp) clears the mask and returns false, error code 2 when normal or
//         +/- tokens were already seen; "+X" / "-X" set / clear bit scanIndexList(X) (error code 2 when a
//         plain token was seen); a plain name clears the mask once, then sets its bit.
//       The error text is "you may not mix normal and +- ops in bitstring lists" with code 2 (the older
//       INI::parseBitString32 of RW 0x42E840 is a different function).
//     The mask is NOT cleared before the loop: an empty line leaves it as it was.
//   * DeathType (RW 0x73A68A, names RW 0xDA39E8): starts from ALL (every bit); "ALL", "NONE", "+X",
//     "-X" with bit (index - 1); anything else throws code 5 "ALL, NONE, +, or - expected" (the
//     existing INI::parseTypeFlagList has the ZH text without the comma).

#pragma once

#include "Common/INI.h"

#include <array>
#include <cstdint>

// The binary's full name enumerations (generated: BitFlagNames.cpp, tools/horde_oracle/gen_names.py).
extern const char *const TheKindOfNames[];         // RW 0xDA0E68 (222 names)
extern const char *const TheModelConditionNames[]; // RW 0xD9FAD8 (591 names)
extern const char *const TheObjectStatusNames[];   // RW 0xD8AFF0 (106 names)
extern const char *const TheWeaponConditionNames[]; // RW 0xDA1328 (104 names)
extern const char *const TheDeathTypeNames[];      // RW 0xDA39E8 (24 names; bit = index - 1)
extern const char *const TheMeleeBehaviorNames[];  // RW 0xDAEB2C (Swarm, WaitForLeader, HoldGround, Amoeba)

typedef std::array<std::uint32_t, 7> KindOfMaskType;        // 224 bits
// The raw 608-bit word array of the INI parser (named ModelConditionMask: Common/ModelState.h owns the canonical ModelConditionFlags, a BitFlags<591>,
// and both headers can be included together)
typedef std::array<std::uint32_t, 19> ModelConditionMask;
typedef std::array<std::uint32_t, 4> ObjectStatusMaskType;  // 128 bits
typedef std::array<std::uint32_t, 4> WeaponConditionFlags;  // 128 bits

// Number of names in a registry list.
size_t BitFlagNameCount(const char *const *names);

// The shared loop (see above). `words` has `wordCount` 32-bit words; bit i is word i/32, bit i%32.
void ParseBitFlags(INI *ini, std::uint32_t *words, size_t wordCount, const char *const *names);

// INI field parse procs: `store` points at the mask. userData is unused.
void ParseKindOfMask(INI *ini, void *instance, void *store, const void *userData);
void ParseModelConditionFlags(INI *ini, void *instance, void *store, const void *userData);
void ParseObjectStatusMask(INI *ini, void *instance, void *store, const void *userData);
void ParseWeaponConditionFlags(INI *ini, void *instance, void *store, const void *userData);

// DeathType flags (RW 0x73A68A): a 32-bit mask, default-initialised by the caller.
void ParseDeathTypeFlags(INI *ini, void *instance, void *store, const void *userData);

// Bit helpers.
template <size_t N>
inline bool BitFlagsTest(const std::array<std::uint32_t, N> &m, size_t bit)
{
	return (m[bit >> 5] >> (bit & 31)) & 1u;
}
template <size_t N>
inline void BitFlagsSet(std::array<std::uint32_t, N> &m, size_t bit)
{
	m[bit >> 5] |= (1u << (bit & 31));
}
