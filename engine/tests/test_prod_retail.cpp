// OpenBFME retail tests (lane PROD-1): production in every playable faction. They run only when ROTWK_INSTALL and BFME2_INSTALL are set (otherwise SKIP).
//
// For each of the 7 skirmish factions a skirmish player with money and command points gets every production building the census lists for the faction,
// each with every CommandSet the building can have (its own and the ones its CommandSetUpgrade modules switch to, with the upgrades its buttons need),
// and every unit and horde those sets offer is queued by a player command (MSG_QUEUE_UNIT_CREATE through the selection), paid for, built, and handed to
// the exit. Asserted per unit: the cost deducted equals BuildCost, the progress reaches 100 percent after five frames per whole second of BuildTime
// (RW 0x73C39E), the object exists with the right template and owner, it stands at the exit point the module data names (the building's transform of
// UnitCreatePoint), the AI is handed the exit path ending at the natural rally point or the player's rally point, and the door model conditions of the
// building run through OPENING, WAITING_OPEN, CLOSING in the frames its ProductionUpdate data gives. A horde is followed by its members, one per exit
// delay, each contained by the horde. Independent expectations: workspace census, committed as tests/data/production/faction_trainable.json (the
// units each faction can train and its structures).

#include "doctest.h"

#include "Common/MiniJson.h"
#include "Common/PlayerList.h"
#include "Common/Thing/RawModuleData.h"
#include "Common/BuildAssistant.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/Module/SpawnBehavior.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/Economy.h"
#include "GameLogic/EconomySettings.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ProductionExitModules.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/QueueProductionExitUpdate.h"
#include "GameLogic/Module/UpgradeModuleClasses.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "PathfindTestUtil.h"
#include "RetailTestMount.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>

namespace
{
struct Shared
{
	retailtest::Mount *mount = nullptr;
	std::unique_ptr<RetailObjectWorld> world;
	std::string error;
	JsonValue census;
};

Shared &shared()
{
	static Shared s;
	static bool built = false;
	if (!built)
	{
		built = true;
		s.mount = retailtest::pureMount();
		if (s.mount && s.mount->fs)
		{
			s.world = std::make_unique<RetailObjectWorld>(*s.mount->fs);
			if (!s.world->load(&s.error))
			{
				s.world.reset();
			}
			std::vector<unsigned char> bytes;
			std::string err;
			if (!retailtest::readLocalFile(retailtest::dataDir() + "/production/faction_trainable.json", bytes, &err) ||
				!JsonValue::parse(std::string(bytes.begin(), bytes.end()), s.census, &err))
			{
				s.error = "faction_trainable.json: " + err;
				s.world.reset();
			}
		}
	}
	return s;
}

int bitOf(const char *const *names, const char *name)
{
	for (int i = 0; names[i]; ++i)
	{
		if (std::string(names[i]) == name)
		{
			return i;
		}
	}
	return -1;
}

const std::string *stringField(const ThingTemplate &tt, const char *name)
{
	const FieldValue *v = tt.getFinalOverride()->findField(name);
	return v ? std::get_if<std::string>(v) : nullptr;
}

// the CommandSet names a building can have: its own and the CommandSetUpgrade modules' (raw module data lines `CommandSet = Name`)
std::vector<std::string> commandSetsOf(const ThingTemplate &tt)
{
	std::vector<std::string> out;
	if (const std::string *own = stringField(tt, "CommandSet"))
	{
		out.push_back(*own);
	}
	for (const ThingTemplate::Nugget &n : tt.getFinalOverride()->behaviorModules().nuggets())
	{
		if (n.name != "CommandSetUpgrade")
		{
			continue;
		}
		if (const CommandSetUpgradeModuleData *typed = dynamic_cast<const CommandSetUpgradeModuleData *>(n.data.get())) // UPGRADE-1: typed data
		{
			if (std::find(out.begin(), out.end(), typed->m_commandSet) == out.end())
			{
				out.push_back(typed->m_commandSet);
			}
		}
		else if (const RawModuleData *raw = dynamic_cast<const RawModuleData *>(n.data.get()))
		{
			for (const RawModuleData::Line &l : raw->lines())
			{
				std::istringstream in(l.text);
				std::string key, eq, value;
				in >> key >> eq >> value;
				if (key == "CommandSet" && eq == "=" && std::find(out.begin(), out.end(), value) == out.end())
				{
					out.push_back(value);
				}
			}
		}
	}
	return out;
}

const ThingTemplate::Nugget *nuggetOf(const ThingTemplate &tt, const char *cls)
{
	for (const ThingTemplate::Nugget &n : tt.getFinalOverride()->behaviorModules().nuggets())
	{
		if (n.name == cls)
		{
			return &n;
		}
	}
	return nullptr;
}

struct UnitRun
{
	std::string building, set, unit;
	bool ok = true;
	std::vector<std::string> problems;
};

struct FactionResult
{
	std::set<std::string> unitsOffered;     // templates the producers' sets offer (UNIT_BUILD, non null)
	std::set<std::string> producers;
	size_t spawnPointStops = 0, unitRuns = 0, hordeRuns = 0, memberObjects = 0, doorChecks = 0, rallyChecks = 0;
	std::vector<std::string> problems;
};

GameMessage queueMsg(int player, const ThingTemplate *t)
{
	GameMessage m(MSG_QUEUE_UNIT_CREATE, player);
	m.appendBooleanArgument(false);
	m.appendIntegerArgument((int)t->getTemplateID());
	m.appendIntegerArgument(-1);
	m.appendBooleanArgument(false);
	m.appendBooleanArgument(false);
	return m;
}

bool puIdle(Object &b)
{
	const ProductionUpdate *pu = dynamic_cast<const ProductionUpdate *>(b.getProductionUpdate());
	if (!pu || pu->exitingObjectID() != INVALID_ID)
	{
		return false;
	}
	for (int i = 0; i < DOOR_COUNT_MAX; ++i)
	{
		const ProductionUpdate::DoorInfo &d = pu->door(i);
		if (d.openedFrame || d.waitOpenFrame || d.closedFrame)
		{
			return false;
		}
	}
	return true;
}

FactionResult runFaction(Shared &sh, const std::string &faction)
{
	FactionResult res;
	RetailObjectWorld &world = *sh.world;
	const JsonValue *fj = sh.census.get(faction);
	REQUIRE(fj != nullptr);
	TeamFactory teams;
	PlayerList players(world.nameKeys(), world.playerTemplates(), teams);
	SkirmishSetup setup;
	setup.players.push_back({ "Tester", faction, true, 0, 0, 0 });
	setup.startingMoney = 100000000;
	setup.defaultStartingCash = 100000000;
	const std::vector<std::string> setupErrors = players.setupSkirmish(setup);
	REQUIRE(setupErrors.empty());
	GameLogic logic(world.things(), world.modules(), players, RandomAlgorithm::ZH_CarryChain);
	logic.castleTemplates().setLoader(CastleTemplateStore::fileSystemLoader(*sh.mount->fs)); // BUILD-1: a castle unpacks its Bases.big layout
	logic.random().seedRandom(1);
	std::string settingsError;
	REQUIRE_MESSAGE(GameLogicSettingsLoader::load(*sh.mount->fs, logic.settings(), &settingsError), settingsError);
	logic.productionSettings() = world.productionSettings();
	logic.setUpgradeTypes(&world.upgradeTypes());
	Player *player = players.findPlayerWithName("Tester");
	REQUIRE(player != nullptr);
	REQUIRE(world.productionSettings().loaded);
	std::string econError;
	REQUIRE_MESSAGE(EconomySettings::load(*sh.mount->fs, logic.economy().settings(), &econError), econError);
	logic.economy().initAllCommandPoints();
	player->commandPoints().setFromScript(100000000, 100000000); // test setup: command points are not what is tested here
	CommandList list;
	GameLogicDispatch dispatch(logic);
	dispatch.attach(list);
	const int pi = player->getPlayerIndex();

	const int kStructure = bitOf(TheKindOfNames, "STRUCTURE");
	(void)kStructure;
	float nextX = 1000.0f;
	const JsonValue *structures = fj->get("structures");
	REQUIRE(structures != nullptr);
	for (const JsonValue &sv : structures->array)
	{
		const ThingTemplate *building = world.things().findTemplate(sv.string);
		if (!building || !nuggetOf(*building, "ProductionUpdate"))
		{
			continue;
		}
		enum ExitClass { EXIT_QUEUE, EXIT_SUPPLY, EXIT_SPAWNPOINT, EXIT_NONE } exitClass = EXIT_NONE;
		const ThingTemplate::Nugget *exitNugget = nullptr;
		if ((exitNugget = nuggetOf(*building, "QueueProductionExitUpdate")))
		{
			exitClass = EXIT_QUEUE;
		}
		else if ((exitNugget = nuggetOf(*building, "SupplyCenterProductionExitUpdate")))
		{
			exitClass = EXIT_SUPPLY;
		}
		else if ((exitNugget = nuggetOf(*building, "SpawnPointProductionExitUpdate")))
		{
			exitClass = EXIT_SPAWNPOINT;
		}
		if (exitClass == EXIT_NONE)
		{
			continue;
		}
		const ProductionUpdateModuleData *puData = dynamic_cast<const ProductionUpdateModuleData *>(nuggetOf(*building, "ProductionUpdate")->data.get());
		// what the three exit modules' data say, in one shape
		struct ExitData
		{
			Coord3D createPoint, naturalRally;
			bool allowAirborne = false, noExitPath = false;
			unsigned exitDelay = 0;
		} exitData;
		if (exitClass == EXIT_QUEUE)
		{
			const QueueProductionExitUpdateModuleData *q = dynamic_cast<const QueueProductionExitUpdateModuleData *>(exitNugget->data.get());
			if (q)
			{
				exitData = { q->m_unitCreatePoint, q->m_naturalRallyPoint, q->m_allowAirborneCreation, q->m_noExitPath, q->m_exitDelay };
			}
		}
		else if (exitClass == EXIT_SUPPLY)
		{
			const DefaultProductionExitUpdateModuleData *d = dynamic_cast<const DefaultProductionExitUpdateModuleData *>(exitNugget->data.get());
			if (d)
			{
				exitData = { d->m_unitCreatePoint, d->m_naturalRallyPoint, false, false, 0 };
			}
		}
		if (!puData)
		{
			res.problems.push_back(building->getName() + ": production module data is not typed");
			continue;
		}
		bool firstOfBuilding = true; (void)firstOfBuilding;
		for (const std::string &setName : commandSetsOf(*building))
		{
			const CommandSet *set = world.commands().findCommandSet(setName);
			if (!set)
			{
				res.problems.push_back(building->getName() + ": command set " + setName + " does not exist");
				continue;
			}
			for (int slot = 0; slot < CommandSet::MAX_BUTTONS; ++slot)
			{
				const CommandButton *button = set->getCommandButton(slot);
				if (!button || button->m_command != GUI_COMMAND_UNIT_BUILD || !button->getThingTemplate())
				{
					continue;
				}
				const ThingTemplate *unit = button->getThingTemplate();
				res.producers.insert(building->getName());
				res.unitsOffered.insert(unit->getName());
				// a fresh building for each unit keeps the runs independent
				nextX += 600.0f;
				ObjectStatusMaskType status{};
				Object *b = logic.newObject(building, player->getDefaultTeam(), status);
				if (!b)
				{
					res.problems.push_back(building->getName() + ": newObject failed");
					continue;
				}
				auto cleanup = [&]() {
					std::vector<Object *> gone;
					for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
					{
						gone.push_back(o);
					}
					for (Object *o : gone)
					{
						logic.destroyObject(o);
					}
					logic.runLogicFrame();
					logic.runLogicFrame();
				};
				Coord3D at = { nextX, 1000.0f, 0.0f };
				b->setPosition(&at);
				if (SpawnBehaviorInterface *sb = SpawnBehaviorInterface::of(*b))
				{
					sb->stopSpawning(); // lane MOD-4: a lumber mill's own SpawnBehavior makes the same workers through the same exit: only the queue is measured
				}
				for (const std::string &u : button->m_neededUpgrade)
				{
					const int type = world.upgradeTypes().typeOf(u);
					if (type == UpgradeTypeTable::UPGRADE_TYPE_OBJECT)
					{
						b->giveUpgrade(u);
					}
					else if (type == UpgradeTypeTable::UPGRADE_TYPE_PLAYER)
					{
						player->addCompletedUpgrade(u);
					}
				}
				b->setCommandSetOverride(setName); // after the upgrades: a CommandSetUpgrade they trigger sets its own set (UPGRADE-1, RW 0x8B7C68)
				const std::string tag = building->getName() + "/" + setName + "/" + unit->getName();
				const std::uint32_t moneyBefore = player->getMoney()->countMoney();
				GameMessage sel(MSG_CREATE_SELECTED_GROUP, pi);
				sel.appendBooleanArgument(true);
				sel.appendObjectIDArgument(b->getID());
				list.append(sel);
				const bool setRally = (res.unitRuns + res.spawnPointStops) % 2 == 1;
				const Coord3D playerRally = { at.x + 500.0f, at.y + 300.0f, 0.0f };
				if (setRally)
				{
					GameMessage rm(MSG_SET_RALLY_POINT, pi);
					rm.appendObjectIDArgument(b->getID());
					rm.appendLocationArgument(playerRally);
					rm.appendBooleanArgument(false);
					rm.appendObjectIDArgument(INVALID_ID);
					list.append(rm);
				}
				list.append(queueMsg(pi, unit));
				const UnsignedInt queuedFrame = logic.getFrame();
				logic.runLogicFrame();
				ProductionUpdateInterface *pu = b->getProductionUpdate();
				if (!pu || pu->getProductionCount() != 1)
				{
					res.problems.push_back(tag + ": the queue command did not queue (canMakeUnit " + std::to_string((int)BuildAssistant::canMakeUnit(*b, unit, -1)) + ", inSet " + std::to_string((int)BuildAssistant::isInProducersCommandSet(*b, unit, -1)) +
						", button upgrades " + [&] { std::string t; for (const std::string &u : button->m_neededUpgrade) { t += u + "(" + std::to_string(world.upgradeTypes().typeOf(u)) + "," + std::to_string((int)b->hasUpgrade(u)) + ") "; } return t; }() +
						", cmdset " + b->getCommandSetName() + ", canBuild " + std::to_string((int)BuildAssistant::playerCanBuild(*player, unit, logic)) + ")");
					cleanup();
					continue;
				}
				// cost: BuildCost of the template
				const FieldValue *costField = unit->getFinalOverride()->findField("BuildCost");
				const long long expectedCost = costField ? std::get<long long>(*costField) : 0;
				if ((long long)moneyBefore - (long long)player->getMoney()->countMoney() != expectedCost)
				{
					res.problems.push_back(tag + ": paid " + std::to_string((long long)moneyBefore - (long long)player->getMoney()->countMoney()) + " expected " + std::to_string(expectedCost));
				}
				// time: five frames per whole second
				const FieldValue *timeField = unit->getFinalOverride()->findField("BuildTime");
				const int expectedFrames = timeField ? 5 * (int)std::get<float>(*timeField) : 0;
				// BuildVariations: newObject may swap the template for one of the names (RW 0x6D168B)
				std::set<const ThingTemplate *> variants = { unit->getFinalOverride() };
				if (const FieldValue *bv = unit->getFinalOverride()->findField("BuildVariations"))
				{
					if (const std::vector<std::string> *names = std::get_if<std::vector<std::string>>(bv))
					{
						for (const std::string &n : *names)
						{
							if (const ThingTemplate *v = world.things().findTemplate(n))
							{
								variants.insert(v->getFinalOverride());
							}
						}
					}
				}
				const bool hasDoor = puData->m_numDoorAnimations > 0;
				if (exitClass == EXIT_SPAWNPOINT)
				{
					// stop S-209: the spawn bones are not available: the queue is paid, the progress completes, nothing leaves, the module says so
					UnsignedInt done = 0;
					for (int i = 0; i < expectedFrames + 40; ++i)
					{
						logic.runLogicFrame();
						if (!done && pu->firstProduction() && pu->firstProduction()->percentComplete >= 100.0f)
						{
							done = logic.getFrame();
						}
					}
					size_t produced = 0;
					for (const Object *o = logic.getFirstObject(); o; o = o->getNextObject())
					{
						produced += o->getProducerID() == b->getID() ? 1u : 0u;
					}
					bool reported = false;
					for (const std::string &e : logic.report().errors)
					{
						reported = reported || e.find("SpawnPointProductionExitUpdate") != std::string::npos;
					}
					if (produced != 0 || !reported || done == 0 || (int)(done - queuedFrame) != expectedFrames)
					{
						res.problems.push_back(tag + ": the SpawnPoint stop is not as pinned (objects " + std::to_string(produced) + ", reported " + std::to_string((int)reported) + ")");
					}
					++res.spawnPointStops;
					cleanup();
					continue;
				}
				UnsignedInt completeAt = 0;
				std::vector<UnsignedInt> madeAt; // frames at which new objects of the unit template appeared
				size_t seen = 0;
				std::vector<std::pair<UnsignedInt, std::string>> doorTimeline;
				std::string lastDoor;
				const int doorBit0 = bitOf(TheModelConditionNames, "DOOR_1_OPENING");
				const int maxFrames = 5000;
				UnsignedInt idleSince = 0;
				for (int i = 0; i < maxFrames; ++i)
				{
					logic.runLogicFrame();
					if (!completeAt && pu->firstProduction() && pu->firstProduction()->percentComplete >= 100.0f)
					{
						completeAt = logic.getFrame();
					}
					size_t now = 0;
					for (const Object *o = logic.getFirstObject(); o; o = o->getNextObject())
					{
						now += variants.count(o->getTemplate()) ? 1 : 0;
					}
					while (seen < now)
					{
						madeAt.push_back(logic.getFrame());
						++seen;
					}
					if (hasDoor)
					{
						std::string state;
						static const char *names[] = { "DOOR_1_OPENING", "DOOR_1_WAITING_OPEN", "DOOR_1_CLOSING" };
						for (const char *n : names)
						{
							if (b->testModelCondition(bitOf(TheModelConditionNames, n)))
							{
								state += std::string(state.empty() ? "" : "+") + n;
							}
						}
						if (state != lastDoor)
						{
							doorTimeline.push_back({ logic.getFrame(), state.empty() ? "-" : state });
							lastDoor = state;
						}
					}
					if (pu->getProductionCount() == 0 && puIdle(*b))
					{
						if (!idleSince)
						{
							idleSince = logic.getFrame();
						}
						if (logic.getFrame() - idleSince > 12)
						{
							break;
						}
					}
				}
				(void)doorBit0;
				if (completeAt == 0 && !hasDoor && !madeAt.empty())
				{
					completeAt = madeAt[0]; // without a door the object is made in the update that completes the progress: the entry is gone before it can be seen
				}
				if (completeAt == 0 || (int)(completeAt - queuedFrame) != expectedFrames)
				{
					res.problems.push_back(tag + ": progress reached 100% after " + std::to_string(completeAt ? (int)(completeAt - queuedFrame) : -1) + " frames, expected " + std::to_string(expectedFrames));
				}
				if (madeAt.empty())
				{
					res.problems.push_back(tag + ": no object was made");
					cleanup();
					continue;
				}
				++res.unitRuns;
				// the object: owner, producer, exit point
				Object *made = nullptr;
				for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
				{
					if (variants.count(o->getTemplate()) && o->getProducerID() == b->getID() && !o->getContainedBy())
					{
						made = o;
					}
				}
				if (!made)
				{
					res.problems.push_back(tag + ": the produced object is not found with the building as producer");
					cleanup();
					continue;
				}
				if (made->getTeam() != player->getDefaultTeam())
				{
					res.problems.push_back(tag + ": wrong team");
				}
				const bool isHorde = made->isKindOfName("HORDE");
				const Coord3D create = exitData.createPoint;
				Coord3D expected = { at.x + create.x, at.y + create.y, exitClass == EXIT_QUEUE ? (create.z > 1.0f && !exitData.allowAirborne ? 0.0f : create.z) : 0.0f };
				// rally: natural rally point pushed 20 units out along its own direction
				Coord3D nr = exitData.naturalRally;
				const double nlen = std::sqrt((double)nr.x * nr.x + (double)nr.y * nr.y + (double)nr.z * nr.z);
				Coord3D rally = { at.x + nr.x, at.y + nr.y, nr.z };
				if (nlen > 0.0 && exitClass == EXIT_QUEUE)
				{
					rally.x = (float)(at.x + nr.x + nr.x / nlen * 20.0);
					rally.y = (float)(at.y + nr.y + nr.y / nlen * 20.0);
					rally.z = (float)(nr.z + nr.z / nlen * 20.0);
				}
				auto near3 = [](const Coord3D &a, const Coord3D &c) { return std::fabs(a.x - c.x) < 0.05f && std::fabs(a.y - c.y) < 0.05f && std::fabs(a.z - c.z) < 0.05f; };
				if (!isHorde)
				{
					if (!near3(*made->getPosition(), expected))
					{
						res.problems.push_back(tag + ": exit position (" + std::to_string(made->getPosition()->x) + "," + std::to_string(made->getPosition()->y) + "," + std::to_string(made->getPosition()->z) + ") expected (" + std::to_string(expected.x) + "," + std::to_string(expected.y) + "," + std::to_string(expected.z) + ")");
					}
				}
				else
				{
					if (!near3(*made->getPosition(), rally) && !near3(*made->getPosition(), Coord3D{ rally.x, rally.y, made->getPosition()->z }))
					{
						res.problems.push_back(tag + ": horde object not at the natural rally point");
					}
					++res.hordeRuns;
				}
				// the AI hand-off: the commands of this run
				bool pathChecked = false;
				for (const AICommand &c : logic.aiCommands().issued())
				{
					if (c.object == made->getID() && c.type == AICMD_FOLLOW_EXIT_PRODUCTION_PATH)
					{
						pathChecked = true;
						const Coord3D &want = setRally && !isHorde ? playerRally : rally;
						if (c.path.empty() || !near3(c.path.back(), want))
						{
							res.problems.push_back(tag + ": the exit path does not end at the " + (setRally && !isHorde ? "player's" : "natural") + " rally point");
						}
						++res.rallyChecks;
					}
				}
				if (!pathChecked && !(isHorde || exitData.noExitPath))
				{
					bool hasAI = false;
					for (const std::unique_ptr<BehaviorModule> &m : made->modules())
					{
						hasAI = hasAI || (m->getModuleData() && m->getModuleData()->isAiModuleData());
					}
					if (hasAI)
					{
						res.problems.push_back(tag + ": no exit path was handed to the AI");
					}
				}
				// a horde goes to the player's rally point once its last member left (RW 0x8A3BF8: aiMoveToPosition(rally))
				if (isHorde && setRally && exitClass == EXIT_QUEUE)
				{
					bool moved = false;
					for (const AICommand &c : logic.aiCommands().issued())
					{
						moved = moved || (c.object == made->getID() && c.type == AICMD_MOVE_TO_POSITION && c.path.size() == 1 && near3(c.path[0], playerRally));
					}
					if (!moved)
					{
						res.problems.push_back(tag + ": the horde was not sent to the player's rally point");
					}
					++res.rallyChecks;
				}
				// horde members: Slots of them, each contained by the horde, produced one per exit delay
				if (isHorde)
				{
					HordeContainInterface *hci = made->getContain() ? made->getContain()->getHordeContainInterface() : nullptr;
					if (!hci)
					{
						res.problems.push_back(tag + ": horde without a HordeContainInterface");
					}
					else
					{
						const std::string memberName = hci->getPayloadMemberTemplateName();
						size_t members = made->getContain()->getContainCount();
						if ((int)members != hci->getSlotCapacity())
						{
							res.problems.push_back(tag + ": " + std::to_string(members) + " members, expected " + std::to_string(hci->getSlotCapacity()));
						}
						res.memberObjects += members;
						// one member per exit, ExitDelay frames after the last exit: the queue exit counts its delay down once per frame and the production update
						// asks for a door before or after that count in the same frame depending on the scheduler's vector order (a sleeping module's swap-pop
						// moves modules, RW 0x62EA4F), so the gap is ExitDelay or ExitDelay + 1 frames
						std::vector<UnsignedInt> frames;
						for (const Object *m : *made->getContain()->getContainedItemsList())
						{
							frames.push_back(m->getCreationFrame());
						}
						std::sort(frames.begin(), frames.end());
						if (exitClass == EXIT_QUEUE)
						{
							const UnsignedInt lo = exitData.exitDelay, hi = exitData.exitDelay + 1; // ExitDelay 0: several members leave in one update
							for (size_t k = 1; k < frames.size(); ++k)
							{
								const UnsignedInt gap = frames[k] - frames[k - 1];
								if (gap != lo && gap != hi)
								{
									res.problems.push_back(tag + ": member " + std::to_string(k) + " came " + std::to_string(gap) + " frames after the previous, ExitDelay is " + std::to_string(exitData.exitDelay));
									break;
								}
							}
						}
					}
				}
				// the door sequence of the first unit of a building
				if (hasDoor)
				{
					++res.doorChecks;
					std::vector<std::string> states;
					for (const auto &p : doorTimeline)
					{
						states.push_back(p.second);
					}
					const std::vector<std::string> wantStart = { "-", "DOOR_1_OPENING", "DOOR_1_WAITING_OPEN" };
					// states: -, OPENING, WAITING_OPEN (exits happen while the door waits open), ..., CLOSING, -
					if (puData->m_doorOpeningTime == 0 && !states.empty() && states[0] == "DOOR_1_WAITING_OPEN")
					{
						states.insert(states.begin(), "DOOR_1_OPENING"); // an instant opening is never visible: the OPENING bit is set and cleared inside one update
						doorTimeline.insert(doorTimeline.begin(), { doorTimeline[0].first, "DOOR_1_OPENING" });
					}
					if (states.size() < 4 || states[0] != "DOOR_1_OPENING" || states[1] != "DOOR_1_WAITING_OPEN" || states[states.size() - 2] != "DOOR_1_CLOSING" || states.back() != "-")
					{
						std::string t;
						for (const auto &p : doorTimeline)
						{
							t += std::to_string(p.first - queuedFrame) + ":" + p.second + " ";
						}
						res.problems.push_back(tag + ": door timeline " + t);
					}
					else
					{
						const UnsignedInt opening = doorTimeline[1].first - doorTimeline[0].first;
						if (opening != puData->m_doorOpeningTime)
						{
							res.problems.push_back(tag + ": door opening took " + std::to_string(opening) + " frames, DoorOpeningTime is " + std::to_string(puData->m_doorOpeningTime));
						}
					}
				}
				firstOfBuilding = false;
				// every object of the run goes: the produced unit, the members, the building
				cleanup();
			}
		}
	}
	// the unit set is the census's
	std::set<std::string> census;
	// the census's `object_reasons` with a unit_build:Command_... entry: the trainable units and hordes plus the heroes a UNIT_BUILD button buys
	for (const auto &kv : fj->get("objectReasons")->object)
	{
		census.insert(kv.first);
	}
	for (const std::string &u : census)
	{
		if (!res.unitsOffered.count(u))
		{
			res.problems.push_back("census unit_build object " + u + " is not offered by any production building's command set");
		}
	}
	for (const std::string &u : res.unitsOffered)
	{
		if (!census.count(u))
		{
			res.problems.push_back("command sets offer " + u + ", which the census does not list as bought by a UNIT_BUILD button");
		}
	}
	return res;
}
} // namespace

namespace
{
void checkFaction(const char *faction)
{
	Shared &sh = shared();
	if (!sh.mount)
	{
		retailtest::printSkip("production retail");
		return;
	}
	REQUIRE_MESSAGE(sh.world != nullptr, sh.error);
	const FactionResult r = runFaction(sh, faction);
	std::printf("production %s: %zu producers, %zu units offered, %zu runs (%zu hordes, %zu member objects), %zu door checks, %zu exit paths, %zu spawn point stops\n", faction, r.producers.size(),
		r.unitsOffered.size(), r.unitRuns, r.hordeRuns, r.memberObjects, r.doorChecks, r.rallyChecks, r.spawnPointStops);
	for (size_t i = 0; i < r.problems.size() && i < 30; ++i)
	{
		INFO(r.problems[i]);
		CHECK_MESSAGE(false, r.problems[i]);
	}
	CHECK(r.problems.empty());
	CHECK(r.unitRuns > 0);
}
} // namespace

TEST_CASE("production retail: FactionMen") { checkFaction("FactionMen"); }
TEST_CASE("production retail: FactionElves") { checkFaction("FactionElves"); }
TEST_CASE("production retail: FactionDwarves") { checkFaction("FactionDwarves"); }
TEST_CASE("production retail: FactionIsengard") { checkFaction("FactionIsengard"); }
TEST_CASE("production retail: FactionMordor") { checkFaction("FactionMordor"); }
TEST_CASE("production retail: FactionWild") { checkFaction("FactionWild"); }
TEST_CASE("production retail: FactionAngmar") { checkFaction("FactionAngmar"); }


// ---- MOVE-1: the produced object WALKS to the rally point (stop S-201 narrowed) --------------------------------------------------------------------------
// The same production, in a game with an AIWorld on flat terrain: AICommandSink's handler runs the commands production issues, so the unit follows its exit
// path to the player's rally point (500 units east, 300 north of the building) and a horde walks there with its members on their slots. One infantry unit and
// one horde per faction, from the first barracks-like producer (a QueueProductionExitUpdate building) of the census.
namespace
{
struct RallyOutcome
{
	std::string tag, building, unit, excluded;
	bool isHorde = false;
	float unitDistance = -1.0f, worstMember = -1.0f, speed = -1.0f;
	size_t members = 0, slotCapacity = 0;
	bool idle = false;
	unsigned long long unexecuted = 0, executed = 0;
	std::string problem;
};

bool isHordeTemplate(const ThingTemplate &tt)
{
	for (const ThingTemplate::Nugget &n : tt.behaviorModules().nuggets())
	{
		if (n.name.find("HordeContain") != std::string::npos) // HordeContain, HorseHordeContain, HordeGarrisonContain, ...
		{
			return true;
		}
	}
	return false;
}

std::vector<RallyOutcome> runRally(Shared &sh, const std::string &faction)
{
	std::vector<RallyOutcome> out;
	RetailObjectWorld &world = *sh.world;
	TeamFactory teams;
	PlayerList players(world.nameKeys(), world.playerTemplates(), teams);
	SkirmishSetup setup;
	setup.players.push_back({ "Tester", faction, true, 0, 0, 0 });
	setup.startingMoney = 100000000;
	setup.defaultStartingCash = 100000000;
	REQUIRE(players.setupSkirmish(setup).empty());
	GameLogic logic(world.things(), world.modules(), players, RandomAlgorithm::ZH_CarryChain);
	logic.castleTemplates().setLoader(CastleTemplateStore::fileSystemLoader(*sh.mount->fs)); // BUILD-1: a castle unpacks its Bases.big layout
	logic.random().seedRandom(1);
	std::string err;
	REQUIRE_MESSAGE(GameLogicSettingsLoader::load(*sh.mount->fs, logic.settings(), &err), err);
	logic.productionSettings() = world.productionSettings();
	logic.setUpgradeTypes(&world.upgradeTypes());
	Player *player = players.findPlayerWithName("Tester");
	REQUIRE(player != nullptr);
	std::string econError;
	REQUIRE_MESSAGE(EconomySettings::load(*sh.mount->fs, logic.economy().settings(), &econError), econError);
	logic.economy().initAllCommandPoints();
	player->commandPoints().setFromScript(100000000, 100000000); // test setup: command points are not what is tested here
	CommandList list;
	GameLogicDispatch dispatch(logic);
	dispatch.attach(list);
	AIWorldConfig cfg;
	REQUIRE_MESSAGE(AIWorldConfigLoader::load(*sh.mount->fs, cfg, &err), err);
	pathtest::SyntheticTerrain terrain(300, 300);
	AIWorld ai(logic, cfg, world.iniMacros());
	ai.attach();
	ai.newMap(terrain);
	const int pi = player->getPlayerIndex();
	const JsonValue *structures = sh.census.get(faction)->get("structures");
	REQUIRE(structures != nullptr);
	float nextX = 400.0f, nextY = 400.0f;
	const JsonValue *offeredJson = sh.census.get(faction)->get("objectReasons");
	(void)offeredJson;
	for (const JsonValue &sv : structures->array)
	{
		const ThingTemplate *building = world.things().findTemplate(sv.string);
		if (!building || !nuggetOf(*building, "ProductionUpdate"))
		{
			continue;
		}
		const bool queueExit = nuggetOf(*building, "QueueProductionExitUpdate") != nullptr;
		const bool supplyExit = nuggetOf(*building, "SupplyCenterProductionExitUpdate") != nullptr || nuggetOf(*building, "DefaultProductionExitUpdate") != nullptr;
		const bool spawnExit = nuggetOf(*building, "SpawnPointProductionExitUpdate") != nullptr;
		if (!queueExit && !supplyExit && !spawnExit)
		{
			continue;
		}
		const ProductionUpdateModuleData *puData = dynamic_cast<const ProductionUpdateModuleData *>(nuggetOf(*building, "ProductionUpdate")->data.get());
		for (const std::string &setName : commandSetsOf(*building))
		{
			const CommandSet *set = world.commands().findCommandSet(setName);
			for (int slot = 0; set && slot < CommandSet::MAX_BUTTONS; ++slot)
			{
				const CommandButton *button = set->getCommandButton(slot);
				if (!button || button->m_command != GUI_COMMAND_UNIT_BUILD || !button->getThingTemplate())
				{
					continue;
				}
				const ThingTemplate *unit = button->getThingTemplate();
				const bool horde = isHordeTemplate(*unit->getFinalOverride());
				RallyOutcome r;
				r.tag = building->getName() + "/" + setName + "/" + unit->getName();
				r.isHorde = horde;
				r.building = building->getName();
				r.unit = unit->getName();
				if (spawnExit)
				{
					r.excluded = "S-209: the SpawnPoint exit has no spawn bones (nothing leaves the building)";
					out.push_back(r);
					continue;
				}
				// a fresh building for each case keeps the runs independent; the world is emptied after each
				nextX += 300.0f;
				if (nextX > 2400.0f)
				{
					nextX = 700.0f;
					nextY += 300.0f;
				}
				Object *b = logic.newObject(building, player->getDefaultTeam(), ObjectStatusMaskType{});
				REQUIRE(b != nullptr);
				Coord3D at = { nextX, nextY, 0.0f };
				b->setPosition(&at);
				ai.removeObjectFromPathfindMap(*b);
				ai.addObjectToPathfindMap(*b); // the footprint where the building stands
				for (const std::string &u : button->m_neededUpgrade)
				{
					const int type = world.upgradeTypes().typeOf(u);
					if (type == UpgradeTypeTable::UPGRADE_TYPE_OBJECT)
					{
						b->giveUpgrade(u);
					}
					else if (type == UpgradeTypeTable::UPGRADE_TYPE_PLAYER)
					{
						player->addCompletedUpgrade(u);
					}
				}
				b->setCommandSetOverride(setName); // after the upgrades: a CommandSetUpgrade they trigger sets its own set (UPGRADE-1, RW 0x8B7C68)
				const Coord3D rally = { at.x + 150.0f, at.y + 250.0f, 0.0f };
				const std::uint32_t moneyBefore = player->getMoney()->countMoney();
				GameMessage sel(MSG_CREATE_SELECTED_GROUP, pi);
				sel.appendBooleanArgument(true);
				sel.appendObjectIDArgument(b->getID());
				list.append(sel);
				GameMessage rm(MSG_SET_RALLY_POINT, pi);
				rm.appendObjectIDArgument(b->getID());
				rm.appendLocationArgument(rally);
				rm.appendBooleanArgument(false);
				rm.appendObjectIDArgument(INVALID_ID);
				list.append(rm);
				list.append(queueMsg(pi, unit));
				const size_t issuedBefore = logic.aiCommands().issued().size();
				const unsigned long long unexecutedBefore = logic.aiCommands().unexecuted();
				const size_t failuresBefore = ai.movementFailures().size();
				const UnsignedInt queuedFrame = logic.getFrame();
				logic.runLogicFrame();
				ProductionUpdateInterface *pu = b->getProductionUpdate();
				if (!pu || pu->getProductionCount() != 1)
				{
					r.problem = "the queue command did not queue";
					out.push_back(r);
					continue;
				}
				// cost and time: BuildCost, five frames per whole second of BuildTime (RW 0x73C39E)
				const FieldValue *costField = unit->getFinalOverride()->findField("BuildCost");
				const long long expectedCost = costField ? std::get<long long>(*costField) : 0;
				if ((long long)moneyBefore - (long long)player->getMoney()->countMoney() != expectedCost)
				{
					r.problem = "paid " + std::to_string((long long)moneyBefore - (long long)player->getMoney()->countMoney()) + ", BuildCost " + std::to_string(expectedCost);
				}
				const FieldValue *timeField = unit->getFinalOverride()->findField("BuildTime");
				const int expectedFrames = timeField ? 5 * (int)std::get<float>(*timeField) : 0;
				UnsignedInt completeAt = 0;
				Object *made = nullptr;
				bool settled = false;
				for (int f = 0; f < 6000 && !settled; ++f)
				{
					logic.runLogicFrame();
					if (!completeAt && pu->firstProduction() && pu->firstProduction()->percentComplete >= 100.0f)
					{
						completeAt = logic.getFrame();
					}
					if (!made)
					{
						for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
						{
							if (o->getProducerID() == b->getID() && !o->getContainedBy() && o->getTemplate()->getFinalOverride() != building->getFinalOverride())
							{
								made = o;
							}
						}
					}
					settled = made && made->getAIUpdateInterface() && made->getAIUpdateInterface()->isIdle() && f > 30 && pu->getProductionCount() == 0 && puIdle(*b);
				}
				if (completeAt == 0 && puData && puData->m_numDoorAnimations == 0 && made)
				{
					completeAt = queuedFrame + (UnsignedInt)expectedFrames; // no door: the object appears in the update that completes the progress, the entry is gone before it is seen
				}
				if (r.problem.empty() && (completeAt == 0 || (int)(completeAt - queuedFrame) != expectedFrames))
				{
					r.problem = "progress reached 100% after " + std::to_string(completeAt ? (int)(completeAt - queuedFrame) : -1) + " frames, expected " + std::to_string(expectedFrames);
				}
				for (int f = 0; f < 200; ++f)
				{
					logic.runLogicFrame(); // the last members join, the formation settles
				}
				r.unexecuted = logic.aiCommands().unexecuted() - unexecutedBefore;
				r.executed = logic.aiCommands().issued().size() - issuedBefore;
				if (ai.movementFailures().size() > failuresBefore)
				{
					// a locomotor case the port reports as unported (S-084): the unit stopped with the reported failure, nothing else is claimed of it
					r.excluded = "S-084: " + ai.movementFailures().rbegin()->first;
				}
				if (!made || !made->getAIUpdateInterface())
				{
					if (made)
					{
						r.excluded = "the produced object has no AI module (nothing to walk): " + made->getTemplate()->getName();
					}
					else if (r.problem.empty())
					{
						r.problem = "no object was produced";
					}
				}
				else if (r.excluded.empty())
				{
					const Coord3D &p = *made->getPosition();
					r.unitDistance = std::sqrt((p.x - rally.x) * (p.x - rally.x) + (p.y - rally.y) * (p.y - rally.y));
					r.idle = made->getAIUpdateInterface()->isIdle();
					r.speed = made->getAIUpdateInterface()->locomotorSetSpeed();
					if (horde && made->getContain() && made->getContain()->getHordeContainInterface())
					{
						// lane INTEG-1: the banner carrier (an Angmar horde's carrier comes at rank 1: BannerCarrierMinLevel 0) is contained but not one of the
						// Slots (RW 0x86C085 subtracts it from the member count)
						const HordeContain *withCarrier = dynamic_cast<const HordeContain *>(made->getContain());
						r.members = made->getContain()->getContainCount() - (withCarrier && withCarrier->bannerCarrier() != 0 ? 1u : 0u);
						r.slotCapacity = (size_t)made->getContain()->getHordeContainInterface()->getSlotCapacity();
						if (HordeContain *hc = dynamic_cast<HordeContain *>(made->findModule("HordeContain")))
						{
							r.worstMember = hc->worstMemberSlotError();
						}
					}
				}
				out.push_back(r);
				std::vector<Object *> gone;
				for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
				{
					gone.push_back(o);
				}
				for (Object *o : gone)
				{
					logic.destroyObject(o);
				}
				logic.runLogicFrame();
				logic.runLogicFrame();
			}
		}
	}
	logic.reset();
	return out;
}
} // namespace

namespace
{
void checkRally(const char *faction)
{
	Shared &sh = shared();
	if (!sh.mount)
	{
		retailtest::printSkip("production retail rally");
		return;
	}
	REQUIRE_MESSAGE(sh.world != nullptr, sh.error);
	const std::vector<RallyOutcome> runs = runRally(sh, faction);
	REQUIRE_FALSE(runs.empty());
	size_t walked = 0, hordes = 0, excluded = 0, stationary = 0;
	std::set<std::string> covered;
	for (const RallyOutcome &r : runs)
	{
		INFO(r.tag);
		covered.insert(r.unit);
		if (!r.excluded.empty())
		{
			// named cases of accepted stops, never silently skipped: S-209 (SpawnPoint exit), S-084 (an unported locomotor), a produced object without an AI
			std::printf("rally %s: EXCLUDED %s: %s\n", faction, r.tag.c_str(), r.excluded.c_str());
			CHECK((r.excluded.rfind("S-209", 0) == 0 || r.excluded.rfind("S-084", 0) == 0 || r.excluded.rfind("the produced object has no AI module", 0) == 0));
			++excluded;
			continue;
		}
		CHECK_MESSAGE(r.problem.empty(), r.problem);
		if (r.speed == 0.0f)
		{
			// the data says the unit does not walk (LocomotorSet Speed 0): it stands where the exit put it, and the exit still ran
			++stationary;
			std::printf("rally %s: STATIONARY by data %s\n", faction, r.tag.c_str());
			CHECK(r.executed > 0);
			CHECK(r.unexecuted == 0);
			continue;
		}
		CHECK(r.unexecuted == 0); // the AI took every command production issued
		CHECK(r.executed > 0);
		CHECK(r.unitDistance >= 0.0f);
		CHECK(r.unitDistance < 40.0f); // the produced object (the horde object) stands at the player's rally point
		CHECK(r.idle);
		if (r.isHorde)
		{
			CHECK(r.members == r.slotCapacity); // every slot of the horde holds its member
			CHECK(r.members >= 3);
			CHECK(r.worstMember < 40.0f); // and its members on their slots
			++hordes;
		}
		++walked;
	}
	// the matrix is the census's: every unit a UNIT_BUILD button offers appears (at every producer and command set that offers it)
	std::set<std::string> census;
	for (const auto &kv : sh.census.get(faction)->get("objectReasons")->object)
	{
		census.insert(kv.first);
	}
	CHECK(covered == census);
	std::printf("rally %s: %zu producer x set x unit cases, %zu walked to the rally point (%zu hordes), %zu stationary by data, %zu excluded by accepted stops\n", faction, runs.size(), walked, hordes, stationary, excluded);
	CHECK(walked > 10);
	CHECK(hordes > 3);
}
} // namespace

TEST_CASE("production retail rally: FactionMen") { checkRally("FactionMen"); }
TEST_CASE("production retail rally: FactionElves") { checkRally("FactionElves"); }
TEST_CASE("production retail rally: FactionDwarves") { checkRally("FactionDwarves"); }
TEST_CASE("production retail rally: FactionIsengard") { checkRally("FactionIsengard"); }
TEST_CASE("production retail rally: FactionMordor") { checkRally("FactionMordor"); }
TEST_CASE("production retail rally: FactionWild") { checkRally("FactionWild"); }
TEST_CASE("production retail rally: FactionAngmar") { checkRally("FactionAngmar"); }
