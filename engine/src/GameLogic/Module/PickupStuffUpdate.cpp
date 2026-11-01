// OpenBFME. GPL-3.0.
// See GameLogic/Module/PickupStuffUpdate.h for the target facts, the donors and what is inference.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/PickupStuffUpdate.h"

#include "Common/GameCommon.h"
#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectFilterMatch.h"

#include <cstdint>
#include <cstring>
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"
#include "GameLogic/SnapshotXfer.h"

#include <cstddef>
#include <stdexcept>

namespace
{
typedef PickupStuffUpdateModuleData Data;

const char *const kStop =
	"[S-1422] PickupStuffUpdate (lane MODCOMPAT-MOD1, ported: RW 0x8957B7 / 0x895741 / 0x895632 / 0x895572 with the AI's temporary state machine RW 0x6630A9 "
	"/ 0x6630D7): NOT ported: SpecialAbilityUpdate's use of the same temporary machine (its initiate RW 0x852FB1 begins one, its finish RW 0x853105 gives it "
	"back: S-859), so a skirmish AI's hero ability neither starts nor ends a pick-up's machine; and while the stuff found is a crate whose crate collide has no "
	"runtime (a crate collide class other than SalvageCrateCollide: the 2.01 VeterancyCrateCollide of TreasureChestLevelUpMedium / Moria, not ported), no pick-up begins: retail's "
	"horde collects the crate on arrival (CrateCollide::onCollide RW 0x8C06B6 destroys it, RW 0x62BBAB), here it could not, and the horde would walk back to it "
	"every ScanIntervalSeconds for the rest of the game";

void parseFilter(INI *ini, void *instance, void *store, const void *)
{
	ObjectFilter f;
	ParseObjectFilter(ini, instance, &f, nullptr); // RW 0x76392F
	*static_cast<ObjectFilter *>(store) = std::move(f);
}

// RW 0xC64600
const FieldParse kFields[] = {
	{ "SkirmishAIOnly", INI::parseBool, nullptr, (int)offsetof(Data, m_skirmishAIOnly) },           // RW 0x42E558
	{ "ScanRange", INI::parseReal, nullptr, (int)offsetof(Data, m_scanRange) },                     // RW 0x42ED00
	{ "StuffToPickUp", parseFilter, nullptr, (int)offsetof(Data, m_stuffToPickUp) },               // RW 0x76392F
	{ "ScanIntervalSeconds", INI::parseReal, nullptr, (int)offsetof(Data, m_scanIntervalSeconds) }, // RW 0x42ED00
	{ nullptr, nullptr, nullptr, 0 }
};

const unsigned kStatusAttached = 0x62; // RW 0x895661: the "must be clear" mask's dword 3 bit 2 (TheObjectStatusNames: ATTACHED)
} // namespace

void PickupStuffUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kFields);
}

void PickupStuffUpdate::registerModule(ModuleFactory &modules)
{
	static const char kName[] = "PickupStuffUpdate";
	modules.bindTypedData<Data>(kName, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(kName, MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(kName) + ": the module data is not typed");
		}
		return std::make_unique<PickupStuffUpdate>(thing, typed);
	});
}

std::vector<std::string> PickupStuffUpdate::stopLines()
{
	return { kStop };
}

// RW 0x895524: + 0x20 = 0, + 0x24 = 0 (no wake call)
PickupStuffUpdate::PickupStuffUpdate(Thing *thing, const Data *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
}

// RW 0x8957B7
UpdateSleepTime PickupStuffUpdate::update()
{
	Object *obj = getObject();
	if (!obj->getAIUpdateInterface()) // Object + 0x260
	{
		return UPDATE_SLEEP_FOREVER;
	}
	if (m_data->m_skirmishAIOnly)
	{
		const Player *player = obj->getControllingPlayer(); // RW 0x68B678
		if (!player || !obj->logic().skirmishAI().findAI(player->getPlayerIndex())) // RW 0x6A950B (a null player: none)
		{
			return UPDATE_SLEEP_FOREVER;
		}
	}
	if (m_pickingUp) // + 0x20
	{
		pickingUpStep();
	}
	else
	{
		scanStep();
	}
	return UPDATE_SLEEP_NONE; // RW returns 1
}

namespace
{
// (frame - last) > LOGICFRAMES_PER_SECOND * seconds, decided exactly in integers from the float's bits (the x87 compare of RW 0x895741 is exact: see
// the header): seconds = sign * m * 2^e with m the 24-bit significand; NaN compares unordered (false)
bool framesExceed(std::int64_t elapsed, float seconds)
{
	std::uint32_t bits;
	std::memcpy(&bits, &seconds, sizeof bits);
	const bool negative = (bits >> 31) != 0;
	const int biased = (int)((bits >> 23) & 0xFFu);
	std::uint32_t frac = bits & 0x7FFFFFu;
	if (biased == 0xFF)
	{
		return frac == 0 && negative; // -inf: any elapsed is above; +inf and NaN: never
	}
	const std::int64_t m = (std::int64_t)(biased ? (frac | 0x800000u) : frac) * LOGICFRAMES_PER_SECOND; // < 2^29
	const int e = (biased ? biased : 1) - 150;
	if (m == 0)
	{
		return elapsed > 0;
	}
	// compare elapsed with sign * m * 2^e
	if (e >= 0)
	{
		if (e > 33)
		{
			return negative; // |rhs| >= 2^34 exceeds any 33-bit elapsed
		}
		const std::int64_t rhs = m << e;
		return negative ? elapsed > -rhs : elapsed > rhs;
	}
	const int k = -e; // elapsed * 2^k against sign * m
	if (k > 30)
	{
		// |elapsed| * 2^k >= 2^31 > m unless elapsed is 0
		return negative ? elapsed >= 0 : elapsed > 0;
	}
	const std::int64_t lhs = elapsed * ((std::int64_t)1 << k); // |elapsed| < 2^33, k <= 30: fits
	return negative ? lhs > -m : lhs > m;
}
} // namespace

// RW 0x895741
void PickupStuffUpdate::scanStep()
{
	Object *obj = getObject();
	const unsigned now = obj->logic().getFrame(); // TheGameLogic + 0x40
	// FILD frame, FILD LOGICFRAMES_PER_SECOND * ScanIntervalSeconds + FILD last (each unsigned through the 2^32 fix-up), FCOMIP: frame > last + rate (see
	// the header: the exact compare, made as the difference)
	if (!framesExceed((std::int64_t)now - (std::int64_t)m_lastScanFrame, m_data->m_scanIntervalSeconds)) // JBE: equal or unordered waits
	{
		return;
	}
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (!ai->hasTemporaryStateMachine()) // AI + 0x34 == 0
	{
		++m_scans;
		Object *stuff = findStuff();
		if (stuff && !crateCollideRuns(*stuff))
		{
			// lane MODCOMPAT-MOD1 (S-1422): retail's horde collects the crate when it arrives: CrateCollide::onCollide RW 0x8C06B6 runs isValidToExecute
			// (vslot 0x34) and executeCrateBehavior (vslot 0x30), then plays ExecuteFX (RW 0x4B1B5A), sets the ever-executed byte (+ 0x14) and destroys the
			// crate (TheGameLogic RW 0x62BBAB), so the scans after that find nothing. SalvageCrateCollide (every 2.01 crate) runs (SalvageCrateCollide.h); a
			// crate of another crate collide class has no runtime here: it would stay and the horde would walk back to it every ScanIntervalSeconds for
			// the rest of the game. No pick-up begins for such a crate; the scan's frame is stamped as retail's (RW 0x8957A9)
			++m_blocked;
			obj->logic().noteStop(kStop);
			stuff = nullptr;
		}
		if (stuff)
		{
			// RW 0x895715
			ai->beginTemporaryStateMachine();      // RW 0x6630A9
			ai->aiMoveToObject(stuff, CMD_FROM_AI); // RW 0x7549D7(stuff, 2)
			m_pickingUp = true;
			++m_pickups;
			obj->logic().noteStop(kStop); // a pick-up's machine runs: what of it is not ported reaches the report
		}
	}
	m_lastScanFrame = obj->logic().getFrame(); // RW 0x8957A9
}

// RW 0x895632
Object *PickupStuffUpdate::findStuff()
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	const Player *owner = obj->getControllingPlayer(); // RW 0x68B678
	PartitionFilterFn filters([&](Object &o) {
		if (o.isEffectivelyDead()) // RW 0xC10E20
		{
			return false;
		}
		if (!ObjectFilterMatch::allows(logic, m_data->m_stuffToPickUp, o, owner)) // RW 0xBE4CC8 (match flag 1)
		{
			return false;
		}
		return !o.testStatus(kStatusAttached); // RW 0xC2E0B0 (RW 0x76A28D: must be clear ATTACHED; must be set nothing)
	});
	for (const PartitionHit &h : logic.partition().iterateObjectsInRange(*obj->getPosition(), m_data->m_scanRange, FROM_CENTER_2D, { &filters }, ITER_SORTED_NEAR_TO_FAR))
	{
		return h.object; // RW 0x444F19: the first
	}
	return nullptr;
}

// lane MODCOMPAT-MOD1: false when the stuff's template lists a crate collide (a behavior module whose class is a CrateCollide: <Name>CrateCollide)
// that this object does not run as a collide (no module, or one without the collide interface: the crate collide classes other than SalvageCrateCollide);
// an object without a crate collide is left to retail's scan
bool PickupStuffUpdate::crateCollideRuns(const Object &stuff)
{
	static const std::string kSuffix = "CrateCollide";
	for (const ThingTemplate::Nugget &n : stuff.getTemplate()->behaviorModules().nuggets())
	{
		if (n.name.size() >= kSuffix.size() && n.name.compare(n.name.size() - kSuffix.size(), kSuffix.size(), kSuffix) == 0)
		{
			BehaviorModule *m = stuff.findModule(n.name);
			if (!m || !m->getCollide())
			{
				return false;
			}
		}
	}
	return true;
}

// RW 0x895572
void PickupStuffUpdate::pickingUpStep()
{
	AIUpdateInterface *ai = getObject()->getAIUpdateInterface();
	if (!ai->isIdle()) // AI vslot 0x1B8
	{
		return;
	}
	if (ai->hasTemporaryStateMachine()) // AI + 0x34
	{
		ai->endTemporaryStateMachine(); // RW 0x6630D7
	}
	m_pickingUp = false;
}

void PickupStuffUpdate::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addBool(m_pickingUp);
	h.addU32(m_lastScanFrame);
}

// RW 0x8955A4's fields (OpenBFME native snapshot, S-4190)
void PickupStuffUpdate::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferUpdateModule(x); // RW 0x850C60
	x.xferBool(&m_pickingUp);
	x.xferUnsignedInt(&m_lastScanFrame);
	SnapshotXfer::u64(x, m_scans);
	SnapshotXfer::u64(x, m_pickups);
}
