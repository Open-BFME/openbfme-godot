// OpenBFME. GPL-3.0.
//
// ModelConditionFlags: the model condition bit set of the RotWK 2.01 binary. Port of ZH GameEngine/Include/Common/ModelState.h
// (ModelConditionFlags = BitFlags<MODELCONDITION_COUNT>, ModelConditionFlags::getBitNames) with the RotWK bit-name table.
//
// TARGET FACTS (RotWK game.dat, caveat S-001): the name array at RW 0xD9FAD8 holds 591 names (NULL terminated); BitFlags::parse at
// RW 0x4B5E05 looks every token up in it (RW 0x42B914, case-insensitive) and the flag object is 19 words = 0x4C bytes
// (memset 0x4C at RW 0x4B5E39; equality is memcmp 0x4C at RW 0x444D9B), i.e. BitFlags<591>. Bit number = index in the array.
// The table is generated from the binary by tools/draw/extract_draw_tables.py (src/Common/ModelConditionNames.inc), never typed
// in: PLAN rule 6 wants the binary's full registry, not the names retail INI happens to use. The BFME2 1.06 decompile says
// BitFlags<117> (spec 4.3); that is BFME2's count, not RotWK's.
//
// Parse grammar (RW 0x4B8B37 + 0x4B5E05; ZH BitFlags::parse with the same three forms):
//   NONE           clears the set and ends the list; illegal after any other token ("you may not mix normal and +- ops in
//                  bitstring lists")
//   NAME NAME ...  the first plain name clears the set, each name sets its bit
//   +NAME / -NAME  set / clear one bit of what the set already holds; illegal after a plain name, and a plain name is illegal
//                  after +/-
// A token that names a #define macro expands to its whitespace separated words, each handled as above (RW 0x4B8B65-0x4B8BBE).

#pragma once

#include "Common/BitFlags.h"
#include "Common/INI.h"

#include <string>

enum
{
	MODELCONDITION_COUNT = 591 ///< RW 0xD9FAD8 name array length
};

typedef BitFlags<MODELCONDITION_COUNT> ModelConditionFlags;

namespace ModelCondition
{
// NULL terminated, MODELCONDITION_COUNT names (the shape INI::scanIndexList wants).
const char *const *bitNames();
int count();
// Case-insensitive; -1 when the name is not in the table.
int indexOf(const std::string &name);
const char *nameOf(int bit);
// "A B C" in bit order; "NONE" for the empty set.
std::string describe(const ModelConditionFlags &flags);

// Parses the rest of the current INI line into `flags` (see the grammar above). Throws INIException with the retail texts.
void parseFromLine(INI *ini, ModelConditionFlags &flags);
// FieldParse-compatible: store points at a ModelConditionFlags.
void parseFromINI(INI *ini, void *instance, void *store, const void *userData);
} // namespace ModelCondition
