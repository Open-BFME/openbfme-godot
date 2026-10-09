// OpenBFME. GPL-3.0.
//
// HordeContain: the experience pools and the attribute modifier hand-down (lane XP-1). See GameLogic/Object/Contain/HordeContainRuntime.h.
//
// TARGET FACTS (RotWK game.dat, caveat S-001), read from the disassembly:
//   * addExperience (HordeContainInterface vslot 0xC0, RW 0x873A02, member, amount): key = the member template's id (template + 0x5E8, 16 bits); the pool map
//     (interface + 0x13C) gets amount + 1.0f for a new key, else pool + amount (SSE single); every contained member of that template with a tracker is set to
//     the pool (setExperienceAndLevel(pool, false), RW 0x86BC09 through contain vslot 0x110); when the pool exceeds the horde's own experience, the horde's tracker
//     is set to it too (setExperienceAndLevel(pool, false)) and a rank gain runs RW 0x8719E4(1): HORDE-2's banner carrier check (HordeBanner.cpp), forced;
//   * addAttributeModifier (vslot 0x1D8, RW 0x870E50, name, filter, duration): unless the list is unknown or of category LEVEL (6), every member (contain vslot
//     0x118; the filter is null on this path) takes it (Object::addAttributeModifier), and so do the members of the second list (+0x54, not ported: S-486);
//     then the horde's own AttributeModifierPoolUpdate adds it (always, a LEVEL list too).
// INFERENCE: contain vslot 0x110 walks the members in list order (its third argument is 1 and was not identified).

#include "GameLogic/Object/Contain/HordeContainRuntime.h"

#include "GameLogic/Module/HordeAIUpdate.h"
#include "GameLogic/Module/HordeContain.h"

#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AttributeModifiers.h"
#include "GameLogic/ExperienceWorld.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/AttributeModifierPool.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"

#include <vector>

void HordeContain::addExperience(Object *member, float amount)
{
	if (!member)
	{
		return;
	}
	const unsigned short key = static_cast<const ThingTemplate *>(member->getTemplate())->getTemplateID();
	auto it = m_experiencePools.find(key);
	if (it == m_experiencePools.end())
	{
		it = m_experiencePools.emplace(key, SimMath::addf32(amount, 1.0f)).first; // RW 0x873A44
	}
	else
	{
		it->second = SimMath::addf32(it->second, amount); // RW 0x873A58
	}
	const float pool = it->second;
	const std::vector<Object *> members(m_members.begin(), m_members.end());
	for (Object *m : members) // RW 0x86BC09
	{
		if (static_cast<const ThingTemplate *>(m->getTemplate())->getTemplateID() != key)
		{
			continue;
		}
		if (ExperienceTracker *t = m->getExperienceTracker())
		{
			t->setExperienceAndLevel(pool, false);
		}
	}
	Object *horde = getObject();
	ExperienceTracker *ht = horde->getExperienceTracker();
	if (ht && pool > ht->getExperience()) // RW 0x873AA7
	{
		const int oldRank = ht->getRank(); // RW 0x873AB0
		ht->setExperienceAndLevel(pool, false);
		if (ht->getRank() > oldRank) // RW 0x873ACA `jle`
		{
			bannerCheck(true); // RW 0x873ACF: RW 0x8719E4(1), HORDE-2's banner carrier check, forced (lane INTEG-1)
		}
	}
}

// RW 0x870F75 (lane INTEG-1; the filter argument is null on the stances' path, so every member qualifies)
void HordeContain::removeAttributeModifier(const std::string &listName)
{
	const std::vector<Object *> members(m_members.begin(), m_members.end());
	for (Object *m : members) // contain slot 0x118
	{
		m->removeAttributeModifier(listName); // RW 0x870FCA
	}
	if (AttributeModifierPool *pool = static_cast<AttributeModifierPool *>(getObject()->findModule("AttributeModifierPoolUpdate"))) // RW 0x871030
	{
		pool->remove(listName); // RW 0x871041: RW 0x8052FB
	}
}

// slot 0x260 (RW 0x86C40D): the old behaviour object goes, the new one is made from the selected data and starts fresh
void HordeContain::setMeleeBehavior(std::shared_ptr<MeleeBehaviorModuleData> data)
{
	m_stanceMeleeBehavior = std::move(data);
	if (HordeAIUpdate *ai = dynamic_cast<HordeAIUpdate *>(getObject()->getAIUpdateInterface()))
	{
		const MeleeBehaviorModuleData *selected = meleeBehaviorData();
		ai->resetMeleeRuntime(selected ? (int)selected->m_kind : (int)MeleeBehaviorModuleData::SWARM);
	}
}

// slot 0x260's choice (RW 0x86C433 .. 0x86C487): the stance's data, else the module data's MeleeBehavior (+0x260), else Swarm (null here)
const MeleeBehaviorModuleData *HordeContain::meleeBehaviorData() const
{
	return m_stanceMeleeBehavior ? m_stanceMeleeBehavior.get() : hordeData().m_meleeBehavior.get();
}

void HordeContain::addAttributeModifier(const std::string &listName, int duration)
{
	const ModifierListTemplate *list = TheAttributeModifierStore ? TheAttributeModifierStore->find(listName) : nullptr;
	if (list && list->m_category != ATTRIBUTE_CATEGORY_LEVEL) // RW 0x870EB5 .. 0x870EBF
	{
		const std::vector<Object *> members(m_members.begin(), m_members.end());
		for (Object *m : members)
		{
			m->addAttributeModifier(listName, duration);
		}
	}
	if (AttributeModifierPool *pool = static_cast<AttributeModifierPool *>(getObject()->findModule("AttributeModifierPoolUpdate"))) // RW 0x870F53
	{
		pool->add(listName, duration);
	}
}
