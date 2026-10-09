// OpenBFME. GPL-3.0.
//
// RetailObjectWorld: the retail object templates of a mounted install, loaded the way RotWK loads them at startup, plus the per-map
// map.ini / solo.ini overrides (lane MAPOBJ-1). It is the production counterpart of the fixture the object-model retail tests build:
// the subsystem INI load with the real Object / ChildObject / ObjectReskin parsers (OBJ-1), the typed draw module data classes bound
// into the ModuleFactory (DRAW-1 and MAPOBJ-1), the Locomotor store, and recording stubs for every other block (their parsers belong to
// other lanes; the load report says so).
//
// TARGET RW 0x627224 / 0x627290 (loadMapINI, the caller of INI::load with load type 2): the map's directory is the map path up to its
// last separator; "<dir>\map.ini" is loaded when it exists, then "<dir>\solo.ini" when it exists, both with load type 2
// (INI_LOAD_CREATE_OVERRIDES), then "<dir>\map.str". ZH GameLogic.cpp loadMapINI is the same. Stop S-116: only the object blocks of a
// map.ini take effect here.

#pragma once

#include "Common/ArchiveFileSystem.h"
#include "Common/INI.h"
#include "Common/INI/INIBlockStubs.h"
#include "Common/NameKeyGenerator.h"
#include "Common/PlayerTemplate.h"
#include "Common/SubsystemLegend.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/RwGrammar.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Upgrade.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/AttributeModifiers.h"
#include "GameLogic/CreateAHeroSystem.h"
#include "GameLogic/ExperienceLevels.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/ProductionSettings.h"
#include "GameLogic/SkirmishAI/SkirmishAIData.h"
#include "GameLogic/SpellStores.h"
#include "GameLogic/Module/StancesBehavior.h"
#include "GameLogic/System/EmotionSystem.h"
#include "GameLogic/UpgradeTypes.h"
#include "GameLogic/WeaponStores.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

// Common/Audio/AudioIni.h pulls GameLogic/ObjectFilter.h (another ModelConditionFlags than Common/ModelState.h): forward declared here.
class AudioIniState;

class RetailObjectWorld
{
public:
	explicit RetailObjectWorld(ArchiveFileSystem &fs);
	~RetailObjectWorld();
	RetailObjectWorld(const RetailObjectWorld &) = delete;
	RetailObjectWorld &operator=(const RetailObjectWorld &) = delete;

	// Runs the subsystem INI load. INI errors are collected in report().errors (retail would stop at the first); returns false only when
	// the load could not run at all (*error says why). Call once.
	bool load(std::string *error);
	bool loaded() const { return m_loaded; }
	const SubsystemLoadReport &report() const { return m_report; }
	double loadSeconds() const { return m_seconds; }

	ThingFactory &things() { return *m_things; }
	WeaponStores &weaponStores() { return m_weaponStores; } // WEAPON-1
	// SPELL-1: the Science, Rank and SpecialPower blocks are parsed for real into these stores
	SpellStores &spellStores() { return *m_spellStores; }
	const SpellStores &spellStores() const { return *m_spellStores; }
	ModuleFactory &modules() { return *m_modules; }
	NameKeyGenerator &nameKeys() { return m_keys; }
	// LOGIC-1: the PlayerTemplate blocks (data\\ini\\playertemplate.ini) are parsed for real; the runtime classes of GameLogic/Module/LogicModules.h are bound
	PlayerTemplateStore &playerTemplates() { return *m_playerTemplates; }
	// PROD-1: the control bar's CommandButton / CommandSet store (loaded after the objects, RW 0x71CE3A)
	CommandStore &commands() { return m_commands; }
	// AI-1: SkirmishAIData / ArmyDefinition / AIBase / AIDozerAssignment (Default\\SkirmishAIData.ini) and PlayerAIType, parsed for real
	SkirmishAIStore &skirmishAI() { return *m_skirmishAI; }
	const SkirmishAIStore &skirmishAI() const { return *m_skirmishAI; }
	// PROD-1: the GameData numbers of the build arithmetic and command points, and the Upgrade types (loaded by load(); a failure is in report().errors)
	const ProductionSettings &productionSettings() const { return m_productionSettings; }
	const UpgradeTypeTable &upgradeTypes() const { return m_upgradeTypes; }
	// UPGRADE-1: TheUpgradeCenter of this world (the Upgrade blocks of the subsystem load; installed as TheUpgradeCenter with the world's context)
	UpgradeCenter &upgrades() { return m_upgrades; }
	const UpgradeCenter &upgrades() const { return m_upgrades; }
	// XP-1: TheExperienceLevelSystem (the ExperienceLevel / ExperienceScalarTable blocks) and TheAttributeModifierStore (the ModifierList blocks)
	ExperienceLevelSystem &experienceLevels() { return m_experienceLevels; }
	const ExperienceLevelSystem &experienceLevels() const { return m_experienceLevels; }
	AttributeModifierStore &attributeModifiers() { return m_attributeModifiers; }
	const AttributeModifierStore &attributeModifiers() const { return m_attributeModifiers; }
	const StanceTemplateStore &stances() const { return m_stances; } // INTEG-1
	const EmotionSystem &emotions() const { return m_emotions; }     // MODULES-2: TheEmotionSystem (the EmotionNugget blocks)
	const CreateAHeroSystem &createAHeroSystem() const { return m_createAHero; } // HERO-2: TheCreateAHeroSystem (Data\INI\CreateAHeroSystem.ini)
	// MOVE-1: the INI macro table the object templates were loaded with (a LocomotorSet block's Speed may be a macro: AIWorld parses those blocks at runtime)
	const INIMacroTable &iniMacros() const { return m_env.macros; }

	struct MapIniResult
	{
		bool mapIniFound = false, soloIniFound = false;
		std::vector<std::string> errors;        ///< "file: message" per INI error (a failing file stops there, as in retail)
		size_t blocksRecorded = 0;              ///< blocks of other kinds, consumed by the stubs and not applied (S-116)
		std::vector<std::string> definitions;   ///< "Kind Name" of the object blocks the files defined or overrode
	};
	// Drops the previous map's overrides (ThingFactory::reset), then loads <mapDir>\map.ini and <mapDir>\solo.ini (load type 2).
	// mapDir is the map's directory in the archives, e.g. "maps\\map wor fangorn".
	MapIniResult applyMapIni(const std::string &mapDir);

	std::vector<std::string> acceptanceStops() const;
	// AUDIO-1: the audio / EVA blocks the INI load parsed (TheAudio / TheEva files, retail order)
	const AudioIniState &audio() const;

	// The process-wide parsing context this world owns: TheLocomotorStore, TheCommandStore, the weapon / armor / damage-FX stores and the audio
	// validator of the contain parse hooks. They are registered, selected and removed TOGETHER as removable owner registrations
	// (Common/GlobalOwnerChain.h), so any number of worlds can live and die in any order. load() leaves this world's context selected;
	// applyMapIni() selects it for the duration of the parse and then selects the context that was live before.
	bool isCurrentContext() const;
	static const RetailObjectWorld *currentContext(); ///< the world whose context is selected, nullptr when none

	// Selects this world's context for a scope and, on exit (normal or exception), selects the context that was live before it, if that world still
	// lives. Runtime code that reads the process-wide stores (stepping the logic, queueing production, creating objects) holds one while several
	// worlds exist; with one world it changes nothing.
	class ContextScope
	{
	public:
		explicit ContextScope(RetailObjectWorld &world);
		~ContextScope();
		ContextScope(const ContextScope &) = delete;
		ContextScope &operator=(const ContextScope &) = delete;

	private:
		RetailObjectWorld &m_world;
		bool m_wasRegistered;
		const void *m_previous;
	};
	std::unique_ptr<ContextScope> enterContext() { return std::unique_ptr<ContextScope>(new ContextScope(*this)); }

private:
	void registerContext(); ///< register (or move to the top of every chain) this world's context
	void unregisterContext();
	ArchiveFileSystem &m_fs;
	NameKeyGenerator m_keys;
	std::unique_ptr<RwGrammar> m_grammar;
	std::unique_ptr<ModuleFactory> m_modules;
	std::unique_ptr<ThingFactory> m_things;
	std::unique_ptr<PlayerTemplateStore> m_playerTemplates;
	INIEnvironment m_env;
	INIBlockRecorder m_recorder;
	SubsystemLegend m_legend;
	LocomotorStore m_locomotors;
	CommandStore m_commands;
	ProductionSettings m_productionSettings;
	UpgradeTypeTable m_upgradeTypes;
	UpgradeCenter m_upgrades; // UPGRADE-1
	ExperienceLevelSystem m_experienceLevels;     // XP-1
	AttributeModifierStore m_attributeModifiers;  // XP-1
	StanceTemplateStore m_stances;                // INTEG-1
	EmotionSystem m_emotions;                     // MODULES-2
	CreateAHeroSystem m_createAHero;              // HERO-2
	std::unique_ptr<AudioIniState> m_audio;
	std::unique_ptr<SkirmishAIStore> m_skirmishAI;
	WeaponStores m_weaponStores; // WEAPON-1: the Weapon, Armor and DamageFX blocks have real parsers
	std::unique_ptr<SpellStores> m_spellStores; // SPELL-1
	SubsystemLoadReport m_report;
	bool m_loaded = false;
	double m_seconds = 0.0;
};
