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

#include "Common/BfmeGame.h"
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
#include "GameClient/LinearCampaign.h"
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
	// the load could not run at all (*error says why). Call once (reload() loads again).
	bool load(std::string *error);
	bool loaded() const { return m_loaded; }
	const SubsystemLoadReport &report() const { return m_s->m_report; }
	double loadSeconds() const { return m_s->m_seconds; }

	ThingFactory &things() { return *m_s->m_things; }
	WeaponStores &weaponStores() { return m_s->m_weaponStores; } // WEAPON-1
	// SPELL-1: the Science, Rank and SpecialPower blocks are parsed for real into these stores
	SpellStores &spellStores() { return *m_s->m_spellStores; }
	const SpellStores &spellStores() const { return *m_s->m_spellStores; }
	ModuleFactory &modules() { return *m_s->m_modules; }
	NameKeyGenerator &nameKeys() { return m_s->m_keys; }
	// LOGIC-1: the PlayerTemplate blocks (data\\ini\\playertemplate.ini) are parsed for real; the runtime classes of GameLogic/Module/LogicModules.h are bound
	PlayerTemplateStore &playerTemplates() { return *m_s->m_playerTemplates; }
	// PROD-1: the control bar's CommandButton / CommandSet store (loaded after the objects, RW 0x71CE3A)
	CommandStore &commands() { return m_s->m_commands; }
	// AI-1: SkirmishAIData / ArmyDefinition / AIBase / AIDozerAssignment (Default\\SkirmishAIData.ini) and PlayerAIType, parsed for real
	SkirmishAIStore &skirmishAI() { return *m_s->m_skirmishAI; }
	const SkirmishAIStore &skirmishAI() const { return *m_s->m_skirmishAI; }
	// PROD-1: the GameData numbers of the build arithmetic and command points, and the Upgrade types (loaded by load(); a failure is in report().errors)
	const ProductionSettings &productionSettings() const { return m_s->m_productionSettings; }
	const UpgradeTypeTable &upgradeTypes() const { return m_s->m_upgradeTypes; }
	// UPGRADE-1: TheUpgradeCenter of this world (the Upgrade blocks of the subsystem load; installed as TheUpgradeCenter with the world's context)
	UpgradeCenter &upgrades() { return m_s->m_upgrades; }
	const UpgradeCenter &upgrades() const { return m_s->m_upgrades; }
	// XP-1: TheExperienceLevelSystem (the ExperienceLevel / ExperienceScalarTable blocks) and TheAttributeModifierStore (the ModifierList blocks)
	ExperienceLevelSystem &experienceLevels() { return m_s->m_experienceLevels; }
	const ExperienceLevelSystem &experienceLevels() const { return m_s->m_experienceLevels; }
	AttributeModifierStore &attributeModifiers() { return m_s->m_attributeModifiers; }
	const AttributeModifierStore &attributeModifiers() const { return m_s->m_attributeModifiers; }
	const StanceTemplateStore &stances() const { return m_s->m_stances; } // INTEG-1
	const EmotionSystem &emotions() const { return m_s->m_emotions; }     // MODULES-2: TheEmotionSystem (the EmotionNugget blocks)
	bool bfme1Data() const { return isBfme1Data(m_fs); } // community PR #36 (Common/BfmeGame.h)
	const LinearCampaignManager &linearCampaigns() const { return m_s->m_linearCampaigns; } // CAMP-1: TheLinearCampaignManager (the LinearCampaign blocks)
	const CreateAHeroSystem &createAHeroSystem() const { return m_s->m_createAHero; } // HERO-2: TheCreateAHeroSystem (Data\INI\CreateAHeroSystem.ini)
	// MOVE-1: the INI macro table the object templates were loaded with (a LocomotorSet block's Speed may be a macro: AIWorld parses those blocks at runtime)
	const INIMacroTable &iniMacros() const { return m_s->m_env.macros; }

	struct MapIniResult
	{
		bool mapIniFound = false, soloIniFound = false;
		std::vector<std::string> errors;        ///< "file: message" per INI error (a failing file stops there, as in retail)
		size_t blocksRecorded = 0;              ///< blocks of other kinds, consumed by the stubs and not applied (S-116)
		std::vector<std::string> definitions;   ///< "Kind Name" of the object blocks the files defined or overrode
	};
	// Loads <mapDir>\map.ini and <mapDir>\solo.ini (load type 2) into a world as a fresh load makes it: when an earlier map or game changed
	// this world, it is reloaded first (reload()). mapDir is the map's directory in the archives, e.g. "maps\\map wor fangorn". Throws
	// std::logic_error before load() and while a game of this world whose map is applied is live.
	MapIniResult applyMapIni(const std::string &mapDir);

	// Lane MAPRESET-1 (stop S-3808, the owner's decision of 2026-10-12: a deliberate departure from RotWK, whose PlayerTemplateStore and
	// SkirmishAIManager keep a map's map.ini blocks for the session: their reset is the empty RW 0x63F3BF, and GameLogic::reset drops only
	// TheGlobalData's overrides, RW 0x62D3F7). Every game starts from a freshly loaded world: reload() discards everything the INI load made
	// (every store, registry, counter, name key, macro, parse record) and runs the startup load again (about 1 s warm). Every reference into the
	// old stores (templates, buttons, upgrades ...) is invalid afterwards; the RetailObjectWorld object itself stays. applyMapIni() reloads a
	// world an earlier map or game changed, and the end of a game reloads its world (endGame), so the next game and its setup see a fresh load.
	// Throws std::logic_error before load() and while a game of this world is live.
	void reload();
	// whether a map or a game changed this world since its last (re)load
	bool changedSinceLoad() const { return m_dirty; }
	// One live game per world (LiveGame::load / ~LiveGame): beginGame fails with *error ("a game of this world is still live") while one is;
	// endGame reloads the world the game changed. Two games at once need two worlds.
	bool beginGame(std::string *error);
	void endGame();
	bool gameLive() const { return m_gameLive; }
	// Setup data: the PlayerTemplate store and the CreateAHeroSystem as the load made them, copies kept outside the reloaded state (refreshed in
	// place by every reload, so the shell's screens may keep them across games). Game setup (NewGame::prepareNewGame, the lobbies' faction lists
	// and limits, the Create-a-Hero screens) reads them, so a live game's map.ini (the shell map's) never reaches the next game's setup
	const PlayerTemplateStore &setupPlayerTemplates() const { return *m_setupTemplates; }
	const CreateAHeroSystem &setupCreateAHeroSystem() const { return *m_setupCreateAHero; }
	// lane MAPRESET-1 (tests): the INI environment the map.ini blocks are parsed with (applyMapIni's INI), and the FX stores the INI load filled
	INIEnvironment &iniEnvironment() { return m_s->m_env; }
	const FXListStore &fxLists() const { return *m_s->m_recorder.fxLists; }
	const FXParticleSystem::FXParticleSystemTemplateStore *fxParticleSystems() const { return m_s->m_recorder.fxParticleSystems.get(); }

	std::vector<std::string> acceptanceStops() const;
	// AUDIO-1: the audio / EVA blocks the INI load parsed (TheAudio / TheEva files, retail order)
	const AudioIniState &audio() const;
	// lane WEATHER-1: an audio event of that name exists in TheAudio's parsed event table (the "Invalid Sound" check, RW 0x73AA94), for INI loaders
	// outside the world (the client's WeatherSettingStore)
	bool audioEventExists(const std::string &name) const;

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
	bool loadState(std::string *error); ///< the startup INI load into m_s (registers the context; kept on success)
	void reloadState();                 ///< reload() without its checks (applyMapIni for a game being loaded, endGame)

	// lane MAPRESET-1: everything the INI load makes, discarded and made again as a whole by reload()
	struct State
	{
		explicit State(ArchiveFileSystem &fs);
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
		LinearCampaignManager m_linearCampaigns;      // CAMP-1
		CreateAHeroSystem m_createAHero;              // HERO-2
		std::unique_ptr<AudioIniState> m_audio;
		std::unique_ptr<SkirmishAIStore> m_skirmishAI;
		WeaponStores m_weaponStores; // WEAPON-1: the Weapon, Armor and DamageFX blocks have real parsers
		std::unique_ptr<SpellStores> m_spellStores; // SPELL-1
		SubsystemLoadReport m_report;
		double m_seconds = 0.0;
		~State();
	};
	void refreshSetup(); ///< lane MAPRESET-1: the setup copies from the (re)loaded state
	ArchiveFileSystem &m_fs;
	std::unique_ptr<State> m_s;
	NameKeyGenerator m_setupKeys; ///< the loaded names, for the setup PlayerTemplate store's lookups
	std::unique_ptr<PlayerTemplateStore> m_setupTemplates;
	std::unique_ptr<CreateAHeroSystem> m_setupCreateAHero;
	bool m_loaded = false;
	bool m_dirty = false;          ///< lane MAPRESET-1: a map or a game changed the state since its (re)load
	bool m_gameLive = false;       ///< lane MAPRESET-1: beginGame .. endGame
	bool m_gameMapApplied = false; ///< the live game's map.ini is applied
};
