// OpenBFME. GPL-3.0.
// Lane HORDE-2: a horde's banner carrier and the replenishment through it. See GameLogic/Module/BannerCarrierUpdate.h.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; read with Ghidra):
//   * HordeContain::update calls RW 0x8719E4(force = 0) every update (RW 0x8730A5; also from the contained state RW 0x871A6F): with an AI, the countdown H+0x27C is
//     decremented when not 0; at 0, with no carrier (H+0x26C) and BannerCarrierMinLevel (data + 0x27C, a byte) < the horde's veterancy level (object + 0x26C -> + 0x24), and
//     the horde not busy (RW 0x8B50A5 on object + 0x254, not ported): unless forced, nothing while frame - 4 * LOGICFRAMES_PER_SECOND <= the horde's last shot frame (firing
//     tracker + 0x44, RW 0x68B645); else RW 0x870204;
//   * RW 0x870204: with no carrier and BannerCarriersAllowed (data + 0x218) not empty, the first name's template is made on the horde's team (the original team for a
//     TEMPORARILY_DEFECTED horde) with the horde's transform; a horde under construction with a producer's exit interface hands it to the exit, else it stands at the
//     horde's position; it joins the horde (HordeContainInterface slot 0x74) and the countdown becomes its BannerCarrierUpdate's MeleeFreeBannerReSpawnTime (data + 0x14);
//   * BannerCarrierUpdate::update RW 0x89ACE4 and the member spawn RW 0x89A392 / 0x873AE3: see BannerCarrierUpdate.h.
// INFERENCE (stop S-587): the carrier's place in the formation is its BannerCarrierPosition entry (UnitType = the payload template, else the first entry), rotated like a
// slot; the carrier id is cleared when the carrier leaves the horde; the copied upgrades / weapon set flags (RW 0x68DEEC / 0x691059 with 0x18 .. 0x1A), the experience
// sharing (RW 0x79DC9E) and the producer's exit are not ported. The rank is XP-1's ExperienceTracker rank (lane INTEG-1); BannerCarrierMinLevel defaults to 1 (2.01
// hordes without the field get a carrier at rank 2), the Angmar hordes set 0 (a carrier at rank 1, from the start) or 6. A horde's rank gain calls bannerCheck(1)
// (RW 0x873ACF, HordeContainExperience.cpp).

#include "GameLogic/Module/BannerCarrierUpdate.h"

#include "Common/GameCommon.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/HordeContain.h"
#include "GameLogic/Object/Contain/HordeContainBehaviorData.h"
#include "GameLogic/Object/Contain/HordeContainCore.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"

#include <algorithm>
#include <stdexcept>

namespace
{
const char *const kStop =
	"[S-587] banner carriers: the spawn (RW 0x8719E4 / 0x870204: countdown, BannerCarrierMinLevel below the veterancy level, no shot for 4 seconds, BannerCarriersAllowed[0], "
	"the countdown from MeleeFreeBannerReSpawnTime; the rank is XP-1's ExperienceTracker rank, and a horde's rank gain forces the check, RW 0x873ACF) and BannerCarrierUpdate's "
	"replenishment (RW 0x89ACE4 / 0x89A392: every IdleSpawnRate, out of combat for MeleeFreeUnitSpawnTime, a new payload member at the carrier while the horde is not full) are "
	"ported; NOT ported: RW 0x8B50A5, the AllowBannerSpawnUpgrade test, ReplenishNearbyHorde (RW 0x89AB8F), the copied upgrades / experience, the producer's exit, MorphCondition / ExpLevelDraw "
	"grammars (raw tokens) and the FX; INFERENCE: the carrier stands at its BannerCarrierPosition, the carrier id is cleared when it leaves, 'in combat' is a member damaged or "
	"attacking within the window (RW 0x68C933 not read)";

HordeContain *hordeContainOf(Object *o)
{
	if (!o)
	{
		return nullptr;
	}
	ContainModuleInterface *c = o->getContain();
	return c && c->getHordeContainInterface() ? dynamic_cast<HordeContain *>(c) : nullptr;
}

const FieldParse kBannerParse[] = {
	{ "IdleSpawnRate", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(BannerCarrierUpdateModuleData, m_idleSpawnRate) },
	{ "MeleeFreeUnitSpawnTime", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(BannerCarrierUpdateModuleData, m_meleeFreeUnitSpawnTime) },
	{ "DiedRespawnTime", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(BannerCarrierUpdateModuleData, m_diedRespawnTime) },
	{ "MeleeFreeBannerReSpawnTime", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(BannerCarrierUpdateModuleData, m_meleeFreeBannerReSpawnTime) },
	{ "MorphCondition", BannerCarrierUpdateModuleData::parseRawLine, nullptr, (int)offsetof(BannerCarrierUpdateModuleData, m_morphConditions) },
	{ "ExpLevelDraw", BannerCarrierUpdateModuleData::parseRawLine, nullptr, (int)offsetof(BannerCarrierUpdateModuleData, m_expLevelDraws) },
	{ "BannerMorphFX", INI::parseAsciiString, nullptr, (int)offsetof(BannerCarrierUpdateModuleData, m_bannerMorphFX) },
	{ "UnitSpawnFX", INI::parseAsciiString, nullptr, (int)offsetof(BannerCarrierUpdateModuleData, m_unitSpawnFX) },
	{ "ReplenishNearbyHorde", INI::parseBool, nullptr, (int)offsetof(BannerCarrierUpdateModuleData, m_replenishNearbyHorde) },
	{ "ReplenishAllNearbyHordes", INI::parseBool, nullptr, (int)offsetof(BannerCarrierUpdateModuleData, m_replenishAllNearbyHordes) },
	{ "ScanHordeDistance", INI::parseReal, nullptr, (int)offsetof(BannerCarrierUpdateModuleData, m_scanHordeDistance) },
	{ "UpgradeRequired", INI::parseAsciiString, nullptr, (int)offsetof(BannerCarrierUpdateModuleData, m_upgradeRequired) },
	{ nullptr, nullptr, nullptr, 0 }
};
} // namespace

void BannerCarrierUpdateModuleData::parseRawLine(INI *ini, void *, void *store, const void *)
{
	std::string line;
	for (const char *tok = ini->getNextTokenOrNull(); tok; tok = ini->getNextTokenOrNull())
	{
		line += (line.empty() ? "" : " ") + std::string(tok);
	}
	static_cast<std::vector<std::string> *>(store)->push_back(line);
}

void BannerCarrierUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kBannerParse); // RW table 0xC66090
}

const char *BannerCarrierUpdate::stopLine()
{
	return kStop;
}

BannerCarrierUpdate::BannerCarrierUpdate(Thing *thing, const BannerCarrierUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	setWakeFrame(getObject(), UPDATE_SLEEP((int)(data->m_idleSpawnRate > 0 ? data->m_idleSpawnRate : 1)));
}

void BannerCarrierUpdate::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<BannerCarrierUpdateModuleData>("BannerCarrierUpdate", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("BannerCarrierUpdate", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const BannerCarrierUpdateModuleData *typed = dynamic_cast<const BannerCarrierUpdateModuleData *>(data);
		if (!typed)
		{
			throw std::logic_error("BannerCarrierUpdate: the module data is not typed");
		}
		return std::make_unique<BannerCarrierUpdate>(thing, typed);
	});
}

// RW 0x89ACE4
UpdateSleepTime BannerCarrierUpdate::update()
{
	Object *obj = getObject();
	const UpdateSleepTime sleep = UPDATE_SLEEP((int)(m_data->m_idleSpawnRate > 0 ? m_data->m_idleSpawnRate : 1));
	if (!m_data->m_upgradeRequired.empty() && !obj->hasUpgrade(m_data->m_upgradeRequired))
	{
		return sleep;
	}
	if (obj->testModelCondition(0x45) || obj->testStatus(0x57) || obj->testModelCondition(0x220) || obj->testStatus(0x3E))
	{
		return sleep;
	}
	if (AIUpdateInterface *ai = obj->getAIUpdateInterface())
	{
		if (ai->isMoving() || ai->mover().isWaitingForPath())
		{
			return sleep;
		}
	}
	if (m_data->m_replenishNearbyHorde)
	{
		return sleep; // RW 0x89AB8F (S-587)
	}
	if (obj->testModelCondition(0x25) || obj->testModelCondition(0x3D))
	{
		return sleep;
	}
	const unsigned frame = obj->logic().getFrame();
	if (ObjectWeapons *w = obj->getWeapons())
	{
		if (w->firingTracker().lastShotFrame() != 0 && !(w->firingTracker().lastShotFrame() <= frame - m_data->m_meleeFreeUnitSpawnTime))
		{
			return sleep;
		}
	}
	HordeContain *hc = hordeContainOf(obj->getContainedBy());
	if (!hc)
	{
		return sleep;
	}
	if (hc->wasInCombatWithin(m_data->m_meleeFreeUnitSpawnTime))
	{
		return sleep;
	}
	// RW 0x89A3CE .. 0x89A3E4 (lane INTEG-1): a member is made only while the horde's slot 0x188 (slot 0x180(0) RW 0x8706DF: the contain count plus the field at
	// interface + 0x58; minus slot 0x184 RW 0x86C085: one for each of H+0x264 and the banner carrier H+0x26C) is below slot 0x17C (RW 0x86C8DE: the contain's Slots,
	// module data + 0x98). HORDE-2's port only asked for a free RankInfo position, so a horde with more positions than Slots (AngmarNecromancerHorde: 15 / 7) grew
	// past its capacity once real carriers appeared (XP-1's rank). INFERENCE: interface + 0x58 and H+0x264 are taken as 0 / none (not identified)
	const int counted = (int)hc->getContainCount() - (hc->bannerCarrier() != 0 ? 1 : 0);
	if (counted >= hc->getSlotCapacity())
	{
		return sleep;
	}
	if (hc->spawnReplacementMember(*obj->getPosition())) // RW 0x89A392
	{
		++m_spawned;
	}
	return sleep;
}

void BannerCarrierUpdate::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addU64(m_spawned);
}

// ---- HordeContain's side ------------------------------------------------------------------------------------------------------------------------------------------------
// RW 0x8719E4
void HordeContain::bannerCheck(bool force)
{
	Object *horde = getObject();
	if (!horde->getAIUpdateInterface())
	{
		return;
	}
	if (m_bannerCountdown != 0)
	{
		--m_bannerCountdown;
	}
	const HordeContainModuleData &data = m_data->horde;
	if (m_bannerCountdown != 0 || m_bannerCarrier != 0)
	{
		return;
	}
	// RW 0x871A1B: the horde object's ExperienceTracker (object + 0x26C, read without a null test: every object has one, RW 0x69A606) rank (+ 0x24, lane XP-1),
	// `jle`: a carrier needs BannerCarrierMinLevel < rank (lane INTEG-1 connected XP-1's tracker here)
	const ExperienceTracker *tracker = horde->getExperienceTracker();
	if (!tracker)
	{
		throw std::logic_error("HordeContain::bannerCheck: the horde object has no ExperienceTracker (every object is made with one, RW 0x69A606)");
	}
	if (!((int)data.m_bannerCarrierMinLevel < tracker->getRank()))
	{
		return;
	}
	const unsigned frame = horde->logic().getFrame();
	if (!force)
	{
		const unsigned lastShot = horde->getWeapons() ? horde->getWeapons()->firingTracker().lastShotFrame() : 0u;
		if ((unsigned)(frame - 4u * (unsigned)LOGICFRAMES_PER_SECOND) <= lastShot)
		{
			return;
		}
	}
	// RW 0x870204
	if (data.m_bannerCarriersAllowed.empty())
	{
		return;
	}
	const ThingTemplate *tt = horde->logic().things().findTemplate(data.m_bannerCarriersAllowed[0]);
	if (!tt)
	{
		return;
	}
	Object *carrier = horde->logic().newObject(tt, horde->getTeam(), ObjectStatusMaskType{});
	if (!carrier)
	{
		return;
	}
	carrier->setPosition(horde->getPosition());
	carrier->setOrientation(horde->getOrientation());
	m_bannerCarrier = carrier->getID();
	addToContain(carrier);
	if (BannerCarrierUpdate *b = dynamic_cast<BannerCarrierUpdate *>(carrier->findModule("BannerCarrierUpdate")))
	{
		m_bannerCountdown = b->data().m_meleeFreeBannerReSpawnTime;
	}
	m_dirty = true;
}

bool HordeContain::bannerMemberSlot(const Object &member, Coord3D &pos) const
{
	if (member.getID() != m_bannerCarrier || m_data->horde.m_bannerCarrierPosition.empty())
	{
		return false;
	}
	const std::string payload = getPayloadMemberTemplateName();
	const BannerCarrierPositionEntry *e = &m_data->horde.m_bannerCarrierPosition[0];
	for (const BannerCarrierPositionEntry &c : m_data->horde.m_bannerCarrierPosition)
	{
		if (c.unitType == payload)
		{
			e = &c;
			break;
		}
	}
	const Object *horde = getObject();
	const float a = horde->getOrientation();
	const float c = SimMath::cosDet(a), s = SimMath::sinDet(a);
	pos.x = SimMath::addf32(horde->getPosition()->x, SimMath::subf32(SimMath::mulf32(e->x, c), SimMath::mulf32(e->y, s)));
	pos.y = SimMath::addf32(horde->getPosition()->y, SimMath::addf32(SimMath::mulf32(e->x, s), SimMath::mulf32(e->y, c)));
	pos.z = horde->logic().getGroundHeight(pos.x, pos.y);
	return true;
}

// RW slot 0x18C (RW 0x873AE3) behind RW 0x89A392's "fewer members than slots"
Object *HordeContain::spawnReplacementMember(const Coord3D &pos)
{
	Object *horde = getObject();
	const std::vector<int> freeSlots = freeSlotIndices();
	if (freeSlots.empty())
	{
		return nullptr;
	}
	const std::string name = getPayloadMemberTemplateName();
	const ThingTemplate *tt = name.empty() ? nullptr : horde->logic().things().findTemplate(name);
	if (!tt)
	{
		return nullptr;
	}
	Object *m = horde->logic().newObject(tt, horde->getTeam(), ObjectStatusMaskType{});
	if (!m)
	{
		return nullptr;
	}
	m->setPosition(&pos);
	m->setOrientation(horde->getOrientation());
	acceptCreatedMember(m); // joins without being put on its slot: it walks there from the carrier
	m_producedMembers.erase(std::remove(m_producedMembers.begin(), m_producedMembers.end(), m->getID()), m_producedMembers.end());
	m_dirty = true;
	return m;
}

bool HordeContain::wasInCombatWithin(unsigned frames) const
{
	const unsigned frame = getObject()->logic().getFrame();
	for (const Object *m : m_members)
	{
		if (!m)
		{
			continue;
		}
		if (m->testStatus((unsigned)CombatNames::statuses().isAttacking))
		{
			return true;
		}
		if (const ActiveBody *b = dynamic_cast<const ActiveBody *>(m->getBodyModule()))
		{
			if (b->lastDamageFrame() != 0 && frame - b->lastDamageFrame() < frames)
			{
				return true;
			}
		}
	}
	return false;
}
