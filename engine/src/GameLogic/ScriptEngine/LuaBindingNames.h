// OpenBFME. GPL-3.0.
//
// The registered Lua function names of the two states, in the retail registration order (RW register-and-load 0x739C10: 0x739C25-0x73A0A3,
// init 0x737654: 0x737686-0x7378CE). Read from the binary by tools/lua_binary_facts.py and checked against it by test_lua_binary_facts.cpp
// (the strings at RW 0xC24ADC-0xC24E90 and 0xC24720-0xC24960); the engine registers exactly these, in this order, and a mod's
// Scripts.lua can call any of them.

#pragma once

static const char *const kLuaLogicBindingNames[41] = {
	"_ALERT", "GetFrame", "EvaluateCondition", "ExecuteAction", "ObjectDescription", "ObjectSpy", "ObjectDispatchEvent",
	"ObjectBroadcastEventToEnemies", "ObjectBroadcastEventToAllies", "ObjectBroadcastEventToCivilians", "ObjectBroadcastEventToUnits",
	"HordeBroadcastEventToMembers", "ObjectTeamName", "ObjectPlayerSide", "ObjectCapturingObjectPlayerSide", "ObjectTemplateName",
	"ObjectTestModelCondition", "ObjectTestCanSufferFear", "ObjectCountNearbyEnemies", "ObjectEnterFearState",
	"ObjectEnterRunAwayPanicState", "ObjectEnterCowerState", "ObjectEnterUncontrollableCowerState", "ObjectEnterAlertState",
	"ObjectEnterRampageState", "ObjectPlaySound", "ObjectSetChanting", "ObjectSetFearFactor", "ObjectSetEnragedState",
	"ObjectDoSpecialPower", "ObjectCreateAndFireTempWeapon", "ObjectHasUpgrade", "ObjectGrantUpgrade", "ObjectRemoveUpgrade",
	"ObjectSetDelayedDeath", "ObjectHideSubObject", "ObjectHideSubObjectPermanently", "ObjectSetGeometryActive",
	"ObjectChangeAllegianceFromNonPlayablePlayer", "GetRandomNumber", "ObjectForbidPlayerCommands"
};

static const char *const kLuaDrawableBindingNames[21] = {
	"_ALERT", "GetFrame", "CurDrawableModelcondition", "CurDrawableObjectStatus", "CurDrawableShowSubObject", "CurDrawableHideSubObject",
	"CurDrawableShowSubObjectPermanently", "CurDrawableHideSubObjectPermanently", "CurDrawableHideModule", "CurDrawableShowModule",
	"CurDrawablePrevAnimationState", "CurDrawablePrevAnimation", "CurDrawableGetCurrentTargetDistance", "CurDrawableGetCurrentTargetHeight",
	"CurDrawableGetCurrentTargetBearing", "CurDrawablePrevAnimFraction", "CurDrawableSetTransitionAnimState", "CurDrawableAllowToContinue",
	"CurDrawablePlaySound", "CurDrawableIsCurrentTargetKindof", "GetClientRandomNumberReal"
};
