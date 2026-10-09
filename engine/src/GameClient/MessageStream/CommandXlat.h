// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// CommandTranslator (ZH Include/GameClient/CommandXlat.h, Source/GameClient/MessageStream/CommandXlat.cpp; B1 CommandXlat.cpp), lane HUD-1: turns a click that did not
// select anything into the context command of the selection (move, attack, waypoint ...), the meta messages (stop, scatter, the order modes) into
// logic messages, and generates the cursor hints.
//
// DONOR FACTS (ZH CommandXlat.cpp): a CREATE_SELECTED_GROUP / ADD_TEAM / SELECT_TEAM message in the stream makes `m_teamExists` true, a DESTROY_SELECTED_GROUP
// false (3543-3568); in the standard mouse setup a left click that selection did not use is the command, in the alternate setup the right click is
// (3678, 3741); the move message type follows the modes: waypoint, attack move, force move, force attack, else MOVETO (issueMoveToLocationCommand,
// 865); evaluateContextCommand (1425) returns the message type the click WOULD issue for DO_HINT / EVALUATE_ONLY and appends it for DO_COMMAND.
//
// NOT PORTED (stop S-286): the context actions whose logic is not ported (resume construction, dock, repair, heal, capture / hijack /
// sabotage / salvage / snipe, combat drop, special powers, the weapon fire commands, the unit voice responses (audio), the statistics collector, the
// academy stats, the debug / demo messages). A message of those kinds is counted by unportedMeta() and reported, never dropped silently.
// TARGET FACT (lane HUD-4, stop S-1673): RotWK's meta handler (RW 0x81F8D8) has no case for SELECT_NEXT_UNIT / SELECT_PREV_UNIT / SELECT_NEXT_WORKER /
// SELECT_PREV_WORKER (0x5D .. 0x60: its switch runs 0x61 / 0x62 .. 0x69 and the ranges below 0x5D; no other code of the binary compares or emits these
// types): Shift+Up / Shift+Down pass every translator and do nothing in retail, and do nothing here (counted by retailNoOpMeta()).
//
// ATTACK RULE (inference, stop S-286): an object is attackable by the click when it is an enemy of the local player and some selected object (or a member of
// a selected horde) can possibly have a weapon (canPossiblyHaveAnyWeapon, RW 0x73C191), which stands for ZH's ActionManager::canAttackObject.

#pragma once

#include "GameClient/HudContext.h"
#include "GameClient/MessageStream/MessageStream.h"

#include <map>
#include <string>

class Object;
class ThingTemplate;

enum class EvaluateType
{
	DoCommand,
	DoHint,
	EvaluateOnly
};

class CommandTranslator : public MessageTranslator
{
public:
	explicit CommandTranslator(HudContext &ctx) : m_ctx(ctx) {}

	MessageDisposition translate(const ClientMessage &message) override;

	// ZH CommandTranslator::evaluateContextCommand: the logic message type the click on `target` (null: the ground at `pos`) would issue, 0 for none.
	int evaluateContextCommand(Object *target, const Coord3D *pos, EvaluateType type);
	int evaluateForceAttack(Object *target, const Coord3D *pos, EvaluateType type);

	// the standard (false) or alternate (true) mouse setup: which button issues the order (GlobalData m_useAlternateMouse; the retail default is stop S-285)
	void setUseAlternateMouse(bool on) { m_alternateMouse = on; }
	bool useAlternateMouse() const { return m_alternateMouse; }
	// a right click on the radar: the selection moves to the ground point (a plain MSG_DO_MOVETO, the same message a click on the map issues)
	void issueRadarMove(const Coord3D &world) { m_ctx.stream.append(MSG_DO_MOVETO).appendLocation(world); }
	bool teamExists() const { return m_teamExists; }

	// ZH InGameUI::areSelectedObjectsControllable: the selection is the local player's and alive
	bool areSelectedObjectsControllable() const;
	// meta commands counted and not executed (name -> count)
	const std::map<std::string, unsigned> &unportedMeta() const { return m_unportedMeta; }
	// lane HUD-4: the meta messages RotWK itself leaves unhandled (S-1673), counted
	const std::map<std::string, unsigned> &retailNoOpMeta() const { return m_retailNoOp; }
	// lane HUD-4: the radar whose events Space visits (RW 0x81FE7B: TheRadar; null: the key does nothing, as with no radar in retail)
	void setRadar(class Radar *radar) { m_radar = radar; }

private:
	bool selectionCanAttack();
	bool selectionCanSetRallyPoint();
	bool objectCanAttack(Object &obj);
	int issueMove(Object *target, const Coord3D *pos, EvaluateType type);
	void hint(const char *cursor);
	bool isClick(const ICoord2D &anchor, const ICoord2D &lift, int downMs, int upMs) const;
	void viewHomeBase();
	void selectAllUnits();
	// lane HUD-4 (RotWK's meta handler RW 0x81F8D8): Ctrl+H (SELECT_HERO 0x63), the stance keys (STANCE_AGGRESSIVE / HOLDGROUND / BATTLE 0x93 .. 0x95) and Space
	// (VIEW_LAST_RADAR_EVENT 0x62)
	void selectHero();
	void changeStance(int stance);
	void viewLastRadarEvent();

	HudContext &m_ctx;
	bool m_teamExists = false;
	bool m_alternateMouse = false;
	ICoord2D m_rightAnchor{}, m_rightLift{};
	int m_rightDown = 0, m_rightUp = 0;
	std::map<const ThingTemplate *, bool> m_weaponCache;
	std::map<std::string, unsigned> m_unportedMeta;
	class Radar *m_radar = nullptr;
	std::map<std::string, unsigned> m_retailNoOp;
};
