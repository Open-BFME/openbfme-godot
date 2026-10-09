// OpenBFME. GPL-3.0.
//
// WeaponTemplate, WeaponStore and WeaponBonusSet. See GameLogic/Weapon.h for the target facts. Lane WEAPON-1.

#include "GameLogic/Weapon.h"

#include "Common/AsciiString.h"
#include "Common/GameCommon.h"
#include "Common/NumericState.h"

#include <algorithm>
#include <cstddef>

thread_local WeaponStore *TheWeaponStore = nullptr; // SMOOTH-1: per thread (the world context of each thread, RetailObjectWorld::ContextScope)

namespace
{
bool iequals(const std::string &a, const char *b)
{
	return AsciiStringUtil::compareNoCase(a, b) == 0;
}

// fild v; fmul 0.005f (PC24, register); fstp qword; the CRT ceil; __ftol (RW 0x6C9E3F-0x6C9E6E) for the signed Min/Max values (no unsigned
// correction). The low word of the _ftol2 result is what is stored.
unsigned minMaxFrames(int v)
{
	const double product = NumericState::pc24MulW((double)v, (double)LOGICFRAMES_PER_MSEC_REAL);
	return NumericState::ftol2Low32(NumericState::ceilD(product));
}

// RW 0x6C9D99 / 0x6C9E7A: `Name = 250` or `Name = Min:250 [Max:500]`. Writes the converted frames.
void parseMinMax(INI *ini, unsigned *minOut, unsigned *maxOut)
{
	const char *sep = ini->getSepsColon();
	const char *token = ini->getNextTokenOrNull(sep);
	if (!token)
	{
		// RW 0x6C9DB5 calls the compare function with NULL here: an access violation in retail.
		throw INIException(3, "Expected additional data after '%s'", sep);
	}
	int mn, mx;
	if (AsciiStringUtil::compareNoCase(token, "Min") != 0) // RW 0x6C9DBE (stricmp against the string at RW 0xDA1850)
	{
		mn = mx = ini->scanInt(token);
	}
	else
	{
		mn = ini->scanInt(ini->getNextToken(sep));
		const char *next = ini->getNextTokenOrNull(sep);
		if (!next)
		{
			throw INIException(3, "Expected additional data after '%s'", sep); // retail: NULL into the compare, a crash
		}
		if (AsciiStringUtil::compareNoCase(next, "Max") == 0) // RW 0x6C9DFB (the string at RW 0xDA184C)
		{
			mx = ini->scanInt(ini->getNextToken(sep));
		}
		else
		{
			mx = mn;
		}
	}
	*minOut = minMaxFrames(mn);
	*maxOut = minMaxFrames(mx);
}

template <typename T>
T *templateOf(void *instance)
{
	return static_cast<T *>(instance);
}
}

// =============================================================================================================================
// reference checks
// =============================================================================================================================
void WeaponCheckFXList(std::vector<WeaponReference> &refs, const std::string &name)
{
	if (name.empty() || AsciiStringUtil::compareNoCase(name, "None") == 0)
	{
		return;
	}
	const WeaponReferenceHost *host = TheWeaponStore ? TheWeaponStore->referenceHost() : nullptr;
	if (!host)
	{
		refs.push_back(WeaponReference{ "FXList", name });
		return;
	}
	if (!host->fxListExists(name))
	{
		throw INIException(3, "iniParseFXList -- FXList %s not found! Either add the FXList or remove the reference to it.", name.c_str()); // RW 0xC24ED8
	}
}

void WeaponCheckAudioEvent(std::vector<WeaponReference> &refs, const std::string &name)
{
	if (name.empty() || AsciiStringUtil::compareNoCase(name, "NoSound") == 0) // RW 0x73AAA9: _strcmpi against "NoSound"
	{
		return;
	}
	const WeaponReferenceHost *host = TheWeaponStore ? TheWeaponStore->referenceHost() : nullptr;
	if (!host)
	{
		refs.push_back(WeaponReference{ "AudioEvent", name });
		return;
	}
	if (!host->audioEventExists(name))
	{
		throw INIException(3, "Invalid Sound '%s'", name.c_str()); // RW 0xC2500C
	}
}

void WeaponCheckParticleSystem(std::vector<WeaponReference> &refs, const std::string &name)
{
	if (name.empty() || AsciiStringUtil::compareNoCase(name, "None") == 0)
	{
		return;
	}
	const WeaponReferenceHost *host = TheWeaponStore ? TheWeaponStore->referenceHost() : nullptr;
	if (!host || !host->particleSystemExists(name))
	{
		// retail stores NULL silently; with no host the name cannot be told apart from a miss: report it either way
		refs.push_back(WeaponReference{ "ParticleSystem", name });
	}
}

// =============================================================================================================================
// WeaponBonusSet (RW 0x210 bytes, ctor 0x6420B2)
// =============================================================================================================================
WeaponBonusSet::WeaponBonusSet()
{
	for (int c = 0; c < WEAPONBONUS_CONDITION_COUNT; ++c)
	{
		for (int f = 0; f < WEAPONBONUS_FIELD_COUNT; ++f)
		{
			m_bonus[c][f] = 1.0f;
		}
	}
}

// RW 0x6CA45B: scanIndexList(tok, 0xDA1740), scanIndexList(tok, 0xDA179C), scanPercentToReal(tok); v[cond][field] = pct.
void WeaponBonusSet::parseWeaponBonusSet(INI *ini)
{
	const int condition = INI::scanIndexList(ini->getNextToken(), TheWeaponBonusConditionNames);
	const int field = INI::scanIndexList(ini->getNextToken(), TheWeaponBonusFieldNames);
	m_bonus[condition][field] = ini->scanPercentToReal(ini->getNextToken());
}

// RW 0x6CA4B5 -> 0x6CA434: for every set condition bit (0..21, ascending) out[k] = (set[i][k] - 1.0f) + out[k] (SSE subss, addss)
void WeaponBonusSet::appendBonuses(std::uint32_t flags, WeaponBonus &bonus) const
{
	for (int i = 0; i < WEAPONBONUS_CONDITION_COUNT; ++i)
	{
		if (flags & (1u << i))
		{
			for (int k = 0; k < WEAPONBONUS_FIELD_COUNT; ++k)
			{
				const float delta = NumericState::sseSub(m_bonus[i][k], 1.0f);
				bonus.m_field[k] = NumericState::sseAdd(delta, bonus.m_field[k]);
			}
		}
	}
}

// RW 0x6CA7AC: out = {1,...}; mask = object mask | extra; the global set first, then the template's own
void ComputeWeaponBonus(std::uint32_t objectBonusMask, std::uint32_t extraMask, const WeaponBonusSet *globalSet, const WeaponTemplate &weapon, WeaponBonus &out)
{
	out.clear();
	const std::uint32_t mask = objectBonusMask | extraMask;
	if (globalSet)
	{
		globalSet->appendBonuses(mask, out);
	}
	if (weapon.m_extraBonus)
	{
		weapon.m_extraBonus->appendBonuses(mask, out);
	}
}

// =============================================================================================================================
// WeaponTemplate
// =============================================================================================================================
WeaponTemplate::WeaponTemplate()
{
	// RW 0x763D11 over the global empty KindOf mask (RW 0xDE49E4): rule NONE, both masks empty, flag false
	m_projectileFilterInContainer = ObjectFilter::none(KindOfMaskType{}, KindOfMaskType{});
}

void WeaponTemplate::appendNugget(std::shared_ptr<WeaponNugget> nugget)
{
	nugget->m_ownedByOverride = m_isOverride; // RW 0x6CC779: nugget +0x144 = weapon +0x160
	m_nuggets.push_back(std::move(nugget));
}

// RW 0x6C9D99
void WeaponTemplate::parseDelayBetweenShots(INI *ini, void *instance, void *, const void *)
{
	WeaponTemplate *self = templateOf<WeaponTemplate>(instance);
	parseMinMax(ini, &self->m_delayBetweenShotsMin, &self->m_delayBetweenShotsMax);
}

// RW 0x6C9E7A
void WeaponTemplate::parseClipReloadTime(INI *ini, void *instance, void *, const void *)
{
	WeaponTemplate *self = templateOf<WeaponTemplate>(instance);
	parseMinMax(ini, &self->m_clipReloadMin, &self->m_clipReloadMax);
}

// RW 0x6CF43F: parseCoord2D into a temporary, pushed on the vector
void WeaponTemplate::parseScatterTarget(INI *ini, void *instance, void *, const void *)
{
	WeaponTemplate *self = templateOf<WeaponTemplate>(instance);
	Coord2D c;
	INI::parseCoord2D(ini, instance, &c, nullptr);
	self->m_scatterTargets.push_back(c);
}

// RW 0x6CF475: X: Y: (scanReal) T: (scanUnsignedInt)
void WeaponTemplate::parseLinearTarget(INI *ini, void *instance, void *, const void *)
{
	WeaponTemplate *self = templateOf<WeaponTemplate>(instance);
	LinearTarget t;
	t.x = ini->scanReal(ini->getNextSubToken("X"));
	t.y = ini->scanReal(ini->getNextSubToken("Y"));
	t.t = ini->scanUnsignedInt(ini->getNextSubToken("T"));
	self->m_linearTargets.push_back(t);
}

// RW 0x6CB5AB: allocate the set on the first line, then one entry
void WeaponTemplate::parseWeaponBonus(INI *ini, void *instance, void *, const void *)
{
	WeaponTemplate *self = templateOf<WeaponTemplate>(instance);
	if (!self->m_extraBonus)
	{
		self->m_extraBonus = std::make_shared<WeaponBonusSet>();
	}
	self->m_extraBonus->parseWeaponBonusSet(ini);
}

// RW 0x6CB608: no token is read. An override deletes only the nuggets it owns (the parent's stay in the parent's list, but
// the override's list is cleared in full); a base template drops everything.
void WeaponTemplate::parseClearNuggets(INI *, void *instance, void *, const void *)
{
	WeaponTemplate *self = templateOf<WeaponTemplate>(instance);
	self->m_nuggets.clear(); // nuggets are shared_ptr: the parent keeps its own, which is the retail ownership rule
	self->m_hasDamageNugget = false; // +0x114; +0x157 is NOT cleared
}

// RW 0x6CD301 and its 18 siblings (userData = the WeaponNuggetInfo)
void WeaponTemplate::parseNugget(INI *ini, void *instance, void *, const void *userData)
{
	WeaponTemplate *self = templateOf<WeaponTemplate>(instance);
	const WeaponNuggetInfo *info = static_cast<const WeaponNuggetInfo *>(userData);
	std::unique_ptr<WeaponNugget> nugget = ParseWeaponNugget(ini, *info, self);
	self->appendNugget(std::shared_ptr<WeaponNugget>(std::move(nugget)));
	if (info->weaponFlagOffset == 0x114)
	{
		self->m_hasDamageNugget = true;
	}
	else if (info->weaponFlagOffset == 0x157)
	{
		self->m_hasGrabNugget = true;
	}
}

// RW 0x42EC5E (parseInt, 4 bytes) into the byte at +0x141: the value's other three bytes land on +0x142, +0x143 (padding) and the
// low byte of FiringDuration (+0x144).
void WeaponTemplate::parseMaxAttackPassengers(INI *ini, void *instance, void *, const void *)
{
	WeaponTemplate *self = templateOf<WeaponTemplate>(instance);
	const std::uint32_t v = (std::uint32_t)ini->scanInt(ini->getNextToken());
	self->m_maxAttackPassengers = (unsigned char)(v & 0xFF);
	self->m_firingDuration = (self->m_firingDuration & ~0xFFu) | ((v >> 24) & 0xFFu);
}

namespace
{
// veterancy index list RW 0xD9F5E4
int parseVeterancyIndex(INI *ini)
{
	return INI::scanIndexList(ini->getNextToken(), TheVeterancyNames);
}
}

// RW 0x6C9CDC (FireFX userData 0, PreAttackFX userData 1): one FXList name fills all four veterancy levels
void WeaponTemplate::parseFireFX(INI *ini, void *instance, void *store, const void *userData)
{
	WeaponTemplate *self = templateOf<WeaponTemplate>(instance);
	std::string *slots = static_cast<std::string *>(store);
	const std::string name = ini->getNextToken();
	WeaponCheckFXList(self->m_references, name);
	for (int i = 0; i < 4; ++i)
	{
		slots[i] = (AsciiStringUtil::compareNoCase(name, "None") == 0) ? std::string() : name;
	}
	(void)userData;
}

// RW 0x6C9C9A
void WeaponTemplate::parseVeterancyFireFX(INI *ini, void *instance, void *store, const void *)
{
	WeaponTemplate *self = templateOf<WeaponTemplate>(instance);
	std::string *slots = static_cast<std::string *>(store);
	const int level = parseVeterancyIndex(ini);
	const std::string name = ini->getNextToken();
	WeaponCheckFXList(self->m_references, name);
	slots[level] = (AsciiStringUtil::compareNoCase(name, "None") == 0) ? std::string() : name;
}

// RW 0x73A302 into the single slot at +0xB4
void WeaponTemplate::parseFireFlankFX(INI *ini, void *instance, void *store, const void *)
{
	WeaponTemplate *self = templateOf<WeaponTemplate>(instance);
	const std::string name = ini->getNextToken();
	WeaponCheckFXList(self->m_references, name);
	*static_cast<std::string *>(store) = (AsciiStringUtil::compareNoCase(name, "None") == 0) ? std::string() : name;
}

// RW 0x6C9D46: one particle system name for all four levels
void WeaponTemplate::parseProjectileExhaust(INI *ini, void *instance, void *store, const void *)
{
	WeaponTemplate *self = templateOf<WeaponTemplate>(instance);
	std::string *slots = static_cast<std::string *>(store);
	const std::string name = ini->getNextToken();
	WeaponCheckParticleSystem(self->m_references, name);
	for (int i = 0; i < 4; ++i)
	{
		slots[i] = (AsciiStringUtil::compareNoCase(name, "None") == 0) ? std::string() : name;
	}
}

// RW 0x6C9D04
void WeaponTemplate::parseVeterancyProjectileExhaust(INI *ini, void *instance, void *store, const void *)
{
	WeaponTemplate *self = templateOf<WeaponTemplate>(instance);
	std::string *slots = static_cast<std::string *>(store);
	const int level = parseVeterancyIndex(ini);
	const std::string name = ini->getNextToken();
	WeaponCheckParticleSystem(self->m_references, name);
	slots[level] = (AsciiStringUtil::compareNoCase(name, "None") == 0) ? std::string() : name;
}

// RW 0x73B217 -> 0x73AA94
void WeaponTemplate::parseFireSound(INI *ini, void *instance, void *store, const void *)
{
	WeaponTemplate *self = templateOf<WeaponTemplate>(instance);
	const std::string name = ini->getNextToken();
	if (AsciiStringUtil::compareNoCase(name, "NoSound") == 0) // RW 0x73AAA9
	{
		static_cast<std::string *>(store)->clear();
		return;
	}
	WeaponCheckAudioEvent(self->m_references, name);
	*static_cast<std::string *>(store) = name;
}

// RW 0x73ACEF -> 0x73AB45
void WeaponTemplate::parseOverrideVoice(INI *ini, void *instance, void *store, const void *)
{
	WeaponTemplate *self = templateOf<WeaponTemplate>(instance);
	VoiceOverride *voice = static_cast<VoiceOverride *>(store);
	const std::string token = ini->getNextToken();
	if (AsciiStringUtil::compareNoCase(token, "NoSound") == 0) // RW 0x73AB5D: _strcmpi
	{
		*voice = VoiceOverride();
		return;
	}
	if (token.size() >= 4 && AsciiStringUtil::compareNoCase(token.substr(0, 4), "EVA:") == 0) // RW 0x73AB8D: strnicmp(token, "EVA:", 4)
	{
		const std::string event = token.substr(4);
		const WeaponReferenceHost *host = TheWeaponStore ? TheWeaponStore->referenceHost() : nullptr;
		if (!host)
		{
			self->m_references.push_back(WeaponReference{ "EvaEvent", event });
		}
		else if (!host->evaEventExists(event))
		{
			throw INIException(3, "Unknown EVA event in EVA:%s", event.c_str()); // RW 0xC25028
		}
		*voice = VoiceOverride();
		voice->name = event;
		voice->isEva = true;
		return;
	}
	VoiceOverride v;
	std::string name = token;
	if (token.size() >= 7 && AsciiStringUtil::compareNoCase(token.substr(0, 7), "+SOUND:") == 0) // RW 0x73AC0A: _strnicmp(token, "+SOUND:", 7)
	{
		v.plusSound = true;
		name = token.substr(7);
	}
	WeaponCheckAudioEvent(self->m_references, name);
	v.name = name;
	*voice = v;
}

// RW 0x76392F into +0x120
void WeaponTemplate::parseProjectileFilter(INI *ini, void *instance, void *store, const void *userData)
{
	ParseObjectFilter(ini, instance, store, userData);
}

// The field table, RW 0xC16DD8.
const FieldParse *WeaponTemplate::getFieldParse()
{
	static const FieldParse table[] = {
		{ "AttackRange", INI::parseReal, nullptr, (int)offsetof(WeaponTemplate, m_attackRange) },
		{ "MinimumAttackRange", INI::parseReal, nullptr, (int)offsetof(WeaponTemplate, m_minimumAttackRange) },
		{ "RangeBonusMinHeight", INI::parseReal, nullptr, (int)offsetof(WeaponTemplate, m_rangeBonusMinHeight) },
		{ "RangeBonus", INI::parseReal, nullptr, (int)offsetof(WeaponTemplate, m_rangeBonus) },
		{ "RangeBonusPerFoot", INI::parseReal, nullptr, (int)offsetof(WeaponTemplate, m_rangeBonusPerFoot) },
		{ "RequestAssistRange", INI::parseReal, nullptr, (int)offsetof(WeaponTemplate, m_requestAssistRange) },
		{ "AcceptableAimDelta", INI::parseAngleReal, nullptr, (int)offsetof(WeaponTemplate, m_aimDelta) },
		{ "AimDirection", INI::parseAngleReal, nullptr, (int)offsetof(WeaponTemplate, m_aimDirection) },
		{ "ScatterRadius", INI::parseReal, nullptr, (int)offsetof(WeaponTemplate, m_scatterRadius) },
		{ "ScatterTargetScalar", INI::parseReal, nullptr, (int)offsetof(WeaponTemplate, m_scatterTargetScalar) },
		{ "ScatterRadiusVsInfantry", INI::parseReal, nullptr, (int)offsetof(WeaponTemplate, m_infantryInaccuracyDist) },
		{ "ScatterIndependently", INI::parseBool, nullptr, (int)offsetof(WeaponTemplate, m_scatterIndependently) },
		{ "DisableScatterForTargetsOnWall", INI::parseBool, nullptr, (int)offsetof(WeaponTemplate, m_disableScatterForTargetsOnWall) },
		{ "WeaponSpeed", INI::parseVelocityReal, nullptr, (int)offsetof(WeaponTemplate, m_weaponSpeed) },
		{ "MinWeaponSpeed", INI::parseVelocityReal, nullptr, (int)offsetof(WeaponTemplate, m_minWeaponSpeed) },
		{ "MaxWeaponSpeed", INI::parseVelocityReal, nullptr, (int)offsetof(WeaponTemplate, m_maxWeaponSpeed) },
		{ "ScaleWeaponSpeed", INI::parseBool, nullptr, (int)offsetof(WeaponTemplate, m_isScaleWeaponSpeed) },
		{ "CanBeDodged", INI::parseBool, nullptr, (int)offsetof(WeaponTemplate, m_canBeDodged) },
		{ "IdleAfterFiringDelay", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(WeaponTemplate, m_idleAfterFiringDelay) },
		{ "HoldAfterFiringDelay", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(WeaponTemplate, m_holdAfterFiringDelay) },
		{ "HoldDuringReload", INI::parseBool, nullptr, (int)offsetof(WeaponTemplate, m_holdDuringReload) },
		{ "CanFireWhileMoving", INI::parseBool, nullptr, (int)offsetof(WeaponTemplate, m_canFireWhileMoving) },
		{ "CanFireWhileCharging", INI::parseBool, nullptr, (int)offsetof(WeaponTemplate, m_canFireWhileCharging) },
		{ "CanSwoop", INI::parseBool, nullptr, (int)offsetof(WeaponTemplate, m_canSwoop) },
		{ "WeaponRecoil", INI::parseAngleReal, nullptr, (int)offsetof(WeaponTemplate, m_weaponRecoil) },
		{ "MinTargetPitch", INI::parseAngleReal, nullptr, (int)offsetof(WeaponTemplate, m_minTargetPitch) },
		{ "MaxTargetPitch", INI::parseAngleReal, nullptr, (int)offsetof(WeaponTemplate, m_maxTargetPitch) },
		{ "PreferredTargetBone", INI::parseAsciiString, nullptr, (int)offsetof(WeaponTemplate, m_preferredTargetBone) },
		{ "FireSound", WeaponTemplate::parseFireSound, nullptr, (int)offsetof(WeaponTemplate, m_fireSound) },
		{ "FireSoundLoopTime", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(WeaponTemplate, m_fireSoundLoopTime) },
		{ "FireFX", WeaponTemplate::parseFireFX, nullptr, (int)offsetof(WeaponTemplate, m_fireFXs) },
		{ "FireFlankFX", WeaponTemplate::parseFireFlankFX, nullptr, (int)offsetof(WeaponTemplate, m_fireFlankFX) },
		{ "PreAttackFX", WeaponTemplate::parseFireFX, nullptr, (int)offsetof(WeaponTemplate, m_preAttackFXs) },
		{ "ProjectileExhaust", WeaponTemplate::parseProjectileExhaust, nullptr, (int)offsetof(WeaponTemplate, m_projectileExhaust) },
		{ "VeterancyFireFX", WeaponTemplate::parseVeterancyFireFX, nullptr, (int)offsetof(WeaponTemplate, m_fireFXs) },
		{ "VeterancyProjectileExhaust", WeaponTemplate::parseVeterancyProjectileExhaust, nullptr, (int)offsetof(WeaponTemplate, m_projectileExhaust) },
		{ "ClipSize", INI::parseInt, nullptr, (int)offsetof(WeaponTemplate, m_clipSize) },
		{ "ContinuousFireOne", INI::parseInt, nullptr, (int)offsetof(WeaponTemplate, m_continuousFireOneShotsNeeded) },
		{ "ContinuousFireTwo", INI::parseInt, nullptr, (int)offsetof(WeaponTemplate, m_continuousFireTwoShotsNeeded) },
		{ "ContinuousFireCoast", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(WeaponTemplate, m_continuousFireCoastFrames) },
		{ "AutoReloadWhenIdle", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(WeaponTemplate, m_autoReloadWhenIdle) },
		{ "ShotsPerBarrel", INI::parseInt, nullptr, (int)offsetof(WeaponTemplate, m_shotsPerBarrel) },
		{ "DamageDealtAtSelfPosition", INI::parseBool, nullptr, (int)offsetof(WeaponTemplate, m_damageDealtAtSelfPosition) },
		{ "ProjectileFilterInContainer", WeaponTemplate::parseProjectileFilter, nullptr, (int)offsetof(WeaponTemplate, m_projectileFilterInContainer) },
		{ "ProjectileSelf", INI::parseBool, nullptr, (int)offsetof(WeaponTemplate, m_projectileSelf) },
		{ "MeleeWeapon", INI::parseBool, nullptr, (int)offsetof(WeaponTemplate, m_meleeWeapon) },
		{ "ChaseWeapon", INI::parseBool, nullptr, (int)offsetof(WeaponTemplate, m_chaseWeapon) },
		{ "LeechRangeWeapon", INI::parseBool, nullptr, (int)offsetof(WeaponTemplate, m_leechRangeWeapon) },
		{ "HitStoredTarget", INI::parseBool, nullptr, (int)offsetof(WeaponTemplate, m_hitStoredTarget) },
		{ "CapableOfFollowingWaypoints", INI::parseBool, nullptr, (int)offsetof(WeaponTemplate, m_capableOfFollowingWaypoints) },
		{ "ShowsAmmoPips", INI::parseBool, nullptr, (int)offsetof(WeaponTemplate, m_showsAmmoPips) },
		{ "AllowAttackGarrisonedBldgs", INI::parseBool, nullptr, (int)offsetof(WeaponTemplate, m_allowAttackGarrisonedBldgs) },
		{ "PlayFXWhenStealthed", INI::parseBool, nullptr, (int)offsetof(WeaponTemplate, m_playFXWhenStealthed) },
		{ "FiringDuration", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(WeaponTemplate, m_firingDuration) },
		{ "ContinueAttackRange", INI::parseReal, nullptr, (int)offsetof(WeaponTemplate, m_continueAttackRange) },
		{ "SuspendFXDelay", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(WeaponTemplate, m_suspendFXDelay) },
		{ "IgnoreLinearFirstTarget", INI::parseBool, nullptr, (int)offsetof(WeaponTemplate, m_ignoreLinearFirstTarget) },
		{ "ForceDisplayPercentReady", INI::parseBool, nullptr, (int)offsetof(WeaponTemplate, m_forceDisplayPercentReady) },
		{ "IsAimingWeapon", INI::parseBool, nullptr, (int)offsetof(WeaponTemplate, m_isAimingWeapon) },
		{ "NoVictimNeeded", INI::parseBool, nullptr, (int)offsetof(WeaponTemplate, m_noVictimNeeded) },
		{ "RotatingTurret", INI::parseBool, nullptr, (int)offsetof(WeaponTemplate, m_rotatingTurret) },
		{ "HitPercentage", INI::parsePercentToReal, nullptr, (int)offsetof(WeaponTemplate, m_hitPercentage) },
		{ "HitPassengerPercentage", INI::parsePercentToReal, nullptr, (int)offsetof(WeaponTemplate, m_hitPassengerPercentage) },
		{ "PreAttackDelay", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(WeaponTemplate, m_preAttackDelay) },
		{ "PreAttackRandomAmount", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(WeaponTemplate, m_preAttackRandomAmount) },
		{ "PassengerProportionalAttack", INI::parseBool, nullptr, (int)offsetof(WeaponTemplate, m_passengerProportionalAttack) },
		{ "MaxAttackPassengers", WeaponTemplate::parseMaxAttackPassengers, nullptr, (int)offsetof(WeaponTemplate, m_maxAttackPassengers) },
		{ "FinishAttackOnceStarted", INI::parseBool, nullptr, (int)offsetof(WeaponTemplate, m_finishAttackOnceStarted) },
		{ "OverrideVoiceAttackSound", WeaponTemplate::parseOverrideVoice, nullptr, (int)offsetof(WeaponTemplate, m_overrideVoiceAttackSound) },
		{ "OverrideVoiceEnterStateAttackSound", WeaponTemplate::parseOverrideVoice, nullptr, (int)offsetof(WeaponTemplate, m_overrideVoiceEnterStateAttackSound) },
		{ "RestrictedHeightRange", INI::parseReal, nullptr, (int)offsetof(WeaponTemplate, m_restrictedHeightRange) },
		{ "CannotTargetCastleVictims", INI::parseBool, nullptr, (int)offsetof(WeaponTemplate, m_cannotTargetCastleVictims) },
		{ "RequireFollowThru", INI::parseBool, nullptr, (int)offsetof(WeaponTemplate, m_requireFollowThru) },
		{ "ShareTimers", INI::parseBool, nullptr, (int)offsetof(WeaponTemplate, m_shareTimers) },
		{ "ShouldPlayUnderAttackEvaEvent", INI::parseBool, nullptr, (int)offsetof(WeaponTemplate, m_shouldPlayUnderAttackEvaEvent) },
		{ "InstantLoadClipOnActivate", INI::parseBool, nullptr, (int)offsetof(WeaponTemplate, m_instantLoadClipOnActivate) },
		{ "LockWhenUsing", INI::parseBool, nullptr, (int)offsetof(WeaponTemplate, m_lockWhenUsing) },
		{ "BombardType", INI::parseBool, nullptr, (int)offsetof(WeaponTemplate, m_bombardType) },
		{ "UseInnateAttributes", INI::parseBool, nullptr, (int)offsetof(WeaponTemplate, m_useInnateAttributes) },
		{ "PreAttackType", INI::parseIndexList, TheWeaponPrefireNames, (int)offsetof(WeaponTemplate, m_preAttackType) },
		{ "AutoReloadsClip", INI::parseIndexList, TheWeaponReloadNames, (int)offsetof(WeaponTemplate, m_autoReloadsClip) },
		{ "RadiusDamageAffects", INI::parseBitString32, TheWeaponAffectsMaskNames, (int)offsetof(WeaponTemplate, m_affectsMask) },
		{ "ProjectileCollidesWith", INI::parseBitString32, TheWeaponCollideMaskNames, (int)offsetof(WeaponTemplate, m_collideMask) },
		{ "FXTrigger", INI::parseIndexList, TheFXTriggerNames, (int)offsetof(WeaponTemplate, m_fxTrigger) },
		{ "DamageType", INI::parseIndexList, TheDamageNames, (int)offsetof(WeaponTemplate, m_damageType) },
		{ "DeathType", INI::parseIndexList, TheWeaponDeathNames, (int)offsetof(WeaponTemplate, m_deathType) },
		{ "DamageFXType", INI::parseIndexList, TheDamageFXTypeNames, (int)offsetof(WeaponTemplate, m_damageFXType) },
		// RW row 87: DamageSubType is parsed against the DamageFXType list (RW 0xDA1510), a retail copy-and-paste slip
		{ "DamageSubType", INI::parseIndexList, TheDamageFXTypeNames, (int)offsetof(WeaponTemplate, m_damageSubType) },
		{ "AntiAirborneVehicle", INI::parseBitInInt32, (const void *)(std::uintptr_t)WEAPON_ANTI_AIRBORNE_VEHICLE, (int)offsetof(WeaponTemplate, m_antiMask) },
		{ "AntiGround", INI::parseBitInInt32, (const void *)(std::uintptr_t)WEAPON_ANTI_GROUND, (int)offsetof(WeaponTemplate, m_antiMask) },
		{ "AntiProjectile", INI::parseBitInInt32, (const void *)(std::uintptr_t)WEAPON_ANTI_PROJECTILE, (int)offsetof(WeaponTemplate, m_antiMask) },
		{ "AntiSmallMissile", INI::parseBitInInt32, (const void *)(std::uintptr_t)WEAPON_ANTI_SMALL_MISSILE, (int)offsetof(WeaponTemplate, m_antiMask) },
		{ "AntiMine", INI::parseBitInInt32, (const void *)(std::uintptr_t)WEAPON_ANTI_MINE, (int)offsetof(WeaponTemplate, m_antiMask) },
		{ "AntiParachute", INI::parseBitInInt32, (const void *)(std::uintptr_t)WEAPON_ANTI_PARACHUTE, (int)offsetof(WeaponTemplate, m_antiMask) },
		{ "AntiAirborneInfantry", INI::parseBitInInt32, (const void *)(std::uintptr_t)WEAPON_ANTI_AIRBORNE_INFANTRY, (int)offsetof(WeaponTemplate, m_antiMask) },
		{ "AntiAirborneMonster", INI::parseBitInInt32, (const void *)(std::uintptr_t)WEAPON_ANTI_AIRBORNE_MONSTER, (int)offsetof(WeaponTemplate, m_antiMask) },
		{ "AntiBallisticMissile", INI::parseBitInInt32, (const void *)(std::uintptr_t)WEAPON_ANTI_BALLISTIC_MISSILE, (int)offsetof(WeaponTemplate, m_antiMask) },
		{ "AntiStructure", INI::parseBitInInt32, (const void *)(std::uintptr_t)WEAPON_ANTI_STRUCTURE, (int)offsetof(WeaponTemplate, m_antiMask) },
		{ "ProjectileStreamName", INI::parseAsciiString, nullptr, (int)offsetof(WeaponTemplate, m_projectileStreamName) },
		{ "DelayBetweenShots", WeaponTemplate::parseDelayBetweenShots, nullptr, 0 },
		{ "ClipReloadTime", WeaponTemplate::parseClipReloadTime, nullptr, 0 },
		{ "ScatterTarget", WeaponTemplate::parseScatterTarget, nullptr, 0 },
		{ "LinearTarget", WeaponTemplate::parseLinearTarget, nullptr, 0 },
		{ "WeaponBonus", WeaponTemplate::parseWeaponBonus, nullptr, 0 },
		// rows 104..123: DamageNugget, ClearNuggets, then the other 18 nuggets (golden table order)
		{ "DamageNugget", WeaponTemplate::parseNugget, &WeaponNuggetInfoFor(NUGGET_DAMAGE), 0 },
		{ "ClearNuggets", WeaponTemplate::parseClearNuggets, nullptr, 0 },
		{ "DamageFieldNugget", WeaponTemplate::parseNugget, &WeaponNuggetInfoFor(NUGGET_DAMAGE_FIELD), 0 },
		{ "WeaponOCLNugget", WeaponTemplate::parseNugget, &WeaponNuggetInfoFor(NUGGET_WEAPON_OCL), 0 },
		{ "ProjectileNugget", WeaponTemplate::parseNugget, &WeaponNuggetInfoFor(NUGGET_PROJECTILE), 0 },
		{ "MetaImpactNugget", WeaponTemplate::parseNugget, &WeaponNuggetInfoFor(NUGGET_META_IMPACT), 0 },
		{ "HordeAttackNugget", WeaponTemplate::parseNugget, &WeaponNuggetInfoFor(NUGGET_HORDE_ATTACK), 0 },
		{ "SpawnAndFadeNugget", WeaponTemplate::parseNugget, &WeaponNuggetInfoFor(NUGGET_SPAWN_AND_FADE), 0 },
		{ "GrabNugget", WeaponTemplate::parseNugget, &WeaponNuggetInfoFor(NUGGET_GRAB), 0 },
		{ "AttributeModifierNugget", WeaponTemplate::parseNugget, &WeaponNuggetInfoFor(NUGGET_ATTRIBUTE_MODIFIER), 0 },
		{ "SpecialModelConditionNugget", WeaponTemplate::parseNugget, &WeaponNuggetInfoFor(NUGGET_SPECIAL_MODEL_CONDITION), 0 },
		{ "ParalyzeNugget", WeaponTemplate::parseNugget, &WeaponNuggetInfoFor(NUGGET_PARALYZE), 0 },
		{ "LuaEventNugget", WeaponTemplate::parseNugget, &WeaponNuggetInfoFor(NUGGET_LUA_EVENT), 0 },
		{ "FireLogicNugget", WeaponTemplate::parseNugget, &WeaponNuggetInfoFor(NUGGET_FIRE_LOGIC), 0 },
		{ "SlaveAttackNugget", WeaponTemplate::parseNugget, &WeaponNuggetInfoFor(NUGGET_SLAVE_ATTACK), 0 },
		{ "DamageContainedNugget", WeaponTemplate::parseNugget, &WeaponNuggetInfoFor(NUGGET_DAMAGE_CONTAINED), 0 },
		{ "DOTNugget", WeaponTemplate::parseNugget, &WeaponNuggetInfoFor(NUGGET_DOT), 0 },
		{ "OpenGateNugget", WeaponTemplate::parseNugget, &WeaponNuggetInfoFor(NUGGET_OPEN_GATE), 0 },
		{ "EmotionWeaponNugget", WeaponTemplate::parseNugget, &WeaponNuggetInfoFor(NUGGET_EMOTION_WEAPON), 0 },
		{ "StealMoneyNugget", WeaponTemplate::parseNugget, &WeaponNuggetInfoFor(NUGGET_STEAL_MONEY), 0 },
		{ nullptr, nullptr, nullptr, 0 }
	};
	return table;
}

// =============================================================================================================================
// WeaponStore
// =============================================================================================================================
void WeaponStore::reindex()
{
	m_index.clear();
	for (size_t i = 0; i < m_templates.size(); ++i)
	{
		m_index.emplace(m_templates[i]->m_name, i); // emplace keeps the FIRST entry of a name, like the linear scan
	}
}

// RW 0x6CBADE
const WeaponTemplate *WeaponStore::findWeaponTemplate(const std::string &name) const
{
	const auto it = m_index.find(name);
	return it == m_index.end() ? nullptr : m_templates[it->second].get();
}

WeaponTemplate *WeaponStore::findWeaponTemplate(const std::string &name)
{
	const auto it = m_index.find(name);
	return it == m_index.end() ? nullptr : m_templates[it->second].get();
}

// RW 0x6CE31C
WeaponTemplate *WeaponStore::newTemplate(const std::string &name)
{
	if (name.empty())
	{
		return nullptr; // RW 0x6CE332: an empty name makes no template
	}
	std::shared_ptr<WeaponTemplate> t = std::make_shared<WeaponTemplate>();
	t->m_name = name;
	m_templates.push_back(t);
	m_index.emplace(name, m_templates.size() - 1);
	return t.get();
}

// RW 0x6CECC6
WeaponTemplate *WeaponStore::newOverride(WeaponTemplate *parent)
{
	if (!parent || parent->m_isOverride)
	{
		return nullptr; // RW 0x6CECDA / 0x6CECDE: the caller then reads the block into NULL and initFromINI throws
	}
	std::shared_ptr<WeaponTemplate> copy = std::make_shared<WeaponTemplate>(*parent); // RW 0x6CE39C: nuggets and bonus set shared
	// the parent stays alive as the override's +4 link; find its shared_ptr in the store
	std::shared_ptr<const WeaponTemplate> parentPtr;
	for (const auto &t : m_templates)
	{
		if (t.get() == parent)
		{
			parentPtr = t;
			break;
		}
	}
	copy->m_nextOverride = parentPtr;
	copy->m_references.clear(); // the parent's references stay with the parent
	copy->m_isOverride = true;
	// the override REPLACES the entry of that key (RW 0x5FF944 erase + 0x90BE00 push_back: it moves to the END of the vector)
	for (size_t i = 0; i < m_templates.size(); ++i)
	{
		if (m_templates[i]->m_name == copy->m_name)
		{
			m_templates.erase(m_templates.begin() + (std::ptrdiff_t)i);
			break;
		}
	}
	m_templates.push_back(copy);
	reindex();
	return copy.get();
}

void WeaponStore::resetOverrides()
{
	for (std::shared_ptr<WeaponTemplate> &entry : m_templates)
	{
		while (entry->m_isOverride && entry->m_nextOverride)
		{
			std::shared_ptr<WeaponTemplate> parent = std::const_pointer_cast<WeaponTemplate>(entry->m_nextOverride);
			entry = parent;
		}
	}
	reindex();
}

// RW 0x6CE82E: marks the template retired, removes it from the vector and parks it in the retired vector
void WeaponStore::retire(WeaponTemplate *t)
{
	for (size_t i = 0; i < m_templates.size(); ++i)
	{
		if (m_templates[i].get() == t)
		{
			t->m_retiredFlag = 1;
			m_retired.push_back(m_templates[i]);
			m_templates.erase(m_templates.begin() + (std::ptrdiff_t)i);
			reindex();
			return;
		}
	}
}

// RW 0x6CED65
void WeaponStore::parseWeaponTemplateDefinition(INI *ini)
{
	const std::string name = ini->getNextToken();
	WeaponTemplate *existing = findWeaponTemplate(name);
	WeaponTemplate *wt = nullptr;
	if (!existing)
	{
		wt = newTemplate(name);
	}
	else if (ini->getLoadType() == INI_LOAD_CREATE_OVERRIDES)
	{
		wt = newOverride(existing);
	}
	else if (ini->getLoadType() == INI_LOAD_RELOAD)
	{
		retire(existing);
		wt = newTemplate(name);
		if (wt)
		{
			wt->m_retiredFlag = 0; // RW 0x6CEDD7
		}
	}
	else
	{
		return; // RW 0x6CEDBD: any other load type reads nothing; the body lines reach the dispatcher
	}
	ini->initFromINI(wt, WeaponTemplate::getFieldParse()); // wt == nullptr throws "INI::initFromINI - Invalid parameters supplied!"
}

void WeaponStore::parseWeaponTemplateDefinitionGlobal(INI *ini)
{
	if (!TheWeaponStore)
	{
		throw INIException(3, "TheWeaponStore==NULL");
	}
	TheWeaponStore->parseWeaponTemplateDefinition(ini);
}

std::vector<WeaponStore::Unverified> WeaponStore::unverifiedReferences() const
{
	std::vector<Unverified> out;
	for (const auto &t : m_templates)
	{
		// walk the override chain: every template of the chain owns its references and the nuggets it parsed itself
		for (const WeaponTemplate *p = t.get(); p; p = p->m_nextOverride.get())
		{
			for (const WeaponReference &r : p->m_references)
			{
				out.push_back(Unverified{ t->m_name, r.kind, r.name });
			}
			for (const auto &n : p->m_nuggets)
			{
				if (n->m_ownedByOverride != p->m_isOverride)
				{
					continue; // an inherited nugget is listed under its owner
				}
				for (const WeaponReference &r : n->m_references)
				{
					out.push_back(Unverified{ t->m_name, r.kind, r.name });
				}
			}
		}
	}
	return out;
}

std::vector<std::string> WeaponStore::acceptanceStops() const
{
	std::vector<std::string> out;
	const std::vector<Unverified> refs = unverifiedReferences();
	if (!refs.empty())
	{
		out.push_back("S-181: " + std::to_string(refs.size()) + " FXList / AudioEvent / ParticleSystem / EVA references in Weapon blocks were not checked against their stores (the stores belong to other lanes); retail throws for an unknown FXList or sound");
	}
	return out;
}

std::vector<std::string> WeaponLaneStops()
{
	return {
		"S-180: the nugget delivery virtuals (isApplicable / apply to a victim / apply at a position / pre-fire hooks, RW vtable slots 1-14) are not ported: the nuggets carry their complete data and constructor defaults, delivery needs the object, partition, FX, OCL, script and AI systems of later lanes",
		"S-182: the RotWK WeaponStore reset path was not located (the store's SubsystemInterface slot 1, RW 0x63F3BF, is a bare ret); WeaponStore::resetOverrides models ZH WeaponStore::reset and does not undo what an override's WeaponBonus lines wrote into the parent's shared WeaponBonusSet",
		"S-183: the BOX geometry distance (RW 0x68F430), calcPitches (RW 0xAD2620) and the garrison range cap (RW 0x6FF412) are decoded structurally only: the range code takes them from its host",
		"S-184: FiringTracker (RW 0x8E326D / 0x8E30DD) is ported from a structural reading: the order of the update steps and the hard-reset variant of shotFired are inference",
		"S-185: the two ARMOR attribute-modifier reads of RW AdjustDamage (0x5D8A64, 0x5D8AF1) are patched into .danetta in the installed image (stacking 1-(1-a)(1-b)); the port implements the additive sum of the unpatched getter and BFME2 1.06 (identical for one modifier)",
		"S-186: no retail oracle runs on this machine: the damage and timing golden vectors come from tools/weapon/damage_golden.py and dump_timing_facts.py, models of the routines' instruction order; x87 sequences are emulated at PC24 (NumericState); the DamageArc cone's x87 fcos (lane MOVE-2 ports the cone of RW 0x90DEF0; the deterministic cos stands in), the radius-damage partition iteration (DistanceCalculationType 4) and the isFlankedBy geometry are not ported",
		"S-187: GrabNugget RemoveTargetFromOtherContain (RW +0x14A) is never written by the retail constructor (operator new does not zero it); the port defaults it to false and records whether the INI line was present",
		"S-188: retail crashes or quirks reproduced as errors or exact layouts: DelayBetweenShots / ClipReloadTime `Min:N` without `Max` (NULL into the compare), a second override of an overridden weapon (initFromINI of NULL), MaxAttackPassengers (a 4 byte parseInt into a byte that overruns into FiringDuration), DamageSubType parsed against the DamageFXType list, the unterminated DefaultWeaponChoiceCritera list",
		"S-189: the logic RNG algorithm of the retail 2.01 is unproven (S-080), so every weapon draw (pre-attack jitter, delay, reload, scatter pick) inherits it; RandomValue.h calls RW 0xDE4364 TheGameLogic, it is TheGlobalData, the logic frame is [0xDE412C]+0x40",
	};
}
