// OpenBFME. GPL-3.0.
// See GameLogic/Object/RetailObjectWorld.h.

#include "GameLogic/Object/RetailObjectWorld.h"

#include "Common/Audio/AudioIni.h"
#include "Common/GlobalOwnerChain.h"
#include "GameLogic/SpellStops.h"
#include "GameLogic/HeroSystem.h"
#include "GameLogic/Module/AISpecialPowerUpdate.h"
#include "GameLogic/Module/Mod4Stops.h"
#include "GameLogic/ActionManager.h"
#include "GameLogic/Module/SlavedUpdate.h"
#include "GameLogic/Module/SpawnBehavior.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Module/SpellBookPowers.h"
#include "Common/INIException.h"
#include "GameLogic/ContainParseHooks.h"
#include "GameLogic/Weapon.h"
#include "GameClient/FXList.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDrawModules.h"
#include "GameClient/MapHordeSpawn.h"
#include "GameLogic/Module/LogicModules.h"
#include "GameLogic/ProductionStops.h"

#include <chrono>
#include <stdexcept>

namespace
{
const RwBinaryData &bfme1Grammar()
{
	static const RwBinaryData data = [] {
		auto result = RwBinaryData::embedded();
		constexpr unsigned line = 0xbf100001;
		result.functions[line] = RwFunction();
		const std::map<std::string, std::vector<std::string>> fields = {
			{"AutoPickUpUpdate", {"PickUpKindOf", "PickUpKindOf2", "Bored", "BoredFilter"}},
			{"DynamicPortalBehaviour", {"AllowKindOf", "RejectKindOf", "AllowEnemies"}},
			{"BloodthirstyUpdate", {"NumToSacrifice", "InitiateVoice2"}},
			{"SlaughterHordeContain", {"MaxHordeCapacity"}},
		};
		for (auto &cls : result.classes)
		{
			auto found = fields.find(cls.name);
			if (found == fields.end()) continue;
			RwTable table;
			table.id = "bfme1-" + cls.name;
			for (const auto &name : found->second) table.rows.push_back({name, line});
			result.tables[table.id] = table;
			cls.tables.push_back({table.id});
		}
		// BFME1 RandomSoundSelectorFieldParse.cpp: SoundState's voice table.
		const char *voices[] = {"VoiceSelect2", "VoiceSelectGroup2", "VoiceMove2", "VoiceAttack2", "VoiceAmbushed", "VoiceSelectBattle2", "SoundAmbientReallyDamaged2"};
		for (auto &entry : result.tables)
		{
			auto &rows = entry.second.rows;
			if (std::none_of(rows.begin(), rows.end(), [](const RwRow &r) { return r.name == "VoiceSelect"; })) continue;
			for (const auto *name : voices)
				if (std::none_of(rows.begin(), rows.end(), [name](const RwRow &r) { return r.name == name; })) rows.push_back({name, line});
		}
		for (auto &entry : result.tables)
		{
			auto &rows = entry.second.rows;
			if (std::any_of(rows.begin(), rows.end(), [](const RwRow &r) { return r.name == "InitiateSound"; }) &&
				std::none_of(rows.begin(), rows.end(), [](const RwRow &r) { return r.name == "InitiateSound2"; })) rows.push_back({"InitiateSound2", line});
		}
		return result;
	}();
	return data;
}
}

RetailObjectWorld::State::State(ArchiveFileSystem &fs)
	: m_fs(fs)
{
	m_keys.init();
	m_grammar = std::make_unique<RwGrammar>(m_fs.doesFileExist("Data\\INI\\Default\\SubsystemLegendExpansion1.ini") ? RwBinaryData::embedded() : bfme1Grammar());
	m_modules = std::make_unique<ModuleFactory>(m_keys, *m_grammar);
	m_modules->init();
	W3DDrawModules::registerTypedDrawModuleData(*m_modules);
	// the horde contain data of the map's horde objects (their members are the drawables of a horde)
	MapHordeSpawn::bindHordeContainData(*m_modules);
	LogicModules::registerAll(*m_modules); // LOGIC-1: ActiveBody and HordeContain run for real, every other class is an UnportedModule (S-140)
	m_things = std::make_unique<ThingFactory>(m_keys, *m_modules, *m_grammar, !m_fs.doesFileExist("Data\\INI\\Default\\SubsystemLegendExpansion1.ini"));
	m_playerTemplates = std::make_unique<PlayerTemplateStore>(m_keys);
	m_spellStores = std::make_unique<SpellStores>(m_keys);
	m_audio = std::make_unique<AudioIniState>();
	m_skirmishAI = std::make_unique<SkirmishAIStore>(m_keys);
	m_env.fileSystem = &m_fs;
	m_legend.registerBlock(m_env.blocks);
	m_things->registerBlocks(m_env.blocks);
	m_playerTemplates->registerBlock(m_env.blocks);
	// every other block: recording stubs (their parsers belong to other port steps; the Locomotor store is the one other real parser)
	std::vector<std::string> skip = { "LoadSubsystem", "Object", "ObjectReskin", "ChildObject", "PlayerTemplate", "CommandButton", "CommandSet", "Upgrade",
		"ExperienceLevel", "ExperienceScalarTable", "ModifierList", // XP-1: these three are parsed for real
		"StanceTemplate", // INTEG-1: TheStanceTemplateStore (RW 0x835967)
		"EmotionNugget", // MODULES-2: TheEmotionSystem (RW 0x8E0D59)
		"CreateAHeroSystem", // HERO-2: TheCreateAHeroSystem (its own file, RW 0x61A10F)
		"LinearCampaign", "MissionObjectiveList" }; // CAMP-1: TheLinearCampaignManager (RW 0x5ECCB9)
	// AUDIO-1: the audio blocks (AudioEvent, DialogEvent, MusicTrack, ..., EvaEvent, AudioSettings, MiscAudio) are parsed for real
	skip.insert(skip.end(), AudioIniState::blockKeywords().begin(), AudioIniState::blockKeywords().end());
	// AI-1: the skirmish AI blocks are parsed for real (GameLogic/SkirmishAI/SkirmishAIData.h)
	skip.insert(skip.end(), SkirmishAIStore::blockKeywords().begin(), SkirmishAIStore::blockKeywords().end());
	for (const char *const *k = SpellStores::blockKeywords(); *k; ++k)
	{
		skip.push_back(*k); // SPELL-1: Science, Rank, SpecialPower
	}
	RegisterRecordingBlockStubs(m_env.blocks, m_recorder, skip, StubExtent::Lenient);
	m_audio->registerBlocks(m_env.blocks);
	m_skirmishAI->registerBlocks(m_env.blocks);
	// PROD-1: the control bar's two block parsers (RW 0x5DA711 / 0x7205B9) replace their recording stubs
	m_env.blocks.registerBlock("CommandButton", [](INI *ini) { CommandStore::parseCommandButtonDefinitionGlobal(ini); });
	m_env.blocks.registerBlock("CommandSet", [](INI *ini) { CommandStore::parseCommandSetDefinitionGlobal(ini); });
	// UPGRADE-1: TheUpgradeCenter (RW 0xDE45A0): the veterancy upgrades exist before any INI (RW 0x66FDD3), the Upgrade block is RW 0x66FCDA. Its sound and Eva
	// fields are validated against this world's audio / Eva tables, which the legend loads first (TheAudio, TheEva precede TheUpgradeCenter)
	m_upgrades.init();
	m_upgrades.registerBlock(m_env.blocks);
	UpgradeParseServices upgradeServices;
	upgradeServices.audioEventExists = [this](const std::string &name) { return !name.empty() && m_audio->infos.contains(name); };
	upgradeServices.evaEventIndex = [this](const std::string &name) { return m_audio->eva.findIndex(name, false); };
	m_upgrades.setParseServices(upgradeServices);
	// XP-1: TheExperienceLevelSystem (RW 0xDE4704; ExperienceLevel RW 0x68A928, ExperienceScalarTable RW 0x689920) and TheAttributeModifierStore (RW 0xDE3C14;
	// ModifierList RW 0x6149AB)
	m_recorder.fxLists = std::make_shared<FXListStore>(); // XP-1: allocated before the load, selected as TheFXListStore with this world's context
	m_experienceLevels.registerBlocks(m_env.blocks);
	m_attributeModifiers.registerBlock(m_env.blocks);
	m_stances.registerBlock(m_env.blocks); // INTEG-1: TheStanceTemplateStore (RW 0xDE8C80; StanceTemplate RW 0x835967)
	m_emotions.registerBlock(m_env.blocks); // MODULES-2: TheEmotionSystem (RW 0xDE8C88; EmotionNugget RW 0x8E0D59)
	m_createAHero.registerBlock(m_env.blocks); // HERO-2: TheCreateAHeroSystem (RW 0xDE3D84)
	SpellStores::registerBlocks(m_env.blocks); // SPELL-1
	m_linearCampaigns.registerBlock(m_env.blocks); // CAMP-1
	m_env.blocks.registerBlock("MissionObjectiveList", [this](INI *ini) {
		using Objectives = std::vector<std::pair<std::string, bool>>;
		static const FieldParse fields[] = {
			{ "MissionObjectiveTag", [](INI *i, void *, void *store, const void *) {
				static_cast<Objectives *>(store)->emplace_back(i->getNextToken(), false);
			}, nullptr, 0 },
			{ "BonusMissionObjectiveTag", [](INI *i, void *, void *store, const void *) {
				static_cast<Objectives *>(store)->emplace_back(i->getNextToken(), true);
			}, nullptr, 0 },
			{ nullptr, nullptr, nullptr, 0 }
		};
		m_missionObjectives.clear();
		ini->initFromINI(&m_missionObjectives, fields);
	});
}

const AudioIniState &RetailObjectWorld::audio() const
{
	return *m_s->m_audio;
}

namespace
{
using AudioExists = std::function<bool(const std::string &)>;

// one chain per thread; the main thread's is never destroyed (a world may outlive static destruction order), a worker's goes with it
GlobalOwnerChain<LocomotorStore *> &locomotorChain()
{
	static thread_local ThreadOwnerChain<LocomotorStore *> chain(TheLocomotorStore);
	return chain.get();
}
GlobalOwnerChain<CommandStore *> &commandChain()
{
	static thread_local ThreadOwnerChain<CommandStore *> chain(TheCommandStore);
	return chain.get();
}
GlobalOwnerChain<UpgradeCenter *> &upgradeChain()
{
	static thread_local ThreadOwnerChain<UpgradeCenter *> chain(TheUpgradeCenter);
	return chain.get();
}
GlobalOwnerChain<ExperienceLevelSystem *> &experienceChain()
{
	static thread_local ThreadOwnerChain<ExperienceLevelSystem *> chain(TheExperienceLevelSystem);
	return chain.get();
}
GlobalOwnerChain<AttributeModifierStore *> &attributeModifierChain()
{
	static thread_local ThreadOwnerChain<AttributeModifierStore *> chain(TheAttributeModifierStore);
	return chain.get();
}
GlobalOwnerChain<StanceTemplateStore *> &stanceChain()
{
	static thread_local ThreadOwnerChain<StanceTemplateStore *> chain(TheStanceTemplateStore); // SMOOTH-1: per thread
	return chain.get();
}
GlobalOwnerChain<EmotionSystem *> &emotionChain()
{
	static thread_local ThreadOwnerChain<EmotionSystem *> chain(TheEmotionSystem); // MODULES-2: per thread, as the stances
	return chain.get();
}
GlobalOwnerChain<LinearCampaignManager *> &linearCampaignChain()
{
	static thread_local ThreadOwnerChain<LinearCampaignManager *> chain(TheLinearCampaignManager); // CAMP-1: per thread, as the stances
	return chain.get();
}
GlobalOwnerChain<CreateAHeroSystem *> &createAHeroChain()
{
	static thread_local ThreadOwnerChain<CreateAHeroSystem *> chain(TheCreateAHeroSystem); // HERO-2: per thread, as the stances
	return chain.get();
}
GlobalOwnerChain<FXListStore *> &fxListChain()
{
	static thread_local ThreadOwnerChain<FXListStore *> chain(TheFXListStore);
	return chain.get();
}
GlobalOwnerChain<AudioExists> &audioChain()
{
	static thread_local ThreadOwnerChain<AudioExists> chain(TheContainParseHooks().audioEventExists);
	return chain.get();
}
GlobalOwnerChain<AudioExists> &weaponTemplateChain()
{
	static thread_local ThreadOwnerChain<AudioExists> chain(TheContainParseHooks().weaponTemplateExists);
	return chain.get();
}
} // namespace

void RetailObjectWorld::registerContext()
{
	locomotorChain().install(this, &m_s->m_locomotors); // the Locomotor block has a real parser (HORDE-1) that needs the store
	commandChain().install(this, &m_s->m_commands);     // PROD-1: CommandButton / CommandSet
	upgradeChain().install(this, &m_s->m_upgrades);     // UPGRADE-1: the Upgrade block and every upgrade mask field of the object modules
	experienceChain().install(this, &m_s->m_experienceLevels);      // XP-1
	attributeModifierChain().install(this, &m_s->m_attributeModifiers); // XP-1
	stanceChain().install(this, &m_s->m_stances);                       // INTEG-1
	emotionChain().install(this, &m_s->m_emotions);                     // MODULES-2
	linearCampaignChain().install(this, &m_s->m_linearCampaigns);       // CAMP-1
	createAHeroChain().install(this, &m_s->m_createAHero);              // HERO-2
	fxListChain().install(this, m_s->m_recorder.fxLists.get());         // XP-1: the FXList blocks fill this world's store; LevelUpFx / ModifierList FX validate against it
	// AUDIO-1: object parsing (OpenContain / HordeContain sound fields) asks whether an audio event exists. No AudioManager exists at INI time,
	// so the predicate is AudioApi::isValidEvent's own (a non-empty name present in the parsed event table), applied to THIS world's table.
	audioChain().install(this, [this](const std::string &name) { return !name.empty() && m_s->m_audio->infos.contains(name); });
	m_s->m_weaponStores.install(); // WEAPON-1
	m_s->m_spellStores->install(); // SPELL-1
	// GARRISON-2: TransportContain's GrabWeapon / ThrowOutPassengersLandingWarhead resolve through TheWeaponStore (RW 0x73AE79 -> 0x6CC5DF: an unknown name is a
	// null pointer, no error); the store is the one install() just made current
	weaponTemplateChain().install(this, [](const std::string &name) { return TheWeaponStore && TheWeaponStore->findWeaponTemplate(name) != nullptr; });
}

void RetailObjectWorld::unregisterContext()
{
	locomotorChain().remove(this);
	commandChain().remove(this);
	upgradeChain().remove(this);
	experienceChain().remove(this);
	attributeModifierChain().remove(this);
	stanceChain().remove(this);
	emotionChain().remove(this);
	linearCampaignChain().remove(this);
	createAHeroChain().remove(this);
	fxListChain().remove(this);
	audioChain().remove(this);
	weaponTemplateChain().remove(this);
	m_s->m_weaponStores.uninstall();
	m_s->m_spellStores->uninstall();
}

bool RetailObjectWorld::isCurrentContext() const
{
	return audioChain().current() == this && commandChain().current() == this && upgradeChain().current() == this && locomotorChain().current() == this && WeaponStores::currentOwner() == &m_s->m_weaponStores
		&& SpellStores::currentOwner() == m_s->m_spellStores.get();
}

const RetailObjectWorld *RetailObjectWorld::currentContext()
{
	return static_cast<const RetailObjectWorld *>(audioChain().current());
}

RetailObjectWorld::ContextScope::ContextScope(RetailObjectWorld &world)
	: m_world(world), m_wasRegistered(audioChain().contains(&world)), m_previous(audioChain().current())
{
	m_world.registerContext();
}

RetailObjectWorld::ContextScope::~ContextScope()
{
	if (m_previous && m_previous != &m_world && audioChain().contains(m_previous))
	{
		const_cast<RetailObjectWorld *>(static_cast<const RetailObjectWorld *>(m_previous))->registerContext();
	}
	else if (!m_wasRegistered)
	{
		m_world.unregisterContext();
	}
}

RetailObjectWorld::RetailObjectWorld(ArchiveFileSystem &fs)
	: m_fs(fs), m_s(std::make_unique<State>(fs))
{
}

RetailObjectWorld::State::~State() = default;

void RetailObjectWorld::refreshSetup()
{
	m_setupKeys = m_s->m_keys;
	if (!m_setupTemplates)
	{
		m_setupTemplates = std::make_unique<PlayerTemplateStore>(m_setupKeys);
	}
	m_setupTemplates->assignFrom(*m_s->m_playerTemplates);
	if (!m_setupCreateAHero)
	{
		m_setupCreateAHero = std::make_unique<CreateAHeroSystem>(m_s->m_createAHero);
	}
	else
	{
		*m_setupCreateAHero = m_s->m_createAHero; // in place: the shell's screens keep the object
	}
}

RetailObjectWorld::~RetailObjectWorld()
{
	unregisterContext();
}

namespace
{
const char *const kGameLive = "RetailObjectWorld: a game of this world is still live (S-3808: every game starts from a freshly loaded world; two games at once need two worlds)";
}

void RetailObjectWorld::reload()
{
	if (!m_loaded)
	{
		throw std::logic_error("RetailObjectWorld::reload: the world is not loaded");
	}
	if (m_gameLive)
	{
		throw std::logic_error(kGameLive);
	}
	reloadState();
}

void RetailObjectWorld::reloadState()
{
	const void *previous = audioChain().current();
	const bool registered = audioChain().contains(this);
	unregisterContext();
	m_s.reset(); // every store the INI load made goes (the old one first: no two states in memory)
	m_s = std::make_unique<State>(m_fs);
	std::string error;
	if (!loadState(&error)) // selects this world's context, as load() does
	{
		throw std::runtime_error("RetailObjectWorld::reload: " + error);
	}
	if (!registered)
	{
		unregisterContext();
	}
	else if (previous && previous != this && audioChain().contains(previous))
	{
		const_cast<RetailObjectWorld *>(static_cast<const RetailObjectWorld *>(previous))->registerContext(); // the order of the chains as before
	}
	m_dirty = false;
}

bool RetailObjectWorld::beginGame(std::string *error)
{
	if (m_gameLive)
	{
		if (error)
		{
			*error = kGameLive;
		}
		return false;
	}
	m_gameLive = true;
	m_gameMapApplied = false;
	return true;
}

void RetailObjectWorld::endGame()
{
	if (!m_gameLive)
	{
		throw std::logic_error("RetailObjectWorld::endGame: no game of this world is live");
	}
	m_gameLive = false;
	m_gameMapApplied = false;
	if (m_dirty)
	{
		reloadState(); // the next game, and its setup, start from a freshly loaded world
	}
}

bool RetailObjectWorld::loadState(std::string *error)
{
	m_s->m_commands.setThingFactory(m_s->m_things.get());
	registerContext(); // selected for the whole load; kept when it succeeds, rolled back when it fails (an error return or an exception)
	bool ok = false;
	const std::shared_ptr<void> rollback(nullptr, [this, &ok](void *) {
		if (!ok)
		{
			unregisterContext();
		}
	});
	INI ini(m_s->m_env);
	SubsystemLoadOptions options;
	options.collectErrors = true;
	options.bfme1 = !m_fs.doesFileExist("Data\\INI\\Default\\SubsystemLegendExpansion1.ini");
	options.cinematics = true; // the census counts the Cinematic folder (338 templates), like the object-model retail test
	const auto t0 = std::chrono::steady_clock::now();
	try
	{
		RunSubsystemIniLoad(m_s->m_legend, ini, options, m_s->m_report);
	}
	catch (const std::exception &e)
	{
		if (error)
		{
			*error = std::string("subsystem INI load failed: ") + e.what();
		}
		unregisterContext();
		return false;
	}
	// AI-1: TheBaseTemplateLibrary's post-load (RW 0x82FA98) reads every AIBase's .bse layout; its errors reach the report like the legend's
	{
		std::vector<std::string> layoutErrors;
		m_s->m_skirmishAI->loadBaseLayouts(m_fs, &layoutErrors, &m_s->m_report.warnings);
		for (const std::string &e : layoutErrors)
		{
			SubsystemLoadReport::FileError fe;
			fe.file = "TheBaseTemplateLibrary";
			fe.code = 0;
			fe.message = e;
			m_s->m_report.errors.push_back(fe);
		}
	}
	// The BFME2 front end exposes both base-game campaigns alongside the expansion.
	// RotWK's subsystem legend only loads LinearCampaignExpansion1.ini.
	if (!m_s->m_linearCampaigns.find("GOOD_CAMPAIGN") && m_fs.doesFileExist("Data\\INI\\LinearCampaign.ini"))
	{
		try
		{
			ini.load("Data\\INI\\LinearCampaign.ini", INI_LOAD_OVERWRITE);
		}
		catch (const INIException &e)
		{
			SubsystemLoadReport::FileError fe;
			fe.file = "Data\\INI\\LinearCampaign.ini";
			fe.code = e.code();
			fe.message = e.what();
			m_s->m_report.errors.push_back(fe);
		}
	}
	// INTEG-1: TheStanceTemplateStore's init (RW 0x83568D) loads Data\INI\Stances.ini itself (INI::load, type 1), not through the legend; an error reaches the report
	if (!options.bfme1) try
	{
		INI stancesIni(m_s->m_env);
		stancesIni.load("Data\\INI\\Stances.ini", INI_LOAD_OVERWRITE); // RW 0xC536E4
	}
	catch (const INIException &e)
	{
		SubsystemLoadReport::FileError fe;
		fe.file = "Data\\INI\\Stances.ini";
		fe.code = e.code();
		fe.message = e.what();
		m_s->m_report.errors.push_back(fe);
	}
	// PROD-1: the ControlBar loads its own three files after the objects (RW 0x71CE3A); its errors reach the report like the legend's
	std::vector<std::string> commandErrors;
	m_s->m_commands.loadFromFiles(m_s->m_env, &commandErrors);
	for (const std::string &e : commandErrors)
	{
		SubsystemLoadReport::FileError fe;
		fe.file = "ControlBar";
		fe.code = 0;
		fe.message = e;
		m_s->m_report.errors.push_back(fe);
	}
	// HERO-2: TheCreateAHeroSystem's init (RW 0x61A10F) loads Data\INI\CreateAHeroSystem.ini itself (type 1) once the upgrades and the control bar are loaded
	if (!options.bfme1) try
	{
		m_s->m_createAHero.load(m_s->m_env);
		if (!m_s->m_createAHero.loaded())
		{
			throw INIException(3, "Data\\INI\\CreateAHeroSystem.ini has no CreateAHeroSystem block");
		}
	}
	catch (const INIException &e)
	{
		SubsystemLoadReport::FileError fe;
		fe.file = "Data\\INI\\CreateAHeroSystem.ini";
		fe.code = e.code();
		fe.message = e.what();
		m_s->m_report.errors.push_back(fe);
	}
	std::string perr;
	if (!ProductionSettings::load(m_fs, m_s->m_productionSettings, &perr))
	{
		SubsystemLoadReport::FileError fe;
		fe.file = "ProductionSettings";
		fe.code = 0;
		fe.message = perr;
		m_s->m_report.errors.push_back(fe);
	}
	// UPGRADE-1: the sub upgrade names resolve once every Upgrade block is known; production's type table is the center's
	perr.clear();
	if (!m_s->m_upgrades.resolveSubUpgrades(&perr))
	{
		SubsystemLoadReport::FileError fe;
		fe.file = "UpgradeCenter";
		fe.code = 0;
		fe.message = perr;
		m_s->m_report.errors.push_back(fe);
	}
	m_s->m_upgrades.resolveTemplates(*m_s->m_things);
	m_s->m_upgradeTypes.assign(m_s->m_upgrades);
	refreshSetup(); // lane MAPRESET-1: game setup's copies
	m_s->m_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	ok = true;
	return true;
}

bool RetailObjectWorld::load(std::string *error)
{
	if (m_loaded)
	{
		if (error)
		{
			*error = "RetailObjectWorld::load called twice";
		}
		return false;
	}
	if (!loadState(error))
	{
		return false;
	}
	m_loaded = true;
	return true;
}

RetailObjectWorld::MapIniResult RetailObjectWorld::applyMapIni(const std::string &mapDir)
{
	MapIniResult result;
	if (!m_loaded)
	{
		throw std::logic_error("RetailObjectWorld::applyMapIni: the world is not loaded");
	}
	if (m_gameLive && m_gameMapApplied)
	{
		throw std::logic_error(kGameLive);
	}
	// lane MAPRESET-1 (S-3808): a world an earlier map or game changed is loaded again: the map.ini applies to a fresh load. (Before, the
	// previous map's ThingFactory / Weapon / ExperienceLevel / Science / Rank / SpecialPower overrides were dropped here as ZH's resets do,
	// and everything else stayed: PLAY-4's S-3808)
	if (m_dirty)
	{
		reloadState(); // also for a game being loaded: it holds nothing of the world before its map.ini
	}
	m_dirty = true;
	m_gameMapApplied = m_gameLive;
	const size_t defsBefore = m_s->m_things->definitions().size();
	const size_t recordsBefore = m_s->m_recorder.records.size();
	ContextScope context(*this); // this world's stores AND audio validator for the whole parse; the previous live context returns on exit
	for (const char *file : { "map.ini", "solo.ini" }) // RW 0x627224, 0x627290
	{
		const std::string path = mapDir + "\\" + file;
		if (!m_fs.doesFileExist(path))
		{
			continue;
		}
		(file[0] == 'm' ? result.mapIniFound : result.soloIniFound) = true;
		INI ini(m_s->m_env);
		try
		{
			ini.load(path, INI_LOAD_CREATE_OVERRIDES);
		}
		catch (const INIException &e)
		{
			result.errors.push_back(path + ": " + e.what());
		}
		catch (const std::exception &e)
		{
			result.errors.push_back(path + ": " + e.what());
		}
	}
	const auto &defs = m_s->m_things->definitions();
	for (size_t i = defsBefore; i < defs.size(); ++i)
	{
		result.definitions.push_back(defs[i].kind + " " + defs[i].name);
	}
	result.blocksRecorded = m_s->m_recorder.records.size() - recordsBefore;
	return result;
}

std::vector<std::string> RetailObjectWorld::acceptanceStops() const
{
	std::vector<std::string> out = m_s->m_things->acceptanceStops();
	for (const std::string &u : m_s->m_playerTemplates->unverified())
	{
		out.push_back(u);
	}
	for (const std::string &l : ProductionStops::lines())
	{
		out.push_back(l);
	}
	for (const std::string &l : CommandStore::acceptanceStops())
	{
		out.push_back(l);
	}
	for (const std::string &l : SpellStops::lines()) // SPELL-1
	{
		out.push_back(l);
	}
	for (const std::string &l : SpellBookPowers::stopLines()) // SPELL-2
	{
		out.push_back(l);
	}
	for (const std::string &l : HeroSystem::stopLines()) // HERO-1
	{
		out.push_back(l);
	}
	for (const std::string &l : SpecialAbilityModules::hero2StopLines()) // HERO-2
	{
		out.push_back(l);
	}
	for (const std::string &l : AISpecialPowerUpdate::stopLines()) // MOD-4, AIPOW-1
	{
		out.push_back(l);
	}
	for (const std::string &l : ActionManager::stopLines()) // AIPOW-1
	{
		out.push_back(l);
	}
	for (const std::string &l : Mod4Stops::lines()) // MOD-4
	{
		out.push_back(l);
	}
	for (const std::string &l : SpawnBehavior::stopLines()) // MOD-4
	{
		out.push_back(l);
	}
	for (const std::string &l : SlavedUpdate::stopLines()) // MOD-4
	{
		out.push_back(l);
	}
	// lane PLAY-4 r2 found it; lane MAPRESET-1: the owner's decision (2026-10-12) to depart from RotWK here
	out.push_back("[S-3808] deliberate departure from RotWK: every game starts from a freshly loaded world. A world a map.ini or a game changed "
				  "is discarded and loaded again (reload: the startup INI load, about 1 s warm) before the next map's map.ini and when its game ends; "
				  "one game per world may be live; game setup reads the load's PlayerTemplates. RotWK keeps a map's PlayerTemplate and skirmish AI "
				  "blocks (and the other stores' map additions) for the session: its PlayerTemplateStore and SkirmishAIManager resets are empty "
				  "(RW 0x63F3BF) and GameLogic::reset drops TheGlobalData's overrides alone (RW 0x62D3F7), so in retail a game after Helm's Deep or "
				  "Minas Tirith places the starting units at those maps' offsets and two peers whose sessions played different maps start with "
				  "different data. A retail session that played such a map before differs from this engine's game; not compared with retail");
	out.push_back("[S-116] map.ini / solo.ini: only the Object / ChildObject / ObjectReskin / CommandButton / CommandSet / ExperienceLevel / ModifierList blocks are applied (load type 2); every other block is consumed by a recording stub");
	for (const std::string &l : m_s->m_experienceLevels.stopLines()) // XP-1
	{
		out.push_back(l);
	}
	return out;
}
