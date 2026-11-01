// OpenBFME. GPL-3.0.
//
// ViewChecks (lane ANTICHEAT-1, owner 2026-10-21: "hover over an area they cannot see for a few seconds"): D2 over a re-simulated game's reported views
// (the replay's VIEW records: each player's client reports its camera and the ground under its pointer twice a second, Network::submitView). OpenBFME's
// own code; not simulation code (it reads the logic and the views, changes neither).
//
// hover_hidden: the pointer rests over an enemy object HIDDEN from the player (its status for the player is SHROUDED: a unit in fog, a building never
// seen) for at least `hoverFrames` frames in a row (3 s), and that enemy was not visible to the player in the `recentFrames` (10 s) before the hover
// began (looking where an army was just seen is ordinary). "Over" = within `radius` of the pointer's ground point (the object's centre). Events of one
// player within 30 s are one encounter.
// Limits, stated: the views come from the player's own client, so a modified client can send anything or nothing (supporting evidence only: band
// "review" at most, never alone a reason); a pointer can rest anywhere by chance, so one episode means little and the cases need many games.
// `views` counts the views each player reported (a signing client that reports none is worth a look; D7 says whether it signed).

#pragma once

#include "Common/Recorder.h"
#include "GameLogic/ObjectTypes.h"

#include <array>
#include <string>
#include <vector>

class GameLogic;
class OrdersIntoFog;

class ViewCheck
{
public:
	struct Settings
	{
		unsigned hoverFrames = 15;   ///< 3 s of game time
		unsigned recentFrames = 50;  ///< the enemy was not visible to the player in the 10 s before the hover began
		unsigned staleFrames = 10;   ///< a view older than 2 s says nothing about now (the client stopped reporting)
		unsigned encounterFrames = 150;
		float radius = 30.0f;        ///< world units around the pointer's ground point
	};
	struct Event
	{
		UnsignedInt frame = 0;      ///< the hover's first frame
		UnsignedInt frames = 0;     ///< how long it lasted
		ObjectID target = 0;        ///< the hidden enemy under the pointer when it began
		std::string targetTemplate;
		int targetOwner = -1;
		float x = 0.0f, y = 0.0f;   ///< the pointer's ground point when it began
		int encounter = 0;
	};
	struct PlayerResult
	{
		int player = -1, slot = -1;
		std::string name;
		unsigned long long views = 0;
		unsigned long long invalidViews = 0; ///< views with a value that is not a finite number (ignored)
		std::vector<Event> events;
		int encounters = 0;
	};

	// `slotPlayerIndex`: the replay header's slot -> player (the views name slots); `fog`: D1, whose samples say what each player saw (it must see the
	// same batches first: FogCheckDriver calls it before this)
	ViewCheck(GameLogic &logic, const std::vector<ReplayFile::View> &views, const std::vector<int> &slotPlayerIndex, const OrdersIntoFog &fog,
		Settings settings);
	ViewCheck(GameLogic &logic, const std::vector<ReplayFile::View> &views, const std::vector<int> &slotPlayerIndex, const OrdersIntoFog &fog);
	void beforeFrame(UnsignedInt frame);
	void abstain(const std::string &reason);
	std::vector<PlayerResult> results() const;
	std::string json(const std::string &gameId, const std::string &crcParity, UnsignedInt frames) const;

private:
	struct Tracked
	{
		PlayerResult result;
		const ReplayFile::View *latest = nullptr;
		UnsignedInt hoverStart = 0, hoverFrames = 0;
		bool hovering = false;
		Event pending;
	};
	ObjectID hiddenEnemyUnder(int player, float x, float y, UnsignedInt frame, Event &e) const;
	GameLogic &m_logic;
	std::vector<ReplayFile::View> m_views; ///< by frame
	size_t m_next = 0;
	std::vector<Tracked> m_tracked;
	const OrdersIntoFog &m_fog;
	Settings m_settings;
	std::string m_abstain;
};
