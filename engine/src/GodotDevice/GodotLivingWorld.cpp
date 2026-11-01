// OpenBFME. GPL-3.0.
// See GodotDevice/GodotLivingWorld.h.  Lane WOTR-1.

#include "GodotDevice/GodotLivingWorld.h"

#include "GameLogic/LivingWorld/BattleResultCapture.h"
#include "GameLogic/LivingWorld/LivingWorldUnits.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/SimMath.h"
#include "Common/NumericState.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "Common/PlayerHeroList.h"
#include "GameLogic/GameMessage.h"
#include "Common/PlayerScience.h"
#include "Common/Science.h"
#include "GameLogic/Economy.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/VictoryConditions.h"
#include "GameLogic/EconomySettings.h"
#include "GodotDevice/GodotGameWorld.h"
#include "GodotDevice/GodotW3DMaterial.h"
#include "GameEngineDevice/W3DDevice/GameClient/W3DObjectLighting.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include "Common/MD5.h"

#include <algorithm>
#include <cstring>

using namespace godot;
using namespace LivingWorld;

namespace
{
String s(const std::string &v)
{
	return String::utf8(v.c_str());
}
std::string n(const String &v)
{
	return std::string(v.utf8().get_data());
}
Vector2 v2(const Coord2D &c)
{
	return Vector2(c.x, c.y);
}
Array spots(const std::vector<Coord2D> &list)
{
	Array a;
	for (const Coord2D &c : list)
	{
		a.push_back(v2(c));
	}
	return a;
}
PackedStringArray strings(const std::vector<std::string> &list)
{
	PackedStringArray a;
	for (const std::string &v : list)
	{
		a.push_back(s(v));
	}
	return a;
}
Dictionary iconDict(const IconTemplate &t)
{
	Dictionary d;
	d["name"] = s(t.name);
	d["kind"] = (int)t.kind;
	Array objects;
	for (const IconObject &o : t.objects)
	{
		Dictionary od;
		od["name"] = s(o.name);
		od["model"] = s(o.model);
		od["sub_objects"] = strings(o.subObjects);
		od["z_offset"] = o.zOffset;
		od["scale"] = o.scale;
		od["orient_angle"] = o.orientAngle;
		od["visible_army_sizes"] = o.visibleArmySizes;
		od["use_house_color"] = o.useHouseColor;
		od["hide_when_unselected"] = o.hideWhenUnselected;
		od["hide_when_unhilighted"] = o.hideWhenUnhilighted;
		od["hide_when_under_construction"] = o.hideWhenUnderConstruction;
		od["hide_when_not_under_construction"] = o.hideWhenNotUnderConstruction;
		od["hide_when_not_producing"] = o.hideWhenNotProducing;
		od["show_only_after_move_order"] = o.showOnlyAfterMoveOrder;
		od["display_at_rally_point"] = o.displayAtRallyPoint;
		objects.push_back(od);
	}
	d["objects"] = objects;
	return d;
}
} // namespace

void LivingWorldGame::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("setup", "game_world"), &LivingWorldGame::setup);
	ClassDB::bind_method(D_METHOD("start", "setup"), &LivingWorldGame::start);
	ClassDB::bind_method(D_METHOD("update"), &LivingWorldGame::update);
	ClassDB::bind_method(D_METHOD("save_state"), &LivingWorldGame::save_state); // lane WOTR2 milestone 3
	ClassDB::bind_method(D_METHOD("load_state", "state", "state_hash", "players", "battle"), &LivingWorldGame::load_state, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("check_state", "state", "state_hash", "players", "battle"), &LivingWorldGame::check_state, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("get_strategic_state"), &LivingWorldGame::get_strategic_state); // lane WOTR-2: not InGameHudNode::get_state (the RENDERWAIT-1 checker matches names)
	ClassDB::bind_method(D_METHOD("apply_map_lighting"), &LivingWorldGame::apply_map_lighting);
	ClassDB::bind_method(D_METHOD("template_field", "game_world", "template_name", "field"), &LivingWorldGame::template_field);
	ClassDB::bind_method(D_METHOD("army_panel", "game_world", "army_id"), &LivingWorldGame::army_panel);
	ClassDB::bind_method(D_METHOD("get_state_hash"), &LivingWorldGame::get_state_hash);
	ClassDB::bind_method(D_METHOD("order_move", "player", "army", "region"), &LivingWorldGame::order_move);
	ClassDB::bind_method(D_METHOD("cancel_move", "player", "army"), &LivingWorldGame::cancel_move);
	ClassDB::bind_method(D_METHOD("can_swap_army_members", "army_a", "keys_a", "army_b", "keys_b"), &LivingWorldGame::can_swap_army_members);
	ClassDB::bind_method(D_METHOD("swap_army_members", "army_a", "keys_a", "army_b", "keys_b"), &LivingWorldGame::swap_army_members);
	ClassDB::bind_method(D_METHOD("cancel_army_member_move", "army", "key"), &LivingWorldGame::cancel_army_member_move);
	ClassDB::bind_method(D_METHOD("order_build", "player", "region", "building", "plot"), &LivingWorldGame::order_build, DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("disband_army_member", "player", "army", "key"), &LivingWorldGame::disband_army_member);
	ClassDB::bind_method(D_METHOD("can_disband_army_member", "army", "key"), &LivingWorldGame::can_disband_army_member);
	ClassDB::bind_method(D_METHOD("order_upgrade_army_member", "player", "army", "key"), &LivingWorldGame::order_upgrade_army_member);
	ClassDB::bind_method(D_METHOD("cancel_upgrade_army_member", "player", "army", "key"), &LivingWorldGame::cancel_upgrade_army_member);
	ClassDB::bind_method(D_METHOD("can_upgrade_army_member", "army", "key"), &LivingWorldGame::can_upgrade_army_member);
	ClassDB::bind_method(D_METHOD("region_stats", "player", "region"), &LivingWorldGame::region_stats);
	ClassDB::bind_method(D_METHOD("take_notices"), &LivingWorldGame::take_notices);
	ClassDB::bind_method(D_METHOD("can_move_army_members_no_room", "army_a", "keys_a", "army_b"), &LivingWorldGame::can_move_army_members_no_room);
	ClassDB::bind_method(D_METHOD("army_help", "game_world", "army"), &LivingWorldGame::army_help);
	ClassDB::bind_method(D_METHOD("building_help", "building"), &LivingWorldGame::building_help);
	ClassDB::bind_method(D_METHOD("can_move_army_member", "army", "key", "target"), &LivingWorldGame::can_move_army_member);
	ClassDB::bind_method(D_METHOD("order_recruit", "player", "region", "building_slot", "army_index"), &LivingWorldGame::order_recruit);
	ClassDB::bind_method(D_METHOD("end_turn", "player"), &LivingWorldGame::end_turn);
	ClassDB::bind_method(D_METHOD("end_phase", "player"), &LivingWorldGame::end_phase);
	ClassDB::bind_method(D_METHOD("can_end_phase", "player"), &LivingWorldGame::can_end_phase);
	ClassDB::bind_method(D_METHOD("can_order_move", "player", "army", "region"), &LivingWorldGame::can_order_move);
	ClassDB::bind_method(D_METHOD("buildable_in", "player", "region"), &LivingWorldGame::buildable_in);
	ClassDB::bind_method(D_METHOD("recruitable_in", "player", "region"), &LivingWorldGame::recruitable_in);
	ClassDB::bind_method(D_METHOD("can_recruit", "player", "region", "building_slot", "army_index"), &LivingWorldGame::can_recruit);
	ClassDB::bind_method(D_METHOD("order_dequeue_at", "player", "region", "building_slot", "queue_index"), &LivingWorldGame::order_dequeue_at);
	ClassDB::bind_method(D_METHOD("order_destroy_building", "player", "region", "building_slot"), &LivingWorldGame::order_destroy_building);
	ClassDB::bind_method(D_METHOD("pending_human_battle"), &LivingWorldGame::pending_human_battle);
	ClassDB::bind_method(D_METHOD("choose_auto_resolve", "battle"), &LivingWorldGame::choose_auto_resolve);
	ClassDB::bind_method(D_METHOD("choose_retreat", "battle", "player"), &LivingWorldGame::choose_retreat);
	ClassDB::bind_method(D_METHOD("resolution_options", "battle", "player"), &LivingWorldGame::resolution_options);
	ClassDB::bind_method(D_METHOD("choose_rts", "battle"), &LivingWorldGame::choose_rts);
	ClassDB::bind_method(D_METHOD("set_local_player", "player"), &LivingWorldGame::set_local_player); // WOTR2 milestone 8
	ClassDB::bind_method(D_METHOD("message_log"), &LivingWorldGame::message_log);
	ClassDB::bind_method(D_METHOD("net_tick"), &LivingWorldGame::net_tick);
	ClassDB::bind_method(D_METHOD("net_save_ask", "name"), &LivingWorldGame::net_save_ask);
	ClassDB::bind_method(D_METHOD("net_save_answer", "yes"), &LivingWorldGame::net_save_answer);
	ClassDB::bind_method(D_METHOD("net_leave_vote", "yes"), &LivingWorldGame::net_leave_vote);
	ClassDB::bind_method(D_METHOD("lan_save_state"), &LivingWorldGame::lan_save_state);
	ClassDB::bind_method(D_METHOD("load_lan_state", "state", "state_hash", "players", "timer", "battle_choice"), &LivingWorldGame::load_lan_state);
	ClassDB::bind_method(D_METHOD("check_lan_state", "state", "state_hash", "players", "timer", "battle_choice"), &LivingWorldGame::check_lan_state);
	ClassDB::bind_method(D_METHOD("take_battle_setup"), &LivingWorldGame::take_battle_setup);
	ClassDB::bind_method(D_METHOD("net_park"), &LivingWorldGame::detachNetwork);
	ClassDB::bind_method(D_METHOD("net_status"), &LivingWorldGame::net_status);
	ClassDB::bind_method(D_METHOD("net_leave"), &LivingWorldGame::net_leave);
	ClassDB::bind_method(D_METHOD("save_state_allowed"), &LivingWorldGame::save_state_allowed);
	ClassDB::bind_method(D_METHOD("apply_battle_result", "battle", "result"), &LivingWorldGame::apply_battle_result);
	ClassDB::bind_method(D_METHOD("battle_player_index", "game_world", "player_name"), &LivingWorldGame::battle_player_index);
	ClassDB::bind_method(D_METHOD("battle_alive", "game_world", "ids"), &LivingWorldGame::battle_alive);
	ClassDB::bind_method(D_METHOD("collect_battle_result", "game_world", "spawned", "players", "left"), &LivingWorldGame::collect_battle_result);
	ClassDB::bind_method(D_METHOD("arm_battle_capture", "game_world", "players"), &LivingWorldGame::arm_battle_capture);
	ClassDB::bind_method(D_METHOD("battle_capture_done"), &LivingWorldGame::battle_capture_done);
	ClassDB::bind_method(D_METHOD("captured_battle_result"), &LivingWorldGame::captured_battle_result);
	ClassDB::bind_method(D_METHOD("battle_reinforcements", "game_world"), &LivingWorldGame::battle_reinforcements);
	ClassDB::bind_method(D_METHOD("call_in_reinforcement", "game_world", "army"), &LivingWorldGame::call_in_reinforcement);
}

// lane WOTR-2: LivingWorldMapInfo's lights (RW 0xBFA838's Ambient + 0x38, SunDir / SunRGB + 0x44 / + 0x4C, Accent1Dir / Accent1RGB + 0x58 / + 0x60,
// Accent2Dir / Accent2RGB + 0x6C / + 0x74; the block lives at RW 0xDE3C08 + 0x14, RW 0x610CFD) as the W3D shaders' light set
// (W3DObjectLightingUtil::fromLivingWorldMap: the doubling and the Dir convention, an inference S-1335).
// lane WOTR-2: the army details panel of an army (StrategicDetailsArmyRetinue.swf, AptStrategicHUD::ArmyPanel): one entry per unit (a summary entry each),
// the hero army's leader apart (FindLeaderEntry: the entry of its hero template), the rest in Populate's order (CompareEntryIDs WB 0x1636990: heroes
// first, then the template name ascending, then the experience descending, then the entry order); each entry's portrait (GetButtonImage WB 0x16548D0:
// ButtonImage, else SelectPortrait, else BuildingNoArt), AutoResolveUnitType, quantity (a horde's InitialPayload members, else 1), level (its
// ExperienceLevel rank, 0 .. 10), progress toward the next level (-1 at the top), DisplayName label and CommandPoints. {} for no such army
Dictionary LivingWorldGame::army_panel(Object *game_world, int64_t army_id) const
{
	Dictionary out;
	const Army *army = m_logic ? m_logic->findArmy((int)army_id) : nullptr;
	if (!army)
	{
		return out;
	}
	struct Entry
	{
		std::string thing;
		float experience = 0.0f;
		int order = 0;
		int key = 0;
		int count = 1;
		int home = 0;
		int mark = 0; // WOTR2 milestone 5: the entry's upgrade mark (+0xCC)
		UnitInfo info;
	};
	// WOTR2 milestone 4: one icon per ArmySummary entry (its key: the member moves name it), its quantity the entry count (x a horde's members)
	std::vector<Entry> entries;
	for (const ArmyMember &m : army->members)
	{
		Entry e;
		e.thing = m.thingTemplate;
		e.experience = m.experience;
		e.order = m.key;
		e.key = m.key;
		e.count = m.quantity;
		e.home = m.homeArmy;
		e.mark = m.upgradeBuilding;
		e.info = m_units(m.thingTemplate);
		entries.push_back(e);
	}
	auto memberDict = [&](const Entry &e) {
		Dictionary d;
		std::string portrait = n(template_field(game_world, s(e.thing), "ButtonImage"));
		if (portrait.empty())
		{
			portrait = n(template_field(game_world, s(e.thing), "SelectPortrait"));
		}
		d["portrait"] = s(portrait.empty() ? std::string("BuildingNoArt") : portrait);
		d["unit_type"] = e.info.autoResolveUnitType;
		int quantity = 1;
		if (e.info.horde)
		{
			quantity = 0;
			for (const auto &p : e.info.hordePayload)
			{
				quantity += p.second;
			}
		}
		d["quantity"] = quantity * e.count;
		d["key"] = e.key;
		d["home"] = e.home;
		d["upgrade_marked"] = e.mark != 0;
		const int rank = e.info.levelForExperience(e.experience);
		d["rank"] = rank;
		float progress = -1.0f;
		for (size_t i = 0; i + 1 < e.info.levels.size(); ++i)
		{
			if (e.info.levels[i].rank == rank)
			{
				const float lo = (float)e.info.levels[i].requiredExperience, hi = (float)e.info.levels[i + 1].requiredExperience;
				// INFERENCE (S-1334): the bar's fraction (SimMath: this file is audited)
				progress = hi > lo ? std::min(1.0f, std::max(0.0f, SimMath::divf32(SimMath::subf32(e.experience, lo), SimMath::subf32(hi, lo)))) : 0.0f;
			}
		}
		d["progress"] = progress;
		d["name"] = template_field(game_world, s(e.thing), "DisplayName");
		d["cp"] = e.info.commandPoints;
		d["hero"] = e.info.hero;
		return d;
	};
	out["hero_army"] = !army->heroTemplate.empty();
	int leader = -1;
	if (!army->heroTemplate.empty())
	{
		for (size_t i = 0; i < entries.size() && leader < 0; ++i)
		{
			if (entries[i].thing == army->heroTemplate)
			{
				leader = (int)i;
			}
		}
		if (leader >= 0)
		{
			out["leader"] = memberDict(entries[(size_t)leader]);
		}
	}
	std::vector<Entry> rest;
	for (size_t i = 0; i < entries.size(); ++i)
	{
		if ((int)i != leader)
		{
			rest.push_back(entries[i]);
		}
	}
	std::stable_sort(rest.begin(), rest.end(), [](const Entry &a, const Entry &b) {
		if (a.info.hero != b.info.hero)
		{
			return a.info.hero;
		}
		if (a.thing != b.thing)
		{
			return a.thing < b.thing;
		}
		if (a.experience != b.experience)
		{
			return a.experience > b.experience;
		}
		return a.order < b.order;
	});
	Array members;
	for (const Entry &e : rest)
	{
		members.push_back(memberDict(e));
	}
	out["members"] = members;
	return out;
}

String LivingWorldGame::template_field(Object *game_world, const String &template_name, const String &field) const
{
	GameWorld *gw = Object::cast_to<GameWorld>(game_world);
	RetailObjectWorld *world = gw ? gw->object_world() : nullptr;
	const ThingTemplate *t = world ? world->things().findTemplate(n(template_name)) : nullptr;
	if (!t)
	{
		return String();
	}
	if (const FieldValue *v = t->findField(n(field)))
	{
		if (const std::string *str = std::get_if<std::string>(v))
		{
			return s(*str);
		}
		if (const RawTokens *raw = std::get_if<RawTokens>(v))
		{
			return raw->tokens.empty() ? String() : s(raw->tokens.front()); // a label row as the row gives it (EndGame's DisplayName)
		}
	}
	return String();
}

Dictionary LivingWorldGame::apply_map_lighting() const
{
	Dictionary out;
	if (!m_stores)
	{
		out["ok"] = false;
		out["error"] = "apply_map_lighting before setup";
		return out;
	}
	const LivingWorld::MapInfo &mi = m_stores->mapInfo;
	const float ambient[3] = { mi.ambient.red, mi.ambient.green, mi.ambient.blue };
	const float rgb[3][3] = { { mi.sunRGB.red, mi.sunRGB.green, mi.sunRGB.blue },
							  { mi.accent1RGB.red, mi.accent1RGB.green, mi.accent1RGB.blue },
							  { mi.accent2RGB.red, mi.accent2RGB.green, mi.accent2RGB.blue } };
	const float dirs[3][2] = { { mi.sunDir.x, mi.sunDir.y }, { mi.accent1Dir.x, mi.accent1Dir.y }, { mi.accent2Dir.x, mi.accent2Dir.y } };
	W3DObjectLighting env;
	W3DObjectLightingUtil::fromLivingWorldMap(ambient, rgb, dirs, env);
	const W3DLightSet &set = env.objects;
	W3D_Apply_Object_Lighting(env);
	out["ok"] = true;
	out["lights"] = (int64_t)set.count;
	Array colorsOut, dirsOut;
	for (int i = 0; i < set.count; ++i)
	{
		colorsOut.push_back(Color(set.color[i][0], set.color[i][1], set.color[i][2]));
		dirsOut.push_back(Vector3(set.toLight[i][0], set.toLight[i][1], set.toLight[i][2])); // SAGE axes, toward the light
	}
	out["colors"] = colorsOut;
	out["to_light"] = dirsOut;
	out["ambient"] = Color(set.ambient[0], set.ambient[1], set.ambient[2]);
	return out;
}

Dictionary LivingWorldGame::setup(Object *game_world)
{
	Dictionary out;
	GameWorld *gw = Object::cast_to<GameWorld>(game_world);
	RetailObjectWorld *world = gw ? gw->object_world() : nullptr;
	if (!world)
	{
		out["ok"] = false;
		out["error"] = "no GameWorld with a loaded object world";
		return out;
	}
	// lane WOTR-2: the campaign outlives the games it starts (every game reloads the world's stores, RetailObjectWorld::reload): it holds the Living
	// World stores of this load and reads the templates through the world at each lookup (before, a fought battle's reload freed both under it)
	detachNetwork(); // WOTR2 milestone 8 (Sol wotr2m8s2b): a session references the logic it was attached to
	m_logic.reset(); // a running campaign references the stores it started with: a new setup ends it first
	m_stores = world->livingWorldShared();
	m_units = makeUnitInfoSource(*world);
	{
		LivingWorld::GameSetup ranks; // the rank data of this load (LivingWorld::setRankData): the power points' trackers
		LivingWorld::setRankData(*world, ranks);
		m_ranks = std::move(ranks.ranks);
		m_rankTemplates = std::move(ranks.rankTemplates);
	}
	{
		EconomySettings economy; // GameData's ResourceMultiplierLimit (the HUD's resource multiplier clamp, RW 0x6E1F2F)
		std::string economyError;
		if (!EconomySettings::load(world->fileSystem(), economy, &economyError))
		{
			out["ok"] = false;
			out["error"] = s("GameData economy: " + economyError);
			return out;
		}
		m_resourceMultiplierLimit = economy.resourceMultiplierLimit;
	}
	{
		// WOTR2 milestone 7: data\ini\bannerui.ini's BannerType blocks (BannerUI::ParseBannerTypeInfo, the field table RW 0xBFBD68: FlagObj, GlowObj, Icon)
		// for the battle's reinforcement banners (AptBannerUI)
		std::vector<std::uint8_t> bytes;
		std::string error;
		if (!world->fileSystem().readFile("data\\ini\\bannerui.ini", bytes, &error))
		{
			out["ok"] = false;
			out["error"] = s("bannerui.ini: " + error);
			return out;
		}
		Dictionary types;
		Dictionary current;
		String currentName;
		std::string text(bytes.begin(), bytes.end());
		size_t pos = 0;
		while (pos < text.size())
		{
			size_t end = text.find('\n', pos);
			std::string line = text.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
			pos = end == std::string::npos ? text.size() : end + 1;
			const size_t semi = line.find(';');
			line = line.substr(0, semi);
			const size_t a = line.find_first_not_of(" \t\r");
			if (a == std::string::npos)
			{
				continue;
			}
			line = line.substr(a, line.find_last_not_of(" \t\r") - a + 1);
			if (line.rfind("BannerType", 0) == 0 && line.size() > 10 && (line[10] == ' ' || line[10] == '\t'))
			{
				currentName = s(line.substr(line.find_first_not_of(" \t", 10)));
				current = Dictionary();
				continue;
			}
			if (line == "End" || line == "END")
			{
				if (!currentName.is_empty())
				{
					types[currentName] = current;
				}
				currentName = String();
				continue;
			}
			const size_t eq = line.find('=');
			if (!currentName.is_empty() && eq != std::string::npos)
			{
				std::string key = line.substr(0, eq), value = line.substr(eq + 1);
				key = key.substr(0, key.find_last_not_of(" \t") + 1);
				const size_t v = value.find_first_not_of(" \t");
				value = v == std::string::npos ? std::string() : value.substr(v);
				current[s(key)] = s(value);
			}
		}
		out["banner_types"] = types;
	}
	const LivingWorldStores &st = *m_stores;
	Array scenarios;
	for (const Campaign *c : st.scenarios())
	{
		Dictionary d;
		d["name"] = s(c->name);
		d["display_name"] = s(c->scenario->displayName);
		d["description"] = s(c->scenario->displayDescription);
		d["game_type"] = s(c->scenario->displayGameType);
		d["objectives"] = s(c->scenario->displayObjectives);
		d["fiction"] = s(c->scenario->displayFiction);
		d["min_players"] = c->scenario->minPlayers;
		d["max_players"] = c->scenario->maxPlayers;
		d["historical"] = c->scenario->historicalScenario;
		d["region_campaign"] = s(c->scenario->regionCampaign);
		d["default_start_spots"] = strings(c->scenario->defaultStartSpots);
		d["disabled_factions"] = strings(c->scenario->disabledFactions);
		d["disable_regions"] = strings(c->scenario->disableRegions);
		scenarios.push_back(d);
	}
	out["scenarios"] = scenarios;
	Array templates;
	for (const LivingWorld::PlayerTemplate &t : st.playerTemplates)
	{
		Dictionary d;
		d["name"] = s(t.name);
		d["faction"] = s(t.faction);
		d["default_army_icon"] = s(t.defaultArmyIconName);
		d["build_plot_icon"] = s(t.buildPlotIconName);
		d["faction_icon"] = s(t.factionIcon);
		d["music"] = s(t.music);
		d["garrison_name"] = s(t.garrisonDisplayNameTag);
		templates.push_back(d);
	}
	out["player_templates"] = templates;
	// the PlayerTemplate store's names by index: the lobby's slot template index is a store index (WorldSkirmishSetupSource)
	PackedStringArray factionNames;
	for (int i = 0; i < world->playerTemplates().getPlayerTemplateCount(); ++i)
	{
		factionNames.push_back(s(world->playerTemplates().getNthPlayerTemplate(i)->getName()));
	}
	out["faction_names"] = factionNames;
	// WOTR2 milestone 4: the RTS PlayerTemplates' evil flag by name (+ 0x1BC: ShowEndGameSplashScreen RW 0x6B694D picks the good / evil splash and cheer)
	Dictionary evil;
	for (int i = 0; i < world->playerTemplates().getPlayerTemplateCount(); ++i)
	{
		const ::PlayerTemplate *pt = world->playerTemplates().getNthPlayerTemplate(i);
		evil[s(pt->getName())] = pt->m_evil;
	}
	out["faction_evil"] = evil;
	// the lobby colours (Multiplayer.ini MultiplayerColor, the slot's colour index), 0xRRGGBB
	Array colors;
	for (const GameLogicSettings::MultiplayerColorDef &c : gw->logic_settings().multiplayerColors)
	{
		Dictionary cd;
		cd["rgb"] = (int64_t)c.rgb;
		cd["living_world"] = (int64_t)c.livingWorldRgb;        // MultiplayerColor LivingWorldColor (the region fill)
		cd["living_world_banner"] = (int64_t)c.livingWorldBannerRgb; // LivingWorldBannerColor
		colors.push_back(cd);
	}
	out["colors"] = colors;
	Array campaigns;
	for (const RegionCampaign &rc : st.regionCampaigns)
	{
		Dictionary d;
		d["name"] = s(rc.name);
		d["effects"] = s(rc.regionEffectsManagerName);
		d["army_placement"] = spots(rc.armyPlacementPos);
		Array regions;
		for (const Region &r : rc.regions)
		{
			Dictionary rd;
			rd["name"] = s(r.name);
			rd["index"] = r.index;
			rd["display_name"] = s(r.displayName);
			rd["sub_object"] = s(r.subObject);
			rd["map_name"] = s(r.mapName);
			rd["portrait"] = s(r.regionPortrait);
			rd["still_image"] = s(r.skirmishStillImage);
			rd["center"] = v2(r.centerPoint);
			rd["custom_center"] = r.customCenterPoint;
			Array conns;
			for (const RegionConnection &c : r.connections)
			{
				conns.push_back(s(c.region));
			}
			rd["connections"] = conns;
			rd["hero_spots"] = spots(r.heroArmySpots);
			rd["garrison_spots"] = spots(r.garrisonArmySpots);
			rd["building_spots"] = spots(r.buildingSpots);
			rd["army_placement"] = spots(r.armyPlacementPos);
			rd["resource_bonus"] = r.resourceBonus;
			rd["attack_bonus"] = r.attackBonus;
			rd["defense_bonus"] = r.defenseBonus;
			rd["experience_bonus"] = r.experienceBonus;
			rd["legendary_bonus"] = r.legendaryBonus;
			rd["army_bonus"] = r.armyBonus;               // lane WOTR-2: the territory page's command point line (+ 0x94)
			rd["fertile_bonus"] = r.fertileTerritoryBonus; // lane WOTR-2: its treasury income line (+ 0xA8)
			rd["cp_limit"] = r.cpLimit;
			rd["auto_fort"] = r.createAutoFort;
			rd["fortress_portrait"] = s(r.fortressPortrait); // lane WOTR-2: the structures page's fortress icon (+ 0x140)
			regions.push_back(rd);
		}
		d["regions"] = regions;
		campaigns.push_back(d);
	}
	out["region_campaigns"] = campaigns;
	Dictionary mi;
	mi["defined"] = st.mapInfo.defined;
	mi["map_object"] = s(st.mapInfo.mapObject);
	mi["center"] = v2(st.mapInfo.center);
	mi["extent"] = v2(st.mapInfo.extent);
	mi["apt_center"] = v2(st.mapInfo.aptCenter);
	mi["apt_zoom"] = st.mapInfo.aptZoom;
	mi["apt_pitch"] = st.mapInfo.aptPitch;
	mi["camera_bound_x"] = st.mapInfo.cameraBoundX;
	mi["camera_bound_y"] = st.mapInfo.cameraBoundY;
	mi["army_height"] = st.mapInfo.armyHeight;
	mi["banner_height"] = st.mapInfo.bannerHeight;
	mi["cloud_border_sub_object"] = s(st.mapInfo.cloudBorderSubObject);
	mi["text_layer_sub_object"] = s(st.mapInfo.textLayerSubObject);
	mi["battle_marker"] = s(st.mapInfo.battleMarker);
	mi["sun_rgb"] = Color(st.mapInfo.sunRGB.red, st.mapInfo.sunRGB.green, st.mapInfo.sunRGB.blue);
	// lane WOTR-2: the map's two accent lights (LivingWorldMapInfo Accent1Dir / RGB, Accent2Dir / RGB) and the army banners (MenBanner .. AngmarBanner,
	// BannerMaxScale, BannerTiltAngle)
	mi["accent1_dir"] = v2(st.mapInfo.accent1Dir);
	mi["accent1_rgb"] = Color(st.mapInfo.accent1RGB.red, st.mapInfo.accent1RGB.green, st.mapInfo.accent1RGB.blue);
	mi["accent2_dir"] = v2(st.mapInfo.accent2Dir);
	mi["accent2_rgb"] = Color(st.mapInfo.accent2RGB.red, st.mapInfo.accent2RGB.green, st.mapInfo.accent2RGB.blue);
	{
		static const char *const kBannerSides[8] = { "Men", "Elves", "Dwarves", "Isengard", "Mordor", "Wild", "Neutral", "Angmar" };
		Dictionary banners;
		for (int i = 0; i < 8; ++i)
		{
			banners[kBannerSides[i]] = s(st.mapInfo.banners[i]);
		}
		mi["banners"] = banners;
	}
	mi["banner_max_scale"] = st.mapInfo.bannerMaxScale;
	mi["banner_tilt_angle"] = st.mapInfo.bannerTiltAngle;
	mi["ambient"] = Color(st.mapInfo.ambient.red, st.mapInfo.ambient.green, st.mapInfo.ambient.blue);
	mi["sun_dir"] = v2(st.mapInfo.sunDir);
	mi["army_line_attacking"] = Color(st.mapInfo.armyLineColorAttacking.red, st.mapInfo.armyLineColorAttacking.green, st.mapInfo.armyLineColorAttacking.blue);
	mi["army_line_neutral"] = Color(st.mapInfo.armyLineColorNeutral.red, st.mapInfo.armyLineColorNeutral.green, st.mapInfo.armyLineColorNeutral.blue);
	// lane WOTR-2: the rest of the army line settings (NumPointsPerArmyLine, ArmyLineHeightBias, ArmyLineWidth, ArmyLineColorAllied, ArmyLineTextureName)
	mi["army_line_allied"] = Color(st.mapInfo.armyLineColorAllied.red, st.mapInfo.armyLineColorAllied.green, st.mapInfo.armyLineColorAllied.blue);
	mi["army_line_points"] = (int64_t)st.mapInfo.numPointsPerArmyLine;
	mi["army_line_height_bias"] = st.mapInfo.armyLineHeightBias;
	mi["army_line_width"] = st.mapInfo.armyLineWidth;
	mi["army_line_texture"] = s(st.mapInfo.armyLineTextureName);
	out["map_info"] = mi;
	Array effects;
	for (const RegionEffects &e : st.regionEffects)
	{
		Dictionary d;
		d["name"] = s(e.name);
		d["region_object"] = s(e.regionObject);
		d["neutral_color"] = Color(e.neutralRegionColor.red, e.neutralRegionColor.green, e.neutralRegionColor.blue);
		d["border_color"] = Color(e.regionBorderColor.red, e.regionBorderColor.green, e.regionBorderColor.blue);
		d["shell_start_color"] = Color(e.shellStartPositionColor.red, e.shellStartPositionColor.green, e.shellStartPositionColor.blue); // lane WOTR-2
		effects.push_back(d);
	}
	out["region_effects"] = effects;
	Array icons;
	for (const IconTemplate &t : st.icons)
	{
		icons.push_back(iconDict(t));
	}
	out["icons"] = icons;
	Array buildings;
	for (const BuildingTemplate &b : st.buildings)
	{
		Dictionary d;
		d["name"] = s(b.name);
		d["icon"] = s(b.buildingIcon);
		d["type"] = b.type;
		d["cost"] = b.strategicResourceCost;
		d["turns"] = b.turnsToBuild;
		d["display_name"] = s(b.displayNameTag);
		d["button_image"] = s(b.constructButtonImage);
		d["button_title"] = s(b.constructButtonTitle); // WOTR2 milestone 4: the palantir's BuildBuildingButton help title (RW 0x9FC310)
		d["button_help"] = s(b.constructButtonHelp);
		d["battle_template"] = s(b.battleThingTemplate);
		buildings.push_back(d);
	}
	out["buildings"] = buildings;
	// every ThingTemplate a LivingWorldPlayerArmy lists (the units the strategic game can carry: persistent units after a battle)
	PackedStringArray armyTemplates;
	for (const PlayerArmy &pa : st.playerArmies)
	{
		for (const ArmyEntry &e : pa.entries)
		{
			if (!armyTemplates.has(s(e.thingTemplate)))
			{
				armyTemplates.push_back(s(e.thingTemplate));
			}
		}
	}
	out["army_templates"] = armyTemplates;
	out["stops"] = strings(stopLines());
	out["ok"] = true;
	return out;
}

Dictionary LivingWorldGame::orderResult(bool ok, const std::string &error) const
{
	Dictionary d;
	d["ok"] = ok;
	d["error"] = s(error);
	return d;
}

Dictionary LivingWorldGame::start(const Dictionary &setup)
{
	if (!m_stores)
	{
		return orderResult(false, "setup() has not run");
	}
	GameSetup gs;
	gs.campaign = n(setup.get("campaign", ""));
	gs.seed = (std::uint32_t)(int64_t)setup.get("seed", 0);
	const Array slots = setup.get("slots", Array());
	for (int i = 0; i < slots.size(); ++i)
	{
		const Dictionary d = slots[i];
		SetupSlot slot;
		slot.name = n(d.get("name", ""));
		slot.faction = n(d.get("faction", ""));
		slot.human = (bool)d.get("human", false);
		slot.aiDifficulty = (int)d.get("ai", 0);
		slot.team = (int)d.get("team", -1);
		slot.color = (int)d.get("color", -1);
		slot.startRegion = n(d.get("start_region", ""));
		slot.handicap = (int)d.get("handicap", 0); // RW 0x932EB8: -(slot + 0x20), the AutoResolveHandicapLevel lookup's level
		gs.slots.push_back(slot);
	}
	gs.ranks = m_ranks;
	gs.rankTemplates = m_rankTemplates;
	gs.resourceMultiplierLimit = m_resourceMultiplierLimit;
	detachNetwork(); // WOTR2 milestone 8 (Sol wotr2m8s2b): a new war never inherits the last one's network session
	m_logic = std::make_unique<LivingWorldLogic>(*m_stores, m_units);
	m_messageLog.clear(); // WOTR2 milestone 8: a new war, a new command stream
	std::string error;
	const bool ok = m_logic->start(gs, &error);
	if (!ok)
	{
		m_logic.reset();
	}
	return orderResult(ok, error);
}

void LivingWorldGame::update()
{
	if (m_logic && !m_session) // WOTR2 milestone 8: a LAN war advances only on its lockstep frames (net_tick)
	{
		m_logic->update();
	}
}

Dictionary LivingWorldGame::save_state() const
{
	Dictionary out;
	out["ok"] = false;
	if (!m_logic)
	{
		out["error"] = "no War of the Ring game runs";
		return out;
	}
	if (m_session)
	{
		// WOTR2 milestone 8 (Sol wotr2m8s2b): RotWK saves a network war's map (GameState mode 6, its quit menu's Save, RW 0x921E3C) only with the other
		// players' agreement: a direct save is refused (step 4: net_save_ask, then lan_save_state on every peer at one frame)
		out["error"] = "a LAN War of the Ring is saved when the other players agree: the quit menu's Save asks them (RW 0x629750)";
		return out;
	}
	std::vector<std::uint8_t> bytes;
	std::string error;
	if (!m_logic->saveState(bytes, &error))
	{
		out["error"] = String::utf8(error.c_str());
		return out;
	}
	PackedByteArray state;
	state.resize((int64_t)bytes.size());
	if (!bytes.empty())
	{
		std::memcpy(state.ptrw(), bytes.data(), bytes.size());
	}
	out["ok"] = true;
	out["state"] = state;
	out["state_hash"] = (int64_t)m_logic->stateHash();
	out["campaign"] = String::utf8(m_logic->campaign().name.c_str());
	out["turn"] = (int64_t)m_logic->turn();
	return out;
}

std::string LivingWorldGame::loadChecked(const PackedByteArray &state, int64_t state_hash, int64_t players, bool battle,
	std::unique_ptr<LivingWorldLogic> &out, int networkedTimer, int battleChoice) const
{
	if (!m_stores)
	{
		return "setup() has not run";
	}
	GameSetup data;
	data.ranks = m_ranks;
	data.rankTemplates = m_rankTemplates;
	data.resourceMultiplierLimit = m_resourceMultiplierLimit;
	const std::vector<std::uint8_t> bytes(state.ptr(), state.ptr() + state.size());
	auto logic = std::make_unique<LivingWorldLogic>(*m_stores, m_units);
	std::string error;
	if (!logic->loadState(data, bytes, &error, battle))
	{
		return error;
	}
	if (networkedTimer >= 0)
	{
		logic->setNetworked(networkedTimer, battleChoice);
	}
	if ((std::uint32_t)state_hash != logic->stateHash())
	{
		return "the loaded War of the Ring game's state hash " + std::to_string(logic->stateHash()) + " is not the save's " +
			std::to_string((std::uint32_t)state_hash);
	}
	if ((int64_t)logic->players().size() != players)
	{
		return "the War of the Ring save's session does not fit its game (" + std::to_string(players) + " slots, " +
			std::to_string(logic->players().size()) + " players)";
	}
	out = std::move(logic);
	return std::string();
}

Dictionary LivingWorldGame::check_state(const PackedByteArray &state, int64_t state_hash, int64_t players, bool battle) const
{
	std::unique_ptr<LivingWorldLogic> logic;
	const std::string error = loadChecked(state, state_hash, players, battle, logic);
	return orderResult(error.empty(), error);
}

Dictionary LivingWorldGame::load_state(const PackedByteArray &state, int64_t state_hash, int64_t players, bool battle)
{
	if (m_session)
	{
		// WOTR2 milestone 8 (Sol wotr2m8s2b): a LAN war's quit menu has no Load (RotWK RW 0x921F83 disables it in every network game); nothing replaces the
		// logic under a running session
		return orderResult(false, "a LAN War of the Ring game cannot load a save");
	}
	std::unique_ptr<LivingWorldLogic> logic;
	const std::string error = loadChecked(state, state_hash, players, battle, logic);
	if (!error.empty())
	{
		return orderResult(false, error);
	}
	if (!battle)
	{
		logic->reseedAfterLoad(); // retail's InitRandom(0) after a map save's load (RW 0x6DEA25: types 4 / 6 only; a battle's save, type 3, keeps its generator)
	}
	m_logic = std::move(logic);
	m_messageLog.clear(); // WOTR2 milestone 8: the loaded war's stream starts here
	return orderResult(true, std::string());
}

int64_t LivingWorldGame::get_state_hash() const
{
	return m_logic ? (int64_t)m_logic->stateHash() : 0;
}

Dictionary LivingWorldGame::get_strategic_state() const
{
	Dictionary out;
	if (!m_logic)
	{
		out["ok"] = false;
		return out;
	}
	const LivingWorldLogic &lw = *m_logic;
	out["ok"] = true;
	out["turn"] = lw.turn();
	out["phase"] = lw.phase();
	out["phase_name"] = turnPhaseName(lw.phase());
	out["finished"] = lw.finished();
	out["winning_team"] = lw.winningTeam();
	out["campaign"] = s(lw.campaign().name);
	out["hash"] = (int64_t)lw.stateHash();
	Array players;
	for (int p = 0; p < (int)lw.players().size(); ++p)
	{
		const PlayerState &ps = lw.players()[(size_t)p];
		Dictionary d;
		d["name"] = s(ps.name);
		d["faction"] = s(ps.faction);
		d["template"] = s(ps.lwTemplate);
		{
			const LivingWorld::PlayerTemplate *pt = m_stores ? m_stores->findPlayerTemplate(ps.lwTemplate) : nullptr;
			d["faction_icon"] = s(pt ? pt->factionIcon : std::string()); // the LivingWorldPlayerTemplate's FactionIcon (+0x28)
		}
		d["human"] = ps.human;
		d["team"] = ps.team;
		d["color"] = ps.color;
		d["capital"] = ps.capital;
		d["treasury"] = lw.treasury(p);
		d["income"] = lw.incomePerTurn(p);
		d["last_income"] = ps.lastIncome;
		d["ended_phase"] = ps.endedTurn; // WOTR2 milestone 6: + 0x344 (the retreat phase's END PHASE)
		{
			// lane WOTR-2: the strategic HUD's stats (LivingWorldLogic::hudStats: RW 0x97E280's reads)
			const LivingWorldLogic::HudStats hs = lw.hudStats(p);
			d["power_points"] = hs.powerPoints;
			// RW 0x97E2F6: fild (exact), fmul by 0.01 (RW 0xBE5600), fadd 1.0 on the x87 at 24-bit precision
			d["resource_multiplier"] = (double)NumericState::pc24Add(NumericState::pc24Mul(SimMath::sseFromInt32(hs.resourceMultiplierPercent), 0.01f), 1.0f);
			Array bonus;
			bonus.push_back(hs.attack);
			bonus.push_back(hs.defense);
			bonus.push_back(hs.experience);
			d["hud_bonus"] = bonus;
		}
		d["cp_limit"] = lw.commandPointLimit(p);
		d["cp_used"] = lw.armiesCommandPoints(p); // the Palantir resource bar's "used" (RW 0x6D577C: the armies, not the queues)
		d["cp_queued"] = lw.players()[(size_t)p].queuedCommandPoints;
		d["regions"] = lw.regionCount(p);
		d["defeated"] = ps.defeated;
		d["ended_turn"] = ps.endedTurn;
		players.push_back(d);
	}
	out["players"] = players;
	Array regions;
	for (int r = 0; r < (int)lw.regions().size(); ++r)
	{
		const RegionState &rs = lw.regions()[(size_t)r];
		Dictionary d;
		d["owner"] = rs.owner;
		d["enabled"] = rs.enabled;
		d["free_slots"] = lw.freeBuildingSlots(r);
		Array buildings;
		for (const Building &b : rs.buildings)
		{
			Dictionary bd;
			bd["template"] = s(b.templateName);
			bd["turns_left"] = b.turnsLeft;
			bd["slot"] = b.slot;
			buildings.push_back(bd);
		}
		d["buildings"] = buildings;
		// each building's queue in order; turns_left is GetBuildTimeAtIndex (WB 0x1643860: the head's BuildTime less its turns spent, the others their
		// BuildTime), queue_index the entry's place in its building's queue
		Array production;
		std::map<int, int> queuePlace;
		for (const ProductionOrder &o : rs.production)
		{
			Dictionary od;
			od["slot"] = o.buildingSlot;
			od["army_index"] = o.armyIndex;
			const int place = queuePlace[o.buildingSlot]++;
			od["queue_index"] = place;
			int buildTime = 0, spent = 0;
			for (const Building &b : rs.buildings)
			{
				if (b.slot != o.buildingSlot)
				{
					continue;
				}
				spent = b.turnsSpent;
				const BuildingTemplate *bt = m_stores->findBuilding(b.templateName);
				const BuildingNugget *nug = bt ? bt->findNugget(BuildingNugget::SPAWN_ARMY) : nullptr;
				if (nug && o.armyIndex >= 0 && o.armyIndex < (int)nug->armiesToSpawn.size())
				{
					buildTime = nug->armiesToSpawn[(size_t)o.armyIndex].buildTime;
				}
			}
			const int left = place == 0 ? buildTime - spent : buildTime;
			od["turns_left"] = left < 0 ? 0 : left;
			production.push_back(od);
		}
		d["production"] = production;
		regions.push_back(d);
	}
	out["regions"] = regions;
	Array armies;
	for (const Army &a : lw.armies())
	{
		Dictionary d;
		d["id"] = a.id;
		d["owner"] = a.owner;
		d["region"] = a.region;
		d["destination"] = a.destination;
		d["moved_from"] = a.movedFrom;
		d["retreating"] = a.retreating;
		d["name"] = s(a.scriptingName);
		d["player_army"] = s(a.playerArmy);
		d["hero"] = s(a.heroTemplate);
		d["icon"] = s(a.icon);
		{
			// lane WOTR-2: the strategic HUD's army slot images and garrison name (the owner's LivingWorldPlayerTemplate: FactionIcon + 0x28,
			// GarrisonSelectionPortraitName + 0x38, GarrisonDisplayNameTag + 0x3C)
			const LivingWorld::PlayerTemplate *pt = (a.owner >= 0 && a.owner < (int)lw.players().size()) ? m_stores->findPlayerTemplate(lw.players()[(size_t)a.owner].lwTemplate) : nullptr;
			d["type_image"] = s(pt ? pt->factionIcon : std::string());
			d["garrison_portrait"] = s(pt ? pt->garrisonSelectionPortraitName : std::string());
			d["garrison_name_tag"] = s(pt ? pt->garrisonDisplayNameTag : std::string());
		}
		d["garrison"] = a.garrison;
		d["banner"] = s(a.banner); // WOTR2 milestone 7: the army's banner name (its BannerType for the battle's reinforcement banner, CreateBanner RW 0x616AF0)
		d["strength"] = lw.armyStrength(a);
		d["cp"] = lw.armyCommandPoints(a);
		d["max_cp"] = lw.armyMaxCommandPoints(a);
		Array members;
		for (const ArmyMember &m : a.members)
		{
			Dictionary md;
			md["template"] = s(m.thingTemplate);
			md["quantity"] = m.quantity;
			members.push_back(md);
		}
		d["members"] = members;
		armies.push_back(d);
	}
	out["armies"] = armies;
	Array retreatList; // WOTR2 milestone 6: LWL + 0x10C, list order (the retreat phase's armies)
	for (int id : lw.retreatList())
	{
		retreatList.push_back(id);
	}
	out["retreat_list"] = retreatList;
	Array battles;
	for (const Battle &b : lw.battles())
	{
		Dictionary d;
		d["region"] = b.region;
		d["attacker"] = b.attacker;
		d["defender"] = b.defender;
		d["resolution"] = b.resolution;
		d["winner"] = b.winner;
		Array ids;
		for (int id : b.armies)
		{
			ids.push_back(id);
		}
		d["armies"] = ids;
		// WOTR2 milestone 5: the sides and their autoresolve counters (StrategicConflictResults)
		Array sides;
		for (const Battle::Side &side : b.sides)
		{
			Array players;
			for (const Battle::SidePlayer &p : side.players)
			{
				Dictionary e;
				e["player"] = p.player;
				e["veterancy"] = p.veterancy;
				e["lost"] = p.unitsLost;
				e["destroyed"] = p.enemyDestroyed;
				e["heroes_lost"] = p.heroesLost;
				players.push_back(e);
			}
			sides.push_back(players);
		}
		d["sides"] = sides;
		d["winning_side"] = b.winningSide;
		d["results_counted"] = b.resultsCounted;
		battles.push_back(d);
	}
	out["battles"] = battles;
	Array events;
	for (const StrategicEvent &e : lw.events())
	{
		Dictionary d;
		d["turn"] = e.turn;
		d["phase"] = e.phase;
		d["text"] = s(e.text);
		events.push_back(d);
	}
	out["events"] = events;
	out["pending_battle"] = lw.pendingHumanBattle();
	return out;
}

// WOTR2 milestone 8 (step 1): every strategic order is an MSG_LW_* message on the strategic command stream (LivingWorldLogic::dispatch, RW 0x6BE6A6): the
// stream keeps it (message_log) and runs it now (a single player game; the network steps put it on the lockstep stream instead)
Dictionary LivingWorldGame::post(const GameMessage &m, int sender)
{
	if (!m_logic)
	{
		return orderResult(false, "no campaign");
	}
	LoggedMessage lm;
	lm.turn = m_logic->turn();
	lm.phase = m_logic->phase();
	lm.sender = sender;
	lm.message = m;
	if (m_session)
	{
		// WOTR2 milestone 8 (step 2b): a LAN war: the order goes to every peer and runs on its frame (net_tick reports the result)
		std::string e;
		const bool ok = m_session->post(m, &e);
		lm.ok = ok;
		m_messageLog.push_back(lm);
		Dictionary out = orderResult(ok, e);
		out["queued"] = ok;
		return out;
	}
	std::string e;
	const bool ok = m_logic->dispatch(m, sender, &e);
	lm.ok = ok;
	m_messageLog.push_back(lm);
	return orderResult(ok, e);
}

bool LivingWorldGame::attachNetwork(std::unique_ptr<Transport> transport, const NetworkConfig &config, int phaseTimerSeconds, int battleChoice, std::string *error)
{
	if (!m_logic)
	{
		if (error)
		{
			*error = "no War of the Ring game runs";
		}
		return false;
	}
	m_transport = std::move(transport);
	m_session = std::make_unique<StrategicSession>(*m_transport, config, *m_logic, phaseTimerSeconds, battleChoice);
	m_localPlayer = m_session->localPlayer();
	m_nextFrameMs = 0;
	m_executedSeen = 0;
	return true;
}

Dictionary LivingWorldGame::net_tick()
{
	Dictionary out;
	out["frames"] = 0;
	out["executed"] = Array();
	if (!m_session)
	{
		return out;
	}
	// the strategic frames at the logic rate (5 a second, RW 0xD9F608): the lockstep runs a frame when every peer's commands are in, never faster
	const std::uint64_t now = NetMilliseconds();
	if (m_nextFrameMs == 0 || now > m_nextFrameMs + 1000)
	{
		m_nextFrameMs = now; // the first frame, or a stall of more than a second: the clock starts again (no burst of frames)
	}
	int frames = 0;
	if (now >= m_nextFrameMs && m_session->step())
	{
		m_nextFrameMs += 200;
		frames = 1;
	}
	out["frames"] = frames;
	out["hash"] = (int64_t)m_logic->stateHash(); // the war's state after the frame (the strategic logic: no battle worker to wait for)
	Array executed;
	const std::vector<StrategicSession::Executed> &all = m_session->executed();
	for (; m_executedSeen < all.size(); ++m_executedSeen)
	{
		const StrategicSession::Executed &e = all[m_executedSeen];
		Dictionary d;
		d["frame"] = (int64_t)e.frame;
		d["sender"] = e.sender;
		d["type"] = e.type;
		d["name"] = String(GameMessageTypeName(e.type));
		d["ok"] = e.ok;
		d["error"] = String::utf8(e.error.c_str());
		executed.push_back(d);
	}
	out["executed"] = executed;
	// WOTR2 milestone 8 (step 4): the saves ordered in this frame (every peer writes its own copy now: the state is the frame's end) and the vote's end
	Array orders;
	for (const NetSaveOrder &o : m_logic->saveOrders())
	{
		Dictionary d;
		d["name"] = String::utf16(o.name.c_str(), (int64_t)o.name.size());
		d["asker"] = o.asker;
		d["frame"] = (int64_t)o.frame;
		orders.push_back(d);
	}
	m_logic->clearSaveOrders();
	out["save_orders"] = orders;
	out["save_asker"] = m_logic->saveAsker();
	out["save_name"] = String::utf16(m_logic->saveName().c_str(), (int64_t)m_logic->saveName().size());
	out["leave_vote_open"] = m_logic->leaveVoteOpen();
	out["war_end_agreed"] = m_logic->warEndAgreed();
	out["refused_orders"] = (int64_t)m_logic->refusedOrders(); // (coordinator 10-30) the debug counter of the owner / phase checks' refusals
	Array errors;
	for (const std::string &e : m_session->errors())
	{
		errors.push_back(String::utf8(e.c_str()));
	}
	out["errors"] = errors;
	return out;
}

Dictionary LivingWorldGame::load_lan_state(const PackedByteArray &state, int64_t state_hash, int64_t players, int64_t timer, int64_t battle_choice)
{
	if (m_session)
	{
		return orderResult(false, "a running LAN war cannot load a save (the resume loads before the strategic session begins)");
	}
	std::unique_ptr<LivingWorldLogic> logic;
	const std::string error = loadChecked(state, state_hash, players, false, logic, (int)timer, (int)battle_choice);
	if (!error.empty())
	{
		return orderResult(false, error);
	}
	logic->resumeAfterLanLoad(); // RW 0x6DEA25: a mode-6 load reseeds the war's generator with 0 (InitRandom(0)), as a map save's; the saved vote is over
	m_logic = std::move(logic);
	m_messageLog.clear();
	return orderResult(true, std::string());
}

String LivingWorldGame::check_lan_state(const PackedByteArray &state, int64_t state_hash, int64_t players, int64_t timer, int64_t battle_choice) const
{
	std::unique_ptr<LivingWorldLogic> logic;
	return String::utf8(loadChecked(state, state_hash, players, false, logic, (int)timer, (int)battle_choice).c_str());
}

Dictionary LivingWorldGame::net_save_ask(const String &name)
{
	if (!m_session || !m_logic)
	{
		return orderResult(false, "no LAN War of the Ring runs");
	}
	const Char16String units = name.utf16();
	const int n = units.length();
	std::u16string wide;
	for (int i = 0; i < n; ++i)
	{
		wide.push_back((char16_t)units[i]);
	}
	std::string why;
	if (!LivingWorldLogic::saveNameValid(wide, &why))
	{
		return orderResult(false, why);
	}
	GameMessage m(MSG_LW_OPENBFME_SAVE, m_session->localPlayer());
	m.appendIntegerArgument(LivingWorldLogic::SAVE_ASK);
	m.appendIntegerArgument(n);
	for (int i = 0; i < n; ++i)
	{
		m.appendIntegerArgument((int)(std::uint16_t)units[i]);
	}
	std::string error;
	return orderResult(m_session->post(m, &error), error);
}

Dictionary LivingWorldGame::net_save_answer(bool yes)
{
	if (!m_session || !m_logic)
	{
		return orderResult(false, "no LAN War of the Ring runs");
	}
	GameMessage m(MSG_LW_OPENBFME_SAVE, m_session->localPlayer());
	m.appendIntegerArgument(yes ? LivingWorldLogic::SAVE_YES : LivingWorldLogic::SAVE_REFUSE);
	m.appendIntegerArgument((int)m_logic->saveRequestSerial()); // (Sol wotr2m8s4 follow-up 1) the request this player answers
	std::string error;
	return orderResult(m_session->post(m, &error), error);
}

Dictionary LivingWorldGame::net_leave_vote(bool yes)
{
	if (!m_session || !m_logic)
	{
		return orderResult(false, "no LAN War of the Ring runs");
	}
	GameMessage m(MSG_LW_REQUEST_ENDGAME, m_session->localPlayer());
	m.appendIntegerArgument(yes ? 1 : 0);
	m.appendIntegerArgument((int)m_logic->saveRequestSerial()); // the save request the vote follows
	std::string error;
	return orderResult(m_session->post(m, &error), error);
}

// the strategic state of a LAN war's save (the frame it was ordered on: every peer has the same bytes); "md5": retail's save identity (the 16-byte MD5
// of the strategic state, mode 6's header RW 0x6DEFD0; here over the port's state bytes), "state_hash": the load check
Dictionary LivingWorldGame::lan_save_state() const
{
	Dictionary out;
	out["ok"] = false;
	if (!m_logic || !m_session)
	{
		out["error"] = "no LAN War of the Ring runs";
		return out;
	}
	std::vector<std::uint8_t> bytes;
	std::string error;
	if (!m_logic->saveState(bytes, &error))
	{
		out["error"] = String::utf8(error.c_str());
		return out;
	}
	PackedByteArray state;
	state.resize((int64_t)bytes.size());
	if (!bytes.empty())
	{
		std::memcpy(state.ptrw(), bytes.data(), bytes.size());
	}
	out["ok"] = true;
	out["state"] = state;
	out["state_hash"] = (int64_t)m_logic->stateHash();
	out["md5"] = String(MD5::ofBytes(bytes.data(), bytes.size()).c_str());
	out["campaign"] = String::utf8(m_logic->campaign().name.c_str());
	out["turn"] = (int64_t)m_logic->turn();
	return out;
}

Dictionary LivingWorldGame::net_status() const
{
	Dictionary out;
	out["network"] = m_session != nullptr;
	if (!m_session || !m_logic)
	{
		return out;
	}
	out["frame"] = (int64_t)m_session->frame();
	out["timer"] = m_logic->phaseTimerEnabled() ? m_logic->phaseSecondsRemaining() : -1;
	// StrategicInGameUI's wait helper (RW 0x8385A3): more than one live human and the local player ended its phase while another has not
	bool waiting = false;
	int humans = 0;
	const std::vector<PlayerState> &ps = m_logic->players();
	for (size_t i = 0; i < ps.size(); ++i)
	{
		if (ps[i].human && !ps[i].defeated)
		{
			++humans;
			waiting = waiting || (!ps[i].endedTurn && (int)i != m_localPlayer);
		}
	}
	const bool localDone = m_localPlayer >= 0 && m_localPlayer < (int)ps.size() && ps[(size_t)m_localPlayer].endedTurn;
	out["waiting"] = humans > 1 && localDone && waiting;
	Array dropped;
	for (int s = 0; s < MAX_SLOTS; ++s)
	{
		if (m_session->network().isDisconnected(s))
		{
			dropped.push_back(s);
		}
	}
	out["dropped"] = dropped;
	out["desyncs"] = (int64_t)m_session->network().desyncs().size(); // the CRC exchange's reports (each names the frame and both hashes)
	if (!m_session->network().desyncs().empty())
	{
		out["desync"] = String::utf8(m_session->network().desyncs().front().text().c_str());
	}
	out["quit_requested"] = m_session->network().quitRequested();
	out["quit_reason"] = String::utf8(m_session->network().quitReason().c_str());
	Array errors; // the session's and its network's protocol errors (never silent)
	for (const std::string &e : m_session->errors())
	{
		errors.push_back(String::utf8(e.c_str()));
	}
	out["errors"] = errors;
	return out;
}

void LivingWorldGame::net_leave()
{
	if (m_session)
	{
		m_session->leave();
		m_session->step(); // the leave goes out
	}
	detachNetwork(); // WOTR2 milestone 8 (Sol wotr2m8s2b): the war is left; the logic stays for the score screen, a single player game after it
}

void LivingWorldGame::detachNetwork()
{
	m_session.reset(); // before the transport it uses
	m_transport.reset();
	m_nextFrameMs = 0;
	m_executedSeen = 0;
}

Dictionary LivingWorldGame::order_move(int64_t player, int64_t army, int64_t region)
{
	GameMessage m(MSG_LW_ARMY_MOVE_REQUEST, (int)player);
	m.appendIntegerArgument((int)army);
	m.appendIntegerArgument((int)region);
	m.appendLocationArgument(Coord3D{}); // the drop point (the port moves region to region)
	return post(m, (int)player);
}

namespace
{
std::vector<int> intList(const Array &a)
{
	std::vector<int> out;
	for (int i = 0; i < a.size(); ++i)
	{
		out.push_back((int)(int64_t)a[i]);
	}
	return out;
}
} // namespace

// WOTR2 milestone 4: MSG_LW_SWAP_ARMY_MEMBERS 0x6AF (a plain move: keys_b empty) and MSG_LW_CANCEL_ARMY_MEMBER_MOVE 0x6B0
bool LivingWorldGame::can_swap_army_members(int64_t a, const Array &keysA, int64_t b, const Array &keysB) const
{
	return m_logic && m_logic->canSwapArmyMembers((int)a, intList(keysA), (int)b, intList(keysB));
}

bool LivingWorldGame::can_move_army_members_no_room(int64_t a, const Array &keysA, int64_t b) const
{
	return m_logic && m_logic->canSwapArmyMembers((int)a, intList(keysA), (int)b, std::vector<int>(), false);
}

Dictionary LivingWorldGame::swap_army_members(int64_t a, const Array &keysA, int64_t b, const Array &keysB)
{
	const Army *army = m_logic ? m_logic->findArmy((int)a) : nullptr;
	const int sender = army ? army->owner : m_localPlayer; // the swapper moves the local player's own members
	GameMessage m(MSG_LW_SWAP_ARMY_MEMBERS, sender);
	m.appendIntegerArgument((int)a);
	m.appendIntegerArgument((int)keysA.size());
	for (int k : intList(keysA))
	{
		m.appendIntegerArgument(k);
	}
	m.appendIntegerArgument((int)b);
	m.appendIntegerArgument((int)keysB.size());
	for (int k : intList(keysB))
	{
		m.appendIntegerArgument(k);
	}
	return post(m, sender);
}

Dictionary LivingWorldGame::cancel_army_member_move(int64_t army, int64_t key)
{
	const Army *a = m_logic ? m_logic->findArmy((int)army) : nullptr;
	const int sender = a ? a->owner : m_localPlayer;
	GameMessage m(MSG_LW_CANCEL_ARMY_MEMBER_MOVE, sender);
	m.appendIntegerArgument((int)army);
	m.appendIntegerArgument((int)key);
	return post(m, sender);
}

Dictionary LivingWorldGame::cancel_move(int64_t player, int64_t army)
{
	GameMessage m(MSG_LW_CANCEL_ARMY_MOVE_REQUEST, (int)player);
	m.appendIntegerArgument((int)army);
	return post(m, (int)player);
}

Dictionary LivingWorldGame::order_build(int64_t player, int64_t region, const String &building, int64_t plot)
{
	if (!m_stores)
	{
		return orderResult(false, "no campaign");
	}
	const std::string name = n(building);
	int index = -1;
	for (size_t i = 0; i < m_stores->buildings.size(); ++i)
	{
		index = m_stores->buildings[i].name == name ? (int)i : index;
	}
	if (index < 0)
	{
		return orderResult(false, "no LivingWorldBuilding '" + name + "'");
	}
	GameMessage m(MSG_LW_BUILD_BUILDING, (int)player);
	m.appendIntegerArgument((int)region);
	m.appendIntegerArgument((int)plot);
	m.appendIntegerArgument(index);
	return post(m, (int)player);
}

Dictionary LivingWorldGame::disband_army_member(int64_t player, int64_t army, int64_t key)
{
	GameMessage m(MSG_LW_DISBAND_ARMY_MEMBER, (int)player);
	m.appendIntegerArgument((int)army);
	m.appendIntegerArgument((int)key);
	return post(m, (int)player);
}

bool LivingWorldGame::can_disband_army_member(int64_t army, int64_t key) const
{
	return m_logic && m_logic->canDisbandArmyMember((int)army, (int)key);
}

Dictionary LivingWorldGame::order_upgrade_army_member(int64_t player, int64_t army, int64_t key)
{
	GameMessage m(MSG_LW_MARK_UNIT_FOR_UPGRADE, (int)player);
	m.appendIntegerArgument((int)army);
	m.appendIntegerArgument((int)key);
	return post(m, (int)player);
}

Dictionary LivingWorldGame::cancel_upgrade_army_member(int64_t player, int64_t army, int64_t key)
{
	GameMessage m(MSG_LW_UNMARK_UNIT_FOR_UPGRADE, (int)player);
	m.appendIntegerArgument((int)army);
	m.appendIntegerArgument((int)key);
	return post(m, (int)player);
}

bool LivingWorldGame::can_upgrade_army_member(int64_t army, int64_t key) const
{
	return m_logic && m_logic->canUpgradeArmyMember((int)army, (int)key);
}

Dictionary LivingWorldGame::army_help(Object *game_world, int64_t army_id) const
{
	Dictionary out;
	GameWorld *gw = Object::cast_to<GameWorld>(game_world);
	RetailObjectWorld *world = gw ? gw->object_world() : nullptr;
	const Army *army = m_logic ? m_logic->findArmy((int)army_id) : nullptr;
	if (!world || !army || !m_stores || !m_stores->armySummaryDescription.defined)
	{
		return out;
	}
	std::vector<std::pair<std::string, int>> entries;
	std::vector<const ArmyMember *> members;
	for (const ArmyMember &m : army->members)
	{
		members.push_back(&m);
	}
	std::sort(members.begin(), members.end(), [](const ArmyMember *a, const ArmyMember *b) { return a->key < b->key; }); // the summary's key order
	for (const ArmyMember *m : members)
	{
		entries.push_back(std::make_pair(m->thingTemplate, m->quantity));
	}
	const ArmyDescription d = describeArmy(m_stores->armySummaryDescription, world->things(), entries);
	// RW 0x620542: the summary's name label (+0x64). INFERENCE [S-1334]: a garrison's is its GarrisonDisplayNameTag; a hero army's is not known (none)
	const LivingWorld::PlayerTemplate *pt =
		(army->owner >= 0 && army->owner < (int)m_logic->players().size()) ? m_stores->findPlayerTemplate(m_logic->players()[(size_t)army->owner].lwTemplate) : nullptr;
	out["title"] = army->garrison && pt ? s(pt->garrisonDisplayNameTag) : String();
	Array heroes;
	for (const std::string &h : d.heroLabels)
	{
		heroes.push_back(s(h));
	}
	out["heroes"] = heroes;
	Array lines;
	for (const auto &l : d.lines)
	{
		Dictionary e;
		e["label"] = s(l.first);
		e["count"] = l.second;
		lines.push_back(e);
	}
	out["lines"] = lines;
	return out;
}

Dictionary LivingWorldGame::building_help(const String &building) const
{
	Dictionary out;
	const BuildingTemplate *t = m_stores ? m_stores->findBuilding(n(building)) : nullptr;
	if (!t)
	{
		return out;
	}
	out["title"] = s(t->displayNameTag);
	out["text"] = s(t->displayDescriptionTag);
	int farm = -1;
	if (t->type == BUILDING_FORTRESS || t->type == BUILDING_RESOURCE) // RW 0x6E0FC4: the first IncreaseTreasury nugget (RW 0x6E0D46)
	{
		if (const BuildingNugget *nug = t->findNugget(BuildingNugget::INCREASE_TREASURY))
		{
			farm = nug->treasureAmount;
		}
	}
	out["farm"] = farm;
	return out;
}

Array LivingWorldGame::take_notices()
{
	Array out;
	if (!m_logic)
	{
		return out;
	}
	for (const StrategicNotice &n : m_logic->notices())
	{
		Dictionary d;
		d["kind"] = n.kind;
		d["region"] = n.region; // WOTR2 milestone 8: RTS_DEMOTED [S-1342]
		d["player"] = n.player;
		d["hero"] = s(n.hero);
		d["defeated"] = n.defeated;
		Array rep;
		for (const auto &r : n.repelled)
		{
			Dictionary e;
			e["hero"] = s(r.first);
			e["region"] = r.second;
			rep.push_back(e);
		}
		d["repelled"] = rep;
		out.push_back(d);
	}
	m_logic->clearNotices();
	return out;
}

Dictionary LivingWorldGame::region_stats(int64_t player, int64_t region) const
{
	Dictionary d;
	if (!m_logic)
	{
		return d;
	}
	int v[3][2];
	m_logic->regionStats((int)player, (int)region, v);
	const char *const keys[3] = { "build_plots", "armory_points", "command_points" };
	for (int i = 0; i < 3; ++i)
	{
		Array a;
		a.push_back(v[i][0]);
		a.push_back(v[i][1]);
		d[keys[i]] = a;
	}
	return d;
}

bool LivingWorldGame::can_move_army_member(int64_t army, int64_t key, int64_t target) const
{
	return m_logic && m_logic->canMoveArmyMember((int)army, (int)key, (int)target);
}

Dictionary LivingWorldGame::order_recruit(int64_t player, int64_t region, int64_t building_slot, int64_t army_index)
{
	const int id = m_logic ? m_logic->buildingIdAt((int)region, (int)building_slot) : 0;
	if (id == 0)
	{
		return orderResult(false, m_logic ? "no building in that plot" : "no campaign");
	}
	GameMessage m(MSG_LW_QUEUE_UNIT, (int)player);
	m.appendIntegerArgument(id);
	m.appendIntegerArgument((int)army_index + 1);
	return post(m, (int)player);
}

Dictionary LivingWorldGame::end_phase(int64_t player)
{
	if (!m_logic)
	{
		return orderResult(false, "no campaign");
	}
	GameMessage m(MSG_LW_DONE_PHASE, (int)player); // RW 0x6B6BA1: (turn, phase, the player)
	m.appendIntegerArgument(m_logic->turn());
	m.appendIntegerArgument(m_logic->phase());
	m.appendIntegerArgument((int)player);
	return post(m, (int)player);
}

bool LivingWorldGame::can_end_phase(int64_t player) const
{
	return m_logic && m_logic->canEndPhase((int)player);
}

bool LivingWorldGame::can_order_move(int64_t player, int64_t army, int64_t region) const
{
	return m_logic && m_logic->canOrderMove((int)player, (int)army, (int)region);
}

Dictionary LivingWorldGame::end_turn(int64_t player)
{
	if (!m_logic)
	{
		return orderResult(false, "no campaign");
	}
	GameMessage m(MSG_LW_DONE_PHASE, (int)player); // RW 0x6B6BA1: (turn, phase, the player)
	m.appendIntegerArgument(m_logic->turn());
	m.appendIntegerArgument(m_logic->phase());
	m.appendIntegerArgument((int)player);
	return post(m, (int)player);
}

Array LivingWorldGame::buildable_in(int64_t player, int64_t region) const
{
	Array out;
	if (!m_logic)
	{
		return out;
	}
	for (const std::string &b : m_logic->buildableIn((int)player, (int)region))
	{
		out.push_back(s(b));
	}
	return out;
}

Dictionary LivingWorldGame::order_dequeue_at(int64_t player, int64_t region, int64_t building_slot, int64_t queue_index)
{
	const int id = m_logic ? m_logic->buildingIdAt((int)region, (int)building_slot) : 0;
	if (id == 0)
	{
		return orderResult(false, m_logic ? "no building in that plot" : "no campaign");
	}
	GameMessage m(MSG_LW_DEQUEUE_UNIT_AT_INDEX, (int)player);
	m.appendIntegerArgument(id);
	m.appendIntegerArgument((int)queue_index);
	return post(m, (int)player);
}

Dictionary LivingWorldGame::order_destroy_building(int64_t player, int64_t region, int64_t building_slot)
{
	GameMessage m(MSG_LW_DESTROY_BUILDING, (int)player);
	m.appendIntegerArgument((int)region);
	m.appendIntegerArgument((int)building_slot);
	return post(m, (int)player);
}

bool LivingWorldGame::can_recruit(int64_t player, int64_t region, int64_t building_slot, int64_t army_index) const
{
	return m_logic && m_logic->canRecruit((int)player, (int)region, (int)building_slot, (int)army_index, nullptr);
}

Array LivingWorldGame::recruitable_in(int64_t player, int64_t region) const
{
	Array out;
	if (!m_logic || region < 0 || region >= (int64_t)m_logic->regions().size() || m_logic->regions()[(size_t)region].owner != (int)player)
	{
		return out;
	}
	for (const Building &b : m_logic->regions()[(size_t)region].buildings)
	{
		const BuildingTemplate *bt = m_stores->findBuilding(b.templateName);
		const BuildingNugget *nug = bt ? bt->findNugget(BuildingNugget::SPAWN_ARMY) : nullptr;
		if (!nug || b.turnsLeft > 0)
		{
			continue;
		}
		Dictionary d;
		d["slot"] = b.slot;
		d["building"] = s(b.templateName);
		d["queue_size"] = nug->queueSize;
		Array armies;
		for (int i = 0; i < (int)nug->armiesToSpawn.size(); ++i)
		{
			const SpawnArmy &sa = nug->armiesToSpawn[(size_t)i];
			// lane WOTR-2: canBuildUnit (WB 0xD98FE0, the IDs the radial lists): a Create-a-Hero army only with a Create-a-Hero profile, none here (S-1326)
			if (const PlayerArmy *pa = m_stores->findPlayerArmy(sa.playerArmy))
			{
				if (!pa->entries.empty() && m_units(pa->entries.front().thingTemplate).createAHero)
				{
					continue;
				}
			}
			Dictionary ad;
			ad["index"] = i;
			ad["player_army"] = s(sa.playerArmy);
			ad["hero"] = s(sa.heroTemplateName);
			ad["build_time"] = sa.buildTime;
			ad["image"] = s(sa.constructButtonImage);
			ad["title"] = s(sa.constructButtonTitle);
			int cost = 0, cp = 0;
			int unitType = -1, firstQuantity = 0, firstCp = 0;
			if (const PlayerArmy *pa = m_stores->findPlayerArmy(sa.playerArmy))
			{
				cost = m_logic->recruitCost((int)player, *pa);    // RW 0x80F018
				cp = m_logic->recruitCommandPoints(*pa);          // RW 0x8E5C5C
				if (!pa->entries.empty()) // lane WOTR-2: the build queue icon's type image and quantity come from the army's first entry (WB 0x1643860)
				{
					const UnitInfo first = m_units(pa->entries.front().thingTemplate);
					unitType = first.autoResolveUnitType;
					firstQuantity = pa->entries.front().quantity;
					firstCp = first.commandPoints; // the panel's CP line: the thing's CommandPoints (+ 0x628)
				}
			}
			ad["cost"] = cost;
			ad["cp"] = cp;
			ad["unit_type"] = unitType;
			ad["first_quantity"] = firstQuantity;
			ad["first_cp"] = firstCp;
			armies.push_back(ad);
		}
		d["armies"] = armies;
		out.push_back(d);
	}
	return out;
}

int64_t LivingWorldGame::pending_human_battle() const
{
	return m_logic ? m_logic->pendingHumanBattle() : -1;
}

Dictionary LivingWorldGame::choose_auto_resolve(int64_t battle)
{
	GameMessage m(MSG_LW_SET_CONFLICT_RESOLUTION_METHOD, m_localPlayer); // SendResolutionMethodMessage RW 0x9DF012: (method, battle)
	m.appendIntegerArgument(1);
	m.appendIntegerArgument((int)battle);
	return post(m, m_localPlayer);
}

Dictionary LivingWorldGame::choose_retreat(int64_t battle, int64_t player)
{
	GameMessage m(MSG_LW_SET_CONFLICT_RESOLUTION_METHOD, (int)player);
	m.appendIntegerArgument(2);
	m.appendIntegerArgument((int)battle);
	return post(m, (int)player);
}

int64_t LivingWorldGame::resolution_options(int64_t battle, int64_t player) const
{
	return m_logic ? m_logic->resolutionOptions((int)battle, (int)player) : 0;
}

Dictionary LivingWorldGame::choose_rts(int64_t battle)
{
	GameMessage m(MSG_LW_SET_CONFLICT_RESOLUTION_METHOD, m_localPlayer); // the real-time vote (method 3, RW 0x9DF012)
	m.appendIntegerArgument(3);
	m.appendIntegerArgument((int)battle);
	Dictionary out = post(m, m_localPlayer);
	if (m_session)
	{
		return out; // WOTR2 milestone 8 (step 3): a LAN war's vote goes to every peer; the decided battle comes from take_battle_setup
	}
	BattleSetup setup;
	if (!(bool)out.get("ok", false) || !m_logic->takeBattleSetup(setup))
	{
		if ((bool)out.get("ok", false))
		{
			out["ok"] = false;
			out["error"] = "the real-time vote left no battle to launch";
		}
		return out;
	}
	battleSetupInto(out, setup);
	return out;
}

Dictionary LivingWorldGame::take_battle_setup()
{
	// WOTR2 milestone 8 (step 3): the real-time battle the lockstep's vote decided (StartRealTimeBattle RW 0x6B95BB on every peer), {ok: false} none
	Dictionary out;
	BattleSetup setup;
	out["ok"] = m_logic && m_logic->takeBattleSetup(setup);
	if ((bool)out["ok"])
	{
		out["error"] = String();
		battleSetupInto(out, setup);
	}
	return out;
}

void LivingWorldGame::battleSetupInto(Dictionary &out, const BattleSetup &setup) const
{
	out["battle"] = setup.battleIndex;
	out["resolved"] = setup.battleIndex < 0; // an AI player retreated: no battle to fight
	out["region"] = setup.region;
	out["map"] = s(setup.mapName);
	Array players;
	for (int p : setup.players)
	{
		players.push_back(p);
	}
	out["players"] = players;
	Array spawns;
	for (const BattleSpawn &sp : setup.spawns)
	{
		Dictionary d;
		d["player"] = sp.player;
		d["army"] = sp.armyId;
		d["member"] = sp.memberIndex;
		d["template"] = s(sp.thingTemplate);
		d["quantity"] = sp.quantity;
		d["experience"] = sp.experience;
		d["upgrades"] = strings(sp.upgrades);
		d["spawn_id"] = s(sp.spawnId);
		spawns.push_back(d);
	}
	out["spawns"] = spawns;
	out["buildings"] = strings(setup.defenderBuildings);
	Array buildingIds;
	for (int id : setup.defenderBuildingIds)
	{
		buildingIds.push_back(id);
	}
	out["building_ids"] = buildingIds;
	out["starting_cash"] = (int64_t)setup.startingCash;
	out["starting_cash_with_fort"] = (int64_t)setup.startingCashWithFort;
	Array starts;
	for (const BattleSetup::PlayerStart &ps : setup.playerStarts)
	{
		Dictionary d;
		d["player"] = ps.player;
		d["garrison"] = ps.garrisonArmyId;
		d["cash"] = (int64_t)ps.cash;
		d["bonus_units"] = strings(ps.bonusUnits);
		Array heroes; // WOTR2 milestone 7: the hero list entries the battle took (RW 0x6E42DA)
		for (const RevivalUnit &h : ps.heroes)
		{
			Dictionary hd;
			hd["template"] = s(h.thing);
			hd["spawn_id"] = s(h.spawnId);
			hd["experience"] = h.experience;
			hd["dead"] = h.dead;
			heroes.push_back(hd);
		}
		d["heroes"] = heroes;
		d["skill_points"] = ps.skillPoints;
		d["bonus_points"] = ps.bonusPoints;
		d["sciences"] = strings(ps.sciences);
		d["upgrades"] = strings(ps.upgrades);
		starts.push_back(d);
	}
	out["player_starts"] = starts;
	Array armyStarts; // WOTR2 milestone 7 (step 5)
	for (const BattleSetup::ArmyStart &a : setup.armyStarts)
	{
		Dictionary ad;
		ad["army"] = a.armyId;
		ad["player"] = a.player;
		ad["state"] = a.state;
		ad["arrival_frame"] = a.arrivalFrame;
		Array points;
		for (const auto &p : a.spawnPoints)
		{
			Array pair;
			pair.push_back(s(p.first));
			pair.push_back(s(p.second));
			points.push_back(pair);
		}
		ad["spawn_points"] = points;
		armyStarts.push_back(ad);
	}
	out["army_starts"] = armyStarts;
}

Dictionary LivingWorldGame::apply_battle_result(int64_t battle, const Dictionary &result)
{
	BattleResult r;
	r.winner = (int)result.get("winner", -1);
	r.collected = (bool)result.get("collected", false);
	const Array survivors = result.get("survivors", Array());
	for (int i = 0; i < survivors.size(); ++i)
	{
		const Dictionary d = survivors[i];
		BattleSurvivor sv;
		sv.armyId = (int)d.get("army", 0);
		sv.thingTemplate = n(d.get("template", ""));
		sv.experience = (float)(double)d.get("experience", 0.0);
		sv.level = (int)d.get("level", 0);
		const PackedStringArray up = d.get("upgrades", PackedStringArray());
		for (int k = 0; k < up.size(); ++k)
		{
			sv.upgrades.push_back(n(up[k]));
		}
		sv.spawnId = n(d.get("spawn_id", ""));
		r.survivors.push_back(sv);
	}
	const Array heroes = result.get("heroes", Array()); // WOTR2 milestone 7
	for (int i = 0; i < heroes.size(); ++i)
	{
		const Dictionary d = heroes[i];
		BattleHeroRecord h;
		h.player = (int)d.get("player", -1);
		h.thingTemplate = n(d.get("template", ""));
		h.spawnId = n(d.get("spawn_id", ""));
		h.armyId = (int)d.get("army", 0);
		h.experience = (float)(double)d.get("experience", 0.0);
		h.rank = (int)d.get("rank", 0);
		h.dead = (bool)d.get("dead", false);
		r.heroes.push_back(h);
	}
	const Array backs = result.get("players", Array()); // WOTR2 milestone 7 (step 2)
	for (int i = 0; i < backs.size(); ++i)
	{
		const Dictionary d = backs[i];
		BattleResult::PlayerBack pb;
		pb.player = (int)d.get("player", -1);
		pb.earnedSpellPoints = (int)d.get("earned_spell_points", 0);
		pb.skillPoints = (float)(double)d.get("skill_points", 0.0);
		const PackedStringArray sci = d.get("sciences", PackedStringArray());
		for (int k = 0; k < sci.size(); ++k) pb.sciences.push_back(n(sci[k]));
		const PackedStringArray upg = d.get("upgrades", PackedStringArray());
		for (int k = 0; k < upg.size(); ++k) pb.upgrades.push_back(n(upg[k]));
		r.players.push_back(pb);
	}
	const Array undeployed = result.get("undeployed_armies", Array());
	for (int i = 0; i < undeployed.size(); ++i)
	{
		r.undeployedArmies.push_back((int)undeployed[i]);
	}
	const Array destroyed = result.get("destroyed_buildings", Array());
	for (int i = 0; i < destroyed.size(); ++i)
	{
		r.destroyedBuildings.push_back((int)destroyed[i]);
	}
	std::string e;
	const bool ok = m_logic && m_logic->applyBattleResult((int)battle, r, &e);
	return orderResult(ok, m_logic ? e : "no campaign");
}

// WOTR2 milestone 6: the battle's end as GameLogic::LivingWorldTacticalBattleComplete RW 0x626662 reads it (ReadBattleResult,
// GameLogic/LivingWorld/BattleResultCapture.h). `spawned`: unused since milestone 7 (the objects' army tags are the logic's LivingWorldBattleTags);
// `players`: the battle players in side order, each {player, name (the tactical player), garrison}. `left`: the local player left by the quit menu
// before the end
namespace
{
bool battleSeats(const Array &players, std::vector<BattleSeat> &out, std::string *error)
{
	for (int i = 0; i < players.size(); ++i)
	{
		if (players[i].get_type() != Variant::DICTIONARY)
		{
			*error = "battle player " + std::to_string(i) + " is not a dictionary";
			return false;
		}
		const Dictionary d = players[i];
		BattleSeat st;
		st.name = n(d.get("name", ""));
		st.player = (int)d.get("player", -1);
		st.garrison = (int)d.get("garrison", 0);
		out.push_back(st);
	}
	return true;
}

Dictionary battleResultDict(const BattleResult &r)
{
	Dictionary out;
	Array survivors;
	for (const BattleSurvivor &v : r.survivors)
	{
		Dictionary sv;
		sv["army"] = v.armyId;
		sv["spawn_id"] = s(v.spawnId);
		sv["template"] = s(v.thingTemplate);
		sv["experience"] = v.experience;
		sv["level"] = v.level;
		sv["upgrades"] = strings(v.upgrades);
		survivors.push_back(sv);
	}
	Array heroes;
	for (const BattleHeroRecord &h : r.heroes)
	{
		Dictionary hd;
		hd["player"] = h.player;
		hd["template"] = s(h.thingTemplate);
		hd["army"] = h.armyId;
		hd["spawn_id"] = s(h.spawnId);
		hd["experience"] = h.experience;
		hd["rank"] = h.rank;
		hd["dead"] = h.dead;
		heroes.push_back(hd);
	}
	Array backs;
	for (const BattleResult::PlayerBack &pb : r.players)
	{
		Dictionary bd;
		bd["player"] = pb.player;
		bd["earned_spell_points"] = pb.earnedSpellPoints;
		bd["skill_points"] = pb.skillPoints;
		bd["sciences"] = strings(pb.sciences);
		bd["upgrades"] = strings(pb.upgrades);
		backs.push_back(bd);
	}
	Array undeployed;
	for (int id : r.undeployedArmies)
	{
		undeployed.push_back(id);
	}
	Array destroyed;
	for (int id : r.destroyedBuildings)
	{
		destroyed.push_back(id);
	}
	out["players"] = backs;
	out["undeployed_armies"] = undeployed;
	out["collected"] = r.collected;
	out["winner"] = r.winner;
	out["survivors"] = survivors;
	out["heroes"] = heroes;
	out["destroyed_buildings"] = destroyed;
	return out;
}
} // namespace

Dictionary LivingWorldGame::collect_battle_result(Object *game_world, const Dictionary &spawned, const Array &players, bool left) const
{
	(void)spawned;
	Dictionary out;
	out["collected"] = false;
	GameWorld *gw = Object::cast_to<GameWorld>(game_world);
	LiveGame *game = gw ? gw->hud_game() : nullptr;
	if (!game)
	{
		out["error"] = "no battle game";
		return out;
	}
	const auto context = gw->object_world() ? gw->object_world()->enterContext() : nullptr;
	std::vector<BattleSeat> seats;
	std::string e;
	if (!battleSeats(players, seats, &e))
	{
		out["error"] = s(e);
		return out;
	}
	const GameLogic &logic = game->logic();
	BattleResult r;
	if (!ReadBattleResult(logic, seats, left ? logic.players().getLocalPlayer() : nullptr, r, &e))
	{
		out["error"] = s(e);
		return out;
	}
	return battleResultDict(r);
}

// WOTR2 milestone 8 (step 3): a LAN war's battle: the result is read at the end of logic frame T_end on every peer (BattleResultCapture), armed once the
// battle game is up (the logic is idle: GameWorld's logic access waits for it)
Dictionary LivingWorldGame::arm_battle_capture(Object *game_world, const Array &players)
{
	Dictionary out;
	out["ok"] = false;
	GameWorld *gw = Object::cast_to<GameWorld>(game_world);
	LiveGame *game = gw ? gw->hud_game() : nullptr;
	if (!game)
	{
		out["error"] = "no battle game";
		return out;
	}
	std::vector<BattleSeat> seats;
	std::string e;
	if (!battleSeats(players, seats, &e))
	{
		out["error"] = s(e);
		return out;
	}
	GameLogic &logic = game->logic();
	for (const BattleSeat &st : seats)
	{
		if (!logic.players().findPlayerWithName(st.name))
		{
			out["error"] = s("the battle player " + st.name + " is not in the game");
			return out;
		}
	}
	m_battleCapture = BattleResultCapture();
	m_battleCapture.arm(logic, seats);
	out["ok"] = true;
	return out;
}

// polled while an exit waits for T_end: no wait for the logic (the capture's own lock)
bool LivingWorldGame::battle_capture_done() const
{
	return m_battleCapture.done();
}

Dictionary LivingWorldGame::captured_battle_result() const
{
	BattleResult r;
	long long frame = -1;
	std::string e;
	if (!m_battleCapture.result(r, &frame, &e))
	{
		Dictionary out;
		out["collected"] = false;
		out["error"] = s(e);
		return out;
	}
	Dictionary out = battleResultDict(r);
	out["frame"] = (int64_t)frame;
	return out;
}

int64_t LivingWorldGame::battle_player_index(Object *game_world, const String &player_name) const
{
	GameWorld *gw = Object::cast_to<GameWorld>(game_world);
	LiveGame *game = gw ? gw->hud_game() : nullptr;
	if (!game)
	{
		return -1;
	}
	const ::Player *p = game->logic().players().findPlayerWithName(n(player_name));
	return p ? (int64_t)p->getPlayerIndex() : -1;
}

Array LivingWorldGame::battle_alive(Object *game_world, const Array &ids) const
{
	Array out;
	GameWorld *gw = Object::cast_to<GameWorld>(game_world);
	LiveGame *game = gw ? gw->hud_game() : nullptr;
	if (!game)
	{
		return out;
	}
	const auto context = gw->object_world() ? gw->object_world()->enterContext() : nullptr;
	for (int i = 0; i < ids.size(); ++i)
	{
		const ::Object *o = game->logic().findObjectByID((::ObjectID)(int64_t)ids[i]);
		if (o && !o->isEffectivelyDead() && !o->isDestroyed())
		{
			out.push_back(ids[i]);
		}
	}
	return out;
}


// WOTR2 milestone 7 (step 5): the battle's reinforcement armies for the banner (BannerUI::update RW 0x6164D7 reads the summary's state, progress RW 0x80FED2
// and remaining frames RW 0x80FF04), the local player's first; and the call-ins so far (LW:ReinforcementsArrived for the local player's)
Dictionary LivingWorldGame::battle_reinforcements(Object *game_world) const
{
	Dictionary out;
	GameWorld *gw = Object::cast_to<GameWorld>(game_world);
	LiveGame *game = gw ? gw->hud_game() : nullptr;
	if (!game)
	{
		return out;
	}
	const GameLogic &logic = game->logic();
	const LivingWorldBattleTags &t = logic.livingWorldTags();
	const ::Player *local = logic.players().getLocalPlayer();
	const std::uint32_t frame = (std::uint32_t)logic.getFrame();
	Array rows;
	for (const LivingWorldBattleTags::Reinforcement &r : t.reinforcements)
	{
		Dictionary d;
		d["army"] = r.armyId;
		d["local"] = local && local->getPlayerIndex() == r.playerIndex;
		d["state"] = r.state;
		d["arrival_frame"] = (int64_t)r.arrivalFrame;
		d["remaining_frames"] = (int64_t)(r.state == 2 && r.arrivalFrame > frame ? r.arrivalFrame - frame : 0u);
		d["elapsed_frames"] = (int64_t)std::min(frame, r.arrivalFrame); // the banner's progress (RW 0x80FED2: elapsed / (arrival - start), start 0) is the client's
		rows.push_back(d);
	}
	Array arrivals;
	for (int id : t.arrivals)
	{
		arrivals.push_back(id);
	}
	out["armies"] = rows;
	out["arrivals"] = arrivals;
	return out;
}

// the banner's call-in: MSG_CALL_IN_REINFORCEMENTS (0x45F, the army id) from the local player, through the game's command path (recorded like any order)
Dictionary LivingWorldGame::call_in_reinforcement(Object *game_world, int64_t army) const
{
	Dictionary out;
	out["ok"] = false;
	GameWorld *gw = Object::cast_to<GameWorld>(game_world);
	LiveGame *game = gw ? gw->hud_game() : nullptr;
	const ::Player *local = game ? game->logic().players().getLocalPlayer() : nullptr;
	if (!game || !local)
	{
		out["error"] = "no battle";
		return out;
	}
	GameMessage m(MSG_CALL_IN_REINFORCEMENTS, local->getPlayerIndex());
	m.appendIntegerArgument((int)army);
	game->commands().append(m);
	out["ok"] = true;
	return out;
}

// WOTR2 milestone 8: the player this client orders for (the battle votes' sender)
void LivingWorldGame::set_local_player(int64_t player)
{
	m_localPlayer = (int)player;
}

// WOTR2 milestone 8: the strategic command stream so far: { turn, phase, sender, type, name, args (ints), ok } per message
Array LivingWorldGame::message_log() const
{
	Array out;
	for (const LoggedMessage &lm : m_messageLog)
	{
		Dictionary d;
		d["turn"] = lm.turn;
		d["phase"] = lm.phase;
		d["sender"] = lm.sender;
		d["type"] = lm.message.getType();
		d["name"] = String(GameMessageTypeName(lm.message.getType()));
		Array args;
		for (size_t i = 0; i < lm.message.getArgumentCount(); ++i)
		{
			const GameMessageArgument *a = lm.message.getArgument((int)i);
			if (a && a->type == ARGUMENTDATATYPE_INTEGER)
			{
				args.push_back(a->integer);
			}
		}
		d["args"] = args;
		d["ok"] = lm.ok;
		out.push_back(d);
	}
	return out;
}
