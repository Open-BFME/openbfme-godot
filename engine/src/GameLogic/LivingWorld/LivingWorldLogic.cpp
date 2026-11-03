// OpenBFME. GPL-3.0.
// See GameLogic/LivingWorld/LivingWorldLogic.h.  Lane WOTR-1.

#include "GameLogic/LivingWorld/LivingWorldLogic.h"

#include "Common/AsciiString.h"
#include "Common/NumericState.h"
#include "Common/StateHash.h"
#include "GameLogic/LivingWorld/LivingWorldAI.h"
#include "GameLogic/LivingWorld/LivingWorldAutoResolveBattle.h"
#include "GameLogic/SimMath.h"

#include <algorithm>
#include <map>
#include <set>

namespace LivingWorld
{

namespace
{
bool sameName(const std::string &a, const std::string &b)
{
	return AsciiStringUtil::compareNoCase(a, b) == 0;
}

bool contains(const std::vector<std::string> &list, const std::string &name)
{
	for (const std::string &s : list)
	{
		if (sameName(s, name))
		{
			return true;
		}
	}
	return false;
}

void setError(std::string *error, const std::string &text)
{
	if (error)
	{
		*error = text;
	}
}
} // namespace

const char *turnPhaseName(int phase)
{
	static const char *const kNames[PHASE_COUNT] = { "Orders", "Movement", "Battles", "Settle", "Ownership", "Retreat", "EndTurn" };
	return phase >= 0 && phase < PHASE_COUNT ? kNames[phase] : "?";
}

const std::vector<std::string> &stopLines()
{
	static const std::vector<std::string> lines = {
		"[S-1320] the presentation blocks LivingWorldObject, LivingWorldSound and LivingWorldAnimObject are consumed by recording stubs, not parsed; StrategicHUD is parsed (its StatsDisplay, DynamicAutoResolveDialog and TypeImages rows follow the retail data)",
		"[S-1321] unported Living World rows (RegionBonus, MapMatchPlayerWorldToMap, DelayCarryoverSpawningOf, the OnEnter / OnVictory upgrades, the Act scripting rows, Tutorial, AddPlayer, EyeTower) stop the parse",
		"[S-1322] presentation fields are kept unvalidated (audio and Eva event names, icon Shadow / AnimMode) and the region effect curves are consumed",
		"[S-1323] AutoResolve data: the Leadership Affects parser is inferred; the object AutoResolve rows are read from the raw template rows (S-071)",
		"[S-1324] moves cost retail's path (1 friendly step, 2 other; garrison 1, hero army 2) and crossing hostile armies are repelled; a human's retreat order in phase 4 (WOTR2 milestone 6; a heroless army goes to the first friendly region: inferred); phase 3 and allied award disputes are not ported",
		"[S-1325] the campaign start (ownership without an OwnershipSet, Act One's armies at the start region, the historical restrictions, the capital) is inferred",
		"[S-1326] buildings: retail turn-end construction, the construction lock, the build queue, production and DestroyBuilding; the buildable-hero list; inferred: a hero's spawn ID on both spawn paths, a fought battle's dead hero returning at once; the IncreaseCommandPoints counters (WOTR2 milestone 4); UpgradeTroops (milestone 5: the marks, the automatic pass, the application leaving the orders phase; inferred: the plot byte RW 0x7F3F58 tests, the rank written by the application only); not ported: Create-a-Hero recruitment, the armory upgrades of units in a real-time battle",
		"[S-1327] economy: retail income (fertile land + farms at turn end), CP counters and limit; the player bonus totals are inferred",
		"[S-1328] autoresolve: retail's battle and its power point trackers; inferred: a killed unit's experience value, the player bonus totals, StrengthenArmy's range test, the strategic game's GameLogic mode (no player skill point scalar table, MaxLevelSP); not ported: rank scalars, kill notices",
		"[S-1329] battles: retail resolution options, retreats, cash, starting units, rule 2, victor, survivor rosters, heroes, rank and powers, destroyed buildings, reinforcements and banners, garrison consumption; the army placement (ArmyPlacer) and the makers' army ids are inferred",
		"[S-1330] the AI is retail's LivingWorldAI (troop producer, builder, garrison and army movers, retreats, the battle vote); inferred: a turn's AI steps run back to back before any human order, the order of chains and of a faction's buildings, the battle players' order of the vote; lookups retail reads unchecked are counted (LivingWorldAI::lookupMisses)",
		"[S-1331] victory: retail order and conditions (victory type by index), RemovePlayer's regions and a team defeat ending the war, the defeats of one check collected before any is removed (WOTR2 milestone 5), the score keepers (retail's turns from 0: inferred); a player without a team, the Rules tab choice and the score screen's way back (the main menu, not the strategic lobby) are inferred or not ported",
		"[S-1332] the War of the Ring map and its single player battles save and load (OpenBFME's block obfme:LivingWorld, not retail's CHUNK_LivingWorldLogic layout; a battle saves only while it is fought); not ported: reading retail .BfME2WotR saves, multiplayer War of the Ring (LanStrategic / OnlineStrategic)",
		"[S-1333] the strategic setup: the map preview is drawn by the device (presentation), its picking, scenario and territory texts are retail's; the rule rows are not ported",
		"[S-1334] strategic HUD: StrategicHUD.apt is the only interface (WOTR2 milestone 4 retires the provisional panel): stats, checklist, END PHASE, new turn, the battle prompt, the details tray's region, army and build queue panels, the radial menus, the palantir's command buttons and the help box; armies picked on the map, member moves; WOTR2 milestone 5: the unit swapper, the map rollover help, the palantir's region stats tray, the strategic notices (each client queues its local player's), the battle statistics after an autoresolve and the score screen (TimeLine type 6); not ported: the real-time battles' score statistics, the profile's strategic statistics, the unified-region notice (its dispatcher is not found), the defeated local player's camera flight",
		"[S-1335] the strategic map's look is presentation: the region effects at their final intensity with the effect colour as the material's emissive (inference), lit by LivingWorldMapInfo's lights with an inferred Dir convention, the text layer at opacity 0.2; livingworld.txt's camera, sounds, the eye tower, army lines and banner house colours are not retail's; WaterShader.FX is its diffuse colour; IMPASSABLE overlays are hidden",
		"[S-1336] the strategic orders are MSG_LW_* messages through LivingWorldLogic::dispatch (RW 0x6BE6A6) with the owner and phase checks kept (stricter than retail); PLAYER_SURRENDERED removes the player and its team; the session, exit-vote and battle-barrier messages, DISBAND_ARMY and the battle-line / award-dispute / auto-resolve pacing messages are not ported",
		"[S-1337] a network War of the Ring's strategic map runs in the lockstep without an empty GameLogic under it (retail ticks one): the strategic state is the whole frame CRC; the clock and phase start are saved with the strategic block (v6)",
		"[S-1338] the war start (CreatePlayers RW 0x932EB8): a random faction or colour and an observer slot are not resolved (refused by name); the handicap is passed as retail's -(slot + 0x20)",
		"[S-1339] a LAN War of the Ring (the LanStrategic page, the war START, the phase timer, the wait message, the battle vote and real-time battles over the LAN: fought only when every live human of the war is in the battle, RW 0x6B5E3E vs the 0x6A7 tally); online War of the Ring is not available yet",
		"[S-1340] a LAN War of the Ring is saved from its map (every peer writes its copy, GameState mode 6) and resumed from the LAN lobby's Load Game; a joiner without the copy, a copy that differs or a saved human's open seat refuses the start by name (stricter than retail); Load is disabled in network games as in retail (RW 0x921F83)",
		"[S-1341] closed: a hung LAN peer that resumes after the others dropped it learns it was dropped (MPDROP-3: the DISCONNECTPLAYER is resent until acknowledged) and runs no further frame (MPDROP-2: LockstepDriver::acquire hands out none once Network::quitRequested(); the strategic session the same)",
		"[S-1342] a War of the Ring battle with more sides than its map's start positions is refused by the battle start (retail's third-side placement RW 0x62D7EA is not read); a LAN war's vote auto-resolves it instead",
	};
	return lines;
}

LivingWorldLogic::LivingWorldLogic(const LivingWorldStores &stores, UnitInfoSource units)
	: m_stores(stores), m_units(std::move(units))
{
}

LivingWorldLogic::~LivingWorldLogic() = default;

UnitInfo LivingWorldLogic::unitInfo(const std::string &thingTemplate) const
{
	return m_units ? m_units(thingTemplate) : UnitInfo();
}

void LivingWorldLogic::addMember(Army &army, ArmyMember member)
{
	member.key = army.nextKey++; // ArmySummary::AddEntry RW 0x811951: + 0x3C, then + 0x3C + 1
	army.members.push_back(member);
}

void LivingWorldLogic::log(const std::string &text)
{
	StrategicEvent e;
	e.turn = m_turn;
	e.phase = m_phase;
	e.text = text;
	m_events.push_back(e);
}

const PlayerTemplate &LivingWorldLogic::playerTemplate(int player) const
{
	return *m_stores.findPlayerTemplate(m_players[(size_t)player].lwTemplate);
}

const BuildingTemplate *LivingWorldLogic::buildingTemplate(const Building &b) const
{
	return m_stores.findBuilding(b.templateName);
}

bool LivingWorldLogic::start(const GameSetup &setup, std::string *error)
{
	if (m_started)
	{
		setError(error, "the campaign was already started");
		return false;
	}
	m_campaign = m_stores.findCampaign(setup.campaign);
	if (!m_campaign || !m_campaign->scenario)
	{
		setError(error, "no LivingWorldCampaign with a Scenario named '" + setup.campaign + "'");
		return false;
	}
	const Scenario &sc = *m_campaign->scenario;
	m_regionCampaign = m_stores.findRegionCampaign(sc.regionCampaign);
	if (!m_regionCampaign)
	{
		setError(error, "the scenario's RegionCampaign '" + sc.regionCampaign + "' is not defined");
		return false;
	}
	const int n = (int)setup.slots.size();
	if (n < sc.minPlayers || n > sc.maxPlayers)
	{
		setError(error, "the scenario takes " + std::to_string(sc.minPlayers) + ".." + std::to_string(sc.maxPlayers) + " players, the setup has " + std::to_string(n));
		return false;
	}
	m_seed = setup.seed;
	m_victoryType = setup.victoryType;
	m_random.initGameLogicRandom(setup.seed, -1); // [S-080] the strategic game's own logic generator
	m_regions.assign(m_regionCampaign->regions.size(), RegionState());
	for (const std::string &r : sc.disableRegions)
	{
		const int idx = m_regionCampaign->regionIndex(r);
		if (idx < 0)
		{
			setError(error, "DisableRegions names an unknown region '" + r + "'");
			return false;
		}
		m_regions[(size_t)idx].enabled = false;
	}
	for (int i = 0; i < n; ++i)
	{
		const SetupSlot &s = setup.slots[(size_t)i];
		const PlayerTemplate *t = m_stores.findPlayerTemplateByFaction(s.faction);
		if (!t)
		{
			setError(error, "slot " + std::to_string(i) + ": no LivingWorldPlayerTemplate plays the faction '" + s.faction + "'");
			return false;
		}
		if (contains(sc.disabledFactions, s.faction))
		{
			setError(error, "slot " + std::to_string(i) + ": the scenario disables the faction '" + s.faction + "'");
			return false;
		}
		PlayerState p;
		p.name = s.name;
		p.faction = s.faction;
		p.lwTemplate = t->name;
		p.human = s.human;
		p.aiDifficulty = s.aiDifficulty;
		p.team = s.team >= 0 ? s.team : 100 + i; // [inference] a player without a team is alone
		p.color = s.color;
		p.handicap = s.handicap;
		std::string start = s.startRegion;
		if (sc.historicalScenario)
		{
			// a historical scenario fixes each faction's start region and team (the data's comment; StartingRestriction)
			bool found = false;
			for (const StartingRestriction &r : sc.startingRestrictions)
			{
				if (contains(r.factions, s.faction) && !r.regions.empty())
				{
					start = r.regions[0];
					if (!r.teams.empty())
					{
						p.team = r.teams[0];
					}
					found = true;
					break;
				}
			}
			if (!found)
			{
				setError(error, "slot " + std::to_string(i) + ": the historical scenario has no StartingRestriction for '" + s.faction + "'");
				return false;
			}
		}
		if (start.empty() && i < (int)sc.defaultStartSpots.size())
		{
			start = sc.defaultStartSpots[(size_t)i];
		}
		if (start.empty())
		{
			// INFERENCE [S-1325] (WOTR-2, review r2): retail's players pick their start territory on the setup's map preview (RW 0x976877, not ported);
			// a player past the scenario's DefaultStartSpots (WOTRScenario001 lists two for its six players) takes the first region of the campaign, in
			// definition order, that is enabled, not in DisallowStartInRegions or DefaultStartSpots, and no earlier player's start
			for (const Region &r : m_regionCampaign->regions)
			{
				const int idx = m_regionCampaign->regionIndex(r.name);
				if (!m_regions[(size_t)idx].enabled || contains(sc.disallowStartInRegions, r.name) || contains(sc.defaultStartSpots, r.name))
				{
					continue;
				}
				bool taken = false;
				for (const PlayerState &o : m_players)
				{
					taken = taken || o.capital == idx;
				}
				if (!taken)
				{
					start = r.name;
					log("[S-1325] " + s.name + " starts in " + r.name + " (past the scenario's DefaultStartSpots)");
					break;
				}
			}
			if (start.empty())
			{
				setError(error, "slot " + std::to_string(i) + ": no start region left (the scenario's DefaultStartSpots has " + std::to_string(sc.defaultStartSpots.size()) + ")");
				return false;
			}
		}
		p.capital = m_regionCampaign->regionIndex(start);
		if (p.capital < 0)
		{
			setError(error, "slot " + std::to_string(i) + ": unknown start region '" + start + "'");
			return false;
		}
		for (const PlayerState &o : m_players)
		{
			if (o.capital == p.capital)
			{
				setError(error, "slot " + std::to_string(i) + ": the start region '" + start + "' is taken");
				return false;
			}
		}
		m_resourceMultiplierLimit = setup.resourceMultiplierLimit;
		m_ranks = setup.ranks;
		m_rankTemplates = setup.rankTemplates;
		if (!setup.ranks.empty() && setup.ranks.front().present)
		{
			p.powerPoints = (int)setup.ranks.front().sciencePurchasePointsGranted; // RW 0x6BB459 -> RW 0x619071 (rank 1's + 0x38)
		}
		m_players.push_back(p);
		initBuildableHeroes((int)m_players.size() - 1); // AddPlayer WB 0xDA5590 -> InitBuildableHeroes WB 0xDEA3B0
	}
	m_turn = 1;
	m_phase = PHASE_ORDERS;
	// ownership, buildings and armies
	const Act *actOne = m_campaign->acts.empty() ? nullptr : &m_campaign->acts[0];
	for (int i = 0; i < n; ++i)
	{
		PlayerState &p = m_players[(size_t)i];
		// RW LivingWorldCampaign::AdjustStartingPositionForScenario (WorldBuilder 0x1356150): with ownership sets, the chosen start region becomes the
		// StartRegion of the set that holds it (OwnershipSet::getStartRegion 0x1507A00: the region itself when the set names none)
		const OwnershipSet *set = nullptr;
		const std::string &chosen = m_regionCampaign->regions[(size_t)p.capital].name;
		for (const OwnershipSet &o : sc.ownershipSets)
		{
			bool holds = sameName(o.startRegion, chosen);
			for (const std::string &r : o.regions)
			{
				holds = holds || sameName(r, chosen);
			}
			if (holds)
			{
				set = &o;
				break;
			}
		}
		if (set && !set->startRegion.empty())
		{
			const int adjusted = m_regionCampaign->regionIndex(set->startRegion);
			if (adjusted < 0)
			{
				setError(error, "OwnershipSet names an unknown StartRegion '" + set->startRegion + "'");
				return false;
			}
			p.capital = adjusted;
		}
		m_regions[(size_t)p.capital].owner = i;
		if (!set)
		{
			// no ownership set: the start region and every Act One army for this template that spawns at act start [inference]
			if (actOne)
			{
				for (const SpawnArmy &a : actOne->spawnArmies)
				{
					if (a.spawnAtActStart && contains(a.spawnForTemplates, p.lwTemplate))
					{
						const int r = a.initialRegion.empty() ? p.capital : m_regionCampaign->regionIndex(a.initialRegion);
						spawnArmy(i, a, r < 0 ? p.capital : r);
					}
				}
			}
			continue;
		}
		for (const std::string &r : set->regions)
		{
			const int idx = m_regionCampaign->regionIndex(r);
			if (idx < 0)
			{
				setError(error, "OwnershipSet names an unknown region '" + r + "'");
				return false;
			}
			m_regions[(size_t)idx].owner = i;
		}
		for (const SpawnBuildings &sb : set->spawnBuildings)
		{
			const int idx = m_regionCampaign->regionIndex(sb.region);
			if (idx < 0)
			{
				setError(error, "SpawnBuildings names an unknown region '" + sb.region + "'");
				return false;
			}
			for (const std::string &name : sb.buildings)
			{
				const BuildingTemplate *b = m_stores.findBuilding(name);
				if (!b)
				{
					setError(error, "SpawnBuildings names an unknown LivingWorldBuilding '" + name + "'");
					return false;
				}
				if (!contains(b->availableTo, p.lwTemplate))
				{
					continue; // a macro lists one building per faction: the owner's is the one its template may build
				}
				Building built;
				built.templateName = b->name;
				built.slot = (int)m_regions[(size_t)idx].buildings.size();
				built.id = m_nextBuildingId++; // ConstructBuildingImmediately RW 0x902C67 [INFERENCE: retail builds the start buildings in this order]
				m_regions[(size_t)idx].buildings.push_back(built);
				onBuildingCompleted(idx, m_regions[(size_t)idx].buildings.back()); // RW 0x902C67 -> RW 0x8E5135
			}
		}
		for (const SpawnArmies &sa : set->spawnArmies)
		{
			const int idx = m_regionCampaign->regionIndex(sa.region);
			if (idx < 0)
			{
				setError(error, "SpawnArmies names an unknown region '" + sa.region + "'");
				return false;
			}
			for (const std::string &name : sa.armies)
			{
				const SpawnArmy *spawn = nullptr;
				if (actOne)
				{
					for (const SpawnArmy &a : actOne->spawnArmies)
					{
						if (sameName(a.scriptingName, name) && contains(a.spawnForTemplates, p.lwTemplate))
						{
							spawn = &a;
							break;
						}
					}
				}
				if (!spawn)
				{
					setError(error, "SpawnArmies names '" + name + "', which Act One does not define for " + p.lwTemplate);
					return false;
				}
				spawnArmy(i, *spawn, idx);
			}
		}
	}
	m_ai = std::make_unique<LivingWorldAI>(*this);
	if (!m_ai->attachAll(false, error))
	{
		return false;
	}
	m_started = true;
	log("campaign " + m_campaign->name + " started with " + std::to_string(n) + " players");
	enterPhase(-1);
	return true;
}

void LivingWorldLogic::spawnArmy(int player, const SpawnArmy &spawn, int region, const std::string &spawnId)
{
	Army a;
	a.id = m_nextArmyId++;
	a.owner = player;
	a.region = region;
	a.scriptingName = spawn.scriptingName;
	a.playerArmy = spawn.playerArmy;
	a.heroTemplate = spawn.heroTemplateName;
	a.icon = spawn.icon;
	a.banner = spawn.banner;
	if (const PlayerArmy *pa = m_stores.findPlayerArmy(spawn.playerArmy))
	{
		// the army's summary is the PlayerArmy's copy (RW 0x71AD41 -> RW 0x81176F): its entries keep the PlayerArmy's keys, 1.. in ArmyEntry order
		// (each parsed through ArmySummary::AddEntry, RW 0x811CF9), and its counter
		for (const ArmyEntry &e : pa->entries)
		{
			ArmyMember m;
			m.thingTemplate = e.thingTemplate;
			m.quantity = e.quantity;
			addMember(a, m);
		}
	}
	else
	{
		log("[S-1325] SpawnArmy " + spawn.scriptingName + ": unknown LivingWorldPlayerArmy '" + spawn.playerArmy + "' (an empty army)");
	}
	a.garrison = spawn.heroTemplateName.empty();
	// lane WOTR-2: spawnArmy (WB 0xD9AA50 / RW 0x6B7229), for each hero entry: the revival entry of the army's ArmyToSpawn when the list holds it, else
	// the first entry of the same thing (WB 0xDEA310); its experience goes to the hero and the entry leaves the list (RemoveRevivalUnit WB 0xDEA200)
	if (m_units)
	{
		for (ArmyMember &m : a.members)
		{
			if (m_units(m.thingTemplate).hero)
			{
				takeRevivalData(player, spawnId, m);
			}
		}
	}
	m_armies.push_back(a);
}

std::string LivingWorldLogic::spawnIdOf(const BuildingTemplate &b, int armyIndex)
{
	return b.name + "#" + std::to_string(armyIndex); // the ArmyToSpawn's identity (its + 0x4C ID in retail)
}

// InitBuildableHeroes (WorldBuilder 0xDEA3B0, from AddPlayer): every LivingWorldBuilding available to the player's template, its SpawnArmy nugget's
// ArmyToSpawn entries whose army's first unit is a HERO (a CREATE_A_HERO one only with a Create-a-Hero profile: none here); one entry per
// ArmyToSpawn (a hero two buildings offer is listed twice). The building data is the only source
void LivingWorldLogic::initBuildableHeroes(int player)
{
	PlayerState &ps = m_players[(size_t)player];
	ps.buildableHeroes.clear();
	if (!m_units)
	{
		return;
	}
	for (const BuildingTemplate &b : m_stores.buildings)
	{
		if (!contains(b.availableTo, ps.lwTemplate))
		{
			continue;
		}
		const BuildingNugget *nug = b.findNugget(BuildingNugget::SPAWN_ARMY);
		if (!nug)
		{
			continue;
		}
		for (int i = 0; i < (int)nug->armiesToSpawn.size(); ++i)
		{
			const PlayerArmy *pa = m_stores.findPlayerArmy(nug->armiesToSpawn[(size_t)i].playerArmy);
			if (!pa || pa->entries.empty())
			{
				continue;
			}
			const UnitInfo u = m_units(pa->entries.front().thingTemplate);
			if (!u.hero || u.createAHero)
			{
				continue;
			}
			RevivalUnit r;
			r.spawnId = spawnIdOf(b, i);
			r.thing = pa->entries.front().thingTemplate;
			ps.buildableHeroes.push_back(r);
		}
	}
}

// HasArmyQueuedInAnyBuilding (WorldBuilder 0xDE9E20 / RW 0x6E24C0): an ArmyToSpawn queued in a finished building of any region the player owns
bool LivingWorldLogic::heroQueuedAnywhere(int player, const std::string &spawnId) const
{
	for (size_t r = 0; r < m_regions.size(); ++r)
	{
		if (m_regions[r].owner != player)
		{
			continue;
		}
		for (const ProductionOrder &o : m_regions[r].production)
		{
			for (const Building &b : m_regions[r].buildings)
			{
				const BuildingTemplate *bt = b.slot == o.buildingSlot && b.turnsLeft == 0 ? buildingTemplate(b) : nullptr;
				if (bt && spawnIdOf(*bt, o.armyIndex) == spawnId)
				{
					return true;
				}
			}
		}
	}
	return false;
}

void LivingWorldLogic::takeRevivalData(int player, const std::string &spawnId, ArmyMember &member)
{
	std::vector<RevivalUnit> &list = m_players[(size_t)player].buildableHeroes;
	auto it = spawnId.empty() ? list.end() : std::find_if(list.begin(), list.end(), [&](const RevivalUnit &r) { return r.spawnId == spawnId; });
	if (it == list.end())
	{
		it = std::find_if(list.begin(), list.end(), [&](const RevivalUnit &r) { return r.thing == member.thingTemplate; }); // WB 0xDEA310
	}
	if (it == list.end())
	{
		return;
	}
	member.experience = it->experience; // GetRevivalUnitData WB 0xDEB6B0
	member.spawnId = it->spawnId;       // entry + 0xD0 [inference S-1326: also on the first path, so a dead hero returns under its building's ID]
	list.erase(it);
}

// AddRevivalUnit (WorldBuilder 0xDEAFB0, from LivingWorldArmy::KillSummaryEntry 0x10394F0 and the autoresolve's applyResults 0x1319DE0): a dead hero
// returns to its owner's list with its experience, under its entry's + 0xD0
void LivingWorldLogic::addRevivalUnit(int player, const ArmyMember &member, float experience)
{
	if (player < 0 || player >= (int)m_players.size())
	{
		return;
	}
	RevivalUnit r;
	r.spawnId = member.spawnId;
	r.thing = member.thingTemplate;
	r.experience = experience;
	r.dead = true;
	m_players[(size_t)player].buildableHeroes.push_back(r);
}

// ---- economy ----

int LivingWorldLogic::treasury(int player) const
{
	return playerTemplate(player).scenarioStartResources + m_players[(size_t)player].treasuryAccum;
}

void LivingWorldLogic::addTreasure(int player, int amount)
{
	// RW 0x6E1A40: start + accumulated never exceeds the template's ScenarioMaxResources
	const PlayerTemplate &t = playerTemplate(player);
	PlayerState &p = m_players[(size_t)player];
	const int start = t.scenarioStartResources;
	if (p.treasuryAccum + start + amount > t.scenarioMaxResources)
	{
		p.treasuryAccum = t.scenarioMaxResources - start;
	}
	else
	{
		p.treasuryAccum += amount;
	}
}

bool LivingWorldLogic::spend(int player, int amount)
{
	if (treasury(player) < amount)
	{
		return false;
	}
	m_players[(size_t)player].treasuryAccum -= amount;
	return true;
}

int LivingWorldLogic::incomePerTurn(int player) const
{
	// RW LivingWorldPlayer::GetPerTurnIncome (WorldBuilder 0xDED480): over the player's regions, the region's FertileTerritoryBonus (+0xA8) plus every
	// completed building's IncreaseTreasury amounts (0x104DD60 -> 0x10EA1A0)
	int income = 0;
	for (size_t r = 0; r < m_regions.size(); ++r)
	{
		if (m_regions[r].owner != player)
		{
			continue;
		}
		income += m_regionCampaign->regions[r].fertileTerritoryBonus;
		for (const Building &b : m_regions[r].buildings)
		{
			const BuildingTemplate *t = buildingTemplate(b);
			if (!t || b.turnsLeft > 0)
			{
				continue;
			}
			for (const BuildingNugget &nug : t->nuggets)
			{
				if (nug.kind == BuildingNugget::INCREASE_TREASURY)
				{
					income += nug.treasureAmount;
				}
			}
		}
	}
	return income;
}

int LivingWorldLogic::commandPointLimit(int player) const
{
	// RW 0x6E1FD8 (LivingWorldPlayer): min(StartingWorldCP + the WORLD IncreaseCommandPoints counter (+0x310) + the ArmyBonus total (+0x29C), MaxWorldCP)
	const PlayerTemplate &t = playerTemplate(player);
	const int cp = t.startingWorldCP + m_players[(size_t)player].worldCPFromBuildings + playerBonuses(player).army;
	return cp < t.maxWorldCP ? cp : t.maxWorldCP;
}

int LivingWorldLogic::armyCommandPoints(const Army &army) const
{
	return membersCommandPoints(army); // RW 0x71A8F9 -> ArmySummary RW 0x80FA5D: every entry, heroes included (WOTR2 milestone 4; S-1327 had them out)
}

int LivingWorldLogic::armiesCommandPoints(int player) const
{
	// RW 0x6E1F6C: the player's armies (+0x1E4), each its summary's entries' CommandPoints x count (RW 0x80FA5D / 0x78012F, heroes included)
	int cp = 0;
	for (const Army &a : m_armies)
	{
		if (a.owner == player)
		{
			cp += membersCommandPoints(a);
		}
	}
	return cp;
}

int LivingWorldLogic::commandPointsUsed(int player) const
{
	return armiesCommandPoints(player) + m_players[(size_t)player].queuedCommandPoints; // RW 0x6E22B3
}

void LivingWorldLogic::onBuildingCompleted(int region, Building &b)
{
	if (b.cpApplied)
	{
		return;
	}
	b.cpApplied = true; // set even when the region has no owner (RW 0x9D3829)
	scoreBuildingCompleted(region, b); // the building-completed listeners (RW 0x8F31EE)
	const int owner = m_regions[(size_t)region].owner;
	const BuildingTemplate *t = buildingTemplate(b);
	if (owner < 0 || !t)
	{
		return;
	}
	for (const BuildingNugget &n : t->nuggets)
	{
		if (n.kind == BuildingNugget::INCREASE_COMMAND_POINTS)
		{
			(n.cpType == 1 ? m_players[(size_t)owner].worldCPFromBuildings : m_players[(size_t)owner].tacticalCPFromBuildings) += n.cpAmount; // RW 0x9D37E8
		}
	}
}

void LivingWorldLogic::onBuildingDestroyed(int region, Building &b)
{
	const BuildingTemplate *t = buildingTemplate(b);
	const int owner = m_regions[(size_t)region].owner;
	scoreBuildingDestroyed(region, b); // the building-destroyed listeners (RW 0x8F2A4D)
	// WOTR2 milestone 5: OnDestroyingBuilding RW 0x6B7139 (phase 0, a complete armory): the marks naming it on the region owner's armies there go
	if (m_phase == PHASE_ORDERS && owner >= 0 && armoryOf(b))
	{
		for (Army &a : m_armies)
		{
			if (a.owner != owner || a.region != region)
			{
				continue;
			}
			for (ArmyMember &e : a.members)
			{
				if (e.upgradeBuilding == b.id)
				{
					cancelUpgrade(e);
				}
			}
		}
	}
	// the SpawnArmy nugget's destructor (RW 0x8FFF41): every queued army's points leave the region owner's counter, no money back
	if (t)
	{
		if (const BuildingNugget *nug = t->findNugget(BuildingNugget::SPAWN_ARMY))
		{
			for (size_t q : queueOf(region, b.slot))
			{
				const int idx = m_regions[(size_t)region].production[q].armyIndex;
				if (idx >= 0 && idx < (int)nug->armiesToSpawn.size())
				{
					onUnitDequeued(region, m_stores.findPlayerArmy(nug->armiesToSpawn[(size_t)idx].playerArmy));
				}
			}
		}
	}
	// the IncreaseCommandPoints nugget's destructor (RW 0x9D3881): its amounts leave the region's CURRENT owner
	if (!b.cpApplied)
	{
		return;
	}
	b.cpApplied = false;
	if (owner < 0 || !t)
	{
		return;
	}
	for (const BuildingNugget &n : t->nuggets)
	{
		if (n.kind == BuildingNugget::INCREASE_COMMAND_POINTS)
		{
			(n.cpType == 1 ? m_players[(size_t)owner].worldCPFromBuildings : m_players[(size_t)owner].tacticalCPFromBuildings) -= n.cpAmount;
		}
	}
}

void LivingWorldLogic::onUnitQueued(int region, const PlayerArmy *pa)
{
	const int owner = m_regions[(size_t)region].owner;
	if (owner >= 0)
	{
		m_players[(size_t)owner].queuedCommandPoints += pa ? recruitCommandPoints(*pa) : 0;
	}
}

void LivingWorldLogic::onUnitDequeued(int region, const PlayerArmy *pa)
{
	const int owner = m_regions[(size_t)region].owner;
	if (owner >= 0)
	{
		int &q = m_players[(size_t)owner].queuedCommandPoints;
		q -= pa ? recruitCommandPoints(*pa) : 0;
		if (q < 0)
		{
			q = 0;
		}
	}
}

// RW 0x80F018 (WorldBuilder 0x10A3D10): the army's FIRST entry's unit's BuildCost (its count and the other entries are not counted); a
// CREATE_A_HERO unit adds its profile's cost (not reached: canRecruit refuses those armies, S-1326); by the unit's AutoResolveUnitType: a siege
// unit (7) costs half while the player's DiscountedSiegeUnits (+0x2C0) is at least 1, else full; a hero (5) half with DiscountedHeroUnits
// (+0x2C4), else full; a structure (6) full; anything else DiscountedBarracksUnits (+0x2CC) less while that is at least 1; never below 0
int LivingWorldLogic::recruitCost(int player, const PlayerArmy &army) const
{
	if (army.entries.empty() || !m_units)
	{
		return 0; // RW: no entry 0 thing -> 0 (canRecruit refuses such an army first)
	}
	return unitCost(player, army.entries.front().thingTemplate);
}

// the cost rule of RW 0x80F018 for one unit (WOTR2 milestone 7: ConsumeGarrisonUnitsFromRTS charges an entry with it)
int LivingWorldLogic::unitCost(int player, const std::string &thingTemplate) const
{
	if (!m_units)
	{
		return 0;
	}
	const UnitInfo u = m_units(thingTemplate);
	const int cost = u.buildCost;
	const PlayerBonuses b = playerBonuses(player);
	int discounted = cost;
	switch (u.autoResolveUnitType)
	{
		case AR_SIEGE:
			if (b.discountedSiegeUnits < 1)
			{
				return cost;
			}
			discounted = cost / 2;
			break;
		case AR_HERO:
			if (b.discountedHeroUnits < 1)
			{
				return cost;
			}
			discounted = cost / 2;
			break;
		case AR_FORTRESS:
			return cost;
		default:
			if (b.discountedBarracksUnits < 1)
			{
				return cost;
			}
			discounted = cost - b.discountedBarracksUnits;
			break;
	}
	return discounted < 0 ? 0 : discounted;
}

int LivingWorldLogic::recruitCommandPoints(const PlayerArmy &army) const
{
	// RW 0x8E5C5C -> ArmySummary::GetArmyCommandPointSize (WorldBuilder 0x109D2D0): every entry's CommandPoints (+0x628) times its count, heroes included
	int cp = 0;
	for (const ArmyEntry &e : army.entries)
	{
		const UnitInfo u = m_units ? m_units(e.thingTemplate) : UnitInfo();
		cp += u.commandPoints * e.quantity;
	}
	return cp;
}

int LivingWorldLogic::membersCommandPoints(const Army &army) const
{
	int cp = 0;
	for (const ArmyMember &m : army.members)
	{
		const UnitInfo u = m_units ? m_units(m.thingTemplate) : UnitInfo();
		cp += u.commandPoints * m.quantity;
	}
	return cp;
}

int LivingWorldLogic::buildingCost(int player, const BuildingTemplate &b) const
{
	// RW 0x6E0B4A: StrategicResourceCost (+0x2C) less the player's BuildingDiscount (+0x2B8), at least 0
	const int cost = b.strategicResourceCost - playerBonuses(player).buildingDiscount;
	return cost < 0 ? 0 : cost;
}

bool LivingWorldLogic::constructionUnderway(int region) const
{
	// the region's construction lock (+0x1C3): set when a building is ordered, cleared at the turn end's construction step (WorldBuilder 0x104DE20)
	// and set again while the building it works on is unfinished (0x104E9C0), cleared by DestroyBuilding (RW 0x7F32F4); one building is under
	// construction at a time, so the lock is that building's
	for (const Building &b : m_regions[(size_t)region].buildings)
	{
		if (b.turnsLeft > 0)
		{
			return true;
		}
	}
	return false;
}

std::vector<size_t> LivingWorldLogic::queueOf(int region, int buildingSlot) const
{
	std::vector<size_t> out;
	const std::vector<ProductionOrder> &production = m_regions[(size_t)region].production;
	for (size_t i = 0; i < production.size(); ++i)
	{
		if (production[i].buildingSlot == buildingSlot)
		{
			out.push_back(i);
		}
	}
	return out;
}

int LivingWorldLogic::armyStrength(const Army &army) const
{
	int s = 0;
	for (const ArmyMember &m : army.members)
	{
		const UnitInfo u = m_units ? m_units(m.thingTemplate) : UnitInfo();
		s += u.strength * m.quantity;
	}
	return s;
}

const Army *LivingWorldLogic::findArmy(int id) const
{
	for (const Army &a : m_armies)
	{
		if (a.id == id)
		{
			return &a;
		}
	}
	return nullptr;
}

void LivingWorldLogic::removeArmy(int id)
{
	for (size_t i = 0; i < m_armies.size(); ++i)
	{
		if (m_armies[i].id == id)
		{
			m_armies.erase(m_armies.begin() + (std::ptrdiff_t)i);
			m_retreatList.erase(std::remove(m_retreatList.begin(), m_retreatList.end(), id), m_retreatList.end()); // DestroyArmy RW 0x6B674D
			return;
		}
	}
}

bool LivingWorldLogic::hostile(int a, int b) const
{
	if (a < 0 || b < 0 || a == b)
	{
		return false;
	}
	return m_players[(size_t)a].team != m_players[(size_t)b].team;
}

int LivingWorldLogic::regionCount(int player) const
{
	int c = 0;
	for (const RegionState &r : m_regions)
	{
		if (r.owner == player)
		{
			++c;
		}
	}
	return c;
}

int LivingWorldLogic::countBuildings(int region, int type) const
{
	int c = 0;
	for (const Building &b : m_regions[(size_t)region].buildings)
	{
		const BuildingTemplate *t = buildingTemplate(b);
		if (t && t->type == type)
		{
			++c;
		}
	}
	return c;
}

int LivingWorldLogic::freeBuildingSlots(int region) const
{
	// the region's BuildingSpot rows are its build plots [inference S-1326]
	const int spots = (int)m_regionCampaign->regions[(size_t)region].buildingSpots.size();
	return spots - (int)m_regions[(size_t)region].buildings.size();
}

std::vector<std::string> LivingWorldLogic::buildableIn(int player, int region) const
{
	std::vector<std::string> out;
	// RW 0x7F19D6: no building while the region's construction lock is set (reason 3)
	if (region < 0 || region >= (int)m_regions.size() || m_regions[(size_t)region].owner != player || freeBuildingSlots(region) <= 0 ||
		constructionUnderway(region))
	{
		return out;
	}
	const Region &def = m_regionCampaign->regions[(size_t)region];
	for (const BuildingTemplate &b : m_stores.buildings)
	{
		if (!contains(b.availableTo, m_players[(size_t)player].lwTemplate))
		{
			continue;
		}
		const int allowed = def.allowedBuildings(b.type);
		if (allowed >= 0 && countBuildings(region, b.type) >= allowed)
		{
			continue;
		}
		out.push_back(b.name);
	}
	return out;
}

// ---- orders ----

bool LivingWorldLogic::orderMove(int player, int armyId, int region, std::string *error)
{
	// MSG_LW_ARMY_MOVE_REQUEST 0x6A8 -> RW 0x6B5847 (phases 0 and 4): the checks (CanArmyMoveTo RW 0x6B57E7) first, a refusal changes nothing; then the
	// old move goes (RW 0x71B12F) and an order to the army's own region stops there; else the new destination
	std::string why;
	if (!checkMove(player, armyId, region, &why))
	{
		setError(error, why);
		return false;
	}
	for (Army &a : m_armies)
	{
		if (a.id == armyId)
		{
			a.destination = -1;
			if (region != a.region)
			{
				moveArmyToRegion(a, region);
			}
			return true;
		}
	}
	return false;
}

bool LivingWorldLogic::canOrderMove(int player, int armyId, int region) const
{
	return checkMove(player, armyId, region, nullptr);
}

bool LivingWorldLogic::checkMove(int player, int armyId, int region, std::string *error) const
{
	const bool retreat = m_phase == PHASE_OWNERSHIP;
	if ((m_phase != PHASE_ORDERS && !retreat) || m_finished)
	{
		setError(error, "armies move only in the orders phase and the retreat phase");
		return false;
	}
	const Army *a = findArmy(armyId);
	if (!a)
	{
		setError(error, "no such army");
		return false;
	}
	if (a->owner != player)
	{
		setError(error, "not the player's army");
		return false;
	}
	// port-side (retail's handler trusts its UI, which picks only these in phase 4: IsArmySelectable RW 0x6B6657 / 0x6B568C): a retreat order is for
	// a hero army in the retreat list
	if (retreat && (a->garrison || std::find(m_retreatList.begin(), m_retreatList.end(), armyId) == m_retreatList.end()))
	{
		setError(error, "only a hero army that must retreat moves in the retreat phase");
		return false;
	}
	if (region < 0 || region >= (int)m_regions.size() || !m_regions[(size_t)region].enabled)
	{
		setError(error, "no such region");
		return false;
	}
	if (region == a->region)
	{
		return true; // RW 0x6B5847: an order to the army's own region cancels its move
	}
	// RW 0x60F00C (ValidateArmyRegionEntry) and its allowance RW 0x6B57B0: a garrison (no hero) may spend 1, a hero army 2; a garrison, and any army in
	// phase 4 (the retreat), only enters a region of its owner or an ally (an unowned region is no ally)
	const Region &from = m_regionCampaign->regions[(size_t)a->region];
	const Region &to = m_regionCampaign->regions[(size_t)region];
	const int owner = m_regions[(size_t)region].owner;
	if ((a->garrison || retreat) && owner != player && (owner < 0 || hostile(owner, player)))
	{
		setError(error, retreat ? std::string("an army retreats only into its owner's or an ally's region")
		                        : std::string("a garrison only moves into its owner's or an ally's region"));
		return false;
	}
	const int cost = moveCost(player, a->region, region);
	const int allowance = a->garrison ? 1 : 2;
	if (cost < 0 || cost > allowance)
	{
		setError(error, from.name + " -> " + to.name + ": the move costs " + std::to_string(cost) + ", the army may spend " + std::to_string(allowance));
		return false;
	}
	return true;
}

int LivingWorldLogic::moveCost(int player, int from, int to) const
{
	// RW LivingWorldRegionManager::ValidateArmyRegionEntry's path (WorldBuilder 0xB5A3F0 -> LivingWorldPathFinder::FindLeastCostPath 0x1046E50 with the
	// validator vtable 0x1E7E520): A* over the region connections; a step into a region of the same owner as the region it leaves, or of an allied
	// owner, costs 1, any other step 2 (0x10663F0, owners -1 = nobody); a region in the middle of the path must be passable: enabled and neutral or
	// held by the mover or an ally (0x1046500).  A* with that cost finds the least cost: computed here as a Dijkstra over the same graph.
	if (from == to)
	{
		return 0;
	}
	const int n = (int)m_regions.size();
	std::vector<int> best((size_t)n, 0x7FFFFFFF);
	std::vector<bool> done((size_t)n, false);
	best[(size_t)from] = 0;
	auto passable = [&](int r) {
		const int owner = m_regions[(size_t)r].owner;
		return m_regions[(size_t)r].enabled && (owner < 0 || !hostile(owner, player));
	};
	auto stepCost = [&](int a, int b) {
		const int oa = m_regions[(size_t)a].owner, ob = m_regions[(size_t)b].owner;
		if (oa == ob || (oa >= 0 && ob >= 0 && !hostile(oa, ob)))
		{
			return 1;
		}
		return 2;
	};
	for (;;)
	{
		int u = -1;
		for (int r = 0; r < n; ++r)
		{
			if (!done[(size_t)r] && best[(size_t)r] != 0x7FFFFFFF && (u < 0 || best[(size_t)r] < best[(size_t)u]))
			{
				u = r;
			}
		}
		if (u < 0)
		{
			return -1;
		}
		if (u == to)
		{
			return best[(size_t)u];
		}
		done[(size_t)u] = true;
		if (u != from && !passable(u))
		{
			continue; // a middle region the path may not cross (0x1046870: only the goal or a passable node is expanded)
		}
		const Region &ru = m_regionCampaign->regions[(size_t)u];
		for (int v = 0; v < n; ++v)
		{
			if (done[(size_t)v] || !ru.connectsTo(m_regionCampaign->regions[(size_t)v].name))
			{
				continue;
			}
			if (v != to && !passable(v))
			{
				continue;
			}
			const int c = best[(size_t)u] + stepCost(u, v);
			if (c < best[(size_t)v])
			{
				best[(size_t)v] = c;
			}
		}
	}
}

int LivingWorldLogic::maxCommandPoints(const Army &army) const
{
	// RW 0x71ABFC: a hero army's is its owner's MaxHeroCP (template + 0x1C); a garrison's its region's (RW 0x7F0FE5): AllyCPLimit when an ally of the
	// army's owner holds the region, else CPLimit
	if (army.owner < 0 || army.owner >= (int)m_players.size())
	{
		return 0;
	}
	if (!army.heroTemplate.empty())
	{
		return playerTemplate(army.owner).maxHeroCP;
	}
	if (army.region < 0 || army.region >= (int)m_regions.size())
	{
		return 0;
	}
	const Region &r = m_regionCampaign->regions[(size_t)army.region];
	const int o = m_regions[(size_t)army.region].owner;
	return (o >= 0 && o != army.owner && !hostile(o, army.owner)) ? r.allyCPLimit : r.cpLimit;
}

bool LivingWorldLogic::canMoveArmyMember(int armyId, int memberKey, int targetArmyId) const
{
	// CanMoveArmyMember RW 0x6B9B65 -> RW 0x6B8C07: the entry (ArmySummary::GetEntry RW 0x80F63B); the gate RW 0x6B3631 (same owner, another army, phase
	// 0; the scripted campaign's check does not apply to the War of the Ring); RW 0x6B77AC: not the army's leader (the entry's thing is the army's
	// hero name + 0x18); the path test from the entry's army, or from its home army when it was lent (+ 0xC8; none when it goes home): the cost
	// (RW 0x6B3603: ValidateArmyRegionEntry between the two armies' regions) within RW 0x6B57B0's allowance, a garrison 0..1, a hero army 0..2
	// (BFME2: 0..1 for both); the room: the target's command points plus the entry's within its maximum (RW 0x71AD29)
	const Army *a = findArmy(armyId);
	const Army *t = findArmy(targetArmyId);
	if (!a || !t || a == t || a->owner != t->owner || m_phase != PHASE_ORDERS)
	{
		return false;
	}
	const ArmyMember *e = nullptr;
	for (const ArmyMember &m : a->members)
	{
		if (m.key == memberKey)
		{
			e = &m;
		}
	}
	if (!e || e->thingTemplate == a->heroTemplate)
	{
		return false;
	}
	auto within = [&](const Army &from) {
		const int c = moveCost(from.owner, from.region, t->region);
		return c >= 0 && c < (from.heroTemplate.empty() ? 2 : 3);
	};
	if (e->homeArmy == 0)
	{
		if (!within(*a))
		{
			return false;
		}
	}
	else if (e->homeArmy != t->id)
	{
		const Army *h = findArmy(e->homeArmy);
		if (!h || !within(*h))
		{
			return false;
		}
	}
	const int entryCp = m_units ? m_units(e->thingTemplate).commandPoints * e->quantity : 0; // RW 0x78012F
	return membersCommandPoints(*t) + entryCp <= maxCommandPoints(*t);
}

bool LivingWorldLogic::moveArmyMember(int armyId, int memberKey, int targetArmyId)
{
	// MoveArmyMember RW 0x6B47DC: the entry's home (+ 0xC8) becomes its army when it had none, none when it goes home; the target takes it
	// (RW 0x71B7F7: ArmySummary::RemoveEntry RW 0x811242, AddEntry RW 0x811951 with the target's next key). The army left behind stays, even empty
	// (RW 0x71B1B6 only refreshes the icons); the turn's garrison merge (RW 0x6B546B) removes it when another garrison shares its region
	if (!canMoveArmyMember(armyId, memberKey, targetArmyId))
	{
		return false;
	}
	Army *a = nullptr;
	Army *t = nullptr;
	for (Army &x : m_armies)
	{
		a = x.id == armyId ? &x : a;
		t = x.id == targetArmyId ? &x : t;
	}
	auto it = std::find_if(a->members.begin(), a->members.end(), [&](const ArmyMember &m) { return m.key == memberKey; });
	ArmyMember e = *it;
	a->members.erase(it);
	if (e.homeArmy == 0)
	{
		e.homeArmy = a->id;
	}
	else if (e.homeArmy == t->id)
	{
		e.homeArmy = 0;
	}
	addMember(*t, e);
	if (a->region != t->region)
	{
		cancelUpgrade(t->members.back()); // LivingWorldArmy::AddEntry RW 0x71B2D0: an entry moved to another region loses its upgrade mark
	}
	return true;
}

bool LivingWorldLogic::canSwapArmyMembers(int a, const std::vector<int> &keysA, int b, const std::vector<int> &keysB, bool roomTest) const
{
	const Army *A = findArmy(a);
	const Army *B = findArmy(b);
	if (!A || !B || A == B || A->owner != B->owner || m_phase != PHASE_ORDERS) // the gate RW 0x6B3631
	{
		return false;
	}
	// CanMoveArmyMembers_internal RW 0x6B7822: each entry's own test (RW 0x6B77AC without the room test), the command points summed
	auto side = [&](const Army &from, const std::vector<int> &keys, const Army &to, int *cp) {
		*cp = 0;
		for (int key : keys)
		{
			const ArmyMember *e = nullptr;
			for (const ArmyMember &m : from.members)
			{
				e = m.key == key ? &m : e;
			}
			if (!e)
			{
				return false; // the message's extraction (RW 0x6BB66E) refuses an entry the army does not have
			}
			if (e->thingTemplate == from.heroTemplate)
			{
				return false;
			}
			auto within = [&](const Army &src) {
				const int c = moveCost(src.owner, src.region, to.region);
				return c >= 0 && c < (src.heroTemplate.empty() ? 2 : 3);
			};
			if (e->homeArmy == 0 ? !within(from) : (e->homeArmy != to.id && (!findArmy(e->homeArmy) || !within(*findArmy(e->homeArmy)))))
			{
				return false;
			}
			*cp += m_units ? m_units(e->thingTemplate).commandPoints * e->quantity : 0; // RW 0x78012F
		}
		return true;
	};
	int cpA = 0, cpB = 0;
	if (!side(*A, keysA, *B, &cpA) || !side(*B, keysB, *A, &cpB))
	{
		return false;
	}
	if (!roomTest)
	{
		return true;
	}
	if (cpA > cpB)
	{
		return membersCommandPoints(*B) + (cpA - cpB) <= maxCommandPoints(*B); // RW 0x71AD02
	}
	return membersCommandPoints(*A) + (cpB - cpA) <= maxCommandPoints(*A);
}

bool LivingWorldLogic::swapArmyMembers(int a, const std::vector<int> &keysA, int b, const std::vector<int> &keysB)
{
	if (!canSwapArmyMembers(a, keysA, b, keysB))
	{
		return false;
	}
	// SwapArmyMembers RW 0x6B79D8: MoveArmyMember (RW 0x6B47DC: the home updated, the target's next key) for A's entries, then B's (their keys were
	// B's before: the entries from A got new keys in B)
	auto move = [&](int src, int dst, int key) {
		Army *S = nullptr;
		Army *D = nullptr;
		for (Army &x : m_armies)
		{
			S = x.id == src ? &x : S;
			D = x.id == dst ? &x : D;
		}
		auto it = std::find_if(S->members.begin(), S->members.end(), [&](const ArmyMember &m) { return m.key == key; });
		if (it == S->members.end())
		{
			return;
		}
		ArmyMember e = *it;
		S->members.erase(it);
		e.homeArmy = e.homeArmy == 0 ? src : (e.homeArmy == dst ? 0 : e.homeArmy);
		addMember(*D, e);
		if (S->region != D->region)
		{
			cancelUpgrade(D->members.back()); // LivingWorldArmy::AddEntry RW 0x71B2D0: an entry moved to another region loses its upgrade mark
		}
	};
	for (int key : keysA)
	{
		move(a, b, key);
	}
	for (int key : keysB)
	{
		move(b, a, key);
	}
	return true;
}

bool LivingWorldLogic::cancelArmyMemberMove(int armyId, int memberKey)
{
	const Army *a = findArmy(armyId);
	if (!a)
	{
		return false;
	}
	for (const ArmyMember &m : a->members)
	{
		if (m.key == memberKey && m.homeArmy != 0 && findArmy(m.homeArmy))
		{
			return moveArmyMember(armyId, memberKey, m.homeArmy); // CanMoveArmyMember (the room tested), then home: the home cleared
		}
	}
	return false;
}

void LivingWorldLogic::consolidateGarrisons()
{
	// RW 0x6B546B (entering phase 0, leaving phase 1): in every owned region (index order), for every player (index order) allied with its owner,
	// the player's garrisons standing there (army order) merge into the first one (RW 0x71B202 -> RW 0x8119D1: the entries in order with the
	// keeper's next keys) and are deleted
	for (int r = 0; r < (int)m_regions.size(); ++r)
	{
		const int owner = m_regions[(size_t)r].owner;
		if (owner < 0)
		{
			continue;
		}
		for (int p = 0; p < (int)m_players.size(); ++p)
		{
			if (p != owner && hostile(p, owner))
			{
				continue;
			}
			int keeper = -1;
			for (size_t i = 0; i < m_armies.size();)
			{
				Army &a = m_armies[i];
				if (a.owner != p || a.region != r || !a.heroTemplate.empty())
				{
					++i;
					continue;
				}
				if (keeper < 0)
				{
					keeper = a.id;
					++i;
					continue;
				}
				Army merged = a;
				m_armies.erase(m_armies.begin() + (std::ptrdiff_t)i);
				for (Army &k : m_armies)
				{
					if (k.id == keeper)
					{
						for (const ArmyMember &m : merged.members)
						{
							addMember(k, m);
						}
					}
				}
			}
		}
	}
}

bool LivingWorldLogic::cancelMove(int player, int armyId, std::string *error)
{
	for (Army &a : m_armies)
	{
		if (a.id == armyId && a.owner == player && m_phase == PHASE_ORDERS)
		{
			a.destination = -1;
			return true;
		}
	}
	setError(error, "no move to cancel");
	return false;
}

bool LivingWorldLogic::orderBuild(int player, int region, const std::string &building, std::string *error, int slot)
{
	if (m_phase != PHASE_ORDERS || m_finished)
	{
		setError(error, "construction is ordered only in the orders phase");
		return false;
	}
	const std::vector<std::string> options = buildableIn(player, region);
	if (!contains(options, building))
	{
		setError(error, "'" + building + "' cannot be built there");
		return false;
	}
	const BuildingTemplate *b = m_stores.findBuilding(building);
	if (!spend(player, buildingCost(player, *b))) // RW 0x7F31CD / 0x7F3120: the cost for the player (RW 0x6E0B4A)
	{
		setError(error, "not enough resources");
		return false;
	}
	RegionState &rs = m_regions[(size_t)region];
	Building nb;
	nb.templateName = b->name;
	nb.turnsLeft = std::max(1, b->turnsToBuild); // "1 = built at start of next turn" (the data's comment)
	if (slot >= 0)
	{
		// the chosen plot (the message's plot id): it must exist and be free (RW 0x7F19D6)
		const int spots = (int)m_regionCampaign->regions[(size_t)region].buildingSpots.size();
		bool taken = slot >= spots;
		for (const Building &o : rs.buildings)
		{
			taken = taken || o.slot == slot;
		}
		if (taken)
		{
			addTreasure(player, buildingCost(player, *b)); // undo the payment above: the order is refused
			setError(error, "that plot is not free");
			return false;
		}
	}
	else
	{
		for (slot = 0;; ++slot)
		{
			bool used = false;
			for (const Building &o : rs.buildings)
			{
				used = used || o.slot == slot;
			}
			if (!used)
			{
				break;
			}
		}
	}
	nb.slot = slot;
	nb.id = m_nextBuildingId++; // ConstructBuilding RW 0x902B96
	rs.buildings.push_back(nb);
	log(m_players[(size_t)player].name + " builds " + b->name + " in " + m_regionCampaign->regions[(size_t)region].name);
	return true;
}

// LivingWorldBuildingNuggetSpawnArmy::canQueueUnit (WorldBuilder 0x1334DE0 / RW 0x8FF50E), in its order: the queue is shorter than QueueSize, the army is
// one of the building's ArmyToSpawn entries, the army and its first entry's unit exist, a hero: the player's buildable-hero list holds the ArmyToSpawn and no queue of
// the player holds it (RW 0x6E2625 / 0x6E24C0), the army's cost for the player (RW 0x80F018) is at most the treasury. Retail checks no
// command points here (only the region's CPLimit when the army spawns). The message's validation (RW 0x6B374F) needs a finished building with a
// SpawnArmy nugget. API guards of this port (retail's handler checks neither: its UI sends the order only then): the orders phase and the player's
// own region. INFERENCE (S-1326): a CREATE_A_HERO army is refused (canBuildUnit WB 0xD98FE0 hides it without a Create-a-Hero profile, which the
// strategic game here has none of)
bool LivingWorldLogic::canRecruit(int player, int region, int buildingSlot, int armyIndex, std::string *error) const
{
	if (m_phase != PHASE_ORDERS || m_finished)
	{
		setError(error, "recruitment is ordered only in the orders phase");
		return false;
	}
	if (region < 0 || region >= (int)m_regions.size() || m_regions[(size_t)region].owner != player)
	{
		setError(error, "not the player's region");
		return false;
	}
	const RegionState &rs = m_regions[(size_t)region];
	const Building *producer = nullptr;
	for (const Building &b : rs.buildings)
	{
		if (b.slot == buildingSlot)
		{
			producer = &b;
		}
	}
	const BuildingTemplate *bt = producer ? buildingTemplate(*producer) : nullptr;
	const BuildingNugget *nug = bt ? bt->findNugget(BuildingNugget::SPAWN_ARMY) : nullptr;
	if (!nug || producer->turnsLeft > 0)
	{
		setError(error, "no completed building there recruits armies");
		return false;
	}
	if ((int)queueOf(region, buildingSlot).size() >= nug->queueSize)
	{
		setError(error, "the building's queue is full");
		return false;
	}
	if (armyIndex < 0 || armyIndex >= (int)nug->armiesToSpawn.size())
	{
		setError(error, "no such army in the building's list");
		return false;
	}
	const SpawnArmy &spawn = nug->armiesToSpawn[(size_t)armyIndex];
	const PlayerArmy *pa = m_stores.findPlayerArmy(spawn.playerArmy);
	if (!pa || pa->entries.empty() || !m_units || !m_units(pa->entries.front().thingTemplate).known)
	{
		setError(error, "the army '" + spawn.playerArmy + "' or its first unit is unknown");
		return false;
	}
	const UnitInfo first = m_units(pa->entries.front().thingTemplate);
	if (first.createAHero)
	{
		setError(error, "[S-1326] a Create-a-Hero army needs a Create-a-Hero profile, which the strategic game has none of");
		return false;
	}
	if (first.hero)
	{
		const std::string id = spawnIdOf(*bt, armyIndex);
		const std::vector<RevivalUnit> &list = m_players[(size_t)player].buildableHeroes;
		if (std::none_of(list.begin(), list.end(), [&](const RevivalUnit &r) { return r.spawnId == id; })) // RW 0x6E2625
		{
			setError(error, "the hero is not in the player's buildable-hero list (alive, or not offered)");
			return false;
		}
		if (heroQueuedAnywhere(player, id)) // RW 0x6E24C0
		{
			setError(error, "the hero is already queued");
			return false;
		}
	}
	if (recruitCost(player, *pa) > treasury(player))
	{
		setError(error, "not enough resources");
		return false;
	}
	return true;
}

// LivingWorldBuildingNuggetSpawnArmy::queueUnit (WorldBuilder 0x1335260 / RW 0x8FFC4A): canQueueUnit; an empty queue restarts the head's turns; the army
// joins the queue; the player's queued command points grow (OnUnitQueued RW 0x6E1970: computed from the queues here) and the cost is paid now
bool LivingWorldLogic::orderRecruit(int player, int region, int buildingSlot, int armyIndex, std::string *error)
{
	if (!canRecruit(player, region, buildingSlot, armyIndex, error))
	{
		return false;
	}
	RegionState &rs = m_regions[(size_t)region];
	Building *producer = nullptr;
	for (Building &b : rs.buildings)
	{
		if (b.slot == buildingSlot)
		{
			producer = &b;
		}
	}
	const BuildingNugget *nug = buildingTemplate(*producer)->findNugget(BuildingNugget::SPAWN_ARMY);
	const SpawnArmy &spawn = nug->armiesToSpawn[(size_t)armyIndex];
	const PlayerArmy *pa = m_stores.findPlayerArmy(spawn.playerArmy);
	if (queueOf(region, buildingSlot).empty())
	{
		producer->turnsSpent = 0;
	}
	ProductionOrder o;
	o.buildingSlot = buildingSlot;
	o.armyIndex = armyIndex;
	rs.production.push_back(o);
	onUnitQueued(region, pa);                       // OnUnitQueued (RW 0x8FFCFF -> RW 0x6E1970)
	addTreasure(player, -recruitCost(player, *pa)); // AddMoney(-cost) (RW 0x6E1A40)
	log(m_players[(size_t)player].name + " recruits " + spawn.playerArmy + " in " + m_regionCampaign->regions[(size_t)region].name);
	return true;
}

// LivingWorldBuildingNuggetSpawnArmy::dequeueUnitAtIndex (WorldBuilder 0x1335520 / RW 0x8FF6F8): an index in the queue; index 0 restarts the head's
// turns; the army's cost for the player NOW is paid back in full (AddMoney, clipped by ScenarioMaxResources), its queued command points go
bool LivingWorldLogic::orderDequeueAt(int player, int region, int buildingSlot, int queueIndex, std::string *error)
{
	if (m_phase != PHASE_ORDERS || m_finished || region < 0 || region >= (int)m_regions.size() || m_regions[(size_t)region].owner != player)
	{
		setError(error, "not the player's region in the orders phase");
		return false;
	}
	RegionState &rs = m_regions[(size_t)region];
	Building *producer = nullptr;
	for (Building &b : rs.buildings)
	{
		if (b.slot == buildingSlot)
		{
			producer = &b;
		}
	}
	const BuildingTemplate *bt = producer ? buildingTemplate(*producer) : nullptr;
	const BuildingNugget *nug = bt ? bt->findNugget(BuildingNugget::SPAWN_ARMY) : nullptr;
	if (!nug || producer->turnsLeft > 0)
	{
		setError(error, "no completed building there recruits armies");
		return false;
	}
	const std::vector<size_t> queue = queueOf(region, buildingSlot);
	if (queueIndex < 0 || queueIndex >= (int)queue.size())
	{
		setError(error, "no such queue entry");
		return false;
	}
	const ProductionOrder o = rs.production[queue[(size_t)queueIndex]];
	if (queueIndex == 0)
	{
		producer->turnsSpent = 0;
	}
	if (o.armyIndex >= 0 && o.armyIndex < (int)nug->armiesToSpawn.size())
	{
		if (const PlayerArmy *pa = m_stores.findPlayerArmy(nug->armiesToSpawn[(size_t)o.armyIndex].playerArmy))
		{
			addTreasure(player, recruitCost(player, *pa));
			onUnitDequeued(region, pa); // RW 0x8FF7EF
		}
	}
	rs.production.erase(rs.production.begin() + (std::ptrdiff_t)queue[(size_t)queueIndex]);
	log(m_players[(size_t)player].name + " cancels a recruitment in " + m_regionCampaign->regions[(size_t)region].name);
	return true;
}

// LivingWorldBuildingNuggetSpawnArmy::dequeueUnit (WorldBuilder 0x1335770 / RW 0x8FF364): the LAST queue entry of that army, none: nothing
bool LivingWorldLogic::orderDequeue(int player, int region, int buildingSlot, int armyIndex, std::string *error)
{
	if (region < 0 || region >= (int)m_regions.size())
	{
		setError(error, "no such region");
		return false;
	}
	const std::vector<size_t> queue = queueOf(region, buildingSlot);
	for (size_t i = queue.size(); i-- > 0;)
	{
		if (m_regions[(size_t)region].production[queue[i]].armyIndex == armyIndex)
		{
			return orderDequeueAt(player, region, buildingSlot, (int)i, error);
		}
	}
	setError(error, "that army is not queued there");
	return false;
}

// LivingWorldRegion::DestroyBuilding(plot, true) (WorldBuilder 0x104F5C0 / RW 0x7F32F4): a building under construction pays back its cost for the player
// (RW 0x6E0B4A) and frees the region's construction lock; a finished one pays nothing; the SpawnArmy nugget's queue goes with it, its command points
// returned but not its money (RW 0x8FFF41). Not ported here: the UpgradeTroops marks it cancels (S-1326)
bool LivingWorldLogic::orderDestroyBuilding(int player, int region, int buildingSlot, std::string *error)
{
	if (m_phase != PHASE_ORDERS || m_finished || region < 0 || region >= (int)m_regions.size() || m_regions[(size_t)region].owner != player)
	{
		setError(error, "not the player's region in the orders phase");
		return false;
	}
	RegionState &rs = m_regions[(size_t)region];
	auto it = std::find_if(rs.buildings.begin(), rs.buildings.end(), [&](const Building &b) { return b.slot == buildingSlot; });
	if (it == rs.buildings.end())
	{
		setError(error, "no building in that plot");
		return false;
	}
	if (it->turnsLeft > 0)
	{
		if (const BuildingTemplate *bt = buildingTemplate(*it))
		{
			addTreasure(player, buildingCost(player, *bt));
		}
	}
	log(m_players[(size_t)player].name + " destroys " + it->templateName + " in " + m_regionCampaign->regions[(size_t)region].name);
	onBuildingDestroyed(region, *it);
	rs.buildings.erase(it);
	rs.production.erase(std::remove_if(rs.production.begin(), rs.production.end(), [&](const ProductionOrder &o) { return o.buildingSlot == buildingSlot; }),
		rs.production.end());
	return true;
}

bool LivingWorldLogic::canDisbandArmyMember(int armyId, int memberKey) const
{
	const Army *a = findArmy(armyId);
	if (!a || m_phase != PHASE_ORDERS)
	{
		return false;
	}
	for (const ArmyMember &m : a->members)
	{
		if (m.key == memberKey)
		{
			return !(m_units && m_units(m.thingTemplate).hero); // CanDisbandEntry RW 0x6B4876: not KindOf HERO (the thing must exist)
		}
	}
	return false;
}

bool LivingWorldLogic::disbandArmyMember(int player, int armyId, int memberKey, std::string *error)
{
	// retail's handler checks neither the owner nor the phase (its UI offers only own armies in phase 0): OpenBFME refuses a foreign or late order
	const Army *found = findArmy(armyId);
	if (!found || found->owner != player || !canDisbandArmyMember(armyId, memberKey))
	{
		setError(error, "that member cannot be disbanded");
		return false;
	}
	for (Army &a : m_armies)
	{
		if (a.id != armyId)
		{
			continue;
		}
		auto it = std::find_if(a.members.begin(), a.members.end(), [&](const ArmyMember &m) { return m.key == memberKey; });
		// RemoveDisbandedEntries RW 0x8108DC: once per entry, the entry's cost now for its owner (RW 0x80F018 on the entry's own thing, the rule
		// recruitCost applies to an army's first entry)
		PlayerArmy one;
		one.entries.push_back({ it->thingTemplate, 1 });
		const int refund = recruitCost(player, one);
		log(m_players[(size_t)player].name + " disbands " + it->thingTemplate + " of army " + std::to_string(armyId));
		cancelUpgrade(*it); // DisbandArmyMember RW 0x6B4933
		a.members.erase(it);
		addTreasure(player, refund);
		return true;
	}
	return false;
}

bool LivingWorldLogic::endTurn(int player, std::string *error)
{
	if (m_phase != PHASE_ORDERS || player < 0 || player >= (int)m_players.size())
	{
		setError(error, "not the orders phase");
		return false;
	}
	m_players[(size_t)player].endedTurn = true;
	return true;
}

bool LivingWorldLogic::canEndPhase(int player) const
{
	if (m_phase != PHASE_OWNERSHIP || m_finished || player < 0 || player >= (int)m_players.size())
	{
		return false;
	}
	for (int id : m_retreatList) // RW 0x6B3CC6: every army of the player in the list has a path
	{
		const Army *a = findArmy(id);
		if (a && a->owner == player && a->destination < 0)
		{
			return false;
		}
	}
	return true;
}

bool LivingWorldLogic::endPhase(int player, std::string *error)
{
	if (!canEndPhase(player))
	{
		setError(error, m_phase != PHASE_OWNERSHIP ? std::string("not the retreat phase") : std::string("an army still needs a retreat destination"));
		return false;
	}
	m_players[(size_t)player].endedTurn = true; // the handler RW 0x6BE6A6 case 0x6A6: + 0x344
	return true;
}

// ---- the phase machine ----

bool LivingWorldLogic::canAdvance() const
{
	switch (m_phase)
	{
		case PHASE_ORDERS:
			for (const PlayerState &p : m_players)
			{
				if (p.human && !p.defeated && !p.endedTurn)
				{
					return false;
				}
			}
			return true;
		case PHASE_BATTLES:
			for (const Battle &b : m_battles)
			{
				if (b.resolution != Battle::RESOLVED)
				{
					return false;
				}
			}
			return true;
		case PHASE_OWNERSHIP:
		{
			// canAdvance RW 0x6B5F78 (every army in the list has a path) and the barrier (RW 0x6BE20A): with a human's army in the list every human
			// sends END PHASE (0x6A6), unless the campaign has ForceAdvanceTurnPhase; otherwise the phase passes by itself
			bool humanArmy = false;
			for (int id : m_retreatList)
			{
				const Army *a = findArmy(id);
				if (a && a->destination < 0)
				{
					return false;
				}
				humanArmy = humanArmy || (a && m_players[(size_t)a->owner].human);
			}
			if (!humanArmy || m_campaign->forceAdvanceTurnPhase)
			{
				return true;
			}
			for (const PlayerState &p : m_players)
			{
				if (p.human && !p.defeated && !p.endedTurn)
				{
					return false;
				}
			}
			return true;
		}
		default:
			return true; // movement is instant in the logic (the client animates it); the settle counter is one update [S-1324]
	}
}

void LivingWorldLogic::update()
{
	if (!m_started)
	{
		return;
	}
	// at most one turn per call: the loop stops when a new turn's orders phase starts (a client animates each turn; tests step turn by turn)
	for (int guard = 0; guard < PHASE_COUNT + 1 && !m_finished && canAdvance(); ++guard)
	{
		advance();
		if (m_phase == PHASE_ORDERS)
		{
			break;
		}
	}
}

void LivingWorldLogic::advance()
{
	const int previous = m_phase;
	if (m_phase == PHASE_ORDERS)
	{
		m_ai->endOfPhase(); // the all-done barrier (RW 0x6BE20A): the AI's garrison orders run after every human ended the turn, still in phase 0
	}
	if (m_phase == PHASE_END_TURN)
	{
		// RW 0x6BDEC7: the turn listeners (the score keepers' collect, RW 0x8F3D4A) before turn+1, phase 0
		scoreCollectAll(retailTurn());
		m_retreatList.clear(); // the turn change clears the retreat list (RW 0x6BDEC7 -> RW 0x6B87E9); it lives through phases 4 .. 6
		++m_turn;
		m_phase = PHASE_ORDERS;
	}
	else
	{
		++m_phase;
	}
	m_phaseStart = m_frame; // WOTR2 milestone 8: L + 0x104 = L + 0x100 whenever a phase begins (RW 0x6BDF54 / RW 0x6BDF04)
	enterPhase(previous);
}

void LivingWorldLogic::enterPhase(int previous)
{
	if (previous == PHASE_ORDERS)
	{
		endOfOrdersUpgrades(); // the turn-phase listeners (0, 1) before the movement: RW 0x71AB1D / RW 0x8E4ACD
	}
	else if (previous == PHASE_MOVEMENT)
	{
		for (Army &a : m_armies)
		{
			for (ArmyMember &e : a.members)
			{
				e.homeArmy = 0; // RW 0x71AB1D (prev 1) -> RW 0x71AAB5: lent entries stay where they went after the movement
			}
		}
	}
	switch (m_phase)
	{
		case PHASE_ORDERS:
			for (PlayerState &p : m_players)
			{
				p.endedTurn = false;
			}
			for (Army &a : m_armies)
			{
				a.movedFrom = -1;
			}
			m_battles.clear();
			consolidateGarrisons(); // RW 0x6BDD98 entering phase 0: RW 0x6B546B
			autoMarkUnitsForUpgrades(); // then AutoMarkUnitsForUpgrades RW 0x6B5F1C (every player)
			m_ai->runOrdersPhase(); // the AI players' steps 0..4 and SubmitOrders (RW 0x900AA7, one AI after another in player order)
			break;
		case PHASE_MOVEMENT:
			doMovement();
			break;
		case PHASE_BATTLES:
			findBattles();
			for (Battle &b : m_battles)
			{
				bool humanInvolved = false;
				for (int id : b.armies)
				{
					const Army *a = findArmy(id);
					if (a && m_players[(size_t)a->owner].human)
					{
						humanInvolved = true;
					}
				}
				if (b.defender >= 0 && m_players[(size_t)b.defender].human)
				{
					humanInvolved = true;
				}
				if (!humanInvolved)
				{
					autoResolve(b);
				}
			}
			break;
		case PHASE_SETTLE:
			m_settleCounter = 0;
			break;
		case PHASE_OWNERSHIP:
			resolveOwnership();
			prepareRetreats(); // phase-enter 4 (RW 0x6BDD98 -> RW 0x6BABA8); retail applies the ownership on leaving phase 2 (RW 0x6BBBFD)
			m_ai->runRetreatPhase(); // the AI players' dispatch with phase 4 (ProcessRetreats RW 0x900533)
			for (PlayerState &p : m_players)
			{
				p.endedTurn = false; // the barrier cleared every player's done flag (+ 0x344) when phase 3 ended (RW 0x6BE20A)
			}
			// WOTR2 milestone 6: a human's hero army with several friendly regions waits in the list for its owner's move order (0x6A8 in phase 4, RW
			// 0x6B5847) and END PHASE (canAdvance). INFERENCE [S-1329]: retail cannot order a heroless army there (its allowance 1 against the battle
			// region's cost 2) and would wait forever; this port sends such an army to the first friendly region in connection order instead
			for (int id : m_retreatList)
			{
				for (Army &a : m_armies)
				{
					if (a.id != id || a.destination >= 0 || !a.garrison || !m_players[(size_t)a.owner].human)
					{
						continue;
					}
					for (const RegionConnection &c : m_regionCampaign->regions[(size_t)a.region].connections)
					{
						const int r = m_regionCampaign->regionIndex(c.region);
						const int o = r >= 0 ? m_regions[(size_t)r].owner : -1;
						if (o >= 0 && (o == a.owner || !hostile(o, a.owner)))
						{
							moveArmyToRegion(a, r);
							break;
						}
					}
				}
			}
			break;
		case PHASE_RETREAT:
			doRetreats(); // RW phase-enter 5: the army moves (RW 0x6B45C7), here the retreats
			break;
		case PHASE_END_TURN:
			endOfTurn();
			checkVictory();
			break;
		default:
			break;
	}
}

void LivingWorldLogic::prepareRetreat(int armyId)
{
	// a battle's losing army joins TheLivingWorldLogic + 0x10C (the battle results RW 0x8FC8A9 / RetreatArmiesForPlayer RW 0x7F7E9D); it leaves the
	// region (resolveOwnership skips it) and gets its destination in phase 4 (prepareRetreats, then the AI's ProcessRetreats)
	Army *a = nullptr;
	for (Army &x : m_armies)
	{
		if (x.id == armyId)
		{
			a = &x;
		}
	}
	if (!a || std::find(m_retreatList.begin(), m_retreatList.end(), armyId) != m_retreatList.end())
	{
		return;
	}
	a->retreating = true;
	a->destination = -1;
	m_retreatList.push_back(armyId);
}

void LivingWorldLogic::prepareRetreats()
{
	// PrepareRetreats RW 0x6BABA8 (phase-enter 4; WorldBuilder 0xDA0780), the list from its last army to its first: back to the region it came from
	// when that region's owner is its owner or an ally (RW 0x6E1940); else the connections of its region (in order, a region listed twice counts
	// twice) whose owner is: one -> there; none -> the army is destroyed (RW 0x6B9679, "LW:HeroKilledTitle" for a hero army); several -> it stays in
	// the list (the AI's ProcessRetreats, a human's order in phase 4). No draw.
	auto friendly = [&](int owner, int r) {
		const int o = m_regions[(size_t)r].owner;
		return o >= 0 && (o == owner || !hostile(o, owner));
	};
	for (size_t k = m_retreatList.size(); k-- > 0;)
	{
		Army *a = nullptr;
		for (Army &x : m_armies)
		{
			if (x.id == m_retreatList[k])
			{
				a = &x;
			}
		}
		if (!a)
		{
			m_retreatList.erase(m_retreatList.begin() + (std::ptrdiff_t)k);
			continue;
		}
		if (a->movedFrom >= 0 && a->movedFrom != a->region && friendly(a->owner, a->movedFrom))
		{
			moveArmyToRegion(*a, a->movedFrom);
			m_retreatList.erase(m_retreatList.begin() + (std::ptrdiff_t)k);
			continue;
		}
		int count = 0, last = -1;
		for (const RegionConnection &c : m_regionCampaign->regions[(size_t)a->region].connections)
		{
			const int r = m_regionCampaign->regionIndex(c.region);
			if (r >= 0 && friendly(a->owner, r))
			{
				++count;
				last = r;
			}
		}
		if (count == 1)
		{
			moveArmyToRegion(*a, last);
			m_retreatList.erase(m_retreatList.begin() + (std::ptrdiff_t)k);
		}
		else if (count == 0)
		{
			const int id = a->id;
			log(m_players[(size_t)a->owner].name + "'s army " + std::to_string(id) + " has nowhere to retreat and is lost (LW:HeroKilled)");
			StrategicNotice n; // every lost army, hero or not (RW 0x6BABA8: the name is empty without a hero)
			n.kind = StrategicNotice::HERO_KILLED;
			n.player = a->owner;
			n.hero = a->heroTemplate;
			m_notices.push_back(n);
			m_retreatList.erase(m_retreatList.begin() + (std::ptrdiff_t)k);
			removeArmy(id);
		}
	}
}

void LivingWorldLogic::moveArmyToRegion(Army &army, int region)
{
	// MoveArmyToRegion RW 0x6B31D8: no adjacency, cost or ownership check (the caller's); the army's own region: nothing
	if (region < 0 || region >= (int)m_regions.size() || army.region == region)
	{
		return;
	}
	army.destination = region;
	if (army.retreating)
	{
		log(m_players[(size_t)army.owner].name + "'s army " + std::to_string(army.id) + " retreats to " + m_regionCampaign->regions[(size_t)region].name);
	}
}

void LivingWorldLogic::doRetreats()
{
	for (Army &a : m_armies)
	{
		if (a.retreating && a.destination >= 0)
		{
			a.movedFrom = a.region;
			a.region = a.destination;
		}
		a.destination = a.retreating ? -1 : a.destination;
		a.retreating = false;
	}
}

void LivingWorldLogic::checkHeadToHeadBattles()
{
	// CheckHeadToHeadBattles RW 0x6BD97E (WOTR2 milestone 5, spec M 5.4): per team (ascending, RW 0x6B759F), its undefeated players in order, their
	// armies with a hero (a garrison or a heroless army never meets) that move, grouped by (from, to) (RW 0x6BD2B2); a group of one team and a group of
	// a later team moving into each other's region (RW 0x6B3569) meet: the losing group's moves are cancelled (RW 0x71B12F) and its armies listed for
	// their owners' "LW:AttackRepelled" box. AWins RW 0x6B44FF: the groups' command points without their heroes' (RW 0x6B4440), then the best hero
	// rank (RW 0x6B4494: the hero entry's + 0x0C), then GetGameLogicRandomValue(0, 1) == 0
	struct Group
	{
		int from = -1, to = -1;
		std::vector<Army *> armies;
	};
	std::set<int> teamIds;
	for (const PlayerState &ps : m_players)
	{
		if (!ps.defeated)
		{
			teamIds.insert(ps.team);
		}
	}
	std::vector<std::vector<Group>> teams;
	for (int team : teamIds)
	{
		std::vector<Group> groups;
		for (int p = 0; p < (int)m_players.size(); ++p)
		{
			if (m_players[(size_t)p].team != team || m_players[(size_t)p].defeated)
			{
				continue;
			}
			for (Army &a : m_armies) // INFERENCE [S-1324]: the player's army vector (+ 0x1E4) in the game's army order
			{
				if (a.owner != p || a.heroTemplate.empty() || a.destination < 0 || a.region < 0)
				{
					continue;
				}
				auto g = std::find_if(groups.begin(), groups.end(), [&](const Group &x) { return x.from == a.region && x.to == a.destination; });
				if (g == groups.end())
				{
					groups.push_back(Group{ a.region, a.destination, {} });
					g = groups.end() - 1;
				}
				g->armies.push_back(&a);
			}
		}
		teams.push_back(groups);
	}
	auto strength = [&](const Group &g) {
		int cp = 0;
		for (const Army *a : g.armies)
		{
			cp += membersCommandPoints(*a) - (m_units ? m_units(a->heroTemplate).commandPoints : 0);
		}
		return cp;
	};
	auto heroRank = [&](const Group &g) {
		int best = 0;
		for (const Army *a : g.armies)
		{
			for (const ArmyMember &m : a->members)
			{
				if (m.thingTemplate == a->heroTemplate)
				{
					best = std::max(best, m.rank);
					break;
				}
			}
		}
		return best;
	};
	std::map<int, std::vector<std::pair<std::string, int>>> repelled; // per owner, collection order
	for (size_t i = 0; i < teams.size(); ++i)
	{
		for (size_t j = i + 1; j < teams.size(); ++j)
		{
			for (Group &ga : teams[i])
			{
				for (Group &gb : teams[j])
				{
					if (ga.from != gb.to || ga.to != gb.from)
					{
						continue;
					}
					bool aWins;
					const int sa = strength(ga), sb = strength(gb);
					if (sa != sb)
					{
						aWins = sa > sb;
					}
					else
					{
						const int ma = heroRank(ga), mb = heroRank(gb);
						aWins = ma != mb ? ma > mb
						                 : m_random.getValue(0, 1, "E:\\Builds\\BFME2X\\Code\\production\\Code\\GameEngine\\Source\\GameLogic\\System\\LivingWorld\\LivingWorldLogic.cpp", 0x1B5E) == 0;
					}
					for (Army *x : (aWins ? gb : ga).armies)
					{
						if (x->destination >= 0)
						{
							log(m_players[(size_t)x->owner].name + "'s army " + std::to_string(x->id) + ": attack repelled on the way to " +
								m_regionCampaign->regions[(size_t)x->destination].name + " (LW:AttackRepelled)");
							x->destination = -1;
						}
						repelled[x->owner].push_back(std::make_pair(x->heroTemplate, x->region));
					}
				}
			}
		}
	}
	for (const auto &r : repelled)
	{
		StrategicNotice n;
		n.kind = StrategicNotice::ATTACK_REPELLED;
		n.player = r.first;
		n.repelled = r.second;
		m_notices.push_back(n);
	}
}

void LivingWorldLogic::doMovement()
{
	checkHeadToHeadBattles(); // entering phase 1 (RW 0x6BDD98 -> RW 0x6BD97E), then the moves (RW 0x6B45C7)
	for (Army &a : m_armies)
	{
		if (a.destination >= 0)
		{
			a.movedFrom = a.region;
			a.region = a.destination;
			a.destination = -1;
			log(m_players[(size_t)a.owner].name + "'s army " + std::to_string(a.id) + " moves to " + m_regionCampaign->regions[(size_t)a.region].name);
		}
	}
	consolidateGarrisons(); // leaving phase 1 (RW 0x6BDD98): RW 0x6B546B
}

void LivingWorldLogic::findBattles()
{
	m_battles.clear();
	for (int r = 0; r < (int)m_regions.size(); ++r)
	{
		std::vector<int> present; // players with armies here, first-army order
		std::vector<int> armies;
		int attacker = -1;
		for (const Army &a : m_armies)
		{
			if (a.region != r)
			{
				continue;
			}
			armies.push_back(a.id);
			if (std::find(present.begin(), present.end(), a.owner) == present.end())
			{
				present.push_back(a.owner);
			}
			if (a.movedFrom >= 0 && attacker < 0)
			{
				attacker = a.owner;
			}
		}
		if (present.empty())
		{
			continue;
		}
		const int owner = m_regions[(size_t)r].owner;
		bool contested = false;
		for (size_t i = 0; i < present.size() && !contested; ++i)
		{
			for (size_t j = i + 1; j < present.size(); ++j)
			{
				if (hostile(present[i], present[j]))
				{
					contested = true;
					break;
				}
			}
		}
		bool defendedByBuildings = false;
		if (!contested && owner >= 0 && hostile(present[0], owner))
		{
			for (const Building &b : m_regions[(size_t)r].buildings)
			{
				const BuildingTemplate *t = buildingTemplate(b);
				if (t && t->canDefendTerritory && b.turnsLeft == 0)
				{
					defendedByBuildings = true;
				}
			}
		}
		if (!contested && !defendedByBuildings)
		{
			continue;
		}
		Battle b;
		b.region = r;
		b.attacker = attacker >= 0 ? attacker : present[0];
		b.defender = -1;
		if (owner >= 0 && hostile(owner, b.attacker))
		{
			b.defender = owner;
		}
		for (int p : present)
		{
			if (b.defender < 0 && hostile(p, b.attacker))
			{
				b.defender = p;
			}
		}
		b.armies = armies;
		std::vector<int> players = present;
		if (owner >= 0 && owner == b.defender && std::find(players.begin(), players.end(), owner) == players.end())
		{
			players.push_back(owner);
		}
		for (int p : players) // the sides by team (RW 0x7F5BBD's grouping), in first-player order
		{
			auto side = std::find_if(b.sides.begin(), b.sides.end(), [&](const Battle::Side &s) { return s.team == m_players[(size_t)p].team; });
			if (side == b.sides.end())
			{
				b.sides.push_back(Battle::Side{ m_players[(size_t)p].team, {} });
				side = b.sides.end() - 1;
			}
			Battle::SidePlayer sp;
			sp.player = p;
			side->players.push_back(sp);
		}
		m_battles.push_back(b);
		log("battle in " + m_regionCampaign->regions[(size_t)r].name + ": " + m_players[(size_t)b.attacker].name + " attacks " +
			(b.defender >= 0 ? m_players[(size_t)b.defender].name : std::string("?")));
	}
}

int LivingWorldLogic::pendingHumanBattle() const
{
	if (m_phase != PHASE_BATTLES)
	{
		return -1;
	}
	for (size_t i = 0; i < m_battles.size(); ++i)
	{
		if (m_battles[i].resolution == Battle::PENDING)
		{
			return (int)i;
		}
	}
	return -1;
}

LivingWorldLogic::HudStats LivingWorldLogic::hudStats(int player) const
{
	HudStats h;
	if (player < 0 || player >= (int)m_players.size())
	{
		return h;
	}
	const PlayerBonuses b = playerBonuses(player);
	h.attack = b.attack;
	h.defense = b.defense;
	h.experience = b.experience;
	const int limit = -100 - SimMath::truncToInt32(SimMath::mulf32(m_resourceMultiplierLimit, -100.0f)); // RW 0x6E1F2F: mulss by -100.0 (RW 0xBE5988), cvttss2si
	h.resourceMultiplierPercent = b.resource < limit ? b.resource : limit;
	h.powerPoints = m_players[(size_t)player].powerPoints + b.legendary; // RW 0x6E185A: + 0x1F0 + 0x2A0
	return h;
}

LivingWorldLogic::PlayerBonuses LivingWorldLogic::playerBonuses(int player) const
{
	// [S-1327 inference] the LivingWorldPlayer bonus totals (+0x294 struct, summed by RW 0x6E19A1, not decompiled): every owned region's bonuses plus
	// every ConcurrentRegionBonus whose regions the player all owns
	PlayerBonuses b;
	for (size_t r = 0; r < m_regions.size(); ++r)
	{
		if (m_regions[r].owner != player)
		{
			continue;
		}
		const Region &rg = m_regionCampaign->regions[r];
		b.resource += rg.resourceBonus;
		b.army += rg.armyBonus;
		b.legendary += rg.legendaryBonus;
		b.attack += rg.attackBonus;
		b.defense += rg.defenseBonus;
		b.experience += rg.experienceBonus;
		b.fertile += rg.fertileTerritoryBonus;
	}
	for (const ConcurrentRegionBonus &c : m_regionCampaign->concurrentRegionBonuses)
	{
		bool all = !c.regions.empty();
		for (const std::string &name : c.regions)
		{
			const int idx = m_regionCampaign->regionIndex(name);
			if (idx < 0 || m_regions[(size_t)idx].owner != player)
			{
				all = false;
				break;
			}
		}
		if (!all)
		{
			continue;
		}
		b.resource += c.resourceBonus;
		b.army += c.armyBonus;
		b.legendary += c.legendaryBonus;
		b.attack += c.attackBonus;
		b.defense += c.defenseBonus;
		b.experience += c.experienceBonus;
		b.extraStartResources += c.extraStartResourcesBonus;
		b.buildingDiscount += c.buildingDiscountBonus;
		b.freeInnUnits += c.freeInnUnitsBonus;
		b.discountedSiegeUnits += c.discountedSeigeUnitsBonus;
		b.discountedHeroUnits += c.discountedHeroUnitsBonus;
		b.freeBuilder += c.freeBuilderBonus;
		b.discountedBarracksUnits += c.discountedBarracksUnitsBonus;
	}
	return b;
}

namespace
{
// RW 0x7F37CD: (float)percent * 0.01f + 1.0f (x87, PC24)
float percentFactor(int percent)
{
	return NumericState::pc24Add(NumericState::pc24Mul((float)percent, 0.01f), 1.0f);
}
} // namespace

std::vector<AutoResolvePlayerSpec> LivingWorldLogic::autoResolveSpecs(const Battle &battle) const
{
	// StartAutoResolveBattle (RW 0x6BDF67) -> SetUpAutoResolveBattle (RW 0x6BD855 / 0x6BD3F3), the battle (LivingWorldAutoResolveBattle.h), applyResults
	const RegionState &rs = m_regions[(size_t)battle.region];
	// the battle's players: the armies' owners in battle order, and the region owner when it defends without an army
	std::vector<int> players;
	for (int id : battle.armies)
	{
		const Army *a = findArmy(id);
		if (a && std::find(players.begin(), players.end(), a->owner) == players.end())
		{
			players.push_back(a->owner);
		}
	}
	if (rs.owner >= 0 && std::find(players.begin(), players.end(), rs.owner) == players.end())
	{
		players.push_back(rs.owner);
	}
	if (players.empty())
	{
		return {};
	}
	// RW 0x7F635E: the reference player is the region owner when it is in the battle, else the first player; side 0 is its team
	const int ref = std::find(players.begin(), players.end(), rs.owner) != players.end() ? rs.owner : players.front();
	return autoResolveSpecsFor(battle.region, players, ref, battle.armies, true);
}

std::vector<AutoResolvePlayerSpec> LivingWorldLogic::autoResolveSpecsFor(int r, const std::vector<int> &players, int ref, const std::vector<int> &armies,
	bool trackers) const
{
	// SetUpAutoResolveBattle RW 0x6BD3F3 (players, owner, armies, region, records, trackers): one record per player in order, its side
	// !isAllied(player, owner) where the owner is the reference (the first player when none), each army of the list owned by the player (RW 0x71A625),
	// and the trackers only when the caller passes them (the AI's test battles WinTestBattle RW 0x9A4EBE pass none)
	const Region &region = m_regionCampaign->regions[(size_t)r];
	const AutoResolveStores &ar = m_stores.autoResolve;
	std::vector<AutoResolvePlayerSpec> specs;
	for (int p : players)
	{
		AutoResolvePlayerSpec s;
		s.player = p;
		s.side = hostile(p, ref) ? 1 : 0; // RW 0x6E1940: the same player or the same team
		const PlayerBonuses pb = playerBonuses(p);
		const PlayerTemplate &pt = playerTemplate(p);
		// the battle bonus (RW 0x60E933 over the regions the player owns -> RW 0x7F37CD; RW 0x7F9242 the final bonuses)
		std::map<std::string, int> keyCounts;
		float extraW = 1.0f, extraA = 1.0f, extraX = 1.0f;
		for (size_t ri = 0; ri < m_regions.size(); ++ri)
		{
			if (m_regions[ri].owner != p)
			{
				continue;
			}
			for (const Building &b : m_regions[ri].buildings)
			{
				const BuildingTemplate *t = buildingTemplate(b);
				if (!t || b.turnsLeft != 0)
				{
					continue;
				}
				for (const BuildingNugget &n : t->nuggets)
				{
					// [S-1326 inference] the StrengthenArmy nugget's RecordBonus (vslot 0x14): THIS_TERRIORITY counts in its own region only
					if (n.kind == BuildingNugget::STRENGTHEN_ARMY && (n.strengtheningRange == 1 || (int)ri == r))
					{
						++keyCounts[n.bonusKey];
					}
				}
				if ((int)ri == r && t->createUnitDuringAutoResolve && !t->battleThingTemplate.empty()) // RW 0x8E4C75
				{
					AutoResolveUnitSpec u;
					u.thingTemplate = t->battleThingTemplate;
					u.buildingRegion = r;
					u.buildingSlot = b.slot;
					s.extraUnits.push_back(u);
				}
			}
			if ((int)ri == r)
			{
				extraW = NumericState::pc24Mul(percentFactor(region.attackBonus), extraW); // RW 0x7F8C43 / 0x7F8C56 / 0x7F8C7E
				extraA = NumericState::pc24Mul(percentFactor(region.defenseBonus), extraA);
				extraX = NumericState::pc24Mul(percentFactor(region.experienceBonus), extraX);
				if (region.createAutoFort) // RW 0x7F37CD +0x13C: the player's FORTRESS buildings that make a unit
				{
					for (const BuildingTemplate &t : m_stores.buildings)
					{
						if (t.type == BUILDING_FORTRESS && t.createUnitDuringAutoResolve &&
							std::find(t.availableTo.begin(), t.availableTo.end(), pt.name) != t.availableTo.end())
						{
							AutoResolveUnitSpec u;
							u.thingTemplate = t.battleThingTemplate;
							s.extraUnits.push_back(u);
						}
					}
				}
			}
		}
		extraW = NumericState::pc24Mul(percentFactor(pb.attack), extraW); // RW 0x6BD4EF
		extraA = NumericState::pc24Mul(percentFactor(pb.defense), extraA);
		extraX = NumericState::pc24Mul(percentFactor(pb.experience), extraX);
		float w = 1.0f, a = 1.0f, x = 1.0f;
		for (const auto &kc : keyCounts) // RW 0x7F9242
		{
			const BuildingNugget::Bonus *best = nullptr;
			for (const BuildingTemplate &t : m_stores.buildings)
			{
				for (const BuildingNugget &n : t.nuggets)
				{
					if (n.kind != BuildingNugget::STRENGTHEN_ARMY || n.bonusKey != kc.first)
					{
						continue;
					}
					for (const BuildingNugget::Bonus &bb : n.bonuses)
					{
						if (bb.minCount <= kc.second && (!best || bb.minCount > best->minCount))
						{
							best = &bb;
						}
					}
				}
			}
			if (best)
			{
				w = NumericState::pc24Mul(best->weapon, w);
				a = NumericState::pc24Mul(best->armor, a);
				x = NumericState::pc24Mul(best->experience, x);
			}
		}
		s.weapon = NumericState::pc24Mul(extraW, w);
		s.armor = NumericState::pc24Mul(extraA, a);
		s.experience = NumericState::pc24Mul(extraX, x);
		if (pb.freeInnUnits > 0 && !pt.factionInnUnitTemplateName.empty()) // RW 0x6BD5A4: one entry of FactionInnUnitTemplateName, count = the bonus
		{
			AutoResolveUnitSpec u;
			u.thingTemplate = pt.factionInnUnitTemplateName;
			u.count = pb.freeInnUnits;
			s.extraUnits.push_back(u);
		}
		if (const AutoResolveBonusSchedule *rb = ar.resourceScheduleFor(pt.name)) // RW 0x824722 / 0x8248BC on the resource bonus total
		{
			if (const AutoResolveBonusSchedule::Bonus *b = rb->bonusFor(pb.resource))
			{
				s.weapon = SimMath::mulf32(s.weapon, b->weapon);
				s.armor = SimMath::mulf32(s.armor, b->armor);
			}
		}
		const AutoResolveHandicap h = ar.handicapFor(m_players[(size_t)p].handicap); // RW 0x823DDA
		s.weapon = SimMath::mulf32(s.weapon, h.weapon);
		s.armor = SimMath::mulf32(s.armor, h.armor);
		s.experience = SimMath::mulf32(s.experience, h.experience);
		// the resolver's tracker of the player (lane WOTR-2, RW 0x6BD73A .. 0x6BD7DF; the resolver's array RW 0x7F7CDC .. 0x7F7D07, element
		// constructor RW 0x6B2FD7): the record's schedule, the template its Faction names, the player's skill points (+ 0x1F4, no scalar), the rank
		// they reach as the scalar table's base rank, then the legendary bonus total (+ 0x2A0) through slot 2
		if (trackers)
		{
			s.scienceSchedule = ar.scienceScheduleFor(pt.name); // RW 0x824722
			SciencePurchasePointTracker tracker(m_ranks);
			const auto rt = m_rankTemplates.find(pt.faction); // RW 0x6BD755 .. 0x6BD76E: nameToKey(template + 4), RW 0x5FCA2E
			if (rt != m_rankTemplates.end())
			{
				tracker.setTemplate(rt->second.maxLevelSP, rt->second.intrinsicSciencePurchasePoints); // RW 0x6BD784: + 0x154
			}
			tracker.addSkillPoints(m_players[(size_t)p].experience, false); // RW 0x6BD79B
			tracker.setScalarBaseRank(tracker.getRankLevel());              // RW 0x6BD7AD (RW 0x71C311: + 0x18 = + 0x14)
			tracker.addSciencePurchasePoints(pb.legendary);                 // RW 0x6BD7C6 .. 0x6BD7D2 (RW 0x441BBD)
			s.rankTracker = tracker;
		}
		for (int id : armies)
		{
			const Army *army = findArmy(id);
			if (!army || army->owner != p)
			{
				continue;
			}
			AutoResolveArmySpec as;
			as.armyId = id;
			as.garrison = army->garrison;
			for (size_t m = 0; m < army->members.size(); ++m)
			{
				for (int q = 0; q < army->members[m].quantity; ++q)
				{
					AutoResolveUnitSpec u;
					u.thingTemplate = army->members[m].thingTemplate;
					u.experience = army->members[m].experience;
					u.upgrades = army->members[m].upgrades; // the entry's set + 0x10 picks the AutoResolveArmor / Weapon (RW 0x73C1E5 / 0x73C222)
					u.armyId = id;
					u.memberIndex = (int)m;
					as.units.push_back(u);
				}
			}
			s.armies.push_back(as);
		}
		specs.push_back(s);
	}
	return specs;
}

void LivingWorldLogic::autoResolve(Battle &battle)
{
	// StartAutoResolveBattle (RW 0x6BDF67) -> SetUpAutoResolveBattle (RW 0x6BD855 / 0x6BD3F3), the battle (LivingWorldAutoResolveBattle.h), applyResults
	const std::vector<AutoResolvePlayerSpec> specs = autoResolveSpecs(battle);
	if (specs.empty())
	{
		battle.resolution = Battle::RESOLVED;
		return;
	}
	AutoResolveBattle fight(m_stores.autoResolve, m_units, specs, m_random);
	fight.runToEnd();
	applyAutoResolve(fight, specs, battle, -1);
	// AutoBattleResolver::Update after applyResults (RW 0x6BC345 .. 0x6BC3C7, lane WOTR-2): each record's player takes its resolver tracker's skill
	// points (+ 0x1F4) and its purchase points less the legendary bonus total read now (+ 0x1F0 through RW 0x619071). applyResults gave the battle's
	// skill points only to a temporary copy (LivingWorldAutoResolveBattle.h), so these are the values the setup computed
	for (const AutoResolvePlayerSpec &s : specs)
	{
		PlayerState &ps = m_players[(size_t)s.player];
		ps.experience = s.rankTracker->getSkillPoints();
		ps.powerPoints = s.rankTracker->getSciencePurchasePoints() - playerBonuses(s.player).legendary;
	}
}

void LivingWorldLogic::countBattleResults(const AutoResolveBattle &fight, const std::vector<AutoResolvePlayerSpec> &specs, Battle &battle)
{
	// RW 0x7F6443 (spec N 2.3): the winning side is the LAST side with a player on the autoresolve's winning side; per kill event the victim's
	// player loses a unit and the killer's destroys one; a dead hero of a player's army is a hero lost; per army that fought, each survivor that
	// reached a rank above 1 here is a veteran, and on a side that did not win each survivor of the army's hero template is a hero lost too
	auto sideOf = [&](int player) {
		for (const AutoResolvePlayerSpec &s : specs)
		{
			if (s.player == player)
			{
				return s.side;
			}
		}
		return -1;
	};
	auto entry = [&](int player) -> Battle::SidePlayer * {
		for (Battle::Side &s : battle.sides)
		{
			for (Battle::SidePlayer &p : s.players)
			{
				if (p.player == player)
				{
					return &p;
				}
			}
		}
		return nullptr;
	};
	battle.winningSide = -1;
	for (size_t i = 0; i < battle.sides.size(); ++i)
	{
		for (const Battle::SidePlayer &p : battle.sides[i].players)
		{
			if (fight.winner() >= 0 && sideOf(p.player) == fight.winner())
			{
				battle.winningSide = (int)i;
			}
		}
	}
	for (const AutoResolveBattle::KillEvent &k : fight.killEvents())
	{
		if (Battle::SidePlayer *v = entry(specs[(size_t)k.victimRecord].player))
		{
			++v->unitsLost;
		}
		if (Battle::SidePlayer *x = entry(specs[(size_t)k.killerRecord].player))
		{
			++x->enemyDestroyed;
		}
	}
	for (const AutoResolveUnitSpec &dead : fight.deadHeroes())
	{
		const Army *a = findArmy(dead.armyId);
		if (Battle::SidePlayer *p = a ? entry(a->owner) : nullptr)
		{
			++p->heroesLost;
		}
	}
	const std::vector<AutoResolveBattle::Survivor> survivors = fight.survivors();
	for (int id : fight.enteredArmies())
	{
		const Army *a = findArmy(id);
		Battle::SidePlayer *p = a ? entry(a->owner) : nullptr;
		if (!p)
		{
			continue;
		}
		bool onWinningSide = false;
		if (battle.winningSide >= 0)
		{
			for (const Battle::SidePlayer &w : battle.sides[(size_t)battle.winningSide].players)
			{
				onWinningSide = onWinningSide || w.player == a->owner;
			}
		}
		for (const AutoResolveBattle::Survivor &sv : survivors)
		{
			if (sv.spec.armyId != id)
			{
				continue;
			}
			p->veterancy += sv.rankedUp ? 1 : 0;
			if (!onWinningSide && !a->heroTemplate.empty() && sv.spec.thingTemplate == a->heroTemplate)
			{
				++p->heroesLost;
			}
		}
	}
	battle.resultsCounted = true;
}

void LivingWorldLogic::applyAutoResolve(const AutoResolveBattle &fight, const std::vector<AutoResolvePlayerSpec> &specs, Battle &battle, int retreatingPlayer)
{
	const int r = battle.region;
	const Region &region = m_regionCampaign->regions[(size_t)r];
	const int winnerSide = fight.winner();
	int winner = -1;
	for (const AutoResolvePlayerSpec &s : specs)
	{
		// a retreat (RW 0x105AAC0) ends the battle for the retreating player: the other side holds the field
		if (retreatingPlayer >= 0 ? hostile(s.player, retreatingPlayer) : s.side == winnerSide)
		{
			winner = s.player;
			break;
		}
	}
	if (retreatingPlayer < 0)
	{
		countBattleResults(fight, specs, battle); // RW 0x6BC274 -> RW 0x7F6443 (the armies of the losers still stand: they only queue retreats)
		scoreAutoResolve(fight, specs, battle);   // then the battle-completed listeners (RW 0x8F2C55)
	}
	// lane WOTR-2: the dead heroes return to their owners' buildable-hero lists with their experience (applyResults WB 0x1319DE0 -> AddRevivalUnit)
	for (const AutoResolveUnitSpec &dead : fight.deadHeroes())
	{
		const Army *a = findArmy(dead.armyId);
		if (a && dead.memberIndex >= 0 && dead.memberIndex < (int)a->members.size())
		{
			addRevivalUnit(a->owner, a->members[(size_t)dead.memberIndex], dead.experience);
		}
	}
	// applyResults (RW 0x8FC8A9): destroyed buildings, then every army that entered rebuilt from its survivors
	for (const AutoResolveBattle::DestroyedBuilding &d : fight.destroyedBuildings())
	{
		std::vector<Building> &bs = m_regions[(size_t)d.region].buildings;
		for (Building &b : bs)
		{
			if (b.slot == d.slot)
			{
				onBuildingDestroyed(d.region, b); // the destroyed building's nuggets (RW 0x9D3881 / 0x8FFF41)
			}
		}
		bs.erase(std::remove_if(bs.begin(), bs.end(), [&](const Building &b) { return b.slot == d.slot; }), bs.end());
	}
	const std::vector<AutoResolveBattle::Survivor> survivors = fight.survivors();
	std::vector<int> destroyed;
	for (int id : fight.enteredArmies())
	{
		for (Army &a : m_armies)
		{
			if (a.id != id)
			{
				continue;
			}
			std::vector<ArmyMember> members;
			std::vector<int> usedKeys;
			for (const AutoResolveBattle::Survivor &sv : survivors)
			{
				if (sv.spec.armyId != id)
				{
					continue;
				}
				const bool known = sv.spec.memberIndex >= 0 && sv.spec.memberIndex < (int)a.members.size();
				const std::string spawnId = known ? a.members[(size_t)sv.spec.memberIndex].spawnId : std::string();
				const ArmyMember *from = known ? &a.members[(size_t)sv.spec.memberIndex] : nullptr;
				if (!members.empty() && members.back().thingTemplate == sv.spec.thingTemplate && members.back().experience == sv.spec.experience &&
					members.back().spawnId == spawnId && from && members.back().key == from->key)
				{
					++members.back().quantity;
					continue;
				}
				ArmyMember m;
				m.thingTemplate = sv.spec.thingTemplate;
				m.quantity = 1;
				m.experience = sv.spec.experience;
				m.spawnId = spawnId;
				// WOTR2 milestone 4: a surviving entry keeps its key; [INFERENCE S-1330] an entry whose survivors part (different experience) gives
				// the later part the army's next key (retail keeps one experience per entry: the units of one entry never part there)
				const int key = known ? a.members[(size_t)sv.spec.memberIndex].key : 0;
				m.homeArmy = known ? a.members[(size_t)sv.spec.memberIndex].homeArmy : 0;
				if (from)
				{
					m.upgrades = from->upgrades; // WOTR2 milestone 5: the entry keeps its upgrades and mark
					m.upgradeBuilding = from->upgradeBuilding;
				}
				m.rank = sv.level; // the summary entry takes the autoresolve unit's level (RW 0x9EE009)
				if (known && std::find(usedKeys.begin(), usedKeys.end(), key) == usedKeys.end())
				{
					m.key = key;
				}
				else
				{
					m.key = a.nextKey++;
				}
				usedKeys.push_back(m.key);
				members.push_back(m);
			}
			a.members = members;
			if (a.members.empty())
			{
				destroyed.push_back(id);
			}
		}
	}
	for (int id : destroyed)
	{
		removeArmy(id);
	}
	// RW LivingWorldLogic::PrepareRetreats (WorldBuilder 0xDA0780): the losing side's remaining armies retreat; after a chosen retreat
	// (RetreatArmiesForPlayer 0x105AAC0) the retreating player's garrisons are lost (0xDA2DE0) and its hero armies retreat (0xD99AF0)
	for (int id : battle.armies)
	{
		const Army *a = findArmy(id);
		if (!a)
		{
			continue;
		}
		if (retreatingPlayer >= 0)
		{
			if (a->owner != retreatingPlayer)
			{
				continue;
			}
			if (a->garrison)
			{
				log(m_players[(size_t)a->owner].name + "'s garrison " + std::to_string(id) + " is lost covering the retreat");
				removeArmy(id);
				continue;
			}
			prepareRetreat(id);
		}
		else if (winner >= 0 && hostile(a->owner, winner))
		{
			prepareRetreat(id);
		}
	}
	battle.winner = winner;
	battle.autoResolveRounds = fight.round();
	battle.resolution = Battle::RESOLVED;
	log("autoresolve in " + region.name + ": " + (winner >= 0 ? m_players[(size_t)winner].name : std::string("nobody")) + " wins after " +
		std::to_string(fight.round()) + " rounds (" + std::to_string(fight.killCount(0)) + " / " + std::to_string(fight.killCount(1)) + " kills, " +
		std::to_string(fight.randomDraws()) + " rolls)");
}

int LivingWorldLogic::resolutionOptions(int battleIndex, int player) const
{
	// RW LivingWorldBattle::GetResolutionOptions (WorldBuilder 0x105B310), single player: a campaign map region (MapIsCampaignMap) is fought only in real
	// time (8); otherwise autoresolve (2), real time (8) and, for a player in the battle with a friendly region next to it that it does not need to keep
	// (0xD9E820, WillPlayerBeEliminatedIfHeOrSheLosesThisRegion: here its last region or a capital its defeat condition names [inference]), retreat (4)
	if (battleIndex < 0 || battleIndex >= (int)m_battles.size())
	{
		return 0;
	}
	const Battle &b = m_battles[(size_t)battleIndex];
	const Region &region = m_regionCampaign->regions[(size_t)b.region];
	if (region.mapIsCampaignMap)
	{
		return RESOLVE_REAL_TIME;
	}
	int bits = RESOLVE_AUTO | RESOLVE_REAL_TIME;
	bool inBattle = false;
	for (int id : b.armies)
	{
		const Army *a = findArmy(id);
		inBattle = inBattle || (a && a->owner == player);
	}
	if (inBattle && player >= 0)
	{
		bool friendlyNeighbour = false;
		for (const RegionConnection &c : region.connections)
		{
			const int r = m_regionCampaign->regionIndex(c.region);
			const int o = r >= 0 ? m_regions[(size_t)r].owner : -1;
			friendlyNeighbour = friendlyNeighbour || (o >= 0 && (o == player || !hostile(o, player)));
		}
		bool vital = false;
		if (m_regions[(size_t)b.region].owner == player)
		{
			vital = regionCount(player) <= 1;
			for (const Condition &c : m_campaign->scenario->playerDefeat)
			{
				vital = vital || (c.loseIfCapitalLost && m_players[(size_t)player].capital == b.region);
			}
		}
		if (friendlyNeighbour && !vital)
		{
			bits |= RESOLVE_RETREAT;
		}
	}
	return bits;
}

bool LivingWorldLogic::chooseRetreat(int battleIndex, int player, std::string *error)
{
	// RW LivingWorldBattle::RetreatArmiesForPlayer (WorldBuilder 0x105AAC0): an autoresolve in which the retreating player's record deals no damage
	// (+0x18 weapon = 0) runs ArmyRetreatRounds rounds (the region campaign's +0x58), then its results; the player's garrisons are lost, its hero armies retreat
	if (m_phase != PHASE_BATTLES || battleIndex < 0 || battleIndex >= (int)m_battles.size() || m_battles[(size_t)battleIndex].resolution != Battle::PENDING)
	{
		setError(error, "no pending battle with that index");
		return false;
	}
	if ((resolutionOptions(battleIndex, player) & RESOLVE_RETREAT) == 0)
	{
		setError(error, "the player cannot retreat from this battle");
		return false;
	}
	aiBattleVotes(battleIndex, player);
	return true;
}

bool LivingWorldLogic::aiBattleVotes(int battleIndex, int humanRetreat)
{
	// the LivingWorld message 0x6A7 (a human's battle choice, RW 0x6BE6A6): every battle player's choice is tallied, an AI player's through
	// LivingWorldAI::GetBattleChoice (RW 0x9001FB: retreat or no opinion, after a test battle); with a retreat among them every player is asked
	// again (an AI's test battle runs a second time) in player order and each answering retreat retreats (RetreatArmiesForPlayer RW 0x7F7E9D).
	// INFERENCE [S-1330]: the battle players in the autoresolve set-up's order (OpenBFME's battles keep no sides). True when the battle ended there.
	Battle &b = m_battles[(size_t)battleIndex];
	std::vector<int> participants;
	for (const AutoResolvePlayerSpec &s : autoResolveSpecs(b))
	{
		participants.push_back(s.player);
	}
	bool retreat = humanRetreat >= 0;
	for (int p : participants)
	{
		if (!m_players[(size_t)p].human && m_ai->battleChoice(p, b) == 2)
		{
			retreat = true;
		}
	}
	if (!retreat)
	{
		return false;
	}
	for (int p = 0; p < (int)m_players.size() && b.resolution == Battle::PENDING; ++p)
	{
		if (p == humanRetreat)
		{
			retreatFrom(battleIndex, p);
		}
		else if (!m_players[(size_t)p].human && std::find(participants.begin(), participants.end(), p) != participants.end() &&
			m_ai->battleChoice(p, b) == 2)
		{
			retreatFrom(battleIndex, p);
		}
	}
	if (b.resolution == Battle::PENDING)
	{
		// RW 0x60E272: a battle left without a human player is autoresolved at once
		bool human = false;
		for (int id : b.armies)
		{
			const Army *a = findArmy(id);
			human = human || (a && m_players[(size_t)a->owner].human);
		}
		const int owner = m_regions[(size_t)b.region].owner;
		human = human || (owner >= 0 && m_players[(size_t)owner].human && regionDefended(b.region)); // a human's defending building is in the battle
		if (!human)
		{
			autoResolve(b);
		}
	}
	return b.resolution != Battle::PENDING;
}

bool LivingWorldLogic::regionDefended(int region) const
{
	// the region owner's finished building that CanDefendTerritory (findBattles' defended region: the building fights for its owner)
	for (const Building &bd : m_regions[(size_t)region].buildings)
	{
		const BuildingTemplate *t = buildingTemplate(bd);
		if (t && t->canDefendTerritory && bd.turnsLeft == 0)
		{
			return true;
		}
	}
	return false;
}

void LivingWorldLogic::retreatFrom(int battleIndex, int player)
{
	Battle &b = m_battles[(size_t)battleIndex];
	std::vector<AutoResolvePlayerSpec> specs = autoResolveSpecs(b);
	for (AutoResolvePlayerSpec &s : specs)
	{
		if (s.player == player)
		{
			s.weapon = 0.0f;
		}
	}
	AutoResolveBattle fight(m_stores.autoResolve, m_units, specs, m_random);
	for (int i = 0; i < m_regionCampaign->armyRetreatRounds; ++i)
	{
		fight.update();
	}
	log(m_players[(size_t)player].name + " retreats from the battle in " + m_regionCampaign->regions[(size_t)b.region].name);
	applyAutoResolve(fight, specs, b, player);
	// the tally after RetreatArmiesForPlayer (RW 0x6BE6A6): the battle ends only when its sides are no longer both present (RW 0x7F5BBD); otherwise it
	// goes on without the retreating player's armies (review wotr2m4: an AI ally's retreat must not end a human's battle)
	std::vector<int> remaining, sides;
	for (int id : b.armies)
	{
		const Army *a = findArmy(id);
		if (a && a->owner != player && !a->retreating)
		{
			remaining.push_back(id);
			sides.push_back(a->owner);
		}
	}
	const int owner = m_regions[(size_t)b.region].owner;
	if (owner >= 0 && owner != player && regionDefended(b.region))
	{
		sides.push_back(owner); // review wotr2m4r2: the region owner's surviving defence (a CanDefendTerritory building) is a side of the battle
	}
	bool opposed = false;
	for (size_t i = 0; i < sides.size() && !opposed; ++i)
	{
		for (size_t j = i + 1; j < sides.size(); ++j)
		{
			if (hostile(sides[i], sides[j]))
			{
				opposed = true;
				break;
			}
		}
	}
	if (opposed)
	{
		b.armies = remaining;
		b.resolution = Battle::PENDING;
		b.winner = -1;
	}
}

bool LivingWorldLogic::chooseAutoResolve(int battleIndex, std::string *error)
{
	if (m_phase != PHASE_BATTLES || battleIndex < 0 || battleIndex >= (int)m_battles.size() || m_battles[(size_t)battleIndex].resolution != Battle::PENDING)
	{
		setError(error, "no pending battle with that index");
		return false;
	}
	if (aiBattleVotes(battleIndex, -1))
	{
		return true; // an AI player retreated and the battle is over
	}
	autoResolve(m_battles[(size_t)battleIndex]);
	return true;
}

bool LivingWorldLogic::chooseRts(int battleIndex, BattleSetup *setup, std::string *error, bool votesTallied)
{
	if (m_phase != PHASE_BATTLES || battleIndex < 0 || battleIndex >= (int)m_battles.size() || m_battles[(size_t)battleIndex].resolution != Battle::PENDING)
	{
		setError(error, "no pending battle with that index");
		return false;
	}
	Battle &b = m_battles[(size_t)battleIndex];
	const Region &region = m_regionCampaign->regions[(size_t)b.region];
	if (region.mapName.empty())
	{
		setError(error, "the region " + region.name + " has no MapName");
		return false;
	}
	if (!votesTallied && aiBattleVotes(battleIndex, -1)) // a network war's vote tallied the AI players already (networkVote)
	{
		if (setup)
		{
			*setup = BattleSetup(); // battleIndex -1: an AI player retreated, there is no battle to fight
		}
		return true;
	}
	b.resolution = Battle::RTS_REQUESTED;
	// WOTR2 milestone 6: PrepareBattleForLoading RW 0x6B851C pass 1: every battle player (side order, then player order) without a garrison army in the
	// battle gets an empty one in the region (CreateEmptyGarrisonArmy RW 0x6B7664, battle.AddArmy RW 0x7F8588: appended to the battle's armies). Its
	// id carries the starting units (RW 0x8125FC) and what the player builds in the battle (spec R 5.1)
	std::vector<std::pair<int, int>> garrisons; // {player, garrison army id}
	for (const Battle::Side &side : b.sides)
	{
		for (const Battle::SidePlayer &sp : side.players)
		{
			int garrisonId = 0;
			for (int id : b.armies)
			{
				const Army *a = findArmy(id);
				if (a && a->owner == sp.player && a->garrison)
				{
					garrisonId = id;
					break;
				}
			}
			if (garrisonId == 0)
			{
				Army a;
				a.id = m_nextArmyId++;
				a.owner = sp.player;
				a.region = b.region;
				a.garrison = true;
				m_armies.push_back(a);
				b.armies.push_back(a.id);
				garrisonId = a.id;
			}
			garrisons.emplace_back(sp.player, garrisonId);
		}
	}
	// WOTR2 milestone 7 (step 5): PrepareBattleForLoading RW 0x6B851C pass 2, per battle player (side order, then player order), its armies in battle order: a
	// garrison (no template name) is on the field (state 4); the first hero army too (or every one when SecondsPerReinforcement < 1); each later hero army
	// counts down from frame 0 to (int)((float)offset * 5.0f), offset growing by SecondsPerReinforcement per hero army (RW 0x80F475, 0x80F3C0)
	std::vector<BattleSetup::ArmyStart> armyStarts;
	{
		const int spacing = m_campaign->secondsPerReinforcement; // + 0x44
		const int battleOwner = m_regions[(size_t)b.region].owner;
		bool oneOnOne = b.sides.size() == 2;
		for (const Battle::Side &side : b.sides)
		{
			oneOnOne = oneOnOne && side.players.size() == 1;
		}
		for (const Battle::Side &side : b.sides)
		{
			for (const Battle::SidePlayer &sp : side.players)
			{
				int engaged = 0, offset = 0;
				for (int id : b.armies)
				{
					const Army *a = findArmy(id);
					if (!a || a->owner != sp.player)
					{
						continue;
					}
					BattleSetup::ArmyStart as;
					as.armyId = id;
					as.player = sp.player;
					if (!a->garrison)
					{
						if (engaged >= 1 && spacing >= 1)
						{
							as.state = 2;
							// [5.0f RW 0xD9F618]; x87 fild / fmul / ftol (PC24): exact for these ints
							as.arrivalFrame = SimMath::truncToInt32(NumericState::pc24Mul(SimMath::sseFromInt32(offset), 5.0f));
							// getSpawnPositions RW 0x80F754 for a reinforcement: the army's region (+ 0x2C: where it came from), the battle region's owner
							const int from = a->movedFrom >= 0 ? a->movedFrom : a->region;
							const std::string &fromName = m_regionCampaign->regions[(size_t)from].name;
							const bool friendOwner = battleOwner >= 0 && !hostile(sp.player, battleOwner);
							const std::pair<std::string, std::string> defender{ "WOTRSpawnPoint_DefenderReinforcements_1", "WOTRGatherPoint_DefenderReinforcements_1" };
							const std::pair<std::string, std::string> walkOn{ "WOTRSpawnPoint_" + fromName + "_1", "WOTRGatherPoint_" + fromName + "_1" };
							if (oneOnOne)
							{
								// owner -1: GetSpawnPointInFarthestPair RW 0x7824FD; a hostile owner: GetFarthestSpawnPointFromPlayerStart RW 0x781E6E [S-1329
								// inference: both reduced to the army's own walk-on points]
								as.spawnPoints.push_back(friendOwner ? defender : walkOn);
							}
							else
							{
								as.spawnPoints.push_back(from == b.region ? defender : walkOn);
								as.spawnPoints.push_back(friendOwner ? defender : walkOn); // RotWK's second try
							}
							as.spawnPoints.push_back({ "Player_2_Start", "Player_2_Start_Rally" }); // StartPos(1)
						}
						++engaged;
						offset += spacing;
					}
					armyStarts.push_back(as);
				}
			}
		}
	}
	if (setup)
	{
		*setup = BattleSetup();
		setup->armyStarts = armyStarts;
		setup->battleIndex = battleIndex;
		setup->region = b.region;
		setup->mapName = region.mapName;
		setup->startingCash = m_campaign->startingCashRTS;
		setup->startingCashWithFort = m_campaign->startingCashRTSWithFort;
		setup->players.push_back(b.attacker);
		if (b.defender >= 0)
		{
			setup->players.push_back(b.defender);
		}
		for (int id : b.armies)
		{
			const Army *a = findArmy(id);
			if (!a)
			{
				continue;
			}
			if (std::find(setup->players.begin(), setup->players.end(), a->owner) == setup->players.end())
			{
				setup->players.push_back(a->owner);
			}
			for (size_t m = 0; m < a->members.size(); ++m)
			{
				BattleSpawn s;
				s.player = a->owner;
				s.armyId = a->id;
				s.memberIndex = (int)m;
				s.thingTemplate = a->members[m].thingTemplate;
				s.quantity = a->members[m].quantity;
				s.experience = a->members[m].experience;
				s.upgrades = a->members[m].upgrades;
				s.spawnId = a->members[m].spawnId;
				setup->spawns.push_back(s);
			}
		}
		bool fort = region.createAutoFort; // RW 0x6B1213: CreateAutoFort (+0x13C) or a completed FORTRESS (RW 0x7F1805(1))
		for (const Building &bd : m_regions[(size_t)b.region].buildings)
		{
			const BuildingTemplate *t = buildingTemplate(bd);
			if (t && bd.turnsLeft == 0 && !t->battleThingTemplate.empty())
			{
				setup->defenderBuildings.push_back(t->battleThingTemplate);
				setup->defenderBuildingIds.push_back(bd.id);
			}
			fort = fort || (t && bd.turnsLeft == 0 && t->type == BUILDING_FORTRESS);
		}
		const int owner = m_regions[(size_t)b.region].owner;
		for (const std::pair<int, int> &g : garrisons)
		{
			// Player::initFromDict RW 0x6B07EF tail: the region owner's battle player gets StartingCashRTSWithFort when the region has a fort, every other
			// one StartingCashRTS (both deposited, RW 0x7B18B8); then its ExtraStartResources. ArmySummary::Load RW 0x812413 / 0x812492: FreeBuilder > 0
			// and FreeInnUnits > 0 each add ONE object (LoadArmyEntry RW 0x811104 is called with count 1)
			BattleSetup::PlayerStart ps;
			ps.player = g.first;
			ps.garrisonArmyId = g.second;
			const PlayerBonuses pb = playerBonuses(g.first);
			ps.cash = (g.first == owner && fort ? m_campaign->startingCashRTSWithFort : m_campaign->startingCashRTS) + (unsigned)pb.extraStartResources;
			const PlayerTemplate &pt = playerTemplate(g.first);
			if (pb.freeBuilder > 0 && !pt.factionDozerTemplateName.empty())
			{
				ps.bonusUnits.push_back(pt.factionDozerTemplateName);
			}
			if (pb.freeInnUnits > 0 && !pt.factionInnUnitTemplateName.empty())
			{
				ps.bonusUnits.push_back(pt.factionInnUnitTemplateName);
			}
			// RW 0x6E42DA (from Player::initBuildableHeroes RW 0x6B16EC at the battle's start): from the last entry to the first, every entry of the hero
			// list no build queue holds (RW 0x6E24C0) leaves it for the battle player's list; what the battle leaves comes back (RW 0x78100E)
			std::vector<RevivalUnit> &list = m_players[(size_t)g.first].buildableHeroes;
			for (int i = (int)list.size() - 1; i >= 0; --i)
			{
				if (!heroQueuedAnywhere(g.first, list[(size_t)i].spawnId))
				{
					ps.heroes.push_back(list[(size_t)i]);
					list.erase(list.begin() + i);
				}
			}
			ps.skillPoints = m_players[(size_t)g.first].experience; // RW 0x8125FC: + 0x1F4, + 0x2A0, + 0x1F8, + 0x204
			ps.bonusPoints = pb.legendary;
			ps.sciences = m_players[(size_t)g.first].sciences;
			ps.upgrades = m_players[(size_t)g.first].upgrades;
			setup->playerStarts.push_back(ps);
		}
	}
	log("battle in " + region.name + " will be fought in real time on '" + region.mapName + "'");
	return true;
}

bool LivingWorldLogic::applyBattleResult(int battleIndex, const BattleResult &result, std::string *error)
{
	if (m_phase != PHASE_BATTLES || battleIndex < 0 || battleIndex >= (int)m_battles.size() || m_battles[(size_t)battleIndex].resolution != Battle::RTS_REQUESTED)
	{
		setError(error, "no real-time battle is waiting for a result with that index");
		return false;
	}
	Battle &b = m_battles[(size_t)battleIndex];
	if (!result.collected)
	{
		// WOTR-2 (review r2): a result never read from the battle would empty every army of the battle
		setError(error, "the battle result was not collected from the battle");
		return false;
	}
	if (result.winner >= 0 && (result.winner >= (int)m_players.size() || std::none_of(b.sides.begin(), b.sides.end(), [&](const Battle::Side &side) {
			return std::any_of(side.players.begin(), side.players.end(), [&](const Battle::SidePlayer &sp) { return sp.player == result.winner; });
		})))
	{
		setError(error, "the battle's victor " + std::to_string(result.winner) + " is not a player of the battle");
		return false;
	}
	for (const BattleSurvivor &sv : result.survivors)
	{
		if (std::find(b.armies.begin(), b.armies.end(), sv.armyId) == b.armies.end() || !findArmy(sv.armyId))
		{
			setError(error, "a survivor names the army " + std::to_string(sv.armyId) + ", which is not in the battle");
			return false;
		}
	}
	// WOTR2 milestone 7 (step 4): a region building that died in the battle is gone from its region (Object::onDie RW 0x6991F3 -> RW 0x60EC74 over the owned
	// regions -> LivingWorldRegion::DestroyBuilding RW 0x7F32F4(building, 0): no refund, its queue goes; retail does it at the death, the war applies it here)
	for (int buildingId : result.destroyedBuildings)
	{
		for (int r = 0; r < (int)m_regions.size(); ++r)
		{
			std::vector<Building> &bs = m_regions[(size_t)r].buildings;
			auto it = std::find_if(bs.begin(), bs.end(), [&](const Building &bd) { return bd.id == buildingId && buildingId != 0; });
			if (it == bs.end())
			{
				continue;
			}
			const int slot = it->slot;
			onBuildingDestroyed(r, *it);
			std::vector<ProductionOrder> &q = m_regions[(size_t)r].production;
			q.erase(std::remove_if(q.begin(), q.end(), [&](const ProductionOrder &o) { return o.buildingSlot == slot; }), q.end());
			log(m_regionCampaign->regions[(size_t)r].name + ": " + it->templateName + " was destroyed in the battle");
			bs.erase(it);
			break;
		}
	}
	// WOTR2 milestone 6 (spec R 5.1): ArmySummarySystem::Save RW 0x811E1F: every deployed army (all of them: reinforcements are not ported, S-1329) loses its
	// roster (RW 0x810B47) and gets one entry, count 1, per survivor carrying its id (AddArmyEntry RW 0x811951: the next key); the dead heroes follow below
	std::vector<int> destroyed;
	std::map<int, std::vector<ArmyMember>> preBattle; // the battle's copies of the rosters (rva003F4921), ConsumeGarrisonUnitsFromRTS reads them
	for (int id : b.armies)
	{
		if (const Army *a = findArmy(id))
		{
			preBattle[id] = a->members;
		}
	}
	for (int id : b.armies)
	{
		auto it = std::find_if(m_armies.begin(), m_armies.end(), [&](const Army &x) { return x.id == id; });
		if (it == m_armies.end() || std::find(result.undeployedArmies.begin(), result.undeployedArmies.end(), id) != result.undeployedArmies.end())
		{
			continue; // RW 0x810C95: only a deployed army's summary (state 4) is rebuilt; a reinforcement that never came keeps its roster
		}
		Army *a = &*it;
		a->members.clear();
		for (const BattleSurvivor &sv : result.survivors)
		{
			if (sv.armyId != id)
			{
				continue;
			}
			ArmyMember m;
			m.thingTemplate = sv.thingTemplate;
			m.quantity = 1;
			m.experience = sv.experience;
			m.rank = sv.level;
			m.upgrades = sv.upgrades;
			m.spawnId = sv.spawnId;
			addMember(*a, m);
		}
		if (a->members.empty())
		{
			destroyed.push_back(id);
		}
	}
	// WOTR2 milestone 7 (step 2): ArmySummarySystem::Save's tail RW 0x811E1F: each battle player's earned spell points, sciences, lasting upgrades and skill points
	for (const BattleResult::PlayerBack &pb : result.players)
	{
		if (pb.player < 0 || pb.player >= (int)m_players.size())
		{
			continue;
		}
		PlayerState &ps = m_players[(size_t)pb.player];
		ps.powerPoints = pb.earnedSpellPoints; // RW 0x619071
		ps.sciences = pb.sciences;
		ps.upgrades = pb.upgrades;
		ps.experience = pb.skillPoints;
	}
	// WOTR2 milestone 7 (step 3): UnitRevivalTracker::retrieveHeroesForArmySummary RW 0x78100E (Save's tail): every HERO + ARMY_SUMMARY record left in a battle
	// player's hero list (one it never bought, a hero that died there) goes back into its army (+ 0xB8, ArmySummary::AddDeadHeroEntry RW 0x811D91: a new entry,
	// count 1, the record's experience, rank and spawn id; nothing when that army is gone) when the player has an army named for that hero (RW 0x6E301F: the
	// War of the Ring player's armies' template names, in a kind 0 game), else to the War of the Ring player's hero list (RW 0x6E42CF)
	for (const BattleHeroRecord &h : result.heroes)
	{
		if (h.player < 0 || h.player >= (int)m_players.size())
		{
			continue;
		}
		const bool named = std::any_of(m_armies.begin(), m_armies.end(), [&](const Army &a) { return a.owner == h.player && !a.heroTemplate.empty() && a.heroTemplate == h.thingTemplate; });
		if (named)
		{
			auto it = std::find_if(m_armies.begin(), m_armies.end(), [&](const Army &a) { return a.id == h.armyId; });
			if (it != m_armies.end())
			{
				ArmyMember m;
				m.thingTemplate = h.thingTemplate;
				m.quantity = 1;
				m.experience = h.experience;
				m.rank = h.rank;
				m.spawnId = h.spawnId;
				addMember(*it, m);
				destroyed.erase(std::remove(destroyed.begin(), destroyed.end(), it->id), destroyed.end());
				log(m_players[(size_t)h.player].name + "'s " + h.thingTemplate + " returns to army " + std::to_string(it->id) + " after the battle");
			}
			continue;
		}
		RevivalUnit r;
		r.spawnId = h.spawnId;
		r.thing = h.thingTemplate;
		r.experience = h.experience;
		r.dead = h.dead;
		m_players[(size_t)h.player].buildableHeroes.push_back(r);
	}
	// review wotr2m6 (Sol): RW 0x6BCC5C never destroys a winner's garrison. When nothing else of the winning team is left in the region (the victor won
	// with what it built, its army summaries all dead), the victor's garrison stays, empty, and holds the region for OnBattleComplete's award
	// (resolveOwnership here); otherwise empty armies go as before [S-1329 inference: when retail removes an empty army was not read]
	if (result.winner >= 0)
	{
		const bool held = std::any_of(b.armies.begin(), b.armies.end(), [&](int id) {
			const Army *a = findArmy(id);
			return a && !hostile(a->owner, result.winner) && std::find(destroyed.begin(), destroyed.end(), id) == destroyed.end();
		});
		if (!held)
		{
			auto keep = std::find_if(destroyed.begin(), destroyed.end(), [&](int id) {
				const Army *a = findArmy(id);
				return a && a->garrison && a->owner == result.winner;
			});
			if (keep != destroyed.end())
			{
				destroyed.erase(keep);
			}
		}
	}
	// LWL::GameLogic_tacticalBattleComplete RW 0x6BCC5C: with a victor, the winning team's garrisons consume (ConsumeGarrisonUnitsFromRTS RW 0x6BC7F0, step 6),
	// the other players' garrisons are destroyed (RW 0x6B9679) and their hero armies retreat (LWL + 0x10C); no victor: rosters only
	if (result.winner >= 0)
	{
		for (int id : b.armies)
		{
			const Army *a = findArmy(id);
			if (a && a->garrison && !hostile(a->owner, result.winner))
			{
				consumeGarrisonUnits(id, b, preBattle, result.undeployedArmies);
				// a garrison it emptied goes like any empty army, unless it alone holds the region for the victor (review wotr2m6)
				const bool othersHold = std::any_of(b.armies.begin(), b.armies.end(), [&](int other) {
					const Army *o = findArmy(other);
					return other != id && o && !o->members.empty() && !hostile(o->owner, result.winner);
				});
				if (findArmy(id)->members.empty() && othersHold && std::find(destroyed.begin(), destroyed.end(), id) == destroyed.end())
				{
					destroyed.push_back(id);
				}
			}
		}
		for (int id : b.armies)
		{
			const Army *a = findArmy(id);
			if (!a || !hostile(a->owner, result.winner) || std::find(destroyed.begin(), destroyed.end(), id) != destroyed.end())
			{
				continue;
			}
			if (a->garrison)
			{
				destroyed.push_back(id);
			}
			else
			{
				prepareRetreat(id);
			}
		}
	}
	for (int id : destroyed)
	{
		removeArmy(id);
	}
	b.winner = result.winner;
	b.resolution = Battle::RESOLVED;
	scoreRtsBattle(b); // LWL + 0x3C slot 1 (RW 0x8F2AA1)
	log("battle in " + m_regionCampaign->regions[(size_t)b.region].name + " ended: " + (result.winner >= 0 ? m_players[(size_t)result.winner].name + " wins" : std::string("no winner")));
	return true;
}

// WOTR2 milestone 7 (step 6): ConsumeGarrisonUnitsFromRTS RW 0x6BC7F0 (BFME2 0x2BBD52, ConsumeGarrisonUnitsFromRTS.cpp; RotWK differs where marked) for a
// winning player's garrison after a fought battle, over that player's deployed hero armies (RW 0x71A9E0: summary state 4), battle order:
//   (a) per entry of a hero army's pre-battle roster: its losses of that unit (pre-battle count - now) refilled from the garrison's units of the same name
//       (the last one, else the first entry: RW 0x31996D), free; then losses refilled from garrison units of the same AutoResolveUnitType (not a hero, 5)
//       whose command points are at most the entry's (the last such, else the first), each charged its cost (RotWK: entry + 0xD6 = 1, RW 0x80F018 ->
//       RW 0x6E1A40(-cost));
//   (b) the garrison's remaining entries sorted by (int)(experience difference), then command points (RW 0x6B4074, ascending): each hero army takes from
//       the back while its points are under its limit, an entry that fits and is not a hero (template + 0x113 bit 4), each charged its cost (RotWK);
//   (c) RotWK: per garrison of the player in the battle, its pre-battle entries take back the remaining entries of the same name; every other remaining
//       entry stays in the garrison, charged its cost (the units built in the battle are bought) [BFME2 dropped them instead].
// [S-1329 inference: std::sort is not stable in retail; ties keep the garrison's order here (stable), so every platform agrees]
void LivingWorldLogic::consumeGarrisonUnits(int garrisonId, const Battle &b, const std::map<int, std::vector<ArmyMember>> &preBattle, const std::vector<int> &undeployed)
{
	auto armyRef = [&](int id) -> Army * {
		auto it = std::find_if(m_armies.begin(), m_armies.end(), [&](const Army &x) { return x.id == id; });
		return it == m_armies.end() ? nullptr : &*it;
	};
	Army *g = armyRef(garrisonId);
	if (!g || g->members.empty() || !m_units)
	{
		return;
	}
	const int player = g->owner;
	auto info = [&](const std::string &name) { return m_units(name); };
	auto cpOf = [&](const ArmyMember &m) { return info(m.thingTemplate).commandPoints * m.quantity; }; // RW 0x78012F: + 0x628 x count
	auto countOf = [](const std::vector<ArmyMember> &list, const std::string &name) {
		int n = 0;
		for (const ArmyMember &m : list)
		{
			n += m.thingTemplate == name ? m.quantity : 0;
		}
		return n;
	};
	auto charge = [&](const ArmyMember &m) { addTreasure(player, -unitCost(player, m.thingTemplate)); }; // RW 0x80F018 -> RW 0x6E1A40
	auto move = [&](Army &to, size_t index) {
		ArmyMember m = g->members[index];
		g->members.erase(g->members.begin() + (long)index);
		m.homeArmy = 0;
		addMember(to, m); // MoveArmyMember RW 0x71B7A1
		return m;
	};
	std::vector<int> heroArmies;
	for (int id : b.armies)
	{
		const Army *a = findArmy(id);
		if (a && a->owner == player && !a->garrison && std::find(undeployed.begin(), undeployed.end(), id) == undeployed.end())
		{
			heroArmies.push_back(id);
		}
	}
	// (a)
	for (int id : heroArmies)
	{
		Army *h = armyRef(id);
		auto pre = preBattle.find(id);
		if (!h || pre == preBattle.end())
		{
			continue;
		}
		for (const ArmyMember &e : pre->second)
		{
			const int had = countOf(pre->second, e.thingTemplate);
			int lost = had - countOf(h->members, e.thingTemplate);
			int available = countOf(g->members, e.thingTemplate);
			for (int k = 0; k < std::min(lost, available) && lost > 0; ++k)
			{
				size_t index = 0;
				for (size_t i = g->members.size(); i-- > 0;)
				{
					if (g->members[i].thingTemplate == e.thingTemplate)
					{
						index = i;
						break;
					}
				}
				move(*h, index);
			}
			lost = had - countOf(h->members, e.thingTemplate);
			const int cp = cpOf(e);
			const int kind = info(e.thingTemplate).autoResolveUnitType;
			available = 0;
			for (const ArmyMember &m : g->members)
			{
				available += info(m.thingTemplate).autoResolveUnitType == kind && cpOf(m) <= cp ? 1 : 0;
			}
			if (lost > 0 && available > 0 && kind != AR_HERO)
			{
				for (int k = 0; k < std::min(lost, available); ++k)
				{
					size_t index = 0;
					for (size_t i = g->members.size(); i-- > 0;)
					{
						if (info(g->members[i].thingTemplate).autoResolveUnitType == kind && cpOf(g->members[i]) <= cp)
						{
							index = i;
							break;
						}
					}
					charge(move(*h, index));
				}
			}
		}
	}
	// (b)
	std::vector<int> order(g->members.size());
	for (size_t i = 0; i < order.size(); ++i)
	{
		order[i] = g->members[i].key;
	}
	auto byKey = [&](int key) -> const ArmyMember * {
		for (const ArmyMember &m : g->members)
		{
			if (m.key == key)
			{
				return &m;
			}
		}
		return nullptr;
	};
	std::stable_sort(order.begin(), order.end(), [&](int ka, int kb) {
		const ArmyMember &a = *byKey(ka), &c = *byKey(kb);
		const int d = SimMath::truncToInt32(SimMath::subf32(a.experience, c.experience)); // RW 0x6B4074: (int)(fsub)
		return d != 0 ? d < 0 : cpOf(a) < cpOf(c);
	});
	for (int id : heroArmies)
	{
		Army *h = armyRef(id);
		if (!h)
		{
			continue;
		}
		const int limit = armyMaxCommandPoints(*h);
		int cp = armyCommandPoints(*h);
		const size_t max = order.size();
		for (size_t j = 0; j < max && cp < limit && !order.empty(); ++j)
		{
			const ArmyMember *last = byKey(order.back());
			const UnitInfo u = info(last->thingTemplate);
			if (cpOf(*last) + cp > limit || u.hero)
			{
				continue;
			}
			const int lastCp = cpOf(*last);
			size_t index = 0;
			for (size_t i = 0; i < g->members.size(); ++i)
			{
				index = g->members[i].key == order.back() ? i : index;
			}
			charge(move(*h, index));
			order.pop_back();
			cp += lastCp;
		}
	}
	// (c)
	for (int id : b.armies)
	{
		const Army *a = findArmy(id);
		auto pre = preBattle.find(id);
		if (!a || a->owner != player || !a->garrison || pre == preBattle.end())
		{
			continue;
		}
		for (const ArmyMember &e : pre->second)
		{
			for (auto it = order.begin(); it != order.end(); ++it)
			{
				const ArmyMember *m = byKey(*it);
				if (m && m->thingTemplate == e.thingTemplate)
				{
					order.erase(it);
					break;
				}
			}
		}
		for (int key : order)
		{
			if (const ArmyMember *m = byKey(key))
			{
				charge(*m);
			}
		}
	}
}

void LivingWorldLogic::resolveOwnership()
{
	for (int r = 0; r < (int)m_regions.size(); ++r)
	{
		int occupant = -1;
		bool mixed = false;
		for (const Army &a : m_armies)
		{
			if (a.region != r || a.retreating)
			{
				continue; // a retreating army is leaving (RW: the battle's delayed region victory is awarded regardless)
			}
			if (occupant < 0)
			{
				occupant = a.owner;
			}
			else if (hostile(occupant, a.owner))
			{
				mixed = true;
			}
		}
		RegionState &rs = m_regions[(size_t)r];
		if (occupant < 0 || mixed || rs.owner == occupant || (rs.owner >= 0 && !hostile(rs.owner, occupant)))
		{
			continue;
		}
		// a region still defended by a building that won its battle stays with its owner
		bool defended = false;
		for (const Battle &b : m_battles)
		{
			if (b.region == r && b.winner >= 0 && b.winner != occupant && !hostile(b.winner, rs.owner))
			{
				defended = true;
			}
		}
		if (defended)
		{
			continue;
		}
		const int previous = rs.owner;
		setRegionOwner(r, occupant);
		// lane WOTR-2: the region's buildings and their queues stay (Region SetOwner RW 0x7F30A3 and OnRegionChangedOwnership WorldBuilder 0xDA20C0
		// touch neither: a queue produces for the new owner, its gold is not paid back)
		log(m_players[(size_t)occupant].name + " takes " + m_regionCampaign->regions[(size_t)r].name + (previous >= 0 ? " from " + m_players[(size_t)previous].name : std::string()));
	}
}

void LivingWorldLogic::endOfTurn()
{
	// RW LivingWorldRegionManager::OnTurnEnding (WorldBuilder 0xB58C10) -> per region (0x104DC60, region order): the owner's income (0x104DC90: the
	// FertileTerritoryBonus, then each completed building's nuggets' turn hook, vslot 4: IncreaseTreasury adds its amount, every add clipped by
	// ScenarioMaxResources, RW 0x6E1A40), then construction (0x104E9C0: only the FIRST building under construction in plot order works,
	// WorkOnUnderConstructionBuilding 0x133DB10: one turn less, finished at 0)
	std::vector<int> income(m_players.size(), 0);
	for (int r = 0; r < (int)m_regions.size(); ++r)
	{
		RegionState &rs = m_regions[(size_t)r];
		if (rs.owner >= 0 && !m_players[(size_t)rs.owner].defeated)
		{
			addTreasure(rs.owner, m_regionCampaign->regions[(size_t)r].fertileTerritoryBonus);
			income[(size_t)rs.owner] += m_regionCampaign->regions[(size_t)r].fertileTerritoryBonus;
			std::vector<Building *> plots;
			for (Building &b : rs.buildings)
			{
				plots.push_back(&b);
			}
			std::sort(plots.begin(), plots.end(), [](const Building *x, const Building *y) { return x->slot < y->slot; });
			for (Building *b : plots)
			{
				const BuildingTemplate *bt = buildingTemplate(*b);
				if (!bt || b->turnsLeft > 0)
				{
					continue;
				}
				// the building's turn hook (WorldBuilder 0x10EA150): each nugget's slot 4 in template order; a SpawnArmy nugget's is its production
				// (lane WOTR-2: runProductionPhase WB 0x1335A10, here before the region's construction)
				for (const BuildingNugget &nug : bt->nuggets)
				{
					if (nug.kind == BuildingNugget::INCREASE_TREASURY)
					{
						addTreasure(rs.owner, nug.treasureAmount);
						income[(size_t)rs.owner] += nug.treasureAmount;
					}
					else if (nug.kind == BuildingNugget::SPAWN_ARMY)
					{
						runProduction(r, *b, nug);
					}
				}
			}
		}
		Building *working = nullptr;
		for (Building &b : rs.buildings)
		{
			if (b.turnsLeft > 0 && (!working || b.slot < working->slot))
			{
				working = &b;
			}
		}
		if (working && --working->turnsLeft == 0)
		{
			onBuildingCompleted(r, *working); // RW 0x902A72 -> RW 0x8E5122
			log(working->templateName + " completed in " + m_regionCampaign->regions[(size_t)r].name);
		}
	}
	for (size_t p = 0; p < m_players.size(); ++p)
	{
		m_players[p].lastIncome = income[p];
	}
}

// LivingWorldBuildingNuggetSpawnArmy::runProductionPhase (WorldBuilder 0x1335A10 / RW 0x8FF849): only the queue's head works. An army the building does not
// list leaves the queue (turns restart). Its turns + 1; below the army's BuildTime nothing more; else it spawns when the region's command points allow it
// (LivingWorldRegion::CanSpawnUnitWithinCPLimit WB 0x104BC40 / RW 0x7F13AC: the owner's first army without a hero standing in the region, its command
// points (GetArmyCommandPointSize) plus the army's, at most the region's CPLimit), and then leaves the queue (its queued command points with it,
// OnUnitDequeued RW 0x6E1986) and the turns restart; when they do not allow it the head waits and its turns go on counting (no money is lost)
void LivingWorldLogic::runProduction(int region, Building &building, const BuildingNugget &nug)
{
	RegionState &rs = m_regions[(size_t)region];
	const std::vector<size_t> queue = queueOf(region, building.slot);
	if (queue.empty() || rs.owner < 0)
	{
		return;
	}
	const ProductionOrder head = rs.production[queue.front()];
	if (head.armyIndex < 0 || head.armyIndex >= (int)nug.armiesToSpawn.size())
	{
		rs.production.erase(rs.production.begin() + (std::ptrdiff_t)queue.front());
		building.turnsSpent = 0;
		return;
	}
	const SpawnArmy &spawn = nug.armiesToSpawn[(size_t)head.armyIndex];
	++building.turnsSpent;
	if (building.turnsSpent < spawn.buildTime)
	{
		return;
	}
	const PlayerArmy *pa = m_stores.findPlayerArmy(spawn.playerArmy);
	const int armyCp = pa ? recruitCommandPoints(*pa) : 0;
	int standing = 0;
	for (const Army &a : m_armies)
	{
		if (a.owner == rs.owner && a.region == region && a.heroTemplate.empty())
		{
			standing = membersCommandPoints(a); // the first such army of the player (WorldBuilder 0xD97E70)
			break;
		}
	}
	if (standing + armyCp > m_regionCampaign->regions[(size_t)region].cpLimit)
	{
		return;
	}
	spawnProducedArmy(rs.owner, spawn, region, spawnIdOf(*buildingTemplate(building), head.armyIndex));
	onUnitDequeued(region, pa); // RW 0x8FF970
	log(m_players[(size_t)rs.owner].name + "'s " + spawn.playerArmy + " is ready in " + m_regionCampaign->regions[(size_t)region].name);
	rs.production.erase(rs.production.begin() + (std::ptrdiff_t)queue.front());
	building.turnsSpent = 0;
}

// LivingWorldLogic::spawnArmy (WorldBuilder 0xD9AA50 / RW 0x6B7229) for a produced army: an army without a hero joins the player's first army without a
// hero in the region when there is one (its units merged, WB 0x1038C60; the new army destroyed), else it stands at a free garrison spot (a new army);
// a hero army is a new army on a hero spot; every hero entry takes its revival data first (spawnArmy)
void LivingWorldLogic::spawnProducedArmy(int player, const SpawnArmy &spawn, int region, const std::string &spawnId)
{
	if (const PlayerArmy *spawned = m_stores.findPlayerArmy(spawn.playerArmy))
	{
		scoreRecruited(player, *spawned); // the SpawnArmy nugget's listeners (RW 0x8F323D)
	}
	if (spawn.heroTemplateName.empty())
	{
		for (Army &a : m_armies)
		{
			if (a.owner != player || a.region != region || !a.heroTemplate.empty())
			{
				continue;
			}
			if (const PlayerArmy *pa = m_stores.findPlayerArmy(spawn.playerArmy))
			{
				for (const ArmyEntry &e : pa->entries)
				{
					ArmyMember m;
					m.thingTemplate = e.thingTemplate;
					m.quantity = e.quantity;
					if (m_units && m_units(m.thingTemplate).hero)
					{
						takeRevivalData(player, spawnId, m);
					}
					addMember(a, m); // the merge RW 0x71B202 -> RW 0x8119D1: each entry joins with the garrison's next key, in order
				}
			}
			return;
		}
	}
	spawnArmy(player, spawn, region, spawnId);
}

const std::vector<Condition> &LivingWorldLogic::victoryConditions(int kind) const
{
	// UseMpRulesVictoryCondition: the campaign's LivingWorldVictoryType chosen on the Rules tab (LivingWorldCampaign::GetVictoryCondition, WorldBuilder
	// 0x1357530: the index when it is in range, else the first); otherwise the scenario's own conditions
	const Scenario &sc = *m_campaign->scenario;
	const std::vector<VictoryType> &types = m_campaign->victoryTypes;
	if (sc.useMpRulesVictoryCondition && !types.empty())
	{
		const VictoryType &vt = m_victoryType >= 0 && m_victoryType < (int)types.size() ? types[(size_t)m_victoryType] : types[0];
		return kind == 0 ? vt.playerDefeat : kind == 1 ? vt.teamDefeat : vt.teamVictory;
	}
	return kind == 0 ? sc.playerDefeat : kind == 1 ? sc.teamDefeat : sc.teamVictory;
}

void LivingWorldLogic::setNetworked(int phaseTimerSeconds, int battleChoice)
{
	m_networked = true;
	m_phaseTimerSeconds = phaseTimerSeconds;
	m_battleChoice = battleChoice;
}

bool LivingWorldLogic::networkVote(int battleIndex, int player, int method, std::string *error)
{
	// MSG_LW_SET_CONFLICT_RESOLUTION_METHOD 0x6A7 in a network war (RW 0x6BE9A5 .. 0x6BEC30, spec U1)
	if (m_phase != PHASE_BATTLES || battleIndex < 0 || battleIndex >= (int)m_battles.size() || m_battles[(size_t)battleIndex].resolution != Battle::PENDING)
	{
		setError(error, "no pending battle with that index");
		return false;
	}
	if (method == 2 && (resolutionOptions(battleIndex, player) & RESOLVE_RETREAT) == 0)
	{
		setError(error, "the player cannot retreat from this battle"); // port check (the client offers it only then)
		return false;
	}
	Battle &b = m_battles[(size_t)battleIndex];
	m_players[(size_t)player].battleVote = method; // + 0x334 (RW 0x6BE9E9)
	m_players[(size_t)player].battleVoteFor = battleIndex;
	// the tally over the battle's participants (RW 0x6BE9EF .. 0x6BEA7E): a human's vote, an AI's LivingWorldAI choice (RW 0x9001FB: 2 retreat, else 4,
	// an abstention: no flag and no active vote (Sol review wotr2m8s3), so only the humans' votes can make a battle real time)
	std::vector<int> participants;
	for (const AutoResolvePlayerSpec &sp : autoResolveSpecs(b))
	{
		if (std::find(participants.begin(), participants.end(), sp.player) == participants.end())
		{
			participants.push_back(sp.player);
		}
	}
	auto voteOf = [&](int p) {
		const PlayerState &ps = m_players[(size_t)p];
		if (ps.human)
		{
			return ps.battleVoteFor == battleIndex ? ps.battleVote : 0;
		}
		return m_ai->battleChoice(p, b) == 2 ? 2 : 4;
	};
	int flags = 0, active = 0;
	for (int p : participants)
	{
		const int t = voteOf(p);
		flags |= t == 0 ? 1 : t == 2 ? 4 : t == 1 ? 2 : t == 3 ? 8 : 0; // 4: an AI's abstention
		active += (t == 1 || t == 3) ? 1 : 0;
	}
	if (flags & 1)
	{
		log(m_players[(size_t)player].name + " votes in the battle in " + m_regionCampaign->regions[(size_t)b.region].name + "; waiting for the others");
		return true; // RW 0x6BEA80: somebody has not voted
	}
	auto resetVotes = [&] {
		for (PlayerState &ps : m_players)
		{
			if (ps.human)
			{
				ps.battleVote = 0;
				ps.battleVoteFor = -1;
			}
		}
	};
	if (flags & 4)
	{
		// RW 0x6BEA94 .. 0x6BEB9F: every player voting retreat retreats, in player order
		for (int p = 0; p < (int)m_players.size() && b.resolution == Battle::PENDING; ++p)
		{
			if (std::find(participants.begin(), participants.end(), p) != participants.end() && voteOf(p) == 2)
			{
				retreatFrom(battleIndex, p);
			}
		}
		resetVotes();
		if (b.resolution != Battle::PENDING)
		{
			return true;
		}
		// RW 0x60E272: a battle left without a human player is auto-resolved at once
		bool human = false;
		for (int id : b.armies)
		{
			const Army *a = findArmy(id);
			human = human || (a && m_players[(size_t)a->owner].human);
		}
		if (!human)
		{
			autoResolve(b);
			return true;
		}
		// the voters that remain vote again (their votes were reset)
		return true;
	}
	// the decision (RW 0x6BEBA5): real time needs every live human (RW 0x6B5E3E(false)) to have voted auto-resolve or real time, else it is auto-resolve;
	// the BattleChoice rule (TheGameInfo + 0x80) orders the two
	int liveHumans = 0;
	for (const PlayerState &ps : m_players)
	{
		liveHumans += ps.human && !ps.defeated ? 1 : 0;
	}
	int f = flags;
	if (active < liveHumans)
	{
		f = (f & ~8) | 2;
	}
	const int first = m_battleChoice == 1 ? 8 : 2, second = m_battleChoice == 1 ? 2 : 8;
	int chosen = (f & first) ? first : (f & second) ? second : 0;
	if (chosen == 8 && m_mapStartCount)
	{
		// [S-1342] the battle start seats one side per start position (NewGame: more sides than positions is refused by name; retail's placement of a third
		// side is not ported): such a battle is auto-resolved on every peer instead of a battle no peer can start
		const Region &region = m_regionCampaign->regions[(size_t)b.region];
		const int starts = m_mapStartCount(region.mapName);
		if (starts == -2)
		{
			// no fallback: an error by name (not the side limit); the battle is auto-resolved alike on every peer, which has the same data
			log("error: battle map '" + region.mapName + "' not found in this install: battle auto-resolved");
			StrategicNotice n;
			n.kind = StrategicNotice::RTS_MAP_MISSING;
			n.region = b.region;
			m_notices.push_back(n);
			chosen = 2;
		}
		else if (starts >= 0 && (int)b.sides.size() > starts)
		{
			log("[S-1342] the battle in " + region.name + " has " + std::to_string(b.sides.size()) + " sides for " + std::to_string(starts) +
				" start positions on its map: auto-resolved instead of fought in real time");
			StrategicNotice n;
			n.kind = StrategicNotice::RTS_DEMOTED;
			n.region = b.region;
			m_notices.push_back(n);
			chosen = 2;
		}
	}
	resetVotes(); // StartBattle RW 0x6BE467: every human's + 0x334 = 0
	if (chosen == 2)
	{
		autoResolve(b); // StartAutoResolveBattle RW 0x6BE15D
		return true;
	}
	if (chosen == 8)
	{
		BattleSetup setup;
		if (!chooseRts(battleIndex, &setup, error, true)) // StartRealTimeBattle RW 0x6B95BB
		{
			return false;
		}
		m_pendingBattleSetups.push_back(setup);
		return true;
	}
	setError(error, "the votes decide nothing");
	return false;
}

bool LivingWorldLogic::phaseTimerEnabled() const
{
	// RW 0x6B359B: !RW 0x6253FD() && not a scripted campaign && TheGameInfo && phase 0 && TheGameInfo + 0x78 != 0. INFERENCE [S-1336]: single player has
	// no TheGameInfo (no timer); RW 0x6253FD is not read
	return m_networked && m_started && !m_finished && m_phase == PHASE_ORDERS && m_phaseTimerSeconds > 0;
}

int LivingWorldLogic::phaseFramesRemaining() const
{
	if (!phaseTimerEnabled())
	{
		return 0x7FFFFFFF;
	}
	const std::uint32_t length = (std::uint32_t)m_phaseTimerSeconds * 5u; // RW 0x6B4779: seconds x the logic rate (RW 0xD9F608)
	const std::uint32_t elapsed = m_frame - m_phaseStart;
	return length <= elapsed ? 0 : (int)(length - elapsed);
}

int LivingWorldLogic::phaseSecondsRemaining() const
{
	return phaseTimerEnabled() ? (phaseFramesRemaining() + 4) / 5 : 0;
}

bool LivingWorldLogic::phaseTimerExpired() const
{
	// WOTR2 milestone 8 (step 4): GameLogic + 0x11D: a pending save request holds the timer's automatic end of the phase (RW 0x629750)
	return phaseTimerEnabled() && phaseFramesRemaining() == 0 && m_saveAsker < 0;
}

bool LivingWorldLogic::mayEndPhase(int player) const
{
	if (!m_started || m_finished || player < 0 || player >= (int)m_players.size() || m_players[(size_t)player].defeated ||
		m_players[(size_t)player].endedTurn)
	{
		return false;
	}
	return m_phase == PHASE_ORDERS || canEndPhase(player);
}

void LivingWorldLogic::surrender(int player)
{
	if (!m_started || m_finished || player < 0 || player >= (int)m_players.size())
	{
		return;
	}
	// RW 0x6BECB5: every player not gone with the same id or the same team, in player order
	const int team = m_players[(size_t)player].team;
	bool battleRunning = false;
	for (const Battle &b : m_battles)
	{
		battleRunning = battleRunning || (m_phase == PHASE_BATTLES && b.resolution == Battle::RTS_REQUESTED);
	}
	for (int p = 0; p < (int)m_players.size(); ++p)
	{
		if (!m_players[(size_t)p].defeated && (p == player || m_players[(size_t)p].team == team))
		{
			log(m_players[(size_t)p].name + " leaves the war");
			removePlayer(p);
		}
	}
	// WOTR2 milestone 8 (step 4): a player who leaves takes its part of a save with it, on every peer at this frame: the asker's request, a request no
	// other live human is left to answer, its leave vote (the vote is settled again over the players still in)
	if (m_saveAsker >= 0)
	{
		bool answerer = false;
		for (int p = 0; p < (int)m_players.size(); ++p)
		{
			answerer = answerer || (p != m_saveAsker && liveHuman(p));
		}
		if (!liveHuman(m_saveAsker) || !answerer)
		{
			log("the save request ends: " + std::string(!liveHuman(m_saveAsker) ? "its asker left" : "nobody is left to answer it"));
			m_saveAsker = -1;
			m_saveName.clear();
		}
	}
	settleLeaveVote();
	checkVictory(); // RW 0x6BA994
	if (battleRunning && !m_finished)
	{
		// RW 0x6BED57: the map is not up (a battle is fought): QuitWar RW 0x6B94C6 (L + 0x164 = -1, GameLogic::DoExitMap: every peer leaves to the shell)
		m_finished = true;
		m_winningTeam = -1;
		log("the war ends: a player left during a battle");
	}
}

void LivingWorldLogic::removePlayer(int p)
{
	// RemovePlayer RW 0x6B89D4 (WOTR2 milestone 4): each region of the player goes to the owner of the biggest army of another player standing in it
	// (the most summary entries), none: neutral; the player's armies leave the retreat list and are destroyed. Buildings and queues stay
	// (WorldBuilder 0xDA5D80). INFERENCE [S-1331]: armies of equal size: the first in army order (the region's army list order not read)
	PlayerState &ps = m_players[(size_t)p];
	ps.defeated = true;
	scoreDefeated(p); // P's observers (RW 0x8F3CFB) before its regions go
	for (int r = 0; r < (int)m_regions.size(); ++r)
	{
		RegionState &rs = m_regions[(size_t)r];
		if (rs.owner != p)
		{
			continue;
		}
		const Army *best = nullptr;
		for (const Army &a : m_armies)
		{
			if (a.region == r && a.owner != p && (!best || a.members.size() > best->members.size()))
			{
				best = &a;
			}
		}
		setRegionOwner(r, best ? best->owner : -1);
	}
	for (size_t i = m_armies.size(); i-- > 0;)
	{
		if (m_armies[i].owner == p)
		{
			const int id = m_armies[i].id;
			m_retreatList.erase(std::remove(m_retreatList.begin(), m_retreatList.end(), id), m_retreatList.end());
			m_armies.erase(m_armies.begin() + (std::ptrdiff_t)i);
		}
	}
	log(ps.name + " has been defeated");
	StrategicNotice n; // the players' observers (RW 0x6BAEB6): an ally / enemy defeated box for every other client
	n.kind = StrategicNotice::PLAYER_DEFEATED;
	n.defeated = p;
	m_notices.push_back(n);
}

void LivingWorldLogic::checkVictory()
{
	// RW 0x6BA994 (the start of a turn): the first team meeting a TeamVictoryCondition wins; else every team meeting a TeamDefeatCondition is defeated,
	// then every remaining player meeting a PlayerDefeatCondition; the war ends when fewer than two teams are left.  The conditions' Teams lists are only
	// asserted (WorldBuilder 0x1337140 / 0x13376E0 / 0x1337B10: "Invalid team for this rule"): the release game evaluates every team and player.
	//   TeamVictory (0x1337B10): the team's regions >= NumControlledRegionsGreaterOrEqualTo, AND every enabled ControlledRegions region is the team's,
	//     AND (ControlledRegionsHeldForTurns > 0) each held at least that many turns;
	//   TeamDefeat (0x13376E0): every player of the team holds <= NumControlledRegionsLessOrEqualTo regions;
	//   PlayerDefeat (0x1337140): LoseIfCapitalLost and the capital is someone else's, or <= NumControlledRegionsLessOrEqualTo regions.
	// WOTR2 milestone 5: the teams are a std::set (RW 0x6B759F: ascending, team -1 first); each phase evaluates every condition before it removes
	// anyone, and the removals run in reverse (RW 0x6B9527 / 0x6B955E walk the players from the last; the collected lists are popped from the back).
	std::set<int> teams; // the teams of the players still playing
	std::set<int> allTeams; // RW 0x6B7606: every player's team
	for (const PlayerState &ps : m_players)
	{
		allTeams.insert(ps.team);
		if (!ps.defeated)
		{
			teams.insert(ps.team);
		}
	}
	auto teamOf = [&](int owner) { return owner >= 0 ? m_players[(size_t)owner].team : -0x7FFFFFFF; };
	const int kNoTeam = -0x7FFFFFFF;
	int won = kNoTeam;
	for (int team : teams)
	{
		for (const Condition &c : victoryConditions(2))
		{
			int owned = 0;
			for (const RegionState &r : m_regions)
			{
				owned += teamOf(r.owner) == team ? 1 : 0;
			}
			if (c.numControlledRegionsGreaterOrEqualTo >= 0 && owned < c.numControlledRegionsGreaterOrEqualTo)
			{
				continue;
			}
			bool all = true;
			for (const std::string &name : c.controlledRegions)
			{
				const int idx = m_regionCampaign->regionIndex(name);
				if (idx < 0 || !m_regions[(size_t)idx].enabled)
				{
					continue;
				}
				const RegionState &r = m_regions[(size_t)idx];
				if (teamOf(r.owner) != team || (c.controlledRegionsHeldForTurns > 0 && m_turn - r.ownerSinceTurn < c.controlledRegionsHeldForTurns))
				{
					all = false;
				}
			}
			if (all)
			{
				won = team;
				break;
			}
		}
		if (won != kNoTeam)
		{
			break;
		}
	}
	bool finish = false;
	auto defeat = [&](int p) { removePlayer(p); };
	if (won != kNoTeam)
	{
		// RW 0x6B955E: every player of another team is removed, the last first
		finish = true;
		log("team " + std::to_string(won) + " meets its victory condition");
		for (int p = (int)m_players.size() - 1; p >= 0; --p)
		{
			if (m_players[(size_t)p].team != won && !m_players[(size_t)p].defeated)
			{
				defeat(p);
			}
		}
	}
	else
	{
		std::vector<int> beatenTeams; // RW 0x902768 per team, then RW 0x6B9527 per collected team from the back
		for (int team : teams)
		{
			bool beaten = false;
			for (const Condition &c : victoryConditions(1))
			{
				if (c.numControlledRegionsLessOrEqualTo < 0)
				{
					continue;
				}
				bool all = true;
				for (int p = 0; p < (int)m_players.size(); ++p)
				{
					if (m_players[(size_t)p].team == team && !m_players[(size_t)p].defeated && regionCount(p) > c.numControlledRegionsLessOrEqualTo)
					{
						all = false;
					}
				}
				beaten = beaten || all;
			}
			if (beaten)
			{
				finish = true; // RW 0x6BA994: a team meeting a TeamDefeatCondition ends the war
				beatenTeams.push_back(team);
			}
		}
		for (size_t i = beatenTeams.size(); i-- > 0;)
		{
			for (int p = (int)m_players.size() - 1; p >= 0; --p)
			{
				if (m_players[(size_t)p].team == beatenTeams[i] && !m_players[(size_t)p].defeated)
				{
					defeat(p);
				}
			}
		}
	}
	std::vector<int> lostPlayers; // RW 0x9026F2 from the last player, then RemovePlayer from the back of that list: the first player first
	for (int p = (int)m_players.size() - 1; p >= 0; --p)
	{
		const PlayerState &ps = m_players[(size_t)p];
		if (ps.defeated)
		{
			continue;
		}
		bool lost = false;
		for (const Condition &c : victoryConditions(0))
		{
			if (c.loseIfCapitalLost && ps.capital >= 0 && m_regions[(size_t)ps.capital].owner != p)
			{
				lost = true;
			}
			if (c.numControlledRegionsLessOrEqualTo >= 0 && regionCount(p) <= c.numControlledRegionsLessOrEqualTo)
			{
				lost = true;
			}
		}
		if (lost)
		{
			lostPlayers.push_back(p);
		}
	}
	for (size_t i = lostPlayers.size(); i-- > 0;)
	{
		defeat(lostPlayers[i]);
	}
	std::set<int> alive;
	for (const PlayerState &ps : m_players)
	{
		if (!ps.defeated)
		{
			alive.insert(ps.team);
		}
	}
	// RW 0x6BA994's end: a victory or team defeat, or fewer than two teams left of a war that had two (RW 0x6B839A / 0x6B83DF). A defeated local
	// player's own end (LWL+0x98) is the client's (wotr.gd _check_war_end).
	if (finish || (alive.size() < 2 && allTeams.size() > 1))
	{
		m_finished = true;
		m_winningTeam = won != kNoTeam ? won : alive.size() == 1 ? *alive.begin() : -1;
		scoreCollectAll(retailTurn()); // EndWar's LWL + 0x4C listeners (RW 0x8F3D59)
		log(m_winningTeam < 0 && alive.size() != 1 ? std::string("the war is over: a team was defeated")
		                                           : "the war is over: team " + std::to_string(m_winningTeam) + " wins");
	}
}

// ---- the state hash ----

void LivingWorldLogic::hashInto(StateHasher &h) const
{
	h.addU32(0x4C574C47u); // "LWLG"
	h.addI32(m_turn);
	h.addI32(m_phase);
	h.addI32(m_nextArmyId);
	h.addI32(m_nextBuildingId);
	h.addU32((std::uint32_t)m_retreatList.size());
	for (int id : m_retreatList)
	{
		h.addI32(id);
	}
	h.addI32(m_settleCounter);
	h.addBool(m_started);
	h.addBool(m_finished);
	if (m_networked)
	{
		// WOTR2 milestone 8: the network war's clock and timer (retail's CRC holds L + 0x100 / + 0x104 in CHUNK_LivingWorldLogic while the map is up)
		h.addU32(m_frame);
		h.addU32(m_phaseStart);
		h.addU32(m_refusedOrders); // (coordinator 10-30) the refused orders: a refusal on one peer only is a desync seen here
		h.addI32(m_phaseTimerSeconds);
		h.addI32(m_battleChoice);
		// WOTR2 milestone 8 (step 4): the save request (GameLogic + 0x11D) and the leave-for-now vote
		h.addI32(m_saveAsker);
		h.addU32((std::uint32_t)m_saveName.size());
		for (char16_t c : m_saveName)
		{
			h.addU32((std::uint32_t)c);
		}
		h.addBool(m_leaveVoteOpen);
		h.addU32((std::uint32_t)m_leaveVotes.size());
		for (int v : m_leaveVotes)
		{
			h.addI32(v);
		}
		h.addBool(m_warEndAgreed);
	}
	h.addI32(m_winningTeam);
	h.addU32(m_seed);
	h.addI32(m_victoryType);
	for (std::uint32_t w : m_random.seedArray())
	{
		h.addU32(w);
	}
	h.addString(m_campaign ? m_campaign->name : std::string());
	h.addU32((std::uint32_t)m_players.size());
	for (const PlayerState &p : m_players)
	{
		h.addString(p.name);
		h.addString(p.faction);
		h.addString(p.lwTemplate);
		h.addBool(p.human);
		h.addI32(p.aiDifficulty);
		h.addI32(p.team);
		h.addI32(p.color);
		h.addI32(p.capital);
		h.addI32(p.treasuryAccum);
		h.addBool(p.defeated);
		h.addBool(p.endedTurn);
		if (m_networked)
		{
			h.addI32(p.battleVote); // WOTR2 milestone 8 (step 3): the network war's vote (+ 0x334)
			h.addI32(p.battleVoteFor);
		}
		h.addI32(p.lastIncome);
		h.addI32(p.handicap);
		h.addI32(p.powerPoints);
		h.addFloat(p.experience);
		h.addI32(p.tacticalCPFromBuildings);
		h.addI32(p.worldCPFromBuildings);
		h.addI32(p.queuedCommandPoints);
		for (const std::vector<std::string> *names : { &p.sciences, &p.upgrades }) // WOTR2 milestone 7
		{
			h.addU32((std::uint32_t)names->size());
			for (const std::string &n : *names)
			{
				h.addString(n);
			}
		}
		{
			// WOTR2 milestone 5: the score keeper (retail's CRC skips it for its wall clock; this model keeps no clock, so all of it is hashed)
			const ScoreKeeper &sk = p.score;
			h.addU32((std::uint32_t)sk.perTurn.size());
			for (const ScoreKeeper::PerTurn &t : sk.perTurn)
			{
				h.addI32(t.territories);
				h.addI32(t.units);
				h.addI32(t.structures);
			}
			for (const std::vector<int> *v : { &sk.regionUnifiedTurns, &sk.fortressTurns })
			{
				h.addU32((std::uint32_t)v->size());
				for (int t : *v)
				{
					h.addI32(t);
				}
			}
			h.addU32((std::uint32_t)sk.rulesHeld.size());
			for (bool b : sk.rulesHeld)
			{
				h.addBool(b);
			}
			for (int v : { sk.defeatTurn, sk.lastTurn, sk.buildingsBuilt[0], sk.buildingsBuilt[1], sk.buildingsBuilt[2], sk.buildingsBuilt[3],
					 sk.buildingsBuilt[4], sk.structuresLost, sk.structuresDestroyed, sk.unitsLostAutoResolve, sk.heroesLostAutoResolve,
					 sk.unitsKilledAutoResolve, sk.rtsBattlesWon, sk.rtsBattlesLost, sk.autoResolveWon, sk.autoResolveLost, sk.territoriesConquered,
					 sk.territoriesLost, sk.regionsLost })
			{
				h.addI32(v);
			}
			h.addU32((std::uint32_t)sk.recruited.size());
			for (const auto &e : sk.recruited)
			{
				h.addString(e.first);
				h.addI32(e.second);
			}
			h.addBool(sk.detached);
		}
		h.addU32((std::uint32_t)p.buildableHeroes.size());
		for (const RevivalUnit &r : p.buildableHeroes)
		{
			h.addString(r.spawnId);
			h.addString(r.thing);
			h.addFloat(r.experience);
			h.addBool(r.dead);
		}
	}
	h.addU32((std::uint32_t)m_regions.size());
	for (const RegionState &r : m_regions)
	{
		h.addI32(r.owner);
		h.addI32(r.ownerSinceTurn);
		h.addBool(r.enabled);
		h.addU32((std::uint32_t)r.buildings.size());
		for (const Building &b : r.buildings)
		{
			h.addString(b.templateName);
			h.addI32(b.turnsLeft);
			h.addI32(b.slot);
			h.addI32(b.turnsSpent);
			h.addI32(b.id);
			h.addBool(b.cpApplied);
			h.addI32(b.upgradesUsed); // WOTR2 milestone 5 (review wotr2m5)
		}
		h.addU32((std::uint32_t)r.production.size());
		for (const ProductionOrder &o : r.production)
		{
			h.addI32(o.buildingSlot);
			h.addI32(o.armyIndex);
		}
	}
	h.addU32((std::uint32_t)m_armies.size());
	for (const Army &a : m_armies)
	{
		h.addI32(a.id);
		h.addI32(a.owner);
		h.addI32(a.region);
		h.addI32(a.destination);
		h.addI32(a.movedFrom);
		h.addString(a.scriptingName);
		h.addString(a.playerArmy);
		h.addString(a.heroTemplate);
		h.addString(a.icon);
		h.addString(a.banner);
		h.addBool(a.garrison);
		h.addBool(a.retreating);
		h.addI32(a.nextKey);
		h.addU32((std::uint32_t)a.members.size());
		for (const ArmyMember &m : a.members)
		{
			h.addString(m.thingTemplate);
			h.addI32(m.quantity);
			h.addFloat(m.experience);
			h.addString(m.spawnId);
			h.addI32(m.key);
			h.addI32(m.homeArmy);
			h.addU32((std::uint32_t)m.upgrades.size());
			for (const std::string &u : m.upgrades)
			{
				h.addString(u);
			}
			h.addI32(m.rank);
			h.addI32(m.upgradeBuilding);
		}
	}
	h.addU32((std::uint32_t)m_battles.size());
	for (const Battle &b : m_battles)
	{
		h.addI32(b.region);
		h.addI32(b.attacker);
		h.addI32(b.defender);
		h.addI32(b.resolution);
		h.addI32(b.winner);
		h.addI32(b.autoResolveRounds);
		h.addU32((std::uint32_t)b.armies.size());
		for (int id : b.armies)
		{
			h.addI32(id);
		}
		h.addU32((std::uint32_t)b.sides.size());
		for (const Battle::Side &side : b.sides)
		{
			h.addI32(side.team);
			h.addU32((std::uint32_t)side.players.size());
			for (const Battle::SidePlayer &p : side.players)
			{
				h.addI32(p.player);
				h.addI32(p.veterancy);
				h.addI32(p.unitsLost);
				h.addI32(p.enemyDestroyed);
				h.addI32(p.heroesLost);
			}
		}
		h.addI32(b.winningSide);
		h.addBool(b.resultsCounted);
	}
}

std::uint32_t LivingWorldLogic::stateHash() const
{
	StateHasher h;
	hashInto(h);
	return h.value();
}

} // namespace LivingWorld
