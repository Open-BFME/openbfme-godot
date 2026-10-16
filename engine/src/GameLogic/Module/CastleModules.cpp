// OpenBFME. GPL-3.0.
// See GameLogic/Module/CastleModules.h for the sources of every rule.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Module/CastleUpgrade.h"
#include "GameLogic/Object/PartitionManager.h"

#include "Common/BuildAssistant.h"
#include "Common/CommandPoints.h"
#include "Common/NumericState.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/BuildPlacement.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Economy.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Construction.h"
#include "GameLogic/ConstructionSite.h"
#include "Common/Team.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/ObjectFilter.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameLogic/SimMath.h"

#include <cstddef>
#include <stdexcept>

namespace
{
void parseOptionalObjectFilter(INI *ini, void *instance, void *store, const void *)
{
	ObjectFilter f;
	ParseObjectFilter(ini, instance, &f, nullptr);
	*static_cast<std::shared_ptr<const ObjectFilter> *>(store) = std::make_shared<const ObjectFilter>(std::move(f));
}
// RW 0x79BD2F (lane BACKLOG2): `<side> <base> [<cost> [<limit>]]`; the counts are INI::scanUnsignedInt of the next tokens when there are any (RW 0x79BDB1 /
// 0x79BDC8: getNextTokenOrNull, RW 0x42EA42); the entry goes into the side's map slot unless one is there (RW 0x79BE0C: map::insert). TheSidesList's RW 0x72DCBB
// call with the base name (RW 0x79BE33: the side list's castle template names) is not ported: the CastleTemplateStore reads a layout when it is asked (S-300)
void parseFactionBaseList(INI *ini, void *, void *store, const void *)
{
	auto *list = static_cast<std::vector<CastleFactionEntry> *>(store);
	CastleFactionEntry e;
	e.faction = ini->getNextAsciiString();
	e.base = ini->getNextAsciiString();
	if (const char *t = ini->getNextTokenOrNull())
	{
		e.cost = ini->scanUnsignedInt(t);
	}
	if (const char *t = ini->getNextTokenOrNull())
	{
		e.limit = ini->scanUnsignedInt(t);
	}
	for (const CastleFactionEntry &have : *list)
	{
		if (have.faction == e.faction)
		{
			return; // the name key is taken: the first line stays
		}
	}
	list->push_back(std::move(e));
}
// RW 0x79CA18: one "<template> <count>" pair per line, appended
void parsePreBuiltList(INI *ini, void *, void *store, const void *)
{
	auto *list = static_cast<std::vector<std::pair<std::string, int>> *>(store);
	std::string name = ini->getNextAsciiString();
	int count = 0;
	INI::parseInt(ini, nullptr, &count, nullptr);
	list->emplace_back(std::move(name), count);
}
// RW 0x79CA81: the parse of FactionDecal is unknown (no retail data uses it): the rest of the line is kept as text
void parseRestOfLine(INI *ini, void *, void *store, const void *)
{
	*static_cast<std::string *>(store) = ini->getNextAsciiString();
}

#define CB_OFF(member) (int)offsetof(CastleBehaviorModuleData, member)
const FieldParse kCastleFieldParse[] = {
	{ "UseTheNewCastleSystemInsteadOfTheClunkyBuildList", INI::parseBool, nullptr, CB_OFF(m_useNewCastleSystem) },
	{ "UseSecondaryBuildList", INI::parseBool, nullptr, CB_OFF(m_useSecondaryBuildList) },
	{ "SidesAllowed", INI::parseAsciiStringVector, nullptr, CB_OFF(m_sidesAllowed) },
	{ "CastleToUnpackForFaction", parseFactionBaseList, nullptr, CB_OFF(m_castleToUnpackForFaction) },
	{ "FilterValidOwnedEntries", parseOptionalObjectFilter, nullptr, CB_OFF(m_filterValidOwnedEntries) },
	{ "FilterCrew", parseOptionalObjectFilter, nullptr, CB_OFF(m_filterCrew) },
	{ "FactionDecal", parseRestOfLine, nullptr, CB_OFF(m_factionDecal) },
	{ "PreBuiltList", parsePreBuiltList, nullptr, CB_OFF(m_preBuiltList) },
	{ "PreBuiltPlyr", INI::parseAsciiString, nullptr, CB_OFF(m_preBuiltPlyr) },
	{ "DecalName", INI::parseAsciiString, nullptr, CB_OFF(m_decalName) },
	{ "DecalSize", INI::parseReal, nullptr, CB_OFF(m_decalSize) },
	{ "FadeTime", INI::parseReal, nullptr, CB_OFF(m_fadeTime) },
	{ "UnpackDelayTime", INI::parseReal, nullptr, CB_OFF(m_unpackDelayTime) },
	{ "BuildTime", INI::parseReal, nullptr, CB_OFF(m_buildTime) },
	{ "ScanDistance", INI::parseReal, nullptr, CB_OFF(m_scanDistance) },
	{ "MaxCastleRadius", INI::parseReal, nullptr, CB_OFF(m_maxCastleRadius) },
	{ "CrewPrepareTime", INI::parseDurationUnsignedInt, nullptr, CB_OFF(m_crewPrepareTime) },
	{ "InstantUnpack", INI::parseBool, nullptr, CB_OFF(m_instantUnpack) },
	{ "KeepDeathKillsEverything", INI::parseBool, nullptr, CB_OFF(m_keepDeathKillsEverything) },
	{ "CrewReleaseFX", INI::parseAsciiString, nullptr, CB_OFF(m_crewReleaseFX) },
	{ "CrewPrepareFX", INI::parseAsciiString, nullptr, CB_OFF(m_crewPrepareFX) },
	{ "CrewPrepareInterval", INI::parseDurationUnsignedInt, nullptr, CB_OFF(m_crewPrepareInterval) },
	{ "DisableStructureRotation", INI::parseBool, nullptr, CB_OFF(m_disableStructureRotation) },
	{ "EvaEnemyCastleSightedEvent", INI::parseAsciiString, nullptr, CB_OFF(m_evaEnemyCastleSightedEvent) },
	{ "Summoned", INI::parseBool, nullptr, CB_OFF(m_summoned) },
	{ "TransferFoundationHealthToCastleUponUnpack", INI::parseBool, nullptr, CB_OFF(m_transferFoundationHealthToCastleUponUnpack) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef CB_OFF

#define CM_OFF(member) (int)offsetof(CastleMemberBehaviorModuleData, member)
const FieldParse kCastleMemberFieldParse[] = {
	{ "CampDestroyedOwnerEvaEvent", INI::parseAsciiString, nullptr, CM_OFF(m_campDestroyedOwnerEvaEvent) },
	{ "CampDestroyedAllyEvaEvent", INI::parseAsciiString, nullptr, CM_OFF(m_campDestroyedAllyEvaEvent) },
	{ "CampDestroyedAttackerEvaEvent", INI::parseAsciiString, nullptr, CM_OFF(m_campDestroyedAttackerEvaEvent) },
	{ "UnderAttackAllyEvaEventIfKeep", INI::parseAsciiString, nullptr, CM_OFF(m_underAttackAllyEvaEventIfKeep) },
	{ "UnderAttackEvaEventIfKeep", INI::parseAsciiString, nullptr, CM_OFF(m_underAttackEvaEventIfKeep) },
	{ "BeingBuiltSound", INI::parseAsciiString, nullptr, CM_OFF(m_beingBuiltSound) },
	{ "StoreUpgradePrice", INI::parseBool, nullptr, CM_OFF(m_storeUpgradePrice) },
	{ "CountsForEvaCastleBreached", INI::parseBool, nullptr, CM_OFF(m_countsForEvaCastleBreached) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef CM_OFF

float normalizeAngle(float a)
{
	const float pi = 3.14159265f, twoPi = 6.2831853f;
	while (a < SimMath::subf32(0.0f, pi))
	{
		a = SimMath::addf32(a, twoPi);
	}
	while (a > pi)
	{
		a = SimMath::subf32(a, twoPi);
	}
	return a;
}

bool templateKindOf(GameLogic &logic, const ThingTemplate &tt, const char *name)
{
	const int bit = ObjectTemplateInfoBuilder::kindOfIndex(name);
	return bit >= 0 && MaskTest(logic.templateInfo(tt.getFinalOverride()).kindOf, (unsigned)bit);
}

struct CastleNames
{
	int castleKeep = CombatNames::kindOf("CASTLE_KEEP");
	int baseFoundation = CombatNames::kindOf("BASE_FOUNDATION");
	int baseDefenseFoundation = CombatNames::kindOf("BASE_DEFENSE_FOUNDATION");
	int wallUpgrade = CombatNames::kindOf("WALL_UPGRADE");
	int doNotPickMeWhenBuilding = CombatNames::kindOf("DO_NOT_PICK_ME_WHEN_BUILDING");
	int dozer = CombatNames::kindOf("DOZER");
	int infantry = CombatNames::kindOf("INFANTRY"), cavalry = CombatNames::kindOf("CAVALRY"), monster = CombatNames::kindOf("MONSTER"), machine = CombatNames::kindOf("MACHINE");
	int noBaseCapture = CombatNames::kindOf("NO_BASE_CAPTURE");
	int inert = CombatNames::kindOf("INERT"), moveOnly = CombatNames::kindOf("MOVE_ONLY"), walkOnTopOfWall = CombatNames::kindOf("WALK_ON_TOP_OF_WALL"), immobile = CombatNames::kindOf("IMMOBILE");
	int doNotPickMe = CombatNames::status("DO_NOT_PICK_ME");
	int noAttack = CombatNames::status("NO_ATTACK");
	int unselectable = CombatNames::status("UNSELECTABLE");
	int awaitingConstruction = CombatNames::modelCondition("AWAITING_CONSTRUCTION");
	int partiallyConstructed = CombatNames::modelCondition("PARTIALLY_CONSTRUCTED");
	int postCollapse = CombatNames::modelCondition("POST_COLLAPSE");
	int justBuilt = CombatNames::modelCondition("JUST_BUILT");
	int baseBuild = CombatNames::modelCondition("BASE_BUILD"); // lane SPELL-3 (the timed unpack)
	int unpacking = CombatNames::modelCondition("UNPACKING");
	int packing = CombatNames::modelCondition("PACKING");
};
const CastleNames &castleNames()
{
	static const CastleNames n;
	return n;
}
// RW 0x79CCF2 clears the status 0x53 of every member of the list + 0x5C before it destroys it (TheObjectStatusNames[83], RW 0xD8AFF0)
const unsigned kPackStatus = 0x53;
} // namespace

const CastleFactionEntry *CastleBehaviorModuleData::entryFor(const std::string &side) const
{
	// the map's find on the side's name key (Player + 0x58 through TheNameKeyGenerator RW 0x49F474: case-sensitive)
	for (const CastleFactionEntry &e : m_castleToUnpackForFaction)
	{
		if (e.faction == side)
		{
			return &e;
		}
	}
	return nullptr;
}

void CastleBehaviorModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	FoundationAIUpdateModuleData::buildFieldParse(p); // RW table 0xC06338 first
	p.add(kCastleFieldParse);
}
void CastleMemberBehaviorModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kCastleMemberFieldParse);
}

// ---- CastleBehavior -----------------------------------------------------------------------------------------------------------------
CastleBehavior::CastleBehavior(Thing *thing, const CastleBehaviorModuleData *data)
	: FoundationAIUpdate(thing, data)
	, m_data(data)
{
	// asleep until the object is complete (onBuildComplete wakes the module); RW 0x79A901 starts the state at 0
}

void CastleBehavior::onCapture(Player *oldOwner, Player *newOwner)
{
	(void)oldOwner;
	Team *to = newOwner ? newOwner->getDefaultTeam() : nullptr;
	if (!to)
	{
		return;
	}
	GameLogic &logic = getObject()->logic();
	if (Object *keep = occupantOf(*getObject()))
	{
		keep->setTeam(to);
	}
	for (const std::vector<ObjectID> *list : { &m_foundations, &m_members, &m_crew, &m_defenseFoundations })
	{
		const std::vector<ObjectID> ids = *list; // a setTeam may run another capture
		for (ObjectID id : ids)
		{
			if (Object *o = logic.findObjectByID(id))
			{
				o->setTeam(to);
			}
		}
	}
}

void CastleBehavior::onBuildComplete()
{
	// RW 0x798238
	Object *obj = getObject();
	if (m_data->m_instantUnpack)
	{
		m_needInstantBuild = true;
	}
	obj->setModelConditionState(Construction::modelConditionIndex("INVULNERABLE"), true);
	const int unattackable = ObjectTemplateInfoBuilder::objectStatusIndex("UNATTACKABLE");
	if (unattackable >= 0)
	{
		obj->setStatus((unsigned)unattackable, true);
	}
	setWakeFrame(obj, UPDATE_SLEEP_NONE);
}

UpdateSleepTime CastleBehavior::update()
{
	// RW 0x79CF2A (lane CASTLE-1): the timer, then the state
	bool expired = false;
	if (m_timer > 0.0f)
	{
		// RW 0x79CF57: cvtsi2ss LOGICFRAMES_PER_SECOND (5), 1.0 / that, subss, comiss against 0
		m_timer = SimMath::subf32(m_timer, SimMath::divf32(1.0f, SimMath::sseFromInt32(5)));
		if (0.0f > m_timer)
		{
			m_timer = 0.0f;
			expired = true;
		}
	}
	switch (m_state)
	{
	case STATE_IDLE:
		if (m_data->m_useNewCastleSystem && !m_bfme1StartChecked)
		{
			// BFME1's castle flags (UseTheNewCastleSystemInsteadOfTheClunkyBuildList, a row no RotWK castle has): a match starts with each player's
			// castle standing. INFERENCE (docs/BFME1.md): on its first update a flag whose owner's faction has a CastleToUnpackForFaction entry
			// unpacks that castle at once and free (the flag's JUST_BUILT "for instant-build" state); a neutral flag stays capturable for the price.
			// The retail trigger (the caller of CastleBehavior::initiateUnpack) is not in Open-BFME-1
			m_bfme1StartChecked = true;
			const Player *owner = getObject()->getControllingPlayer();
			if (owner && m_data->entryFor(owner->getSide()))
			{
				m_needInstantBuild = true;
			}
		}
		if (tryInstantUnpack())
		{
			m_state = STATE_UNPACKED;
		}
		scanForCapture(); // RW 0x79CFF7
		return (UpdateSleepTime)5; // RW 0x79D0D5: LOGICFRAMES_PER_SECOND
	case STATE_UNPACKED:
		if (shouldAbandon())
		{
			abandon();
		}
		else if (!m_crew.empty())
		{
			getObject()->logic().noteStop("[S-951] castle: a castle with a crew (RW 0x79B0F1: the crew's preparation and release) is not ported");
		}
		return UPDATE_SLEEP_NONE;
	case STATE_ABANDONED:
		if (expired)
		{
			m_state = STATE_IDLE;
			pack();
			m_timer = m_data->m_unpackDelayTime; // RW 0x79CCCA
		}
		return UPDATE_SLEEP_NONE;
	case STATE_PACKED:
		// lane SPELL-3: RW 0x79D09D: state 2, the unpack (RW 0x79BE6A(0)), the timer = FadeTime (data + 0x1C)
		m_state = STATE_UNPACKING;
		if (!unpack(false))
		{
			// review r1: the port's unpack can refuse (no base for the side, an unknown layout); retail's RW 0x79BE6A has no such outcome: the refusal goes to
			// the game's report and the castle stays packed (state 0) instead of fading in and building nothing
			Object *castle = getObject();
			castle->logic().reportError("CastleBehavior of object " + std::to_string(castle->getID()) + " (" + (castle->getTemplate() ? castle->getTemplate()->getName() : std::string("?")) +
										"): the timed unpack failed: " + m_lastError);
			m_state = STATE_IDLE;
			return UPDATE_SLEEP_NONE;
		}
		m_state = STATE_UNPACKING; // the port's unpack ends in state 4 (its instant callers' RW 0x79C1CE / 0x79D0C6); the timed path stays in 2
		m_timer = m_data->m_fadeTime;
		return UPDATE_SLEEP_NONE;
	case STATE_UNPACKING:
		// RW 0x79D035 .. 0x79D098: the first update after the unpack (1 / LOGICFRAMES_PER_SECOND + timer == FadeTime, SSE, an exact compare) would move the
		// stragglers off the walls (RW 0x79ADE9: not ported, S-951); on expiry: state 3, the keep and the members (+ 0x5C) lose JUST_BUILT and get BASE_BUILD
		// (RW 0x799F0F(clear, set)), the timer = BuildTime (data + 0x24)
		if (SimMath::addf32(SimMath::divf32(1.0f, SimMath::sseFromInt32(5)), m_timer) == m_data->m_fadeTime)
		{
			getObject()->logic().noteStop("[S-951] castle: the timed unpack's straggler teleport off the walls (RW 0x79ADE9) is not ported");
		}
		if (expired)
		{
			m_state = STATE_BUILDING;
			setMembersModelCondition(castleNames().justBuilt, castleNames().baseBuild);
			m_timer = m_data->m_buildTime;
		}
		return UPDATE_SLEEP_NONE;
	case STATE_BUILDING:
		// RW 0x79CFE8 .. 0x79D02E: on expiry: state 4, BASE_BUILD cleared (RW 0x799F0F), NO_ATTACK cleared on the keep and the members (RW 0x79A017(5, 0))
		if (expired)
		{
			m_state = STATE_UNPACKED;
			setMembersModelCondition(castleNames().baseBuild, -1);
			setMembersStatus(castleNames().noAttack, false);
		}
		return UPDATE_SLEEP_NONE;
	default:
		return UPDATE_SLEEP_NONE;
	}
}

// lane SPELL-3: RW 0x79C761 (CreateModuleInterface slot 0, vtable RW 0xC30DD8): a Summoned castle (data + 0x3E) that can unpack (RW 0x7983E3(true): no instant build
// pending, state 0, the timer run out; BFME2 decomp Rva00395F57CanUnpack.cpp, tier A) initiates its timed unpack (RW 0x79C17D(false, null); BFME2 decomp
// CastleBehaviorRegisterOwnedObject.cpp CastleBehavior::initiateUnpack, tier A): + 0x4C = 0, state 1; the faction entry's payment (RW 0x799039: the optional
// count of the CastleToUnpackForFaction line, which no retail summoned castle sets, deposited to the player: not ported, S-951) and the alert map's reset
// (RW 0x7985AA on + 0xA0) are not ported
void CastleBehavior::onCreate()
{
	if (!m_data->m_summoned || !canUnpack(true))
	{
		return;
	}
	initiateUnpack(false, nullptr); // lane BACKLOG2: RW 0x79C17D(0, null): state 1 and the side's price (none for a retail summoned castle); the constructor's wake runs it
}

// RW 0x799F0F: the keep (+ 0x38) and the members (+ 0x5C) clear one model condition and set another (Object RW 0x68D607); their CastleMemberBehavior's own
// sub-object call (RW 0x798057) is not ported (S-951)
void CastleBehavior::setMembersModelCondition(int clear, int set)
{
	GameLogic &logic = getObject()->logic();
	auto apply = [&](Object *o) {
		if (!o)
		{
			return;
		}
		if (clear >= 0)
		{
			o->setModelConditionState(clear, false);
		}
		if (set >= 0)
		{
			o->setModelConditionState(set, true);
		}
	};
	apply(m_keepId != INVALID_ID ? logic.findObjectByID(m_keepId) : nullptr);
	for (ObjectID id : m_members)
	{
		apply(logic.findObjectByID(id));
	}
}

std::string CastleBehavior::baseNameFor(const Player &owner) const
{
	// RW 0x798F70: the base of the owner's side entry (lane BACKLOG2: the map's exact name key, not BUILD-1's case-insensitive compare)
	const CastleFactionEntry *e = m_data->entryFor(owner.getSide());
	return e ? e->base : std::string();
}

bool CastleBehavior::tryInstantUnpack()
{
	// RW 0x79C265 (lane CASTLE-1 completed it)
	if (!m_needInstantBuild)
	{
		return false;
	}
	Object *castle = getObject();
	GameLogic &logic = castle->logic();
	// the owner: PreBuiltPlyr's player (RW 0x79C2B8: the first player of the list whose name equals it; none: nothing is unpacked), else the castle's controlling player
	Player *owner = m_data->m_preBuiltPlyr.empty() ? castle->getControllingPlayer() : logic.players().findPlayerWithName(m_data->m_preBuiltPlyr);
	if (owner && owner->getDefaultTeam())
	{
		castle->setTeam(owner->getDefaultTeam()); // RW 0x79C2FB (then RW 0x68C18F / 0x68E31F / 0x68D8A8)
		if (!unpack())
		{
			logic.reportError("CastleBehavior of object " + std::to_string(castle->getID()) + " (" + (castle->getTemplate() ? castle->getTemplate()->getName() : std::string("?")) + "): " + m_lastError);
		}
		// RW 0x79C31B: one pre-built structure per PreBuiltList entry whose template exists (RW 0x6D1305), instant
		for (const auto &entry : m_data->m_preBuiltList)
		{
			if (const ThingTemplate *tt = logic.things().findTemplate(entry.first))
			{
				preBuild(*tt, entry.second, true);
			}
		}
	}
	m_needInstantBuild = false;
	castle->setModelConditionState(Construction::modelConditionIndex("JUST_BUILT"), true); // RW 0x79C38F
	setMembersStatus(castleNames().doNotPickMe, false); // RW 0x79C3A6: RW 0x79A017(DO_NOT_PICK_ME, 0)
	return true;
}

// RW 0x79A5EC: the pad of the list (the BASE_DEFENSE_FOUNDATIONs for an FS_BASE_DEFENSE template when there are any, else the foundations) at `index` (clamped to the list;
// -2: the first pad that takes it) builds the template through RW 0x79A518
Object *CastleBehavior::preBuild(const ThingTemplate &tt, int index, bool instant)
{
	const std::vector<ObjectID> &list = templateKindOf(getObject()->logic(), tt, "FS_BASE_DEFENSE") && !m_defenseFoundations.empty() ? m_defenseFoundations : m_foundations;
	if (index == -2)
	{
		for (ObjectID pad : std::vector<ObjectID>(list))
		{
			if (Object *made = buildOnPad(pad, tt, instant))
			{
				return made;
			}
		}
		return nullptr;
	}
	if (list.empty())
	{
		return nullptr;
	}
	// RW 0x79A63F: an unsigned compare first (a negative index is above the last), then the signed one
	const std::uint32_t last = (std::uint32_t)list.size() - 1u;
	std::uint32_t at = (std::uint32_t)index;
	if (last < at)
	{
		at = last;
	}
	else if (index < 0)
	{
		at = 0;
	}
	return buildOnPad(list[at], tt, instant);
}

// RW 0x79A518
Object *CastleBehavior::buildOnPad(ObjectID padId, const ThingTemplate &tt, bool instant)
{
	GameLogic &logic = getObject()->logic();
	Object *pad = logic.findObjectByID(padId);
	if (!pad || pad->isKindOf((unsigned)castleNames().wallUpgrade))
	{
		return nullptr;
	}
	const CastleMemberBehavior *member = dynamic_cast<const CastleMemberBehavior *>(pad->findModule("CastleMemberBehavior"));
	if (!instant && member && member->occupantId() != INVALID_ID && member->plotIsTaken())
	{
		return nullptr; // RW 0x79A596
	}
	FoundationAIUpdate *foundation = nullptr;
	for (const std::unique_ptr<BehaviorModule> &m : pad->modules())
	{
		if (FoundationAIUpdate *fa = m->getFoundationAIUpdate())
		{
			foundation = fa;
			break;
		}
	}
	if (!foundation || occupantOf(*pad) || !pad->getControllingPlayer())
	{
		return nullptr; // RW 0x79A5A1 .. 0x79A5B7: no foundation, or it holds something already
	}
	// RW 0x79A5D6: the creation interface RW 0x859198(pad, template, pad position, 0.0, the pad's player, instant): the site clearing (RW 0x858C41), construct
	// RW 0x858701, the push out of the units in the way (RW 0x858CFC, logic random draws) (lane BACKLOG2)
	return foundation->createOnSite(tt, *pad->getPosition(), 0.0f, *pad->getControllingPlayer(), instant);
}

bool CastleBehavior::unpack(bool instant)
{
	// RW 0x79BE6A
	Object *castle = getObject();
	GameLogic &logic = castle->logic();
	m_lastError.clear();
	Player *owner = castle->getControllingPlayer();
	if (!owner)
	{
		m_lastError = "the castle has no owner";
		return false;
	}
	if (AIWorld *ai = logic.aiWorld())
	{
		ai->removeObjectFromPathfindMap(*castle); // RW 0x79BE8E
	}
	m_hasWalkOnWall = false; // RW 0x79BEA3
	if (!m_explicitTemplate.empty())
	{
		// lane BACKLOG2: RW 0x79BEAC .. 0x79BED9: an explicit object (MSG_CASTLE_UNPACK_EXPLICIT_OBJECT): the template by name (RW 0x6D1305), made by
		// createOwnedObject(template) RW 0x79B98B (BFME2 decomp CastleBehaviorCreateOwnedObject.cpp, tier A): the castle's creation interface (slot 4,
		// RW 0x859198) with the castle's position and orientation, its controlling player and instant 0, then registerOwnedObject RW 0x79AC19; + 0x98 empties
		const std::string name = m_explicitTemplate;
		m_explicitTemplate.clear();
		if (const ThingTemplate *tt = logic.things().findTemplate(name))
		{
			if (Object *made = createOnSite(*tt->getFinalOverride(), *castle->getPosition(), castle->getOrientation(), *owner, false))
			{
				m_owned.push_back(made->getID());
				registerOwnedObject(*made);
			}
		}
	}
	else
	{
		const std::string base = baseNameFor(*owner);
		if (base.empty())
		{
			m_lastError = "CastleToUnpackForFaction names no base for the faction '" + owner->getSide() + "'";
			return false;
		}
		std::string error;
		const CastleTemplate *layout = logic.castleTemplates().find(base, &error);
		if (!layout)
		{
			m_lastError = error;
			return false;
		}
		for (const CastleTemplateEntry &entry : layout->entries)
		{
			if (Object *made = createOwnedObject(entry, *owner, instant)) // RW 0x79B903(instant) -> 0x7987EE
			{
				m_owned.push_back(made->getID());
				registerOwnedObject(*made); // RW 0x79B94E
			}
		}
	}
	// lane CASTLE-1: RW 0x79BF76 .. 0x79BFF7: the unpack frame (+ 0x48, the crew's clock) and every member of the lists learns the keep (member + 0x14, RW 0x7990D9: the port
	// keeps a pad's occupant in its occupantId, see S-952; lane DECOMP-2: the keep goes to the member's keepId, which Object::updateUpgradeModules reads)
	for (const std::vector<ObjectID> *list : { &m_foundations, &m_members, &m_defenseFoundations }) // RW + 0x50, + 0x5C, + 0x74 (RW 0x79BF3B .. 0x79BF5A)
	{
		for (ObjectID id : *list)
		{
			Object *o = logic.findObjectByID(id);
			if (CastleMemberBehavior *m = o ? dynamic_cast<CastleMemberBehavior *>(o->findModule("CastleMemberBehavior")) : nullptr)
			{
				m->setKeepId(m_keepId);
			}
		}
	}
	m_unpackFrame = logic.getFrame();
	// RW 0x79C03A: the castle's model condition PACKING clears and UNPACKING is set
	castle->setModelConditionState(castleNames().packing, false);
	castle->setModelConditionState(castleNames().unpacking, true);
	const int unselectable = ObjectTemplateInfoBuilder::objectStatusIndex("UNSELECTABLE");
	if (unselectable >= 0)
	{
		castle->setStatus((unsigned)unselectable, true); // RW 0x79C07E
	}
	// RW 0x79C0FD ..: the keep takes the castle's price (truncated to an integer) and its script name (ScriptEngine RW 0x759467: a non-empty name moves), the castle is
	// renamed "No Name" (RW 0xC30F30)
	if (Object *keep = m_keepId != INVALID_ID ? logic.findObjectByID(m_keepId) : nullptr)
	{
		keep->setBuildCostPaid((float)SimMath::truncToInt32(castle->getBuildCostPaid()));
		if (!castle->getName().empty())
		{
			keep->setName(castle->getName());
		}
		castle->setName("No Name");
	}
	m_state = STATE_UNPACKED;
	return true;
}

Object *CastleBehavior::createOwnedObject(const CastleTemplateEntry &entry, Player &owner, bool instant)
{
	// RW 0x7987EE
	Object *castle = getObject();
	GameLogic &logic = castle->logic();
	if (entry.templateName.empty())
	{
		return nullptr; // RW 0x798827: an entry without a name makes nothing
	}
	const ThingTemplate *tt = logic.things().findTemplate(entry.templateName);
	if (!tt)
	{
		logic.reportError("CastleBehavior: the base layout names the template '" + entry.templateName + "' which is not defined");
		return nullptr;
	}
	if (templateKindOf(logic, *tt, "OPTIMIZED_PROP"))
	{
		return nullptr; // RW 0x798850
	}
	if (templateKindOf(logic, *tt, "WALK_ON_TOP_OF_WALL"))
	{
		m_hasWalkOnWall = true; // RW 0x798862
	}
	// RW 0x798876 ff: the owner's team when the template passes the filter, else the neutral player's
	bool mine = true;
	if (m_data->m_filterValidOwnedEntries)
	{
		mine = ObjectFilterMatch::allows(logic, *m_data->m_filterValidOwnedEntries, tt, nullptr, nullptr);
	}
	Team *team = mine ? owner.getDefaultTeam() : (logic.players().getNeutralPlayer() ? logic.players().getNeutralPlayer()->getDefaultTeam() : nullptr);
	if (!team)
	{
		logic.reportError("CastleBehavior: no team to own '" + entry.templateName + "'");
		return nullptr;
	}
	// RW 0x798899 ff: the castle's transform applied to the entry's position; the angle is the entry's plus the castle's
	const float castleAngle = castle->getOrientation();
	const float c = SimMath::cosDet(castleAngle), s = SimMath::sinDet(castleAngle);
	Coord3D pos;
	pos.x = SimMath::addf32(castle->getPosition()->x, SimMath::subf32(SimMath::mulf32(entry.x, c), SimMath::mulf32(entry.y, s)));
	pos.y = SimMath::addf32(castle->getPosition()->y, SimMath::addf32(SimMath::mulf32(entry.x, s), SimMath::mulf32(entry.y, c)));
	pos.z = SimMath::addf32(castle->getPosition()->z, entry.z);
	const float angle = normalizeAngle(SimMath::addf32(castleAngle, entry.angle));
	// BFME1 authored castle pieces overlap at seams and gateways. They are a
	// single predefined layout, not independent player building placements.
	// Keep the RotWK placement check for its castle system.
	if (!m_data->m_useNewCastleSystem && !templateKindOf(logic, *tt, "COMMANDCENTER") &&
		BuildPlacement::isLocationLegalToBuild(logic, pos, *tt, angle, LLF_NO_OBJECT_OVERLAP, castle, &owner) != LBC_OK)
	{
		return nullptr;
	}
	// lane BACKLOG2: RW 0x79899A: the castle's creation interface (its + 0x20 vtable RW 0xC30DE8 slot 4 = RW 0x859198) with (the castle, the template, the position,
	// the angle, the player of the team chosen above, instant): the site is cleared first (RW 0x858C41), the object made (slot 7, RW 0x858701: below, the port's
	// placement, S-303), then the units in the way are pushed out (RW 0x858CFC)
	Player *creator = mine ? &owner : logic.players().getNeutralPlayer();
	ConstructionSite::clearSiteForFoundation(logic, *tt, pos, angle);
	Object *obj = logic.newObject(tt, team, ObjectStatusMaskType{});
	if (!obj)
	{
		return nullptr;
	}
	obj->setOrientation(angle);
	obj->setPosition(&pos);
	obj->friend_onBuildComplete();
	if (AIWorld *ai = logic.aiWorld())
	{
		ai->addObjectToPathfindMap(*obj);
	}
	if (mine && obj->isKindOfName("STRUCTURE"))
	{
		// lane PLAY-4: RW 0x7987EE makes no Player::onStructureCreated call (RW 0x6AAF3B is not among its calls): since that call sets GettingBuiltBehavior's
		// + 0x36 and spawns the worker of a plot or castle builder, the castle's complete members do not get it here (they spawned workers that faded at once)
		Construction::onStructureConstructionComplete(owner, castle, *obj, false);
	}
	if (creator)
	{
		ConstructionSite::moveObjectsForFoundation(logic, *castle, *tt, pos, angle, *creator); // RW 0x8591F4
	}
	if (!instant)
	{
		// lane SPELL-3 (review r1): RW 0x798AE4 .. 0x798B33, the timed unpack's member only: frames = _ftol2(FadeTime * [RW 0xD9F60C] = 30) (x87: fld FadeTime,
		// fild 30, fmul); the drawable fades in over them (RW 0x670AA2, client), the object keeps JUST_BUILT for them (setSpecialModelConditionState RW 0x68B581
		// with 0xDA), gets the status NO_ATTACK (RW 0x62684D(5, 1)) and its drawable takes the flags at once (RW 0x67449C(0))
		const UnsignedInt frames = SimMath::ftol2Low32(SimMath::pc24MulW((double)m_data->m_fadeTime, 30.0));
		GameLogic &lg = obj->logic();
		if (ObjectClientHooks *client = lg.clientHooks())
		{
			client->fadeIn(*obj, frames);
		}
		obj->setSpecialModelConditionState(castleNames().justBuilt, frames);
		obj->setStatus((unsigned)castleNames().noAttack, true);
		if (ObjectClientHooks *client = lg.clientHooks())
		{
			client->flushModelConditions(*obj);
		}
	}
	return obj;
}

// ---- lane CASTLE-1: the castle after its unpack (see the header) ----------------------------------------------------------------------------

// RW 0x79AC19
void CastleBehavior::registerOwnedObject(Object &obj)
{
	const CastleNames &n = castleNames();
	Object *castle = getObject();
	GameLogic &logic = castle->logic();
	CastleMemberBehavior *member = dynamic_cast<CastleMemberBehavior *>(obj.findModule("CastleMemberBehavior"));
	if (member)
	{
		member->setCastleId(castle->getID()); // RW 0x79AC87
	}
	if (obj.isKindOf((unsigned)n.doNotPickMeWhenBuilding))
	{
		obj.setStatus((unsigned)n.doNotPickMe, true); // RW 0x79AC9C
	}
	if (obj.isKindOf((unsigned)n.castleKeep))
	{
		if (m_keepId == INVALID_ID)
		{
			// RW 0x79ACB3: the keep; the stored price (+ 0x4C) is never set in this port; the member's + 0x24 byte is not modelled (S-650); the keep is the castle's own occupant
			// (RW 0x8582DE: the castle stays UNSELECTABLE, which the unpack sets)
			m_keepId = obj.getID();
			if (member)
			{
				member->setOccupantId(m_keepId);
			}
		}
		else
		{
			logic.destroyObject(&obj); // RW 0x79AD0D: a second keep
		}
	}
	else if (obj.isKindOf((unsigned)n.baseDefenseFoundation))
	{
		m_defenseFoundations.push_back(obj.getID()); // RW 0x79AD25
	}
	else if (obj.isKindOf((unsigned)n.baseFoundation) || obj.isKindOf((unsigned)n.wallUpgrade))
	{
		m_foundations.push_back(obj.getID()); // RW 0x79ADA8
		if (obj.isKindOf((unsigned)n.wallUpgrade))
		{
			obj.setStatus((unsigned)n.noAttack, false); // RW 0x79ADC1
		}
	}
	else
	{
		// RW 0x79AD4E: FilterCrew (default NONE: null here) and FilterValidOwnedEntries both accept the object for the castle's player
		const Player *owner = castle->getControllingPlayer();
		const bool crew = m_data->m_filterCrew && ObjectFilterMatch::allows(logic, *m_data->m_filterCrew, obj, owner) &&
			(!m_data->m_filterValidOwnedEntries || ObjectFilterMatch::allows(logic, *m_data->m_filterValidOwnedEntries, obj, owner));
		if (crew)
		{
			m_crew.push_back(obj.getID()); // RW 0x79AD8A
			logic.noteStop("[S-951] castle: a crew member's preparation for the unpack (RW 0x798397) is not ported");
		}
		else
		{
			m_members.push_back(obj.getID()); // RW 0x79AD99
		}
	}
	// RW 0x79ADC6: RW 0x625E0A(object, module + 0x9C) is not identified (S-952)
}

Object *CastleBehavior::occupantOf(const Object &foundation) const
{
	GameLogic &logic = getObject()->logic();
	if (&foundation == getObject())
	{
		return m_keepId != INVALID_ID ? logic.findObjectByID(m_keepId) : nullptr; // RW 0x858B97 on the castle: its occupant (+ 0x28) is the keep
	}
	const CastleMemberBehavior *member = dynamic_cast<const CastleMemberBehavior *>(foundation.findModule("CastleMemberBehavior"));
	if (!member || member->occupantId() == INVALID_ID)
	{
		return nullptr;
	}
	return logic.findObjectByID(member->occupantId()); // INFERENCE (S-952): RW keeps the id in the plot's FoundationAIUpdate + 0x28 until the plot's update finds it gone
}

bool CastleBehavior::isPlayerAllowedToCapture(const Player &player) const
{
	// RW 0x7998FF: the map is not empty and holds the player's side (BFME2 decomp Rva003973EBFind.cpp, tier A)
	return m_data->entryFor(player.getSide()) != nullptr;
}

// ---- lane BACKLOG2: a player's unpack and pack (see the header) ------------------------------------------------------------------------------------------------

bool CastleBehavior::canUnpack(bool checkTimer) const
{
	// RW 0x7983E3
	if (m_needInstantBuild)
	{
		return false;
	}
	if (m_state != STATE_IDLE)
	{
		return false;
	}
	return !(checkTimer && m_timer > 0.0f);
}

bool CastleBehavior::isPlayerAllowedToPackOrUnpack(const Player *player) const
{
	// RW 0x79A472: alreadyMyCastle ? true : isPlayerAllowedToCapture (RW 0x7998FF reads the player unchecked; the dispatcher passes the controller)
	if (!player)
	{
		return false;
	}
	return player == getObject()->getControllingPlayer() || isPlayerAllowedToCapture(*player);
}

std::uint32_t CastleBehavior::unpackCost(const Player *player) const
{
	// RW 0x7995FB
	if (!player || player->getSide().empty())
	{
		return 0;
	}
	const CastleFactionEntry *e = m_data->entryFor(player->getSide());
	if (!e)
	{
		return 0;
	}
	if ((std::int32_t)e->limit > 0 && player->commandPointsAvailable() < (std::int32_t)e->limit)
	{
		return 99999999u; // RW 0x799672: 0x5F5E0FF
	}
	return e->cost;
}

bool CastleBehavior::canAffordUnpack(const Player *player) const
{
	// RW 0x79969F
	if (!player || getObject()->getControllingPlayer() != player)
	{
		return false;
	}
	return player->getMoney()->countMoney() >= unpackCost(player);
}

bool CastleBehavior::canAffordUnpackOf(const Player *player, const ThingTemplate *tt) const
{
	// RW 0x797B6D
	if (!player || !tt || getObject()->getControllingPlayer() != player)
	{
		return false;
	}
	return player->getMoney()->countMoney() >= (std::uint32_t)BuildAssistant::calcCostToBuild(*tt, player, nullptr, -1);
}

float CastleBehavior::unpackReadiness() const
{
	// RW 0x797EF9: x87 1.0 - timer / UnpackDelayTime (fld, fdiv, fsubr) in state 0, else 0.0 (RW 0xC1B594)
	if (m_state != STATE_IDLE)
	{
		return 0.0f;
	}
	return SimMath::fstpDword(SimMath::pc24SubW(1.0, SimMath::pc24DivW((double)m_timer, (double)m_data->m_unpackDelayTime)));
}

void CastleBehavior::payForUnpack()
{
	// RW 0x799039 (BFME2 decomp Rva00396B25Receiver.cpp, tier B same-shape): the side's entry price (record + 0x18, without the limit test) is withdrawn
	// (Money RW 0x7B17EF with the score keeper, sound on) and kept at + 0x4C (fild of the unsigned amount, the 2^32 correction for one above 2^31)
	Player *player = getObject()->getControllingPlayer();
	const CastleFactionEntry *e = player ? m_data->entryFor(player->getSide()) : nullptr;
	if (!e)
	{
		return;
	}
	player->withdrawMoney(e->cost, true);
	m_storedPrice = SimMath::fstpDword(SimMath::fildU32(e->cost));
}

void CastleBehavior::payForUnpackOf(const ThingTemplate &tt)
{
	// RW 0x797B06: the template's calcCostToBuild(player, no producer, -1) is withdrawn and kept at + 0x4C
	Player *player = getObject()->getControllingPlayer();
	if (!player)
	{
		return;
	}
	const std::uint32_t cost = (std::uint32_t)BuildAssistant::calcCostToBuild(tt, player, nullptr, -1);
	player->withdrawMoney(cost, true);
	m_storedPrice = SimMath::fstpDword(SimMath::fildU32(cost));
}

void CastleBehavior::initiateUnpack(bool instant, const ThingTemplate *explicitTemplate)
{
	// RW 0x79C17D
	Object *castle = getObject();
	if (explicitTemplate)
	{
		m_explicitTemplate = explicitTemplate->getName(); // RW 0x79C18D: the template's name (+ 0x64) into + 0x98
	}
	m_storedPrice = 0.0f; // RW 0x79C1A4
	if (instant)
	{
		if (!unpack(true))
		{
			castle->logic().reportError("CastleBehavior of object " + std::to_string(castle->getID()) + ": the instant unpack failed: " + m_lastError);
		}
		castle->setModelConditionState(castleNames().justBuilt, true); // RW 0x79C1B4: word + 0x124 bit 26
		m_state = STATE_UNPACKED;                                       // RW 0x79C1CE
		return;
	}
	m_state = STATE_PACKED; // RW 0x79C1D9: state 1
	if (explicitTemplate)
	{
		payForUnpackOf(*explicitTemplate); // RW 0x79C1E3
	}
	else
	{
		payForUnpack(); // RW 0x79C1EA
	}
	// RW 0x79C1F5: the alert map's reset (RW 0x7985AA on + 0xA0) is not ported (S-951)
}

// RW 0x799ACB
bool CastleBehavior::shouldAbandon()
{
	const CastleNames &n = castleNames();
	Object *castle = getObject();
	GameLogic &logic = castle->logic();
	// RW 0x799ACC ..: ftol(5 * SecondsBeforeBaseCheckActive), the VictoryConditions delay
	const unsigned delay = (unsigned)SimMath::truncToInt32(SimMath::mulf32(SimMath::sseFromInt32(5), logic.settings().secondsBeforeBaseCheckActive));
	if (logic.getFrame() < delay)
	{
		return false;
	}
	Object *keep = m_keepId != INVALID_ID ? logic.findObjectByID(m_keepId) : nullptr;
	bool killAll = false;
	if (!keep)
	{
		if (m_data->m_keepDeathKillsEverything)
		{
			killAll = true;
		}
		else
		{
			m_keepGone = true; // RW 0x799BCF
		}
	}
	else
	{
		m_keepGone = false;
		if (keep->isEffectivelyDead())
		{
			if (m_data->m_keepDeathKillsEverything)
			{
				killAll = true;
			}
			m_keepGone = true;
		}
		if (!killAll)
		{
			if (keep->testModelCondition(n.awaitingConstruction))
			{
				m_keepGone = true; // RW 0x799B8A
			}
			if (keep->testModelCondition(n.partiallyConstructed))
			{
				const Object *builder = keep->getBuilderID() != INVALID_ID ? logic.findObjectByID(keep->getBuilderID()) : nullptr;
				if (!builder || (builder->isEffectivelyDead() && builder->isKindOf((unsigned)n.dozer)))
				{
					m_keepGone = true; // RW 0x799BBE
				}
			}
			if (keep->testModelCondition(n.postCollapse))
			{
				m_keepGone = true; // RW 0x799BCF
			}
		}
	}
	if (killAll)
	{
		// RW 0x799B42: everything dies, the foundations and the castle are destroyed; the castle counts for nothing from now on
		forEachMember(true);
		forEachMember(false);
		m_keepGone = true;
		logic.destroyObject(castle);
		return false;
	}
	if (!m_keepGone)
	{
		return false;
	}
	// RW 0x799BD8: a foundation (not a WALL_UPGRADE) that holds a structure keeps the castle
	for (const std::vector<ObjectID> *list : { &m_foundations, &m_defenseFoundations })
	{
		for (ObjectID id : *list)
		{
			const Object *o = logic.findObjectByID(id);
			if (o && !o->isKindOf((unsigned)n.wallUpgrade) && occupantOf(*o))
			{
				return false;
			}
		}
	}
	return true;
}

// RW 0x7999E2: the callback on the castle, its occupant, then the lists + 0x74, + 0x50, + 0x68, + 0x5C (ids looked up at their turn)
void CastleBehavior::forEachMember(bool kill)
{
	const CastleNames &n = castleNames();
	Object *castle = getObject();
	GameLogic &logic = castle->logic();
	auto apply = [&](Object *o) {
		if (!o)
		{
			return;
		}
		if (kill)
		{
			// RW 0x797F16: a BASE_FOUNDATION's occupant dies in its place; Object::kill(UNRESISTABLE, NORMAL)
			Object *victim = o->isKindOf((unsigned)n.baseFoundation) ? occupantOf(*o) : o;
			if (victim)
			{
				victim->kill(DEATH_NORMAL);
			}
		}
		else if (o->isKindOf((unsigned)n.baseFoundation))
		{
			logic.destroyObject(o); // RW 0x797F49
		}
	};
	apply(castle);
	apply(occupantOf(*castle)); // RW 0x7999F8: the +0x20 interface's slot 0x18
	for (const std::vector<ObjectID> *list : { &m_defenseFoundations, &m_foundations, &m_crew, &m_members })
	{
		const std::vector<ObjectID> ids = *list;
		for (ObjectID id : ids)
		{
			apply(logic.findObjectByID(id));
		}
	}
}

// RW 0x79A017: the keep and the list + 0x5C
void CastleBehavior::setMembersStatus(int status, bool on)
{
	GameLogic &logic = getObject()->logic();
	if (Object *keep = m_keepId != INVALID_ID ? logic.findObjectByID(m_keepId) : nullptr)
	{
		keep->setStatus((unsigned)status, on);
	}
	for (ObjectID id : m_members)
	{
		if (Object *o = logic.findObjectByID(id))
		{
			o->setStatus((unsigned)status, on);
		}
	}
}

// RW 0x79A237 (the crew's own release RW 0x798344 is not ported: S-951)
void CastleBehavior::releaseCrew(bool kill)
{
	GameLogic &logic = getObject()->logic();
	for (ObjectID id : m_crew)
	{
		if (Object *o = logic.findObjectByID(id))
		{
			logic.noteStop("[S-951] castle: the crew's release (RW 0x798344, EntEnragedUpdate / LifetimeUpdate) is not ported");
			if (kill)
			{
				o->kill(DEATH_NORMAL); // RW 0x79A2D9: Object::kill(UNRESISTABLE, NORMAL)
			}
		}
	}
	m_crew.clear();
	setMembersStatus(castleNames().doNotPickMe, false); // RW 0x79A36B
}

// RW 0x79C78C: the movable units near the castle are ordered out (see the header for what is inference)
void CastleBehavior::pushUnitsOut()
{
	const CastleNames &n = castleNames();
	Object *castle = getObject();
	GameLogic &logic = castle->logic();
	const Coord3D centre = *castle->getPosition();
	// RW 0x79C7D9: the castle's bounding sphere (GeometryInfo + 0x14) * 1.1 (x87 fmul, stored as a float)
	PathfindGeometry geometry;
	ObjectGeometry::fillPathfindGeometry(*static_cast<const ThingTemplate *>(castle->getTemplate())->getFinalOverride(), geometry);
	const float radius = SimMath::mulf32(geometry.boundingSphereRadius(), 1.1f);
	logic.noteStop("[S-952] castle: the push out's geometry overlap filter (RW 0x67C5F3 -> RW 0xAD2CE0) is not ported: the partition query (RW 0x79C826, type 3) runs without it");
	std::vector<Object *> movers;
	// RW 0x79C826: ThePartitionManager, distance type 3 (FROM_BOUNDINGSPHERE_3D), unsorted (lane MODULES-2)
	for (const PartitionHit &hit : logic.partition().iterateObjectsInRange(centre, radius, FROM_BOUNDINGSPHERE_3D, {}, ITER_FASTEST))
	{
		Object *o = hit.object;
		if (o == castle || o->isDestroyed())
		{
			continue;
		}
		if (o->isKindOf((unsigned)n.inert) || o->isKindOf((unsigned)n.moveOnly) || o->isKindOf((unsigned)n.baseFoundation) || o->isKindOf((unsigned)n.walkOnTopOfWall) ||
			o->isKindOf((unsigned)n.immobile))
		{
			continue; // RW 0x79C849 .. 0x79C887
		}
		if (o->getAIUpdateInterface())
		{
			movers.push_back(o); // RW 0x79C8AA: the AI (+ 0x260); RW 0x79C88F's slot 0x1C8 test is not ported (S-952)
		}
	}
	const float out = SimMath::mulf32(radius, 1.5f); // RW 0x79C920
	for (Object *o : movers)
	{
		float dx = SimMath::subf32(o->getPosition()->x, centre.x), dy = SimMath::subf32(o->getPosition()->y, centre.y);
		// RW 0x79C8E8: the 2D length (RW 0x405482); above 0 the vector is normalised with z = 0 (RW 0x403175), else (1, 0); the root's precision is stop S-167
		if (SimMath::length2d(dx, dy) > 0.0f)
		{
			const float len = (float)SimMath::length3d(dx, dy, 0.0f);
			dx = SimMath::divf32(dx, len);
			dy = SimMath::divf32(dy, len);
		}
		else
		{
			dx = 1.0f;
			dy = 0.0f;
		}
		Coord3D dest;
		dest.x = SimMath::addf32(SimMath::mulf32(out, dx), centre.x);
		dest.y = SimMath::addf32(SimMath::mulf32(out, dy), centre.y);
		dest.z = SimMath::addf32(centre.z, SimMath::mulf32(out, 0.0f));
		o->getAIUpdateInterface()->aiMoveToPosition(dest, CMD_FROM_AI); // RW 0x79C967: aiMoveToPosition(dest, 2)
	}
}

// RW 0x79CB47
void CastleBehavior::abandon()
{
	const CastleNames &n = castleNames();
	Object *castle = getObject();
	// RW 0x799440: the camp-destroyed EVA of the local player (client)
	m_state = STATE_ABANDONED;
	pushUnitsOut();
	// RW 0x79CC12 ..: the castle's model conditions and selectability
	castle->setModelConditionState(n.justBuilt, false);
	if (castle->testModelCondition(n.unpacking) || !castle->testModelCondition(n.packing))
	{
		castle->setModelConditionState(n.unpacking, false);
		castle->setModelConditionState(n.packing, true);
	}
	castle->setStatus((unsigned)n.unselectable, false); // RW 0x79CC59
	setMembersStatus(n.noAttack, true);                 // RW 0x79CC75: RW 0x79A017(NO_ATTACK, 1)
	m_timer = m_data->m_fadeTime;                       // RW 0x79CC83
}

// RW 0x79CCF2
void CastleBehavior::pack()
{
	Object *castle = getObject();
	GameLogic &logic = castle->logic();
	for (ObjectID id : m_foundations)
	{
		if (Object *o = logic.findObjectByID(id))
		{
			logic.destroyObject(o);
		}
	}
	m_foundations.clear();
	for (ObjectID id : m_members)
	{
		if (Object *o = logic.findObjectByID(id))
		{
			o->setStatus(kPackStatus, false);
			logic.destroyObject(o);
		}
	}
	m_members.clear();
	for (ObjectID id : m_defenseFoundations)
	{
		if (Object *o = logic.findObjectByID(id))
		{
			logic.destroyObject(o);
		}
	}
	m_defenseFoundations.clear();
	m_hasWalkOnWall = false;
	releaseCrew(true); // RW 0x79CDB5: RW 0x79A237(1)
	// RW 0x79A37D frees the castle's decal objects (+ 0x80, client)
	if (Object *keep = m_keepId != INVALID_ID ? logic.findObjectByID(m_keepId) : nullptr)
	{
		logic.destroyObject(keep);
	}
	m_keepId = INVALID_ID;
	// RW 0x79CE0E: the castle returns to PlyrCivilian (the binary's own name, RW 0xC24164)
	if (Player *civilian = logic.players().findPlayerWithName("PlyrCivilian"))
	{
		if (civilian->getDefaultTeam())
		{
			castle->setTeam(civilian->getDefaultTeam());
		}
	}
	pushUnitsOut();
	castle->logic().noteStop("[S-952] castle: a packed castle's upgrades are not reset (RW 0x68E083)");
	if (AIWorld *ai = logic.aiWorld())
	{
		ai->addObjectToPathfindMap(*castle); // RW 0x79CE5B: RW 0x6E85E9
	}
}

// RW 0x79B3C4
void CastleBehavior::scanForCapture()
{
	const CastleNames &n = castleNames();
	Object *castle = getObject();
	GameLogic &logic = castle->logic();
	// RW 0x7983E3(0): no instant build pending and state 0
	if (m_needInstantBuild || m_state != STATE_IDLE)
	{
		return;
	}
	Player *civilian = logic.players().findPlayerWithName("PlyrCivilian");
	if (!(m_data->m_scanDistance > 0.0f) || m_data->m_instantUnpack)
	{
		return;
	}
	int score[20];
	for (int &v : score)
	{
		v = -1; // RW 0x79B500: 20 entries of -1
	}
	std::vector<const Player *> seen; // RW 0x79B505: a set of players (the contest test only asks whether one of them is not an ally)
	bool contested = false;
	// RW 0x79B4D2: ThePartitionManager within ScanDistance, distance type 0 (FROM_CENTER_2D), unsorted; the filters (RW 0x797E8D, RW 0xC10E20 alive, the KindOf filter
	// RW 0x797EAC) are pure tests, applied below in the hits' order (lane MODULES-2)
	for (const PartitionHit &hit : logic.partition().iterateObjectsInRange(*castle->getPosition(), m_data->m_scanDistance, FROM_CENTER_2D, {}, ITER_FASTEST))
	{
		Object *o = hit.object;
		if (o->isDestroyed() || o->isEffectivelyDead())
		{
			continue; // RW 0xC10E20: alive
		}
		if (!(o->isKindOf((unsigned)n.infantry) || o->isKindOf((unsigned)n.cavalry) || o->isKindOf((unsigned)n.monster) || o->isKindOf((unsigned)n.machine)) ||
			o->isKindOf((unsigned)n.noBaseCapture))
		{
			continue; // RW 0xC309B4 / 0xC309C0
		}
		const Player *p = o->getControllingPlayer();
		if (!p || p == civilian)
		{
			continue;
		}
		for (const Player *q : seen)
		{
			if (p->getRelationship(q->getDefaultTeam()) != ALLIES)
			{
				contested = true; // RW 0x79B569
				break;
			}
		}
		bool known = false;
		for (const Player *q : seen)
		{
			known = known || q == p;
		}
		if (!known)
		{
			seen.push_back(p);
		}
		const int index = p->getPlayerIndex();
		if (index < 0 || index >= 20)
		{
			continue;
		}
		const int w = p->getPlayerType() == PLAYER_HUMAN ? 2 : 1; // RW 0x79B57D: (Player + 0x5C == 0) + 1
		if (score[index] < 0)
		{
			score[index] = w;
		}
		score[index] = CpMath::add(score[index], w);
		score[index] = CpMath::add(score[index], CpMath::mul((int)Economy::templateInt(*static_cast<const ThingTemplate *>(o->getTemplate()), "CommandPoints"), w));
	}
	int best = -1, bestScore = -1;
	for (int i = 0; i < 20; ++i)
	{
		if (score[i] > bestScore)
		{
			bestScore = score[i];
			best = i;
		}
	}
	Player *winner = best >= 0 ? logic.players().getNthPlayer(best) : nullptr;
	Team *newTeam = nullptr;
	if (winner && !contested)
	{
		if (winner->getDefaultTeam() != castle->getTeam() && isPlayerAllowedToCapture(*winner))
		{
			newTeam = winner->getDefaultTeam(); // RW 0x79B60F
		}
	}
	else if (logic.getFrame() > 5 && civilian && civilian != castle->getControllingPlayer() && civilian->getDefaultTeam())
	{
		newTeam = civilian->getDefaultTeam(); // RW 0x79B639
	}
	if (newTeam)
	{
		castle->setTeam(newTeam); // RW 0x79B63C (then RW 0x68C18F / 0x68E31F / 0x68D8A8)
	}
}

std::vector<std::string> CastleBehavior::stopLines()
{
	return {
		"[S-950] castle: PreBuiltList camps: closed by lane BACKLOG2 (RW 0x79C31B -> 0x79A5EC / 0x79A518: one instant structure per entry on the castle's pad of that index, "
		"through the pad's creation interface RW 0x859198: the site clearing RW 0x858C41, construct RW 0x858701, the push out RW 0x858CFC of the allied / neutral units "
		"to the nearer of two drawn spots; GameLogic/ConstructionSite.h); its inference (an object's own geometry, a unit's x axis) stays open in S-653",
		"[S-951] castle: the castle after its unpack is ported (registerOwnedObject RW 0x79AC19, the keep check RW 0x799ACB, KeepDeathKillsEverything RW 0x7999E2, abandon RW 0x79CB47, the "
		"fade, pack RW 0x79CCF2, the capture scan RW 0x79B3C4), and lane BACKLOG2 ported a player's unpack and pack: the dispatcher cases MSG_CASTLE_UNPACK RW 0x77C2C1, MSG_CASTLE_PACK "
		"RW 0x77C265 and MSG_CASTLE_UNPACK_EXPLICIT_OBJECT RW 0x77C338 (canUnpack RW 0x7983E3, RW 0x79A472, the side's price RW 0x7995FB / 0x79969F with CastleToUnpackForFaction's "
		"cost and limit columns RW 0x79BD2F, the template's RW 0x797B6D, initiateUnpack RW 0x79C17D with the payments RW 0x799039 / 0x797B06, the explicit object RW 0x79B98B), the "
		"buttons' availability (RW 0x942733 cases 33 / 34 / 50) and messages (RW 0x940AAC .. 0x940AE9); not ported: the wall straggler teleport RW 0x79ADE9 (FindPositionAround's "
		"FPF_USE_HIGHEST_LAYER is not), the members' RW 0x798057, the crew (RW 0x79B0F1, 0x798397, 0x798344), the castle's alert map (+ 0xA0, RW 0x79B374, its reset RW 0x7985AA), "
		"RW 0x625E0A, the camp EVA (RW 0x799440), the button's readiness clock and the client fades / decals; reached at runtime they are noted",
		"[S-952] castle: inference in the ported states: the capture scan and the push out run on ThePartitionManager (types 0 and 3, S-1020), the "
		"push out's geometry overlap filter (RW 0x67C5F3 -> RW 0xAD2CE0) and the horde's slot 0x1C8 test (RW 0x79C88F) are not ported, a plot's occupant counts while the object exists (retail clears the "
		"plot's + 0x28 in the plot's own update RW 0x8584DB), a packed castle's upgrades are not reset (RW 0x68E083); FilterValidOwnedEntries unset is taken as accepting everything; RW 0x683955 and "
		"RW 0x79B881 of the unpack were not read; retail's member + 0x14 is the keep's id (RW 0x7990D9 / 0x79A09D), the port keeps a pad's occupant there (RW 0x79A518's keep test is not ported)",
	};
}

void CastleBehavior::crc(StateHasher &h) const
{
	FoundationAIUpdate::crc(h);
	h.addI32(m_state);
	h.addBool(m_needInstantBuild);
	if (m_data->m_useNewCastleSystem)
	{
		h.addBool(m_bfme1StartChecked); // BFME1 flags only: RotWK castles keep their hashes
	}
	h.addBool(m_keepGone);
	h.addBool(m_hasWalkOnWall);
	h.addFloat(m_timer);
	h.addU32(m_keepId);
	h.addU32(m_unpackFrame);
	if (m_storedPrice != 0.0f || !m_explicitTemplate.empty())
	{
		h.addFloat(m_storedPrice); // lane BACKLOG2: hashed once a player's unpack set them (the starting fortress keeps its hash)
		h.addString(m_explicitTemplate);
	}
	for (const std::vector<ObjectID> *list : { &m_owned, &m_foundations, &m_members, &m_crew, &m_defenseFoundations })
	{
		h.addU32((std::uint32_t)list->size());
		for (ObjectID id : *list)
		{
			h.addU32(id);
		}
	}
}

// ---- CastleMemberBehavior -----------------------------------------------------------------------------------------------------------
CastleMemberBehavior::CastleMemberBehavior(Thing *thing, const CastleMemberBehaviorModuleData *data)
	: BehaviorModule(thing, data)
	, m_data(data)
{
}

bool CastleMemberBehavior::plotIsTaken() const
{
	if (m_occupantId == INVALID_ID)
	{
		return false;
	}
	const Object *occupant = getObject()->logic().findObjectByID(m_occupantId);
	return occupant && !occupant->isDestroyed();
}

void CastleMemberBehavior::crc(StateHasher &h) const
{
	BehaviorModule::crc(h);
	h.addU32(m_castleId);
	h.addU32(m_occupantId);
	h.addU32(m_keepId);
	h.addBool(m_breached);
}

void CastleMemberBehavior::onDamage(const DamageInfo &info)
{
	if (info.m_output.m_actualDamageDealt > 0.0f)
	{
		++getObject()->logic().combat().counters().castleMemberHits; // RW 0x79B374 (the castle's alert) is not ported (S-343)
	}
}

void CastleMemberBehavior::onBodyDamageStateChange(BodyDamageType, BodyDamageType newState)
{
	Object *obj = getObject();
	if (newState == BODY_RUBBLE)
	{
		if (m_data->m_countsForEvaCastleBreached && obj->getControllingPlayer())
		{
			// RW 0x79A10D..0x79A177 plays the EVA only for the LOCAL player's member: that filter is the client's (a peer-dependent test must not decide hashed state), the breach is recorded
			// for every owner
			obj->logic().combat().recordCastleBreach(obj->logic().getFrame(), obj->getID(), obj->getControllingPlayer()->getPlayerIndex());
		}
		m_breached = true;
	}
	else if (newState < BODY_REALLYDAMAGED)
	{
		m_breached = false;
	}
}

void CastleModules::registerAll(ModuleFactory &modules)
{
	CastleUpgrade::registerClass(modules); // lane FACT-B: the fortress improvements' upgrade module
	modules.bindTypedData<CastleBehaviorModuleData>("CastleBehavior", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("CastleBehavior", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const CastleBehaviorModuleData *typed = dynamic_cast<const CastleBehaviorModuleData *>(data);
		if (!typed)
		{
			throw std::logic_error("CastleBehavior: the module data is not typed");
		}
		return std::make_unique<CastleBehavior>(thing, typed);
	});
	modules.bindTypedData<CastleMemberBehaviorModuleData>("CastleMemberBehavior", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("CastleMemberBehavior", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const CastleMemberBehaviorModuleData *typed = dynamic_cast<const CastleMemberBehaviorModuleData *>(data);
		if (!typed)
		{
			throw std::logic_error("CastleMemberBehavior: the module data is not typed");
		}
		return std::make_unique<CastleMemberBehavior>(thing, typed);
	});
}
