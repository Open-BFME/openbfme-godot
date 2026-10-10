// OpenBFME. GPL-3.0.
// The hero the Create-a-Hero builder edits (lane CAH-1). See GameClient/GUI/CreateAHero/AptMyHero.h for the target facts.

#include "GameClient/GUI/CreateAHero/AptMyHero.h"

#include "Common/BuildAssistant.h"
#include "Common/Thing/ThingFactory.h"
#include "GameClient/ControlBarCommands.h"
#include "GameClient/CreateAHeroHeroList.h"
#include "GameClient/GUI/GameTextSource.h"
#include "GameClient/GUI/LoadScreenInfo.h"
#include "GameClient/GUI/WindowManager.h"
#include "GameLogic/CreateAHeroSystem.h"

#include <cstdio>

namespace
{
// RW 0x61BA93: the position in the group's list of the bling whose upgrade is `upgrade` (-1 when none)
int positionOfUpgrade(const CreateAHeroSystem &system, const std::vector<int> &list, const std::string &upgrade)
{
	for (size_t i = 0; i < list.size(); ++i)
	{
		const CreateAHeroBling *b = system.bling(list[i]);
		if (b && b->upgradeName == upgrade)
		{
			return (int)i;
		}
	}
	return -1;
}

std::string dec(int v)
{
	char buf[32];
	std::snprintf(buf, sizeof buf, "%d", v); // "%d" RW 0xBD4194
	return buf;
}

// "MyHero::<Prefix>Attrib_<i>" / "MyHero::NumAppearance_<i>" / ...: the index after the prefix (-1 when the name is not of that form)
int suffixIndex(const std::string &name, const std::string &prefix)
{
	if (name.size() <= prefix.size() || name.compare(0, prefix.size(), prefix) != 0)
	{
		return -1;
	}
	int v = 0;
	for (size_t i = prefix.size(); i < name.size(); ++i)
	{
		if (name[i] < '0' || name[i] > '9')
		{
			return -1;
		}
		v = v * 10 + (name[i] - '0');
	}
	return v;
}
} // namespace

AptMyHero::AptMyHero(const CreateAHeroSystem &system, WindowManager *windows, const GameTextSource *text)
	: m_system(system), m_windows(windows), m_text(text)
{
	// RW 0x80C572(0, 0, 0, "", -1, 0xFF707070, -1)
	m_hero.primaryColor = 0xFFFFFFFFu;
	m_hero.secondaryColor = 0xFF707070u;
	m_hero.tertiaryColor = 0xFFFFFFFFu;
	m_hero.flags = CreateAHeroHero::LOAD_FLAGS;
	for (const char *t : { "APT:MyHeroName", "APT:MyHeroClass", "APT:MyHeroType", "APT:MyHeroAttribPoints", "APT:MyHeroCost" })
	{
		setText(t, u" "); // RW 0x9C0150 ..: DAT_00BD16E4 = L" "
	}
}

void AptMyHero::setText(const std::string &name, const std::u16string &text)
{
	if (m_windows)
	{
		m_windows->setAptText(name, loadScreenU16ToUtf8(text));
	}
}

std::u16string AptMyHero::label(const std::string &tag) const
{
	return fetchOrMissing(m_text, tag);
}

void AptMyHero::edit(const CreateAHeroHero &hero, bool fresh)
{
	m_hero = hero;
	m_new = fresh;
	rebuild();
	if (fresh) // RW 0x9C0E89: + 0x150 (the class page's new hero)
	{
		resetToMinimum(KIND_ATTRIBUTE);
		setDefaults(KIND_APPEARANCE);
	}
}

void AptMyHero::setClass(std::uint32_t cls, std::uint32_t sub)
{
	m_hero.classIndex = cls;
	m_hero.subClassIndex = sub;
	m_hero.flags |= 3; // RW 0x80ACE3 applies the class / subclass upgrades on flag 1 / 2
	rebuild();
}

int AptMyHero::currentIndex(int kind, int slot) const
{
	const std::vector<Slot> &s = slots(kind);
	if (slot < 0 || (size_t)slot >= s.size() || s[(size_t)slot].group.empty())
	{
		return -1;
	}
	const std::uint32_t *v = m_hero.findBling(s[(size_t)slot].group); // RW 0x80A710
	return v ? (int)*v : 0;
}

void AptMyHero::rebuild()
{
	const CreateAHeroSubClass *sub = m_system.subClass(m_hero.classIndex, m_hero.subClassIndex);
	// RW 0x9C07C5 clears the slots; RW 0x619DEF
	m_slots[0].clear();
	m_slots[1].clear();
	m_maximum = m_remaining = sub ? sub->spendableAttributePoints : 0;
	const std::vector<CreateAHeroBlingBinder> &binders = m_system.binders();
	for (size_t bi = 0; bi < binders.size(); ++bi)
	{
		const CreateAHeroBlingBinder &b = binders[bi];
		const int kind = b.blingType;
		if (kind != KIND_ATTRIBUTE && kind != KIND_APPEARANCE)
		{
			continue;
		}
		std::vector<Slot> &list = m_slots[(size_t)kind];
		const std::uint32_t slot = b.uiSlot;
		if (slot >= 64) // a mod's UISlot -1 (unset) is refused by the parser; this guards the vector
		{
			continue;
		}
		if (list.size() <= slot) // RW 0x9C0AB6
		{
			list.resize((size_t)slot + 1);
		}
		Slot &e = list[(size_t)slot];
		if (!e.group.empty())
		{
			continue; // a slot already taken keeps its first binder
		}
		e.group = b.groupName;
		e.defaultIndex = 0;
		const bool added = m_hero.findBling(e.group) == nullptr; // RW 0x80A73B
		if (added)
		{
			m_hero.setBling(e.group, 0);
		}
		int cur = currentIndex(kind, (int)slot);
		const std::vector<int> *groupList = sub ? sub->findGroup(e.group) : nullptr;
		const int groupCount = groupList ? (int)groupList->size() : 0; // RW 0x618F3A
		if (kind == KIND_ATTRIBUTE)
		{
			const CreateAHeroAttribute *a = sub ? sub->findAttribute(e.group) : nullptr;
			if (groupCount == 0)
			{
				// RW 0x9C0BB8: the debug range of a group without a list
				e.minimum = (int)(((m_hero.classIndex + 1) * (m_hero.subClassIndex + 1) * (std::uint32_t)(bi + 1)) % 10u) + 5;
				e.maximum = ((int)(bi * 3) % 10 - (int)m_hero.classIndex) + 3 + e.minimum;
			}
			else
			{
				e.minimum = a ? positionOfUpgrade(m_system, *groupList, a->minValueUpgrade) : -1;
				e.maximum = a ? positionOfUpgrade(m_system, *groupList, a->maxValueUpgrade) : -1;
				e.defaultIndex = a ? positionOfUpgrade(m_system, *groupList, a->defaultValueUpgrade) : -1;
			}
			if (added)
			{
				cur = e.minimum;
			}
			m_hero.setBling(e.group, (std::uint32_t)e.minimum); // RW 0x80A6C8
			setText("APT:MyHeroAttribute_" + std::to_string(slot), label(b.labelTag));
		}
		else
		{
			e.minimum = 0;
			e.maximum = groupCount - 1;
			e.defaultIndex = 0;
			if (sub && groupList) // RW 0x61BC63: the position of the subclass's default bling in the list
			{
				for (const auto &d : sub->blingDefaults)
				{
					if (d.first == e.group)
					{
						for (size_t i = 0; i < groupList->size(); ++i)
						{
							if ((*groupList)[i] == d.second)
							{
								e.defaultIndex = (int)i;
							}
						}
					}
				}
			}
			setText("APT:MyHeroAppearance_" + std::to_string(slot), label(b.labelTag));
			if (added)
			{
				cur = e.defaultIndex;
			}
		}
		// LAB_009C0D6E
		if (e.maximum < e.minimum)
		{
			e.minimum = e.maximum;
		}
		e.count = e.maximum - e.minimum + 1;
		if ((std::uint32_t)cur < (std::uint32_t)e.minimum || (std::uint32_t)e.maximum < (std::uint32_t)cur) // unsigned compares (jb / ja)
		{
			cur = e.minimum;
		}
		if (e.defaultIndex < e.minimum || e.maximum < e.defaultIndex)
		{
			e.defaultIndex = e.minimum;
		}
		setSlotIndex(kind, (int)slot, cur);
	}
	notify();
	if (m_windows) // RW 0x83FFD6: the movie's UpdateHeroBaseAttributes
	{
		m_windows->note("cah-invoke", "UpdateHeroBaseAttributes");
	}
}

bool AptMyHero::setSlotIndex(int kind, int slot, int index)
{
	std::vector<Slot> &list = m_slots[(size_t)(kind == KIND_APPEARANCE)];
	if (slot < 0 || (size_t)slot >= list.size())
	{
		return false;
	}
	const Slot &e = list[(size_t)slot];
	if (e.group.empty() || index < e.minimum || index > e.maximum)
	{
		return false;
	}
	const int cur = currentIndex(kind, slot);
	if (kind == KIND_ATTRIBUTE)
	{
		const int remaining = (cur - index) + m_remaining;
		if (remaining < 0 || remaining > m_maximum)
		{
			return false;
		}
		m_remaining = remaining;
		setText("APT:MyHeroAttribPoints", asciiToU16(dec(remaining))); // L"%d" RW 0xBDF1B0
	}
	else
	{
		const CreateAHeroSubClass *sub = m_system.subClass(m_hero.classIndex, m_hero.subClassIndex);
		const std::vector<int> *groupList = sub ? sub->findGroup(e.group) : nullptr;
		const CreateAHeroBling *b = groupList && (size_t)index < groupList->size() ? m_system.bling((*groupList)[(size_t)index]) : nullptr; // RW 0x619B02
		setText("APT:MyHeroAppearanceVal_" + std::to_string(slot), b ? label(b->nameTag) : std::u16string());
	}
	m_hero.setBling(e.group, (std::uint32_t)index);
	m_hero.flags |= 4; // RW 0x80A6C8 marks the bling dirty for RW 0x80ACE3
	return true;
}

void AptMyHero::step(int kind, int slot, int delta)
{
	const std::vector<Slot> &list = slots(kind);
	if (slot < 0 || (size_t)slot >= list.size() || list[(size_t)slot].count == 0)
	{
		return;
	}
	const Slot &e = list[(size_t)slot];
	int cur = currentIndex(kind, slot);
	if (kind == KIND_ATTRIBUTE)
	{
		if (delta != 0)
		{
			// RW 0x9BFD58: a step the points cannot pay moves toward 0
			while (delta != 0 && m_remaining - delta > m_maximum)
			{
				++delta;
			}
			while (delta != 0 && m_remaining - delta < 0)
			{
				--delta;
			}
		}
		if (cur + delta > e.maximum)
		{
			delta = e.maximum - cur;
		}
		else if (cur + delta < e.minimum)
		{
			delta = e.minimum - cur;
		}
		cur += delta;
	}
	else
	{
		cur += delta;
		while (cur > e.maximum)
		{
			cur -= e.count;
		}
		while (cur < e.minimum)
		{
			cur += e.count;
		}
	}
	setSlotIndex(kind, slot, cur);
	notify();
}

void AptMyHero::resetToMinimum(int kind)
{
	const std::vector<Slot> list = slots(kind);
	for (size_t i = 0; i < list.size(); ++i)
	{
		setSlotIndex(kind, (int)i, list[i].minimum);
	}
	notify();
}

void AptMyHero::setDefaults(int kind)
{
	if (kind == KIND_ATTRIBUTE)
	{
		resetToMinimum(KIND_ATTRIBUTE); // RW 0x9BFE3A
	}
	const std::vector<Slot> list = slots(kind);
	for (size_t i = 0; i < list.size(); ++i)
	{
		setSlotIndex(kind, (int)i, list[i].defaultIndex);
	}
	notify();
}

void AptMyHero::randomize(int kind, const std::function<int(int, int)> &random)
{
	const std::vector<Slot> list = slots(kind);
	for (size_t i = 0; i < list.size(); ++i)
	{
		setSlotIndex(kind, (int)i, random(list[i].minimum, list[i].maximum)); // RW 0x6D32E4 (client random, inclusive)
	}
	notify();
}

void AptMyHero::setDefaultColors()
{
	if (const CreateAHeroSubClass *sub = m_system.subClass(m_hero.classIndex, m_hero.subClassIndex)) // RW 0x619D6E / 0x619D99 / 0x619DC4
	{
		setPrimaryColor(sub->defaultPrimaryColor);
		setSecondaryColor(sub->defaultSecondaryColor);
		setTertiaryColor(sub->defaultTertiaryColor);
	}
	m_hero.flags |= 0x100;
}

void AptMyHero::setPrimaryColor(std::uint32_t c)
{
	if (m_hero.primaryColor != c)
	{
		m_hero.flags |= 8;
		m_hero.primaryColor = c;
	}
}

void AptMyHero::setSecondaryColor(std::uint32_t c)
{
	if (m_hero.secondaryColor != c)
	{
		m_hero.flags |= 8;
		m_hero.secondaryColor = c;
	}
}

void AptMyHero::setTertiaryColor(std::uint32_t c)
{
	if (m_hero.tertiaryColor != c)
	{
		m_hero.flags |= 8;
		m_hero.tertiaryColor = c;
	}
}

void AptMyHero::setName(const std::u16string &name)
{
	if (m_hero.name != name)
	{
		m_hero.name = name;
		m_hero.flags |= 0x10;
	}
	if (m_hero.uniqueID.empty()) // RW 0x80A389: CoCreateGuid
	{
		m_hero.uniqueID = CreateAHeroHeroList::newUniqueID();
		m_hero.valid = true;
	}
}

int AptMyHero::buildCost() const
{
	if (!TheCommandStore || !TheCommandStore->thingFactory())
	{
		return -1;
	}
	int cost = CreateAHeroGame::powerCostOf(m_hero);
	if (const ThingTemplate *tt = TheCommandStore->thingFactory()->findTemplate("CreateAHero")) // RW 0x809D0A: the template name RW 0xC11A74
	{
		cost += BuildAssistant::calcCostToBuild(*tt, nullptr, nullptr, -1);
	}
	return cost;
}

void AptMyHero::notify()
{
	if (onChanged)
	{
		onChanged();
	}
}

std::vector<std::string> AptMyHero::providerNames()
{
	std::vector<std::string> out;
	for (int i = 0; i < NUM_ATTRIBUTE_BARS; ++i)
	{
		out.push_back("MyHero::BaseAttrib_" + std::to_string(i));
		out.push_back("MyHero::CurAttrib_" + std::to_string(i));
		out.push_back("MyHero::MaxAttrib_" + std::to_string(i));
	}
	for (int i = 0; i < NUM_APPEARANCE_SLOTS; ++i)
	{
		out.push_back("MyHero::NumAppearance_" + std::to_string(i));
	}
	out.push_back("MyHero::MaxAttribute");
	out.push_back("MyHero::MaxAwards");
	out.push_back("MyHero::IsSystemHero");
	out.push_back("MyHero::HeroBuildCost");
	return out;
}

bool AptMyHero::provide(const std::string &name, std::string &value) const
{
	int i;
	const std::vector<Slot> &attributes = slots(KIND_ATTRIBUTE);
	if ((i = suffixIndex(name, "MyHero::BaseAttrib_")) >= 0)
	{
		if ((size_t)i < attributes.size())
		{
			value = dec(attributes[(size_t)i].minimum + 1);
		}
		return true;
	}
	if ((i = suffixIndex(name, "MyHero::CurAttrib_")) >= 0)
	{
		if ((size_t)i < attributes.size())
		{
			value = dec(currentIndex(KIND_ATTRIBUTE, i) + 1);
		}
		return true;
	}
	if ((i = suffixIndex(name, "MyHero::MaxAttrib_")) >= 0)
	{
		if ((size_t)i < attributes.size())
		{
			value = dec(attributes[(size_t)i].maximum + 1);
		}
		return true;
	}
	if ((i = suffixIndex(name, "MyHero::NumAppearance_")) >= 0)
	{
		const std::vector<Slot> &app = slots(KIND_APPEARANCE);
		if ((size_t)i < app.size())
		{
			value = dec(app[(size_t)i].count);
		}
		return true;
	}
	if (name == "MyHero::MaxAttribute")
	{
		value = dec(MAX_ATTRIBUTE);
		return true;
	}
	if (name == "MyHero::MaxAwards")
	{
		value = dec(0); // TheCreateAHeroSystem + 0x1E4: the award count (S-1403: the awards are not ported)
		return true;
	}
	if (name == "MyHero::IsSystemHero")
	{
		value = m_hero.isSystemHero ? "1" : "0";
		return true;
	}
	if (name == "MyHero::HeroBuildCost")
	{
		value = dec(buildCost());
		return true;
	}
	return false;
}
