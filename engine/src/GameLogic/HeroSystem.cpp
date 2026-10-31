// OpenBFME. GPL-3.0.
// See GameLogic/HeroSystem.h.

#include "GameLogic/HeroSystem.h"

#include "GameLogic/CreateAHeroSystem.h"

#include "Common/BuildAssistant.h"
#include "Common/Player.h"
#include "Common/PlayerHeroList.h"
#include "Common/Science.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/EconomySettings.h"
#include "GameLogic/Economy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/HeroModules.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"

namespace
{
bool hasScienceNamed(const Player &player, const char *name)
{
	if (!TheScienceStore)
	{
		return false;
	}
	const ScienceType st = TheScienceStore->getScienceFromInternalName(name); // RW 0x5FEF8F
	return st != SCIENCE_INVALID && player.hasScience(st);                     // RW 0x6AC207
}
} // namespace

// RW 0x6B16EC
void HeroSystem::initPlayer(GameLogic &logic, Player &player)
{
	const EconomyContext &es = logic.economy().context();
	// RW 0x5FF924 (campaign RW 0x626355 or the Living World RW 0x6253FD): the Living World flag of the economy context; a campaign game is not modelled (S-852)
	if (es.gameKind != 3 || es.livingWorld)
	{
		return; // the campaign / WotR branch (RW 0x6B1771 .. 0x6B17E5: the saved hero records) is S-852
	}
	const PlayerTemplate *pt = player.getPlayerTemplate();
	if (!pt)
	{
		return; // RW 0x6B1720: template + 0x34 null
	}
	for (const std::string &name : pt->m_buildableHeroesMP)
	{
		if (const ThingTemplate *tt = logic.things().findTemplate(name))
		{
			player.heroes().addPurchase(*tt);
		}
	}
}

// WOTR2 milestone 7: RW 0x6B16EC (kind != 3, the player's Living World id) -> RW 0x6E42DA: each entry of the War of the Ring player's list no queue holds
// leaves that list and joins this one (RW 0x781861 -> push RW 0x6E40A6): the entry's fields (RW 0x6E267E), start frame -1, production id 0, UI factor 1,
// army id (+ 0xB8) the player's garrison in the battle region; a dead entry's cost / seconds are its RespawnUpdate rule's of its rank (RW 0x6E2D92 ->
// RW 0x8B3637 / 0x8B36B2). [S-1329 inference: the strategic list keeps only the experience, so the rank is 1 and the rule is level 1's]
void HeroSystem::initLivingWorldPlayer(GameLogic &logic, Player &player, const std::vector<LivingWorldHero> &heroes, int garrisonArmyId, std::vector<std::string> &errors)
{
	for (const LivingWorldHero &h : heroes)
	{
		const ThingTemplate *tt = logic.things().findTemplate(h.templateName);
		if (!tt)
		{
			errors.push_back("the War of the Ring hero " + h.templateName + " has no ThingTemplate");
			continue;
		}
		// the War of the Ring entry is a purchase record (InitBuildableHeroes builds it as RW 0x780713 does: the template's BuildCost (u16 + 0x5EA) and
		// BuildTime, rank 1; review wotr2m7 (Sol): without them the hero was free and instant), its experience and dead flag the war's
		HeroRecord r;
		r.cost = BuildAssistant::buildCost(*tt);
		r.seconds = SimMath::truncToInt32(BuildAssistant::buildTime(*tt));
		r.rank = 1;
		r.experience = h.experience;
		r.startFrame = -1;
		r.productionID = 0;
		r.dead = h.dead;
		r.templateName = h.templateName;
		if (h.dead)
		{
			const RespawnUpdateModuleData *data = nullptr;
			for (const ThingTemplate::Nugget &n : tt->behaviorModules().nuggets())
			{
				data = data ? data : dynamic_cast<const RespawnUpdateModuleData *>(n.data.get());
			}
			if (data)
			{
				const RespawnRule *rule = data->ruleFor((unsigned)r.rank);
				r.cost = rule ? rule->cost : 1000u;
				r.seconds = rule ? rule->timeMs / 1000 : SimMath::truncToInt32(SimMath::mulf32(0.005f, 30000.0f));
			}
		}
		player.heroes().addRecord(r);
		LivingWorldBattleTags::Tag tag;
		tag.armyId = garrisonArmyId;
		tag.spawnId = h.spawnId;
		logic.livingWorldTags().heroes[{ player.getPlayerIndex(), h.templateName }] = tag;
	}
}

// RW 0x781792 -> RW 0x780DD4
int HeroSystem::addDeadHero(Player &player, Object &obj, const RespawnUpdate &respawn, bool autoSpawn)
{
	HeroRecord r;
	if (const ExperienceTracker *t = obj.getExperienceTracker())
	{
		r.experience = t->getExperience(); // tracker + 0x10
		r.rank = t->getRank();             // + 0x24
		r.baseRank = t->getBaseRank();     // RW 0x79D102: helper + 0x0C
	}
	r.upgradeMask = obj.getUpgradeMask();  // RW 0x780E2B: Object + 0x28C
	r.startFrame = -1;
	r.dead = true;
	r.objectFlag = obj.isCampaignCarryover(); // RW 0x780E3F: + 0xB1 = Object + 0x458 bit 4 (lane BFME2CAMP1: the campaign carry-over flag)
	r.productionID = 0;                    // RW 0x780E4F: + 0xB4 = 0
	r.objectName = obj.getName();          // + 0xE0 from Object + 0x88
	r.cost = respawn.ruleCost();           // RW 0x780E99
	r.seconds = respawn.ruleSeconds();     // RW 0x780EA4
	static const int kCreateAHero = ObjectTemplateInfoBuilder::kindOfIndex("CREATE_A_HERO");
	if (kCreateAHero >= 0 && obj.isKindOf((unsigned)kCreateAHero) && TheCreateAHeroSystem)
	{
		// lane HERO-2, RW 0x780F21 .. 0x780F5E: a CREATE_A_HERO with a controlling player: calcCostToBuild(player, 0, -1) (with the record's surcharge)
		// * TheCreateAHeroSystem's HeroRevivalDiscount (+ 0x1CC, 75) / 100, unsigned
		if (Player *owner = obj.getControllingPlayer())
		{
			const unsigned full = (unsigned)BuildAssistant::calcCostToBuild(*obj.getTemplate(), owner, nullptr, -1);
			r.cost = (int)(full * (unsigned)TheCreateAHeroSystem->heroRevivalDiscount / 100u);
		}
	}
	r.templateName = respawn.respawnTemplate()->getName(); // RW 0x8B316B
	if (autoSpawn)
	{
		r.cost = 0; // RW 0x7817BA
	}
	// WOTR2 milestone 7: the record keeps the dead object's army and spawn ids (RW 0x780DD4: record + 0xB8 / + 0xBC from Object + 0x47C / + 0x480)
	LivingWorldBattleTags &tags = obj.logic().livingWorldTags();
	if (tags.active)
	{
		if (const LivingWorldBattleTags::Tag *t = tags.object(obj.getID()))
		{
			tags.heroes[{ player.getPlayerIndex(), r.templateName }] = *t;
		}
	}
	return player.heroes().addRecord(r);
}

// RW 0x78142F
Object *HeroSystem::produce(GameLogic &logic, Player &player, std::uint32_t productionID, const Coord3D &pos)
{
	PlayerHeroList &list = player.heroes();
	const HeroRecord *found = list.findByProductionID(productionID);
	if (!found)
	{
		return nullptr;
	}
	const HeroRecord r = *found; // RW 0x78147B: a copy (the list entry is erased at the end)
	const size_t index = (size_t)(found - list.records().data());
	const ThingTemplate *tt = logic.things().findTemplate(r.templateName);
	if (!tt)
	{
		return nullptr;
	}
	Object *obj = logic.newObject(tt, player.getDefaultTeam(), ObjectStatusMaskType{}); // RW 0x6D165E(tt, player + 0x30C, status 0, 0)
	if (!obj)
	{
		return nullptr;
	}
	Coord3D at = pos;
	obj->teleportTo(&at, false); // RW 0x7814CF: RW 0x696E63(pos, 0)
	// RW 0x7814D9 / 0x7814E1: Object + 0x47C / + 0x480 from the record's + 0xB8 / + 0xBC (WOTR2 milestone 7: the War of the Ring army id and spawn id, kept
	// in the battle's tags); + 0x488 from the record: not identified (S-852)
	LivingWorldBattleTags &tags = logic.livingWorldTags();
	if (tags.active)
	{
		if (const LivingWorldBattleTags::Tag *t = tags.hero(player.getPlayerIndex(), r.templateName))
		{
			tags.objects[obj->getID()] = *t;
		}
	}
	if (!r.objectName.empty())
	{
		obj->setName(r.objectName); // RW 0x759467 (TheScriptEngine names the object)
	}
	// RW 0x781506: the record's + 0xB1 sets Object + 0x458 bit 4 (lane BFME2CAMP1: the campaign carry-over flag, so a revived carried hero is kept by
	// the won mission, RW 0x5EBAEE); RW 0x781514: RW 0x68C7E9 (not identified, S-852)
	if (r.objectFlag)
	{
		obj->setCampaignCarryover(true);
	}
	if (r.dead || r.experience > 1.0f)
	{
		if (ExperienceTracker *t = obj->getExperienceTracker())
		{
			// GameLogic + 0x98 is cleared around the call (RW 0x781536 .. 0x7815A5; the byte is not identified, S-852)
			t->addExperiencePoints(SimMath::subf32(r.experience, 1.0f), false, false, false, false); // subss [0xBD1908]
			t->restoreBaseRank(r.baseRank);                                                        // RW 0x79D745
		}
		obj->friend_orUpgradeMask(r.upgradeMask); // RW 0x68CC6C on Object + 0x28C
	}
	// lane HERO-2, RW 0x78159F .. 0x7815CC: a CREATE_A_HERO's record builds the command set of the record's rank ([ebp - 0xFC] = the copy's + 0xC)
	if (obj->isKindOfName("CREATE_A_HERO"))
	{
		logic.createAHeroes().buildCommandSet(*obj, r.rank);
	}
	if (RespawnUpdate *ru = dynamic_cast<RespawnUpdate *>(obj->findModule("RespawnUpdate")))
	{
		ru->setInitialSpawn(!r.dead); // RW 0x78163B: + 0x41 = (record + 0xB0 == 0)
	}
	static const int kGandalf = ObjectTemplateInfoBuilder::kindOfIndex("GANDALF");
	static const int kAragorn = ObjectTemplateInfoBuilder::kindOfIndex("ARAGORN");
	// RW 0x781663 .. 0x7816F3: the two science hooks the binary names (template KindOf bits 168 / 169)
	if (hasScienceNamed(player, "SCIENCE_GandalftheWhite") && kGandalf >= 0 && obj->isKindOf((unsigned)kGandalf))
	{
		obj->addAttributeModifier("SpellBookGandalfWhite", -1); // RW 0x68F1A8
		logic.noteStop("[S-852] Gandalf the White revive: RW 0x68B934 after the SpellBookGandalfWhite modifier is not identified");
	}
	if (hasScienceNamed(player, "SCIENCE_Anduril") && kAragorn >= 0 && obj->isKindOf((unsigned)kAragorn))
	{
		obj->addAttributeModifier("SpellBookAnduril", -1);
	}
	list.erase(index); // RW 0x7813FF: the first record with the production id
	return obj;
}

std::vector<std::string> HeroSystem::stopLines()
{
	std::vector<std::string> lines = {
		"[S-850] hero recruiting UI: the control bar's REVIVE buttons show RW 0x943D6F's first pass (the jth REVIVE slot stands for the jth template of "
		"BuildableRingHeroesMP + BuildableHeroesMP, its record's image RW 0x7810BC, cost, state and production clock; a press is the build-index "
		"MSG_QUEUE_UNIT_CREATE) and a hero's special power buttons their lock / ready / recharge state and clock; not ported: the second pass (leftover records "
		"into free slots), RW 0x942733's UI rules beyond canMakeUnit, Create-A-Hero's own image, the HeroSelect side list of the live heroes' portraits",
		"[S-851] RespawnUpdate AutoSpawn: the writer of state 3 (the auto respawn countdown) is not located and the refund slot RW 0x8B3017 has no caller; retail data has no "
		"AutoSpawn:Yes rule, a mod's would never respawn by itself here (state 3 reached is a runtime error)",
		"[S-852] hero records: not ported: the campaign / WotR saved heroes (RW 0x6B1771), the record "
		"fields Object + 0x47C / + 0x480 / + 0x488 / + 0x458 bit 4 and the tracker's leveled byte, GameLogic + 0x98, RW 0x68C7E9, forced drawable model-condition refresh on creation (RW 0x70E013 / 0x67449C(true)), RW 0x68B934; "
		"the rank of a purchase record is 1 (RW 0x73CE2A's create module vslot is not identified)",
		"[S-854] RespawnUpdate details: the producer's exit slot 9 call of the revive (RW 0x8B38D8) is not made, the AI idle test before the exit (AI vslot 0x1B8 or RW "
		"0x660AC1 == 16) is AIUpdateInterface::isIdle, a respawn health "
		"above 100% (the max health growth of RW 0x8C47F6) is not ported",
		"[S-855] hero production: a revive or purchase entry pays at queue time like a unit; a hero entry whose record cannot start (RW 0x7812B2 false) keeps the money "
		"(retail's order: the withdrawal comes first); the Brutal AI discount of hero cost and time (AI data + 0x95C / + 0x960) is S-204's; a hero template with a BuildFadeInOnCreateTime holds its "
		"producer's exit (retail's own order: the record is erased when the hero is made and the build-index gate RW 0x8A1F3D then never passes; no retail hero has one)",
		"[S-856] AttributeModifierAuraUpdate: not ported: Object + 0x11C bit 1 (skips the pulse and the target), the WALK_ON_TOP_OF_WALL source (geometry radius, no "
		"relationship filter), the TAINT / ELVEN_WOOD terrain areas (RW 0xAD49B0: no object is in one), the Object + 0x458 bit 3 filter RW 0xC0F374 (the bit is "
		"never set), the pool's AntiCategory disable (XP-1's S-633: counted); the aura and AutoHeal scans run on ThePartitionManager (lane MODULES-2, S-1020: the "
		"retail order of equal distances), the special power trigger and LevelGrant scans still sort the object list by distance (equal distances keep the "
		"list's order: as an applied modifier can grant an upgrade at once (a CostModifierUpgrade appends to the player's ordered list), that order can change "
		"shared player state)",
		"[S-858] AutoHealBehavior: not ported: NonStackable (the body's last heal frame, body vslot 0x48: the heal stacks), AffectsWholePlayer (no retail use), "
		"Object + 0x458 bit 3 in the eligibility, the contain's FX flag; the combat and recent "
		"damage tests of RW 0x855533 read the healer (verified)",
	};
	for (const std::string &l : SpecialAbilityModules::stopLines())
	{
		lines.push_back(l);
	}
	return lines;
}
