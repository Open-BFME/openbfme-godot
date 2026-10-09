// OpenBFME. GPL-3.0.
// See GameLogic/SkirmishAI/AIThreatFinder.h for the RW facts and what is not ported (S-1300).

#include "GameLogic/SkirmishAI/AIThreatFinder.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Thing/KindOfTokens.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SkirmishAI/SkirmishAIData.h"

#include <cstring>
#include <variant>

namespace
{
constexpr float kThreatLevelUnset = 1.0f;      // RW 0x73FE51: the template constructor's ThreatLevel (RW 0xBD1908)
constexpr float kThreatLevelFloor = 100.0f;    // RW 0xBD88D8
constexpr float kThreatLevelDefault = 150.0f;  // RW 0xC041F8
constexpr float kRankScale = 0.2f;             // RW 0xBDAD78
constexpr float kRankBase = 0.8f;              // RW 0xBDE8D8
constexpr float kRelativeRadius = 400.0f;      // RW 0xBDBCA0 (RW 0x6C650B / 0x6C6623)
constexpr int kObjectRank = 1;                 // object + 0x4B8: the constructor's 1 (RW 0x699D25); its writer RW 0x691A2F is not ported (S-1300)
constexpr float kCounterWeak = 0.25f;          // RW 0xBD1904 (and the double RW 0xBE4400)
constexpr float kCounterStrong = 1.5f;         // RW 0xBDE8C8

struct Kinds
{
	int structure = KindOfTokens::indexOf("STRUCTURE");
	int canAttack = KindOfTokens::indexOf("CAN_ATTACK");
	int hero = KindOfTokens::indexOf("HERO");
	int support = KindOfTokens::indexOf("SUPPORT");
	int baseDefense = KindOfTokens::indexOf("FS_BASE_DEFENSE");
};

const Kinds &kinds()
{
	static const Kinds k;
	return k;
}

bool is(const Object &o, int kind)
{
	return kind >= 0 && o.isKindOf((unsigned)kind);
}

// the template's ThreatLevel (+ 0x52C, parseReal RW 0x42ED00; the constructor's 1.0 when the template does not set it)
float threatLevel(const Object &obj)
{
	const ThingTemplate *t = obj.getTemplate();
	const ThingTemplate *f = t ? t->getFinalOverride() : nullptr;
	const FieldValue *v = f ? f->findField("ThreatLevel") : nullptr;
	const float *x = v ? std::get_if<float>(v) : nullptr;
	return x ? *x : kThreatLevelUnset;
}

} // namespace

// RW 0x7EDB72: the value goes to the total and to its category's slot; the category -1 (no ThreatBreakdown) is the total's slot again (+ 4 + 4 * -1 = + 0)
void AIThreatFinder::accumulate(ThreatRecord &r, int category, float v)
{
	r.v[0] = SimMath::addf32(r.v[0], v);                                    // RW 0x7EDB91 .. 0x7EDB9E
	const size_t slot = (size_t)(category + 1);
	r.v[slot] = SimMath::addf32(v, r.v[slot]);                               // RW 0x7EDBA2 .. 0x7EDBB3
}

namespace
{

// RW 0x7ED963: what the enemies' value `v` of category `cat` counts against the record `a` (whose matching categories it consumes): v * 0.25 when a counter is
// there (RW 0xBD1904), v * 1.5 when it beats what is there (RW 0xBDE8C8), else v (x87: the product stays in the register)
double counterOne(AIThreatFinder::ThreatRecord &a, int cat, float v)
{
	float mult = 1.0f; // RW 0xBD1908
	auto x87Less = [&](size_t slot) { a.v[slot] = SimMath::fstpDword(SimMath::pc24SubW((double)a.v[slot], SimMath::pc24MulW((double)v, 0.25))); }; // fld; fld v; fmul qword; fsubp
	auto sseLess = [&](size_t slot) { a.v[slot] = SimMath::subf32(a.v[slot], v); };
	switch (cat)
	{
	case 0: // RW 0x7EDA94: INFANTRY against CAVALRY (+ 0x10), else PIKEMAN (+ 0xC)
		if (a.v[4] > 0.0f)
		{
			a.v[4] = SimMath::subf32(a.v[4], SimMath::mulf32(v, kCounterWeak));
			mult = kCounterWeak;
		}
		else if (a.v[3] > 0.0f)
		{
			sseLess(3);
			mult = kCounterStrong;
		}
		break;
	case 2: // RW 0x7EDA54: PIKEMAN against INFANTRY (+ 4), else CAVALRY (+ 0x10)
		if (a.v[1] > 0.0f)
		{
			x87Less(1);
			mult = kCounterWeak;
		}
		else if (a.v[4] > 0.0f)
		{
			sseLess(4);
			mult = kCounterStrong;
		}
		break;
	case 3: // RW 0x7EDA15: CAVALRY against PIKEMAN (+ 0xC), else INFANTRY (+ 4)
		if (a.v[3] > 0.0f)
		{
			x87Less(3);
			mult = kCounterWeak;
		}
		else if (a.v[1] > 0.0f)
		{
			sseLess(1);
			mult = kCounterStrong;
		}
		break;
	case 7: // RW 0x7ED99B: SIEGEWEAPON against CAVALRY (+ 0x10), else categories 6 (+ 0x1C), 9 (+ 0x28), 11 (+ 0x30)
		if (a.v[4] > 0.0f)
		{
			x87Less(4);
			mult = kCounterWeak;
		}
		else if (a.v[7] > 0.0f)
		{
			sseLess(7);
			mult = kCounterStrong;
		}
		else if (a.v[10] > 0.0f)
		{
			sseLess(10);
			mult = kCounterStrong;
		}
		else if (a.v[12] > 0.0f)
		{
			sseLess(12);
			mult = kCounterStrong;
		}
		break;
	default:
		break;
	}
	return SimMath::pc24MulW((double)v, (double)mult); // RW 0x7EDAE6: fmul dword [mult]
}

// RW 0x7EE166's sum over the scanned records
AIThreatFinder::ThreatRecord sumRecords(GameLogic &logic, const AIThreatFinder::ThreatRecord (&records)[AIThreatFinder::kRecords], const Player &owner, int mode)
{
	AIThreatFinder::ThreatRecord out; // RW 0x7ED8A3
	for (int i = 0; i < AIThreatFinder::kRecords; ++i)
	{
		const Player *p = logic.players().getNthPlayer(i); // RW 0x6A844E
		if (!p)
		{
			continue;
		}
		const Relationship r = owner.getRelationship(p); // RW 0x6ACEAF
		if ((mode == 0 && r == ENEMIES) || (mode == 1 && r == ALLIES))
		{
			for (int k = 0; k < AIThreatFinder::kSlots; ++k)
			{
				out.v[k] = SimMath::addf32(records[i].v[k], out.v[k]); // RW 0x7ED8D9
			}
		}
	}
	return out;
}
} // namespace

int AIThreatFinder::threatCategory(const ThingTemplate &tt)
{
	// RW 0x73C073: a ThreatBreakdown block parsed with the table RW 0xC26ABC (one field, AIKindOf, RW 0x8ED575 -> RW 0x8ED505) into template + 0x530; the
	// constructor's value is -1 (RW 0x73FE59). The block is kept as raw lines by the template loader (S-071); the last block's last AIKindOf wins
	int category = -1;
	const ThingTemplate *f = tt.getFinalOverride();
	for (const RawBlock &b : (f ? f : &tt)->rawBlocks())
	{
		if (b.field != "ThreatBreakdown")
		{
			continue;
		}
		for (const RawLine &line : b.lines)
		{
			std::string text = line.text;
			for (const char *c : { ";", "//" })
			{
				const size_t at = text.find(c);
				if (at != std::string::npos)
				{
					text.erase(at);
				}
			}
			std::vector<std::string> tokens;
			std::string cur;
			for (char ch : text)
			{
				if (ch == ' ' || ch == '\t' || ch == '=' || ch == '\r' || ch == '\n')
				{
					if (!cur.empty())
					{
						tokens.push_back(cur);
						cur.clear();
					}
				}
				else
				{
					cur.push_back(ch);
				}
			}
			if (!cur.empty())
			{
				tokens.push_back(cur);
			}
			if (tokens.size() >= 2 && tokens[0] == "AIKindOf")
			{
				for (int i = 0; i < SkirmishAI::AI_KINDOF_COUNT; ++i)
				{
					if (std::strcmp(SkirmishAI::TheAIKindOfNames[i], tokens[1].c_str()) == 0) // RW 0x8ED505 (an unknown name throws at load: S-1300)
					{
						category = i;
					}
				}
			}
		}
	}
	return category;
}

double AIThreatFinder::threatValueWide(const Object &obj)
{
	const Kinds &k = kinds();
	float local = 0.0f;
	if (is(obj, k.structure))
	{
		if (is(obj, k.canAttack))
		{
			if (is(obj, k.baseDefense))
			{
				return SimMath::pc24AddW((double)obj.getUpgradeCostPaid(), (double)obj.getBuildCostPaid()); // RW 0x68F116: fld + 0x340; fadd + 0x33C (unscaled)
			}
			local = threatLevel(obj);
			if (!(kThreatLevelFloor < local))
			{
				local = kThreatLevelDefault;
			}
		}
	}
	else
	{
		const float f = SimMath::addf32(SimMath::mulf32(SimMath::sseFromInt32(kObjectRank), kRankScale), kRankBase);
		local = SimMath::addf32(SimMath::mulf32(obj.getBuildCostPaid(), f), obj.getUpgradeCostPaid());
		if (local == 0.0f)
		{
			local = SimMath::mulf32(threatLevel(obj), f);
		}
	}
	// the body's slot 0x14 (RW 0x8C1D75: fld health; fdiv max, 0 without a positive max), then fmul local (RW 0x68F1A3)
	const BodyModuleInterface *body = obj.getBodyModule();
	double ratio = 0.0;
	if (body && body->getMaxHealth() > 0.0f)
	{
		ratio = SimMath::pc24DivW((double)body->getHealth(), (double)body->getMaxHealth());
	}
	return SimMath::pc24MulW(ratio, (double)local);
}

float AIThreatFinder::threatValue(const Object &obj)
{
	return SimMath::fstpDword(threatValueWide(obj)); // RW 0x7EDB83: fstp dword
}

void AIThreatFinder::scan(GameLogic &logic, const Coord3D &position, float radius, bool noStructures, ThreatRecord (&records)[kRecords])
{
	for (ThreatRecord &r : records)
	{
		r = ThreatRecord{}; // RW 0x7EDB33
	}
	const Kinds &k = kinds();
	PartitionFilterFn alive([](Object &c) { return !c.isEffectivelyDead(); }); // RW 0xC10E20 slot 1 = RW 0x660E71
	const PartitionHits hits = logic.partition().iterateObjectsInRange(position, radius, FROM_CENTER_2D, { &alive }, ITER_FASTEST);
	for (const PartitionHit &hit : hits)
	{
		const Object &o = *hit.object;
		const bool structure = is(o, k.structure);
		const bool wanted = is(o, k.canAttack) || is(o, k.hero) || is(o, k.support) || (!noStructures && structure);
		if (!wanted || (noStructures && structure))
		{
			continue;
		}
		const Player *p = o.getControllingPlayer(); // RW 0x68B678
		if (!p)
		{
			continue; // RW reads the player's + 0x54 without a test (an object always has a team there)
		}
		const int index = p->getPlayerIndex();
		const float v = threatValue(o);
		if (0.0f < v && index >= 0 && index < kRecords && o.getTemplate())
		{
			accumulate(records[index], threatCategory(*o.getTemplate()), v);
		}
	}
}

float AIThreatFinder::threatTotal(GameLogic &logic, const Coord3D &position, float radius, bool noStructures, const Player &owner, int mode)
{
	ThreatRecord records[kRecords];
	scan(logic, position, radius, noStructures, records);
	return sumRecords(logic, records, owner, mode).v[0];
}

float AIThreatFinder::relativeThreat(GameLogic &logic, const Coord3D &position, const Player &owner)
{
	// RW 0x6C650B: the target's finder moved to `position` with radius 400, enemies (mode 0, a rescan) then allies (mode 1: the frame is the scan's, no
	// rescan), the finder restored
	ThreatRecord records[kRecords];
	scan(logic, position, kRelativeRadius, true, records);
	const float enemies = sumRecords(logic, records, owner, 0).v[0];
	const float allies = sumRecords(logic, records, owner, 1).v[0];
	return 0.0f < allies ? SimMath::subf32(allies, enemies) : 0.0f; // RW 0x6C6600 .. 0x6C6608
}

double AIThreatFinder::counterDifference(ThreatRecord &a, const ThreatRecord &b, float *aTotal, float *sumOut)
{
	// RW 0x7EDC2F(this = a, b): for each category c of 17 with a positive value in b, sum = fstp(RW 0x7ED963(c, value) + sum) (a's categories are consumed);
	// the outputs a's total and the sum; the result a's total minus the sum, in the register
	float sum = 0.0f;
	for (int c = 0; c < SkirmishAI::AI_KINDOF_COUNT; ++c)
	{
		const float x = b.v[c + 1];
		if (x > 0.0f)
		{
			sum = SimMath::fstpDword(SimMath::pc24AddW(counterOne(a, c, x), (double)sum)); // RW 0x7EDC5A .. 0x7EDC62
		}
	}
	if (aTotal)
	{
		*aTotal = a.v[0];
	}
	if (sumOut)
	{
		*sumOut = sum;
	}
	return SimMath::pc24SubW((double)a.v[0], (double)sum); // RW 0x7EDC8A: fld [ecx]; fsub
}

AIThreatFinder::ThreatRecord AIThreatFinder::scanSum(GameLogic &logic, const Coord3D &position, float radius, bool noStructures, const Player &owner, int mode)
{
	ThreatRecord records[kRecords];
	scan(logic, position, radius, noStructures, records);
	return sumRecords(logic, records, owner, mode);
}

double AIThreatFinder::counteredThreat(GameLogic &logic, const Coord3D &position, const Player &owner, float &alliesOut, float &countersOut)
{
	// RW 0x6C6623: as RW 0x6C650B, then, with a positive allies' total, RW 0x7EDC2F(this = allies, enemies); without allies 0 and the outputs untouched (the
	// caller zeroed them, RW 0x8F14A3)
	ThreatRecord records[kRecords];
	scan(logic, position, kRelativeRadius, true, records);
	const ThreatRecord enemies = sumRecords(logic, records, owner, 0);
	ThreatRecord allies = sumRecords(logic, records, owner, 1);
	if (!(0.0f < allies.v[0]))
	{
		return 0.0;
	}
	return counterDifference(allies, enemies, &alliesOut, &countersOut);
}
