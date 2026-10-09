// OpenBFME. GPL-3.0.
// StancesBehavior and the StanceTemplate store (lane INTEG-1). See GameLogic/Module/StancesBehavior.h for the target facts and what is not ported.

#include "GameLogic/Module/StancesBehavior.h"

#include "Common/INI.h"
#include "Common/INIException.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "GameLogic/AI/AICommands.h"
#include "GameLogic/AI/AIGroup.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/HordeContain.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"

#include <cstddef>
#include <stdexcept>

const char *const TheStanceNames[] = { "Uninitialized", "Battle", "Aggressive", "HoldGround", "Porcupine", "HoldGroundMoving", nullptr }; // RW 0xC53660

thread_local StanceTemplateStore *TheStanceTemplateStore = nullptr; // SMOOTH-1: per thread (the world context of each thread, RetailObjectWorld::ContextScope)

namespace
{
const unsigned kStatusHidden = 0x10;          // RW 0x8622BA: status 16 (HIDDEN in the binary's status names)
const unsigned kStatusLeavingFactory = 0x5A;  // RW 0x8622F3: status 90 (IS_LEAVING_FACTORY)

const char *const kStop =
	"[S-585] stances: StanceTemplate (RW 0x835967 / 0x83555A), StancesBehavior's update (RW 0x8622E9: Battle once out of the factory), setStance (RW 0x8620DD: the "
	"entries' ModifierLists through the attribute modifier pool, the horde's MeleeBehavior through HordeContain slot 0x260, the porcupine formation toggled back, the "
	"idle commands) and MSG_CHANGE_STANCE (RW 0x77BC59 -> AIGroup::setStance RW 0x76FF27) are ported; NOT ported: RW 0x662A21 (the AI side of hold ground), the "
	"listener notice RW 0x861F97, the callers of onMoveStart / onMoveEnd (RW 0x66A948, RW 0x743882: a HoldGround unit keeps its stance while it moves), the group "
	"manager branch (S-223), the scripted and map object stances (RW 0x94D4EB, 0x695B93), a ModifierList's removal side effects (S-633); HordeNotifyTargetsOf"
	"ImminentProbableCrushingUpdate (RW 0x8D3167) is not ported";

const FieldParse kModuleParse[] = {
	{ "StanceTemplate", INI::parseAsciiString, nullptr, (int)offsetof(StancesBehaviorModuleData, m_stanceTemplate) }, // RW 0xC58CC8
	{ nullptr, nullptr, nullptr, 0 }
};

// RW 0x83555A: the stance's entry, then its sub-block
void parseStance(INI *ini, void *, void *store, const void *)
{
	int index = 0;
	INI::parseIndexList(ini, nullptr, &index, TheStanceNames); // RW 0x42B999
	StanceTemplate *t = static_cast<StanceTemplate *>(store);
	const FieldParse entryParse[] = {
		{ "AttributeModifier", INI::parseAsciiString, nullptr, (int)offsetof(StanceTemplate::Entry, attributeModifier) },     // RW 0x548990 at +0
		{ "MeleeBehavior", HordeContainModuleData::parseMeleeBehavior, nullptr, (int)offsetof(StanceTemplate::Entry, meleeBehavior) }, // RW 0x86C30A at +4
		{ nullptr, nullptr, nullptr, 0 }
	};
	ini->initFromINI(&t->entries[(size_t)index], entryParse);
}

const FieldParse kTemplateParse[] = {
	{ "Stance", parseStance, nullptr, 0 },
	{ nullptr, nullptr, nullptr, 0 }
};

HordeContain *hordeOf(Object &obj)
{
	ContainModuleInterface *c = obj.getContain();
	return c && c->getHordeContainInterface() ? dynamic_cast<HordeContain *>(c) : nullptr; // contain slot 0x7C
}
} // namespace

// ---- the store ----------------------------------------------------------------------------------------------------------------------------------------------------
void StanceTemplateStore::registerBlock(INIBlockRegistry &registry)
{
	registry.registerBlock("StanceTemplate", [](INI *ini) {
		if (!TheStanceTemplateStore)
		{
			throw INIException(8, "StanceTemplate block: TheStanceTemplateStore is not installed");
		}
		TheStanceTemplateStore->parseStanceTemplate(ini);
	});
}

void StanceTemplateStore::parseStanceTemplate(INI *ini)
{
	const std::string name = ini->getNextToken();
	if (m_templates.count(name))
	{
		throw INIException(3, "%s(%d) : Stance %s already defined", ini->getFilename().c_str(), (int)ini->getLineNum(), name.c_str()); // RW 0xC5373C
	}
	auto t = std::make_unique<StanceTemplate>();
	t->name = name;
	ini->initFromINI(t.get(), kTemplateParse);
	m_templates.emplace(name, std::move(t)); // RW 0x83587A
}

const StanceTemplate *StanceTemplateStore::find(const std::string &name) const
{
	auto it = m_templates.find(name);
	return it == m_templates.end() ? nullptr : it->second.get();
}

// ---- the module ---------------------------------------------------------------------------------------------------------------------------------------------------
void StancesBehaviorModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kModuleParse);
}

StancesBehavior::StancesBehavior(Thing *thing, const StancesBehaviorModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
}

void StancesBehavior::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<StancesBehaviorModuleData>("StancesBehavior", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("StancesBehavior", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const StancesBehaviorModuleData *typed = dynamic_cast<const StancesBehaviorModuleData *>(data);
		if (!typed)
		{
			throw std::logic_error("StancesBehavior: the module data is not typed");
		}
		return std::make_unique<StancesBehavior>(thing, typed);
	});
}

StancesBehavior *StancesBehavior::of(Object &obj)
{
	return dynamic_cast<StancesBehavior *>(obj.findModule("StancesBehavior"));
}

StancesBehavior::Stats &StancesBehavior::stats()
{
	static Stats s;
	return s;
}

std::vector<std::string> StancesBehavior::stopLines()
{
	return { kStop };
}

int StancesBehavior::stanceClass(StanceType stance)
{
	// RW 0x861D8E
	if (stance == STANCE_AGGRESSIVE)
	{
		return 2;
	}
	if (stance >= STANCE_HOLD_GROUND && stance <= STANCE_HOLD_GROUND_MOVING)
	{
		return 3;
	}
	return 1;
}

// RW 0x8622E9
UpdateSleepTime StancesBehavior::update()
{
	Object *obj = getObject();
	if (obj && obj->testStatus(kStatusLeavingFactory))
	{
		return UPDATE_SLEEP(1);
	}
	if (m_stance == STANCE_UNINITIALIZED)
	{
		setStance(STANCE_BATTLE);
	}
	return UPDATE_SLEEP_FOREVER;
}

// RW 0x8620DD
bool StancesBehavior::setStance(StanceType stance)
{
	if (stance == m_stance)
	{
		return false;
	}
	Object &obj = *getObject();
	HordeContain *horde = hordeOf(obj);
	if (horde && m_stance == STANCE_PORCUPINE && horde->hordeData().m_isPorcupineFormation && horde->canToggleFormation()) // slots 0xF0, 0x5C
	{
		horde->toggleFormation(); // slot 0x60 (RW 0x86C7F6); the swap sets Battle on this module (RW 0x87629F)
		horde = hordeOf(obj);     // RW 0x862143: the interface again
	}
	AIUpdateInterface *ai = obj.getAIUpdateInterface();
	if (stance == STANCE_HOLD_GROUND && ai)
	{
		ai->aiIdle(CMD_FROM_PLAYER); // RW 0x86216B: AI + 0x20, RW 0x5E821A(0)
	}
	if (m_data->m_stanceTemplate.empty() || !TheStanceTemplateStore)
	{
		return false;
	}
	const StanceTemplate *t = TheStanceTemplateStore->find(m_data->m_stanceTemplate);
	if (!t)
	{
		return false;
	}
	// the current stance is read here, after a toggle above may have changed it (RW 0x862199 reads +0x30 now)
	const std::string &oldList = t->entries[(size_t)m_stance].attributeModifier;
	const StanceTemplate::Entry &entry = t->entries[(size_t)stance];
	if (oldList != entry.attributeModifier)
	{
		if (!oldList.empty())
		{
			obj.removeAttributeModifier(oldList); // RW 0x68F259
		}
		if (!entry.attributeModifier.empty())
		{
			obj.addAttributeModifier(entry.attributeModifier, -1); // RW 0x68F1A8
		}
	}
	if (horde)
	{
		horde->setMeleeBehavior(entry.meleeBehavior); // slot 0x260 (RW 0x86C40D)
	}
	const StanceType old = m_stance;
	const int oldClass = stanceClass(old);
	m_stance = stance;
	if (old != STANCE_UNINITIALIZED && oldClass == stanceClass(stance))
	{
		return true;
	}
	stats().listenerNotices.fetch_add(1, std::memory_order_relaxed); // RW 0x861F97 (S-585)
	ai = obj.getAIUpdateInterface();
	if (ai && ai->isIdle()) // AI slot 0x1B8
	{
		if (stance == STANCE_BATTLE || stance == STANCE_HOLD_GROUND || stance == STANCE_PORCUPINE)
		{
			stats().aiHoldModeNotSet.fetch_add(1, std::memory_order_relaxed); // RW 0x662A21(0 / 1) (S-585)
		}
		else if (old == STANCE_BATTLE || old == STANCE_HOLD_GROUND)
		{
			ai->aiIdle(CMD_FROM_AI); // RW 0x862257: RW 0x5E821A(2)
		}
	}
	return true;
}

// RW 0x862275
void StancesBehavior::onMoveStart()
{
	if (m_stance == STANCE_HOLD_GROUND)
	{
		setStance(STANCE_HOLD_GROUND_MOVING);
	}
	else if (m_stance == STANCE_PORCUPINE)
	{
		setStance(STANCE_BATTLE);
	}
}

// RW 0x86228E
void StancesBehavior::onMoveEnd()
{
	if (m_stance == STANCE_HOLD_GROUND_MOVING)
	{
		setStance(STANCE_HOLD_GROUND);
	}
	if (m_stance == STANCE_HOLD_GROUND || m_stance == STANCE_BATTLE || m_stance == STANCE_PORCUPINE)
	{
		Object *obj = getObject();
		if (obj && !obj->testStatus(kStatusHidden) && obj->getAIUpdateInterface())
		{
			stats().aiHoldModeNotSet.fetch_add(1, std::memory_order_relaxed); // RW 0x8622E1: RW 0x662A21 (S-585)
		}
	}
}

// RW 0x861E17
bool StancesBehavior::applyMeleeBehavior(StanceType stance)
{
	HordeContain *horde = hordeOf(*getObject());
	if (!horde)
	{
		return true;
	}
	const StanceTemplate *t = !m_data->m_stanceTemplate.empty() && TheStanceTemplateStore ? TheStanceTemplateStore->find(m_data->m_stanceTemplate) : nullptr;
	if (!t)
	{
		return false;
	}
	horde->setMeleeBehavior(t->entries[(size_t)stance].meleeBehavior);
	return true;
}

void StancesBehavior::crc(StateHasher &hasher) const
{
	UpdateModule::crc(hasher);
	hasher.addI32((int)m_stance); // RW 0x861DDF: "StanceEnum"
}

// ---- MSG_CHANGE_STANCE --------------------------------------------------------------------------------------------------------------------------------------------
void StancesBehavior::registerHandlers(GameLogicDispatch &d)
{
	// RW 0x77BC59: the player's group (none: nothing), argument 0 the stance; GameData + 0x11CA's group manager branch (RW 0x7575A0) is not ported (S-223, S-585)
	d.registerHandler(MSG_CHANGE_STANCE, "INTEG-1", [](GameLogic &logic, const GameMessage &m) {
		const GameMessageArgument *a = m.getArgument(0);
		if (!a || a->type != ARGUMENTDATATYPE_INTEGER || !logic.players().getNthPlayer(m.getPlayerIndex()))
		{
			return false;
		}
		if (a->integer < 0 || a->integer >= STANCE_COUNT)
		{
			return false; // INFERENCE: RW indexes the template's six entries with the argument unchecked; an out-of-range stance is refused here
		}
		AIGroup group(logic, AICommands::selection(logic, m.getPlayerIndex()));
		for (Object *o : group.members()) // RW 0x76FF27
		{
			if (StancesBehavior *s = of(*o))
			{
				s->setStance((StanceType)a->integer);
			}
		}
		return true;
	});
}
