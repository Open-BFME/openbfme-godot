// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Small value types shared by the live object layer (lane LOGIC-1): object ids, the update scheduler's sleep times and phases, the
// object status and KindOf masks.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * UPDATE_SLEEP_NONE = 1, UPDATE_SLEEP_FOREVER = 0x3FFFFFFF (RW 0x6250C2: `mov edx, 0x3FFFFFFF; cmp eax, edx; cmova eax, edx` in
//     UpdateModule::friend_setNextCallFrame); the phases are the four vectors the scheduler keeps (RW 0x62E9CA: `imul edi, 0xC` over
//     `GameLogic + 0xC8`), selected by getUpdatePhase().
//   * The object status mask is 128 bits (RW 0xD8AFF0, 106 names): bit 0 is DESTROYED, which makes the scheduler give an object's modules
//     UPDATE_SLEEP_FOREVER (RW 0x62EA85 `test byte [obj + 0x94], 1`; B1's "unnamed status bit0" of spec 5.4 is DESTROYED). Bit 4 is
//     NO_COLLISIONS and bit 74 IGNORE_AI_COMMAND (the per-object end-of-frame checks, RW 0x62EC73 / 0x62EC87).
// DONOR: ZH Include/GameLogic/Module/UpdateModule.h (the sleep and phase enums), ZH Include/Common/GameCommon.h (ObjectID).

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

typedef std::uint32_t ObjectID;
enum
{
	INVALID_ID = 0 ///< ZH GameCommon.h: no object has id 0
};

typedef std::uint32_t UnsignedInt;

// the same typedefs as GameLogic/BitFlags.h (a repeated identical typedef is legal; that header cannot be included next to
// Common/ModelState.h, see HordeContainBehaviorData.h)
typedef std::array<std::uint32_t, 7> KindOfMaskType;        // 224 bits, RW 0xDA0E68
typedef std::array<std::uint32_t, 4> ObjectStatusMaskType;  // 128 bits, RW 0xD8AFF0

enum ObjectStatusBit
{
	OBJECT_STATUS_DESTROYED = 0,        // the three bits the scheduler and the end-of-frame checks test by number (RW constants)
	OBJECT_STATUS_UNDER_CONSTRUCTION = 2, // RW 0x797894 (BuildAssistant slot 0x38: the mask word | 4); AutoDepositUpdate's `status 2` (lane BUILD-1)
	OBJECT_STATUS_NO_COLLISIONS = 4,
	OBJECT_STATUS_IGNORE_AI_COMMAND = 74
};

// RotWK's 12 DisabledTypes (lane DECOMP-1): the name table RW 0xDAD904 (DEFAULT, DISABLED_USER_PARALYZED, ... DISABLED_USER_FROZEN, NULL-terminated, used by the
// DisabledTypesToProcess parsers) and the bounds of setDisabledUntil RW 0x6907F1 (type <= 11), clearDisabled RW 0x692443 and checkDisabledStatus RW 0x690A42
// (types 0 .. 11). BFME2 1.06 has 11 (the decomp's setDisabledUntil attempt); the order of the first eleven is RotWK's table, read here, not ZH's.
typedef std::uint32_t DisabledMaskType;
enum
{
	DISABLEDMASK_NONE = 0,
	DISABLED_DEFAULT = 0,
	DISABLED_USER_PARALYZED = 1,
	DISABLED_EMP = 2,
	DISABLED_HELD = 3,
	DISABLED_PARALYZED = 4,
	DISABLED_UNMANNED = 5,
	DISABLED_UNDERPOWERED = 6,
	DISABLED_FREEFALL = 7,
	DISABLED_TEMPORARILY_BUSY = 8,
	DISABLED_SCRIPT_DISABLED = 9,
	DISABLED_SCRIPT_UNDERPOWERED = 10,
	DISABLED_USER_FROZEN = 11,
	DISABLED_TYPE_COUNT = 12
};

// ZH UpdateModule.h
enum UpdateSleepTime : int
{
	UPDATE_SLEEP_INVALID = 0,
	UPDATE_SLEEP_NONE = 1,
	UPDATE_SLEEP_FOREVER = 0x3fffffff
};
inline UpdateSleepTime UPDATE_SLEEP(int numFrames)
{
	return (UpdateSleepTime)numFrames;
}

// ZH UpdateModule.h SleepyUpdatePhase; BFME keeps one vector per phase (spec 5.4)
enum SleepyUpdatePhase
{
	PHASE_INITIAL = 0,
	PHASE_PHYSICS = 1,
	PHASE_NORMAL = 2,
	PHASE_FINAL = 3,
	PHASE_COUNT = 4
};

// ZH Include/Common/GameCommon.h; the relationship a player has to a team or another player
enum Relationship
{
	ENEMIES = 0,
	NEUTRAL,
	ALLIES
};

template <std::size_t N>
inline bool MaskTest(const std::array<std::uint32_t, N> &m, unsigned bit)
{
	return bit < N * 32 && ((m[bit >> 5] >> (bit & 31)) & 1u) != 0;
}
template <std::size_t N>
inline void MaskSet(std::array<std::uint32_t, N> &m, unsigned bit, bool value)
{
	if (bit < N * 32)
	{
		if (value)
		{
			m[bit >> 5] |= (1u << (bit & 31));
		}
		else
		{
			m[bit >> 5] &= ~(1u << (bit & 31));
		}
	}
}
