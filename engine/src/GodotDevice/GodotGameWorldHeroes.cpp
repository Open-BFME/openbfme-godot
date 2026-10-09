// OpenBFME. GPL-3.0.
// Lane HERO-1: GameWorld's hero methods (see GodotDevice/GodotGameWorld.h): the hero list, recruiting / reviving by build index and casting an object's special
// power go through the lockstep command path (LiveGame::commands()); gain_hero_levels is a scenario helper like create_object.

#include "GodotDevice/GodotGameWorld.h"

#include "Common/Player.h"
#include "Common/PlayerHeroList.h"
#include "Common/PlayerList.h"
#include "Common/SpecialPower.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/LiveGame.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Object/AttributeModifierPool.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/CreateAHeroSystem.h"
#include "Common/AsciiString.h"
#include "GameNetwork/GameInfo.h"
#include "GodotDevice/GodotRetailFileSystem.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/RetailObjectWorld.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

#include <fstream>
#include <iterator>

using namespace godot;

void GameWorld::bindHeroMethods()
{
	ClassDB::bind_method(D_METHOD("get_heroes", "player", "producer"), &GameWorld::get_heroes, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("queue_hero", "player", "producer", "index"), &GameWorld::queue_hero);
	ClassDB::bind_method(D_METHOD("cancel_hero", "player", "producer", "index"), &GameWorld::cancel_hero);
	ClassDB::bind_method(D_METHOD("get_object_powers", "id"), &GameWorld::get_object_powers);
	ClassDB::bind_method(D_METHOD("cast_object_power", "player", "id", "power", "target"), &GameWorld::cast_object_power, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("ready_object_powers", "id"), &GameWorld::ready_object_powers);
	ClassDB::bind_method(D_METHOD("get_ability_state", "id"), &GameWorld::get_ability_state);
	ClassDB::bind_method(D_METHOD("damage_object", "id", "amount"), &GameWorld::damage_object);
	ClassDB::bind_method(D_METHOD("gain_hero_levels", "id", "levels"), &GameWorld::gain_hero_levels);
	ClassDB::bind_method(D_METHOD("kill_hero", "id"), &GameWorld::kill_hero);
	ClassDB::bind_method(D_METHOD("give_money", "player", "amount"), &GameWorld::give_money);
	ClassDB::bind_method(D_METHOD("get_system_heroes"), &GameWorld::get_system_heroes);
	ClassDB::bind_method(D_METHOD("get_create_a_hero_record", "name"), &GameWorld::get_create_a_hero_record);
	ClassDB::bind_method(D_METHOD("get_create_a_hero", "player"), &GameWorld::get_create_a_hero);
}

Array GameWorld::get_heroes(int64_t player, int64_t producer) const
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	Array out;
	if (!m_game)
	{
		return out;
	}
	GameLogic &logic = m_game->logic();
	const ::Player *p = logic.players().getNthPlayer((int)player);
	if (!p)
	{
		return out;
	}
	const ::Object *prod = producer > 0 ? logic.findObjectByID((::ObjectID)producer) : nullptr;
	const PlayerHeroList &list = p->heroes();
	for (size_t i = 0; i < list.size(); ++i)
	{
		const HeroRecord &r = list.records()[i];
		Dictionary d;
		d["index"] = (int64_t)i;
		d["template"] = String(r.templateName.c_str());
		d["dead"] = r.dead;
		d["in_production"] = r.startFrame != -1;
		d["cost"] = (int64_t)list.costAt(logic, *p, (int)i, prod);
		d["frames"] = (int64_t)list.framesAt(logic, *p, (int)i, prod);
		d["progress"] = list.progressAt(logic, *p, (int)i, prod);
		out.append(d);
	}
	return out;
}

static void heroIndexMessage(LiveGame &game, GameMessageType type, int64_t player, int64_t producer, int64_t index)
{
	GameMessage sel(MSG_CREATE_SELECTED_GROUP, (int)player);
	sel.appendBooleanArgument(true);
	sel.appendObjectIDArgument((::ObjectID)producer);
	game.commands().append(sel);
	GameMessage m(type, (int)player);
	m.appendBooleanArgument(true); // the build-index flag (RW 0x77A764 / 0x77A844)
	m.appendIntegerArgument((int)index);
	if (type == MSG_QUEUE_UNIT_CREATE)
	{
		m.appendIntegerArgument(-1);
		m.appendBooleanArgument(false);
		m.appendBooleanArgument(false);
	}
	else
	{
		m.appendBooleanArgument(false);
	}
	game.commands().append(m);
}

void GameWorld::queue_hero(int64_t player, int64_t producer, int64_t index)
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	if (!m_game)
	{
		m_commandErrors.push_back("queue_hero: no map is loaded");
		return;
	}
	heroIndexMessage(*m_game, MSG_QUEUE_UNIT_CREATE, player, producer, index);
}

void GameWorld::cancel_hero(int64_t player, int64_t producer, int64_t index)
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	if (!m_game)
	{
		m_commandErrors.push_back("cancel_hero: no map is loaded");
		return;
	}
	heroIndexMessage(*m_game, MSG_CANCEL_UNIT_CREATE, player, producer, index);
}

Array GameWorld::get_object_powers(int64_t id) const
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	Array out;
	const ::Object *o = m_game && id > 0 ? m_game->logic().findObjectByID((::ObjectID)id) : nullptr;
	if (!o)
	{
		return out;
	}
	for (const std::unique_ptr<BehaviorModule> &m : o->modules())
	{
		const SpecialPowerModule *sp = dynamic_cast<const SpecialPowerModule *>(m.get());
		if (!sp || !sp->getSpecialPowerTemplate())
		{
			continue;
		}
		Dictionary d;
		d["name"] = String(sp->getSpecialPowerTemplate()->getName().c_str());
		d["ready"] = sp->isReadyForDisplay();
		d["paused"] = sp->pauseCount() != 0;
		d["percent"] = sp->getPercentReadyForDisplay();
		d["update_module_starts_attack"] = sp->spData()->m_updateModuleStartsAttack;
		d["attribute_modifier"] = String(sp->spData()->m_attributeModifier.c_str());
		// lane HERO-1 part 2: the SpecialAbilityUpdate that drives it (RW 0x897E87), and whether it is cast at a target (a StartAbilityRange; the AutoHeal
		// enums 0x80 / 0x8F are cast without one)
		const SpecialAbilityUpdate *su = dynamic_cast<const SpecialAbilityUpdate *>(SpecialAbilityModules::findUpdate(*o, sp->getSpecialPowerTemplate()));
		const int type = sp->getSpecialPowerTemplate()->m_type;
		d["update"] = !su ? String() : dynamic_cast<const WeaponFireSpecialAbilityUpdate *>(su) ? String("WeaponFire") : dynamic_cast<const ToggleMountedSpecialAbilityUpdate *>(su) ? String("ToggleMounted")
			: dynamic_cast<const HeroModeSpecialAbilityUpdate *>(su) ? String("HeroMode") : dynamic_cast<const LevelGrantSpecialPower *>(su) ? String("LevelGrant")
			: dynamic_cast<const ModelConditionSpecialAbilityUpdate *>(su) ? String("ModelCondition") : String("SpecialAbility");
		d["targeted"] = su && su->abilityData()->m_startAbilityRange < 10000000.0f && type != 0x80 && type != 0x8F;
		out.append(d);
	}
	return out;
}

Dictionary GameWorld::get_ability_state(int64_t id) const
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	Dictionary out;
	const ::Object *o = m_game && id > 0 ? m_game->logic().findObjectByID((::ObjectID)id) : nullptr;
	if (!o)
	{
		return out;
	}
	out["mounted"] = o->testModelCondition(CombatNames::modelCondition("MOUNTED"));
	out["disguised"] = o->testModelCondition(CombatNames::modelCondition("DISGUISED")); // lane HERO-2 (the media's effect label)
	out["level"] = o->getExperienceTracker() ? (int64_t)o->getExperienceTracker()->getRank() : (int64_t)0;
	Array lists;
	if (const AttributeModifierPool *pool = dynamic_cast<const AttributeModifierPool *>(o->findModule("AttributeModifierPoolUpdate")))
	{
		for (const std::string &n : pool->listNames())
		{
			lists.append(String(n.c_str()));
		}
	}
	out["modifiers"] = lists;
	out["experience"] = o->getExperienceTracker() ? (double)o->getExperienceTracker()->getExperience() : 0.0;
	Dictionary powers;
	for (const std::unique_ptr<BehaviorModule> &m : o->modules())
	{
		const SpecialAbilityUpdate *su = dynamic_cast<const SpecialAbilityUpdate *>(m.get());
		if (!su || !su->abilityData())
		{
			continue;
		}
		Dictionary p;
		p["triggered"] = (int64_t)su->abilitiesTriggered();
		p["active"] = su->isActive();
		if (const WeaponFireSpecialAbilityUpdate *wf = dynamic_cast<const WeaponFireSpecialAbilityUpdate *>(su))
		{
			p["shots"] = (int64_t)wf->shotsFired();
		}
		if (const ToggleMountedSpecialAbilityUpdate *tm = dynamic_cast<const ToggleMountedSpecialAbilityUpdate *>(su))
		{
			p["toggles"] = (int64_t)tm->toggles();
		}
		if (const LevelGrantSpecialPower *lg = dynamic_cast<const LevelGrantSpecialPower *>(su))
		{
			p["granted"] = (int64_t)lg->granted();
		}
		if (const ModelConditionSpecialAbilityUpdate *mc = dynamic_cast<const ModelConditionSpecialAbilityUpdate *>(su))
		{
			p["emotions"] = (int64_t)mc->emotionRequests();
		}
		powers[String(su->abilityData()->m_specialPowerTemplateName.c_str())] = p;
	}
	out["powers"] = powers;
	return out;
}

int64_t GameWorld::ready_object_powers(int64_t id)
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	::Object *o = m_game && id > 0 ? m_game->logic().findObjectByID((::ObjectID)id) : nullptr;
	int64_t n = 0;
	if (!o)
	{
		return 0;
	}
	for (const std::unique_ptr<BehaviorModule> &m : o->modules())
	{
		SpecialPowerModule *sp = dynamic_cast<SpecialPowerModule *>(m.get());
		if (sp && sp->pauseCount() == 0 && !sp->isReady())
		{
			sp->setReadyFrame(m_game->logic().getFrame());
			++n;
		}
	}
	return n;
}

bool GameWorld::cast_object_power(int64_t player, int64_t id, const String &power, int64_t target)
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	if (!m_game || !TheSpecialPowerStore || id <= 0)
	{
		return false;
	}
	const SpecialPowerTemplate *t = TheSpecialPowerStore->findSpecialPowerTemplate(power.utf8().get_data());
	if (!t || !m_game->logic().findObjectByID((::ObjectID)id))
	{
		return false;
	}
	if (target > 0)
	{
		GameMessage m(MSG_DO_SPECIAL_POWER_AT_OBJECT, (int)player); // {id, target, options, source}
		m.appendIntegerArgument((int)t->getID());
		m.appendObjectIDArgument((::ObjectID)target);
		m.appendIntegerArgument(0);
		m.appendObjectIDArgument((::ObjectID)id);
		m_game->commands().append(m);
	}
	else
	{
		GameMessage m(MSG_DO_SPECIAL_POWER, (int)player); // {id, options, source}
		m.appendIntegerArgument((int)t->getID());
		m.appendIntegerArgument(0);
		m.appendObjectIDArgument((::ObjectID)id);
		m_game->commands().append(m);
	}
	return true;
}

void GameWorld::gain_hero_levels(int64_t id, int64_t levels)
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	::Object *o = m_game && id > 0 ? m_game->logic().findObjectByID((::ObjectID)id) : nullptr;
	if (o && o->getExperienceTracker() && levels > 0)
	{
		o->getExperienceTracker()->gainExpForLevel((int)levels, true, false);
	}
}

void GameWorld::kill_hero(int64_t id)
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	::Object *o = m_game && id > 0 ? m_game->logic().findObjectByID((::ObjectID)id) : nullptr;
	if (!o)
	{
		return;
	}
	DamageInfo info; // a killing UNRESISTABLE hit without a source (the body's own path: RespawnBody / RespawnUpdate, the death)
	info.m_input.m_damageType = DAMAGE_UNRESISTABLE;
	info.m_input.m_amount = 1000000.0f;
	info.m_input.m_kill = true;
	o->attemptDamage(info);
}

void GameWorld::damage_object(int64_t id, double amount)
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	::Object *o = m_game && id > 0 ? m_game->logic().findObjectByID((::ObjectID)id) : nullptr;
	if (!o)
	{
		return;
	}
	DamageInfo info;
	info.m_input.m_damageType = DAMAGE_UNRESISTABLE;
	info.m_input.m_amount = (float)amount;
	o->attemptDamage(info);
}

void GameWorld::give_money(int64_t player, int64_t amount)
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	::Player *p = m_game ? m_game->logic().players().getNthPlayer((int)player) : nullptr;
	if (p && amount > 0)
	{
		p->depositMoney((std::uint32_t)amount, false); // scenario helper (outside the command path, like create_object)
	}
}

// ---- lane HERO-2: Create-a-Hero ------------------------------------------------------------------------------------------------------------------------------------
static std::string asciiName(const std::u16string &n)
{
	std::string out;
	for (char16_t c : n)
	{
		out.push_back(c < 0x80 ? (char)c : '?');
	}
	return out;
}

Array GameWorld::get_system_heroes() const
{
	Array out;
	ArchiveFileSystem *afs = m_fs.is_valid() ? m_fs->archive_fs() : nullptr;
	if (!afs)
	{
		return out;
	}
	for (const CreateAHeroHero &h : CreateAHeroLibrary::systemHeroes(*afs, nullptr))
	{
		Dictionary d;
		d["name"] = String(asciiName(h.name).c_str());
		d["class"] = (int64_t)h.classIndex;
		d["subclass"] = (int64_t)h.subClassIndex;
		d["unique_id"] = String(h.uniqueID.c_str());
		d["valid"] = h.valid;
		out.append(d);
	}
	return out;
}

Dictionary GameWorld::get_create_a_hero_record(const String &name) const
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	Dictionary out;
	Array errors;
	out["ok"] = false;
	ArchiveFileSystem *afs = m_fs.is_valid() ? m_fs->archive_fs() : nullptr;
	if (!afs || !m_world)
	{
		errors.append(String("get_create_a_hero_record: GameWorld.setup has not run"));
		out["errors"] = errors;
		return out;
	}
	const std::string wanted = name.utf8().get_data();
	std::vector<std::uint8_t> bytes;
	bool found = false;
	if (wanted.size() > 4 && AsciiStringUtil::compareNoCase(wanted.substr(wanted.size() - 4), ".cah") == 0)
	{
		std::ifstream f(wanted, std::ios::binary);
		if (f)
		{
			bytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
			found = true;
		}
	}
	else
	{
		std::vector<std::string> loadErrors;
		for (const CreateAHeroHero &h : CreateAHeroLibrary::systemHeroes(*afs, &loadErrors))
		{
			if (asciiName(h.name) == wanted && h.valid)
			{
				bytes = h.save();
				found = true;
			}
		}
		for (const std::string &e : loadErrors)
		{
			errors.append(String(e.c_str()));
		}
	}
	SkirmishGameSlot slot;
	std::string why;
	if (!found)
	{
		errors.append(String(("get_create_a_hero_record: no system hero or .cah file " + wanted).c_str()));
	}
	else if (!slot.setCreateAHeroBytes(bytes, &why) || !m_world->createAHeroSystem().validateHero(slot.createAHero, m_world->commands(), &why))
	{
		errors.append(String(("get_create_a_hero_record: " + wanted + ": " + why).c_str()));
	}
	else
	{
		PackedByteArray b;
		b.resize((int64_t)bytes.size());
		for (size_t k = 0; k < bytes.size(); ++k)
		{
			b.set((int64_t)k, bytes[k]);
		}
		out["ok"] = true;
		out["record"] = b;
		out["name"] = String(asciiName(slot.createAHero.name).c_str());
		out["class"] = (int64_t)slot.createAHero.classIndex;
		out["subclass"] = (int64_t)slot.createAHero.subClassIndex;
		out["unique_id"] = String(slot.createAHero.uniqueID.c_str());
	}
	out["errors"] = errors;
	return out;
}

Dictionary GameWorld::get_create_a_hero(int64_t player) const
{
	Dictionary out;
	::Player *p = m_game ? m_game->logic().players().getNthPlayer((int)player) : nullptr;
	const CreateAHeroHero *h = p ? m_game->logic().createAHeroes().heroOf(*p) : nullptr;
	if (!h)
	{
		return out;
	}
	out["name"] = String(asciiName(h->name).c_str());
	out["unique_id"] = String(h->uniqueID.c_str());
	out["class"] = (int64_t)h->classIndex;
	out["subclass"] = (int64_t)h->subClassIndex;
	out["surcharge"] = (int64_t)p->getCreateAHeroSurcharge();
	out["can_build"] = TheCreateAHeroSystem && p->hasUpgradeComplete(TheCreateAHeroSystem->canBuildUpgradeName);
	return out;
}
