// OpenBFME. GPL-3.0.
//
// Lane BFME2CAMP1: the campaign's carry-over units (records, their spawn, the won mission's recording). Citations in CampaignCarryover.h.

#include "GameLogic/CampaignCarryover.h"

#include "Common/Player.h"
#include "Common/PlayerHeroList.h"
#include "Common/PlayerList.h"
#include "Common/PlayerScience.h"
#include "Common/Science.h"
#include "Common/StateHash.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Module/GiantBirdAIUpdate.h"
#include "GameLogic/Module/HeroModules.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"

#include <cstring>
#include <iomanip>
#include <sstream>

namespace CampaignCarryover
{
namespace
{
Player *firstHuman(PlayerList &players)
{
	// RW 0x6A8755 / 0x6A85EE: the players in index order, the first human (+ 0x5C == 0)
	for (int i = 0; i < players.getPlayerCount(); ++i)
	{
		Player *p = players.getNthPlayer(i);
		if (p && p->getPlayerType() == PLAYER_HUMAN)
		{
			return p;
		}
	}
	return nullptr;
}

// RW 0x780172 CarryoverUnit::createSingleObject (see CampaignCarryover.h)
Object *createSingleObject(GameLogic &logic, const Record &r, Player &player, std::vector<std::string> &stops)
{
	const ThingTemplate *tt = logic.things().findTemplate(r.templateName); // RW 0x7800DC
	if (!tt)
	{
		// retail makes nothing and keeps the record; the port also reports it (lane BFME2CAMP1 review: missing data reaches the report)
		logic.reportError("campaign carry-over: no template '" + r.templateName + "' for a carried unit (kept, not created)");
		return nullptr;
	}
	Object *obj = logic.newObject(tt, player.getDefaultTeam(), ObjectStatusMaskType{}); // RW 0x6D165E (Player + 0x30C)
	if (!obj)
	{
		return nullptr;
	}
	obj->friend_onBuildComplete(); // RW 0x68D252
	if (ExperienceTracker *xp = obj->getExperienceTracker())
	{
		if (r.experience > xp->getExperience()) // RW 0x7801E7 .. 0x7801FB: comiss
		{
			xp->setExperienceAndLevel(r.experience, true); // RW 0x79D8EF
			xp->clearGainedLevel();                        // tracker + 0x20 = 0
		}
	}
	obj->friend_orUpgradeMask(r.upgrades); // RW 0x68CC6C
	obj->updateUpgradeModules();           // RW 0x6936FE
	if (obj->isKindOfName("HORDE"))
	{
		// RW 0x78024F .. 0x78027B: each member of a horde gets the mask and updateUpgradeModules (RW 0x780156); no campaign's record names a horde
		stops.push_back("[S-4904] a carry-over horde (" + r.templateName + "): its members' upgrade mask (RW 0x780156) is not ported");
	}
	return obj;
}

// RW 0x6917BB: an object's record (without the statistics block)
Record recordOf(const Object &o, std::vector<std::string> &stops)
{
	Record r;
	r.templateName = static_cast<const ThingTemplate *>(o.getTemplate())->getName();
	if (o.isKindOfName("HERO"))
	{
		// RW 0x8B316B: the RespawnUpdate's RespawnAsTemplate when it names one
		if (const RespawnUpdate *ru = dynamic_cast<const RespawnUpdate *>(const_cast<Object &>(o).findModule("RespawnUpdate")))
		{
			if (ru->data() && !ru->data()->m_respawnAsTemplate.empty())
			{
				r.templateName = ru->data()->m_respawnAsTemplate;
			}
		}
	}
	else if (o.isKindOfName("HORDE"))
	{
		stops.push_back("[S-4904] a carry-over horde (" + r.templateName + "): its LivingWorldOverloadTemplate name (RW 0x86D6FD) is not read");
	}
	if (const ExperienceTracker *xp = o.getExperienceTracker())
	{
		r.experience = xp->getExperience(); // tracker + 0x10
		r.rank = xp->getRank();             // tracker + 0x24
	}
	r.upgrades = o.getUpgradeMask(); // Object + 0x28C
	r.quantity = 1;
	return r;
}

void hashRecord(StateHasher &h, const Record &r)
{
	h.addString(r.templateName);
	h.addFloat(r.experience);
	h.addI32(r.rank);
	for (unsigned i = 0; i < UpgradeMaskType::activeWords(); ++i) // the mask's retail words (lane MODS-3: more only for a mod with more upgrades)
	{
		h.addU32(r.upgrades.words[i]);
	}
	h.addI32(r.quantity);
}

bool sameRecord(const Record &a, const Record &b)
{
	return a.templateName == b.templateName && std::memcmp(&a.experience, &b.experience, sizeof(float)) == 0 && a.rank == b.rank &&
		   a.upgrades.words == b.upgrades.words && a.quantity == b.quantity;
}

bool named(const std::vector<std::string> &types, const Record &r)
{
	for (const std::string &t : types) // RW 0x9A7C56: std::find over AsciiStrings (an exact compare)
	{
		if (t == r.templateName)
		{
			return true;
		}
	}
	return false;
}

std::uint32_t floatBits(float f)
{
	std::uint32_t b = 0;
	std::memcpy(&b, &f, sizeof(b));
	return b;
}

float bitsFloat(std::uint32_t b)
{
	float f = 0.0f;
	std::memcpy(&f, &b, sizeof(f));
	return f;
}
} // namespace

bool State::operator==(const State &o) const
{
	auto sameList = [](const std::vector<Record> &a, const std::vector<Record> &b) {
		if (a.size() != b.size())
		{
			return false;
		}
		for (size_t i = 0; i < a.size(); ++i)
		{
			if (!sameRecord(a[i], b[i]))
			{
				return false;
			}
		}
		return true;
	};
	return sameList(records, o.records) && sameList(used, o.used) && floatBits(skillPoints) == floatBits(o.skillPoints) && sciences == o.sciences &&
		   upgrades == o.upgrades && floatBits(bountyShare) == floatBits(o.bountyShare);
}

// one line per field, names and floats exact (the float's bits in hex); a record "record <list> <name>|<experience>|<rank>|<mask words>|<quantity>"
std::string State::serialize() const
{
	std::ostringstream s;
	s << std::hex;
	s << "skill " << floatBits(skillPoints) << "\n";
	s << "bounty " << floatBits(bountyShare) << "\n";
	for (const std::string &n : sciences)
	{
		s << "science " << n << "\n";
	}
	for (const std::string &n : upgrades)
	{
		s << "upgrade " << n << "\n";
	}
	auto list = [&s](const char *which, const std::vector<Record> &rs) {
		for (const Record &r : rs)
		{
			s << "record " << which << " " << floatBits(r.experience) << " " << r.rank << " " << r.quantity << " ";
			for (unsigned i = 0; i < UpgradeMaskType::activeWords(); ++i) // as the state hash and the saves carry the mask (lane MODS-3)
			{
				s << (i ? "," : "") << r.upgrades.words[i];
			}
			s << " " << r.templateName << "\n";
		}
	};
	list("left", records);
	list("used", used);
	return s.str();
}

bool State::parse(const std::string &text, State &out, std::string *error)
{
	State st;
	auto fail = [&](const std::string &why) {
		if (error)
		{
			*error = "campaign carry-over: " + why;
		}
		return false;
	};
	auto hex = [](const std::string &v, std::uint32_t &n) {
		if (v.empty() || v.size() > 8 || v.find_first_not_of("0123456789abcdef") != std::string::npos)
		{
			return false;
		}
		n = (std::uint32_t)std::stoul(v, nullptr, 16);
		return true;
	};
	std::istringstream in(text);
	std::string line;
	bool skill = false, bounty = false;
	while (std::getline(in, line))
	{
		if (line.empty())
		{
			continue;
		}
		const size_t sp = line.find(' ');
		const std::string key = line.substr(0, sp);
		const std::string rest = sp == std::string::npos ? std::string() : line.substr(sp + 1);
		std::uint32_t v = 0;
		if (key == "skill" || key == "bounty")
		{
			if (!hex(rest, v) || (key == "skill" ? skill : bounty))
			{
				return fail("bad or repeated " + key + ": " + rest);
			}
			(key == "skill" ? st.skillPoints : st.bountyShare) = bitsFloat(v);
			(key == "skill" ? skill : bounty) = true;
		}
		else if (key == "science" || key == "upgrade")
		{
			if (rest.empty())
			{
				return fail("an empty " + key + " name");
			}
			(key == "science" ? st.sciences : st.upgrades).push_back(rest);
		}
		else if (key == "record")
		{
			std::istringstream r(rest);
			std::string which, exp, rank, qty, mask;
			if (!(r >> which >> exp >> rank >> qty >> mask))
			{
				return fail("a short record: " + rest);
			}
			std::string name;
			std::getline(r, name);
			if (!name.empty() && name[0] == ' ')
			{
				name.erase(0, 1);
			}
			Record rec;
			std::uint32_t bits = 0, rk = 0, q = 0;
			if ((which != "left" && which != "used") || name.empty() || !hex(exp, bits) || !hex(rank, rk) || !hex(qty, q) || rk > 0xFF || q > 0xFFFF)
			{
				return fail("a bad record: " + rest);
			}
			rec.templateName = name;
			rec.experience = bitsFloat(bits);
			rec.rank = (int)rk;
			rec.quantity = (int)q;
			if (mask.back() == ',')
			{
				return fail("a bad upgrade mask: " + mask);
			}
			std::istringstream m(mask);
			std::string w;
			size_t i = 0;
			while (std::getline(m, w, ','))
			{
				std::uint32_t word = 0;
				if (i >= UpgradeMaskType::activeWords() || !hex(w, word))
				{
					return fail("a bad upgrade mask: " + mask);
				}
				rec.upgrades.words[i++] = word;
			}
			if (i != UpgradeMaskType::activeWords())
			{
				return fail("a short upgrade mask: " + mask);
			}
			(which == "left" ? st.records : st.used).push_back(rec);
		}
		else
		{
			return fail("unknown line: " + line);
		}
	}
	if (!skill || !bounty)
	{
		return fail("the skill points or the bounty share is missing");
	}
	out = st;
	return true;
}

void State::crc(StateHasher &h) const
{
	h.addU32((std::uint32_t)records.size());
	for (const Record &r : records)
	{
		hashRecord(h, r);
	}
	h.addU32((std::uint32_t)used.size());
	for (const Record &r : used)
	{
		hashRecord(h, r);
	}
	h.addFloat(skillPoints);
	h.addU32((std::uint32_t)sciences.size());
	for (const std::string &n : sciences)
	{
		h.addString(n);
	}
	h.addU32((std::uint32_t)upgrades.size());
	for (const std::string &n : upgrades)
	{
		h.addString(n);
	}
	h.addFloat(bountyShare);
}

State beginCampaign(const std::vector<std::string> &carryoverUnits)
{
	State out; // the ctor RW 0x5EB223's defaults
	for (const std::string &name : carryoverUnits) // RW 0x5EC4EC: in INI order (RW 0x5EA7F2), + 0xA0 = 1
	{
		Record r;
		r.templateName = name;
		r.quantity = 1;
		out.records.push_back(r);
	}
	return out;
}

bool isDelayed(const std::vector<std::string> &delayed, const Record &r)
{
	for (const std::string &d : delayed) // RW 0x5EAFDD: AsciiString == (RW 0x4065D4), case-sensitive
	{
		if (d == r.templateName)
		{
			return true;
		}
	}
	return false;
}

bool startPosWaypointLocations(GameLogic &logic, Coord3D &start, Coord3D &rally)
{
	// RW 0x782326: start (0, 0, 0), rally (1.0, 0, 0) ([0xBD1908]) before the lookup; RW 0x781B17: player 0 + 1 (TheTerrainLogic vslot 0x88)
	start = Coord3D{ 0.0f, 0.0f, 0.0f };
	rally = Coord3D{ 1.0f, 0.0f, 0.0f };
	const TerrainLogic *terrain = logic.terrain();
	const Waypoint *s = terrain ? terrain->findWaypointByName("Player_1_Start") : nullptr;
	if (!s)
	{
		return false;
	}
	start = s->location; // waypoint + 0xC
	if (const Waypoint *r = terrain->findWaypointByName("Player_1_Start_Rally"))
	{
		rally = r->location;
	}
	else
	{
		rally = start;
		start.x = SimMath::sseAdd(start.x, 10.0f); // RW 0xBD83D8
	}
	return true;
}

SpawnResult addCarryoverStuffToMap(GameLogic &logic, PlayerList &players, State &state, const std::vector<std::string> &delayed)
{
	SpawnResult out;
	state.used.clear(); // RW 0x5EB533
	Player *player = firstHuman(players);
	if (!player)
	{
		return out;
	}
	out.player = player;
	// RW 0x5EBE0A .. 0x5EBE98: the player's reset to the campaign's values
	PlayerScience &science = player->science();
	science.reset();                                   // Player + 8 vslot 0xC (RW 0x6AEE11)
	science.resetSciences();                           // RW 0x6AED4B
	science.addSkillPoints(state.skillPoints, false);  // RW 0x782AA4(+ 0x10, false)
	science.setScalarBaseRank(science.getRankLevel()); // RW 0x71C311: + 0x18 = Player + 0x1C
	for (const std::string &name : state.sciences)     // + 0x14, each RW 0x6AE3CC
	{
		const ScienceType st = TheScienceStore ? TheScienceStore->getScienceFromInternalName(name) : SCIENCE_INVALID;
		if (st == SCIENCE_INVALID)
		{
			out.stops.push_back("[S-4904] the carried science " + name + " is not a Science");
			continue;
		}
		science.grantAndPayScience(st);
	}
	for (const std::string &name : state.upgrades) // + 0x20, each bit: RW 0x66F218 -> RW 0x6AEE22(u, 2, 1)
	{
		if (const UpgradeTemplate *u = Player::resolveUpgrade(name, false))
		{
			player->addUpgrade(u, Player::UPGRADE_STATUS_COMPLETE, true);
		}
		else
		{
			out.stops.push_back("[S-4904] the carried upgrade " + name + " is not an Upgrade");
		}
	}
	player->setBountyPercent(state.bountyShare); // RW 0x6AA847(+ 0xB0)
	// RW 0x5EBEBB .. 0x5EBF50
	for (size_t i = 0; i < state.records.size();)
	{
		const Record r = state.records[i];
		bool made = false;
		if (!isDelayed(delayed, r) && r.quantity > 0)
		{
			for (int n = r.quantity; n > 0; --n)
			{
				if (Object *obj = createSingleObject(logic, r, *player, out.stops))
				{
					obj->setCampaignCarryover(true); // Object + 0x458 |= 0x10
					out.objects.push_back(obj);
					made = true;
				}
			}
		}
		if (made)
		{
			state.used.push_back(r);                                       // RW 0x5EB78F
			state.records.erase(state.records.begin() + (std::ptrdiff_t)i); // RW 0x5EB411
		}
		else
		{
			++i;
		}
	}
	if (!out.objects.empty()) // RW 0x782332: an empty list places nothing
	{
		// the army placer RW 0x782326 -> RW 0x781B17 (the answer ignored) -> PlaceObjects RW 0x781FAF
		Coord3D start, rally;
		(void)startPosWaypointLocations(logic, start, rally);
		const float angle = (float)SimMath::atan2d(SimMath::sseSub(rally.y, start.y), SimMath::sseSub(rally.x, start.x)); // RW 0x441BD2, fstp dword
		if (out.objects.size() > 1)
		{
			out.stops.push_back("[S-4904] " + std::to_string(out.objects.size()) +
								" carry-over objects: the WalkOnTemplate formation's offsets (TheFormationAssistant RW 0x8345F4, S-4460) are not ported; each is "
								"placed at the start");
		}
		for (Object *obj : out.objects)
		{
			Coord3D pos = start; // the formation slot's offset: 0 for one object (INFERENCE, see CampaignCarryover.h)
			if (dynamic_cast<GiantBirdAIUpdate *>(obj->getAIUpdateInterface()))
			{
				out.stops.push_back("[S-4904] a carry-over flyer: its flight height at the placement (RW 0x78218F) is not ported");
			}
			obj->teleportTo(&pos, true); // RW 0x696E63 (keepAI)
			obj->setOrientation(angle);  // RW 0x70C31E
		}
	}
	for (const std::string &s : out.stops)
	{
		logic.noteStop(s);
	}
	return out;
}

void onCurrentMissionWon(GameLogic &logic, PlayerList &players, State &state)
{
	std::vector<std::string> stops;
	// RW 0x5EBB2D ..: the logic's objects in list order: the carry-over flag and not effectively dead
	for (const Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->isCampaignCarryover() && !o->isEffectivelyDead())
		{
			state.records.push_back(recordOf(*o, stops));
		}
	}
	// RW 0x5EBB9C ..: every player's revival list, entries with the flag (+ 0xB1; RW 0x780F9E)
	for (int i = 0; i < players.getPlayerCount(); ++i)
	{
		const Player *p = players.getNthPlayer(i);
		if (!p)
		{
			continue;
		}
		for (const HeroRecord &h : p->heroes().records())
		{
			if (!h.objectFlag)
			{
				continue;
			}
			Record r;
			r.templateName = h.templateName; // entry + 0xE4
			r.experience = h.experience;     // + 8
			r.rank = h.rank;                 // + 0xC
			r.upgrades = h.upgradeMask;      // + 0x14
			r.quantity = 1;
			state.records.push_back(r);
		}
	}
	state.used.clear(); // RW 0x5EB533
	// the first human player's data
	if (Player *player = firstHuman(players))
	{
		state.skillPoints = player->science().getSkillPoints(); // Player + 0x14
		state.sciences.clear();
		for (ScienceType st : player->science().sciences()) // Player + 0x310, the grantable ones (RW 0x5FECBA)
		{
			if (TheScienceStore && TheScienceStore->isScienceGrantable(st))
			{
				state.sciences.push_back(TheScienceStore->getInternalNameForScience(st));
			}
		}
		state.upgrades.clear();
		if (TheUpgradeCenter)
		{
			for (unsigned bit = 0; bit < 0x480; ++bit) // Player + 0x14C bits whose template has PersistsInCampaign (+ 0x78)
			{
				if (!player->getCompletedUpgradeMask().test(bit))
				{
					continue;
				}
				const UpgradeTemplate *u = TheUpgradeCenter->findUpgradeByMaskBit((int)bit);
				if (u && u->m_persistsInCampaign)
				{
					state.upgrades.push_back(u->getUpgradeName());
				}
			}
		}
		state.bountyShare = player->getBountyPercent(); // Player + 0x334
	}
	for (const std::string &s : stops)
	{
		logic.noteStop(s);
	}
}

void restartMission(State &state)
{
	state.records.insert(state.records.end(), state.used.begin(), state.used.end()); // RW 0x5EC452
	state.used.clear();
}

Object *spawnDelayed(GameLogic &logic, State &state, const std::vector<std::string> &types, Player &player)
{
	for (size_t i = 0; i < state.records.size(); ++i)
	{
		if (!named(types, state.records[i])) // RW 0x5EA817
		{
			continue;
		}
		std::vector<std::string> stops;
		const Record r = state.records[i];
		Object *obj = createSingleObject(logic, r, player, stops);
		if (obj)
		{
			obj->setCampaignCarryover(true); // Object + 0x458 |= 0x10
		}
		state.used.push_back(r); // RW 0x5EB78F, whether or not an object was made
		if (state.records[i].quantity <= 1)
		{
			state.records.erase(state.records.begin() + (std::ptrdiff_t)i); // RW 0x5EB411
		}
		else
		{
			--state.records[i].quantity;
		}
		for (const std::string &s : stops)
		{
			logic.noteStop(s);
		}
		return obj;
	}
	return nullptr;
}

bool hasDelayed(const State &state, const std::vector<std::string> &types)
{
	for (const Record &r : state.records)
	{
		if (named(types, r))
		{
			return true;
		}
	}
	return false;
}
} // namespace CampaignCarryover
