// OpenBFME. GPL-3.0.
// See GameLogic/Object/RetailObjectWorld.h.

#include "GameLogic/Object/RetailObjectWorld.h"

#include "Common/Audio/AudioIni.h"
#include "Common/GlobalOwnerChain.h"
#include "GameLogic/SpellStops.h"
#include "GameLogic/HeroSystem.h"
#include "GameLogic/Module/AISpecialPowerUpdate.h"
#include "GameLogic/Module/Mod4Stops.h"
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

RetailObjectWorld::RetailObjectWorld(ArchiveFileSystem &fs)
	: m_fs(fs)
{
	m_keys.init();
	m_grammar = std::make_unique<RwGrammar>(RwBinaryData::embedded());
	m_modules = std::make_unique<ModuleFactory>(m_keys, *m_grammar);
	m_modules->init();
	W3DDrawModules::registerTypedDrawModuleData(*m_modules);
	// the horde contain data of the map's horde objects (their members are the drawables of a horde)
	MapHordeSpawn::bindHordeContainData(*m_modules);
	LogicModules::registerAll(*m_modules); // LOGIC-1: ActiveBody and HordeContain run for real, every other class is an UnportedModule (S-140)
	m_things = std::make_unique<ThingFactory>(m_keys, *m_modules, *m_grammar);
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
		"CreateAHeroSystem" }; // HERO-2: TheCreateAHeroSystem (its own file, RW 0x61A10F)
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
}

const AudioIniState &RetailObjectWorld::audio() const
{
	return *m_audio;
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
	locomotorChain().install(this, &m_locomotors); // the Locomotor block has a real parser (HORDE-1) that needs the store
	commandChain().install(this, &m_commands);     // PROD-1: CommandButton / CommandSet
	upgradeChain().install(this, &m_upgrades);     // UPGRADE-1: the Upgrade block and every upgrade mask field of the object modules
	experienceChain().install(this, &m_experienceLevels);      // XP-1
	attributeModifierChain().install(this, &m_attributeModifiers); // XP-1
	stanceChain().install(this, &m_stances);                       // INTEG-1
	emotionChain().install(this, &m_emotions);                     // MODULES-2
	createAHeroChain().install(this, &m_createAHero);              // HERO-2
	fxListChain().install(this, m_recorder.fxLists.get());         // XP-1: the FXList blocks fill this world's store; LevelUpFx / ModifierList FX validate against it
	// AUDIO-1: object parsing (OpenContain / HordeContain sound fields) asks whether an audio event exists. No AudioManager exists at INI time,
	// so the predicate is AudioApi::isValidEvent's own (a non-empty name present in the parsed event table), applied to THIS world's table.
	audioChain().install(this, [this](const std::string &name) { return !name.empty() && m_audio->infos.contains(name); });
	m_weaponStores.install(); // WEAPON-1
	m_spellStores->install(); // SPELL-1
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
	createAHeroChain().remove(this);
	fxListChain().remove(this);
	audioChain().remove(this);
	weaponTemplateChain().remove(this);
	m_weaponStores.uninstall();
	m_spellStores->uninstall();
}

bool RetailObjectWorld::isCurrentContext() const
{
	return audioChain().current() == this && commandChain().current() == this && upgradeChain().current() == this && locomotorChain().current() == this && WeaponStores::currentOwner() == &m_weaponStores
		&& SpellStores::currentOwner() == m_spellStores.get();
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

RetailObjectWorld::~RetailObjectWorld()
{
	unregisterContext();
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
	m_commands.setThingFactory(m_things.get());
	registerContext(); // selected for the whole load; kept when it succeeds, rolled back when it fails (an error return or an exception)
	const std::shared_ptr<void> rollback(nullptr, [this](void *) {
		if (!m_loaded)
		{
			unregisterContext();
		}
	});
	INI ini(m_env);
	SubsystemLoadOptions options;
	options.collectErrors = true;
	options.cinematics = true; // the census counts the Cinematic folder (338 templates), like the object-model retail test
	const auto t0 = std::chrono::steady_clock::now();
	try
	{
		RunSubsystemIniLoad(m_legend, ini, options, m_report);
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
		m_skirmishAI->loadBaseLayouts(m_fs, &layoutErrors);
		for (const std::string &e : layoutErrors)
		{
			SubsystemLoadReport::FileError fe;
			fe.file = "TheBaseTemplateLibrary";
			fe.code = 0;
			fe.message = e;
			m_report.errors.push_back(fe);
		}
	}
	// INTEG-1: TheStanceTemplateStore's init (RW 0x83568D) loads Data\INI\Stances.ini itself (INI::load, type 1), not through the legend; an error reaches the report
	try
	{
		INI stancesIni(m_env);
		stancesIni.load("Data\\INI\\Stances.ini", INI_LOAD_OVERWRITE); // RW 0xC536E4
	}
	catch (const INIException &e)
	{
		SubsystemLoadReport::FileError fe;
		fe.file = "Data\\INI\\Stances.ini";
		fe.code = e.code();
		fe.message = e.what();
		m_report.errors.push_back(fe);
	}
	// PROD-1: the ControlBar loads its own three files after the objects (RW 0x71CE3A); its errors reach the report like the legend's
	std::vector<std::string> commandErrors;
	m_commands.loadFromFiles(m_env, &commandErrors);
	for (const std::string &e : commandErrors)
	{
		SubsystemLoadReport::FileError fe;
		fe.file = "ControlBar";
		fe.code = 0;
		fe.message = e;
		m_report.errors.push_back(fe);
	}
	// HERO-2: TheCreateAHeroSystem's init (RW 0x61A10F) loads Data\INI\CreateAHeroSystem.ini itself (type 1) once the upgrades and the control bar are loaded
	try
	{
		m_createAHero.load(m_env);
		if (!m_createAHero.loaded())
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
		m_report.errors.push_back(fe);
	}
	std::string perr;
	if (!ProductionSettings::load(m_fs, m_productionSettings, &perr))
	{
		SubsystemLoadReport::FileError fe;
		fe.file = "ProductionSettings";
		fe.code = 0;
		fe.message = perr;
		m_report.errors.push_back(fe);
	}
	// UPGRADE-1: the sub upgrade names resolve once every Upgrade block is known; production's type table is the center's
	perr.clear();
	if (!m_upgrades.resolveSubUpgrades(&perr))
	{
		SubsystemLoadReport::FileError fe;
		fe.file = "UpgradeCenter";
		fe.code = 0;
		fe.message = perr;
		m_report.errors.push_back(fe);
	}
	m_upgrades.resolveTemplates(*m_things);
	m_upgradeTypes.assign(m_upgrades);
	m_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	m_loaded = true;
	return true;
}

RetailObjectWorld::MapIniResult RetailObjectWorld::applyMapIni(const std::string &mapDir)
{
	MapIniResult result;
	m_things->reset(); // ZH ThingFactory::reset: the previous map's overrides go
	const size_t defsBefore = m_things->definitions().size();
	const size_t recordsBefore = m_recorder.records.size();
	ContextScope context(*this); // this world's stores AND audio validator for the whole parse; the previous live context returns on exit
	m_weaponStores.resetOverrides(); // the previous map's Weapon overrides go (ZH WeaponStore::reset)
	m_experienceLevels.resetOverrides(); // XP-1: the previous map's ExperienceLevel overrides go
	m_spellStores->resetOverrides(); // SPELL-1: and the Science / Rank / SpecialPower overrides
	for (const char *file : { "map.ini", "solo.ini" }) // RW 0x627224, 0x627290
	{
		const std::string path = mapDir + "\\" + file;
		if (!m_fs.doesFileExist(path))
		{
			continue;
		}
		(file[0] == 'm' ? result.mapIniFound : result.soloIniFound) = true;
		INI ini(m_env);
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
	const auto &defs = m_things->definitions();
	for (size_t i = defsBefore; i < defs.size(); ++i)
	{
		result.definitions.push_back(defs[i].kind + " " + defs[i].name);
	}
	result.blocksRecorded = m_recorder.records.size() - recordsBefore;
	return result;
}

std::vector<std::string> RetailObjectWorld::acceptanceStops() const
{
	std::vector<std::string> out = m_things->acceptanceStops();
	for (const std::string &u : m_playerTemplates->unverified())
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
	for (const std::string &l : AISpecialPowerUpdate::stopLines()) // MOD-4
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
	out.push_back("[S-116] map.ini / solo.ini: only the Object / ChildObject / ObjectReskin / CommandButton / CommandSet / ExperienceLevel / ModifierList blocks are applied (load type 2); every other block is consumed by a recording stub");
	for (const std::string &l : m_experienceLevels.stopLines()) // XP-1
	{
		out.push_back(l);
	}
	return out;
}
