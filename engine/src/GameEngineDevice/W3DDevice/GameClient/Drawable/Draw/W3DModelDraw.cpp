// OpenBFME. GPL-3.0.
//
// Draw module data parsing for W3DScriptedModelDraw / W3DHordeModelDraw / W3DDefaultDraw. See W3DModelDraw.h for the sources.

#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDraw.h"

#include "Common/AsciiString.h"
#include "Common/INIException.h"

#include <algorithm>
#include <stdexcept>
#include <cstddef>
#include <cstring>

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

namespace
{
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDrawNameLists.inc"

std::string lowerCopy(const std::string &s) { return AsciiStringUtil::lowered(s); }

// AsciiString::isNone (RW 0x437720): the string is "None", compared without case.
bool isNoneName(const std::string &s) { return AsciiStringUtil::compareNoCase(s, "NONE") == 0; }

// ---------------------------------------------------------------------------------------------------------------------
// Field parsers shared by the tables
// ---------------------------------------------------------------------------------------------------------------------

// RW 0x4B65BA: next ascii string, lower-cased (ZH parseAsciiStringLC).
void parseAsciiStringLC(INI *ini, void *, void *store, const void *)
{
	std::string s = ini->getNextAsciiString();
	AsciiStringUtil::toLower(s);
	*static_cast<std::string *>(store) = s;
}

// RW 0x4C21EE: Model = NAME [ExtraMesh Yes]. A missing NAME does nothing; a plain Model (no ExtraMesh, or ExtraMesh with a
// false/absent value) empties the list first; the name is stored as written (no lower-casing here).
void parseModel(INI *ini, void *instance, void *, const void *)
{
	ModelConditionInfo *self = static_cast<ModelConditionInfo *>(instance);
	if (!self)
	{
		return;
	}
	const char *model = ini->getNextTokenOrNull(ini->getSepsColon());
	if (!model)
	{
		return;
	}
	const std::string name = model;
	bool keep = false;
	if (const char *mesh = ini->getNextTokenOrNull(ini->getSepsColon()))
	{
		if (AsciiStringUtil::compareNoCase(mesh, "ExtraMesh") == 0)
		{
			if (const char *flag = ini->getNextTokenOrNull(ini->getSepsColon()))
			{
				keep = ini->scanBool(flag);
			}
		}
	}
	if (!keep)
	{
		self->modelNames.clear();
	}
	self->modelNames.push_back(name);
}

// RW 0x4C317B: WeaponFireFXBone / WeaponLaunchBone / WeaponRecoilBone / WeaponMuzzleFlash = SLOT BONE. The slot comes from
// RW 0xDA12E4, the bone is lower-cased, NONE clears it; the bone is added to the state's public bones (ZH parseWeaponBoneName).
void parseWeaponBoneName(INI *ini, void *instance, void *store, const void *)
{
	ModelConditionInfo *self = static_cast<ModelConditionInfo *>(instance);
	std::vector<std::string> *arr = static_cast<std::vector<std::string> *>(store);
	const int slot = INI::scanIndexList(ini->getNextToken(), kWeaponSlotNames);
	std::string bone = ini->getNextAsciiString();
	AsciiStringUtil::toLower(bone);
	if (isNoneName(bone))
	{
		bone.clear();
	}
	if ((int)arr->size() <= slot)
	{
		arr->resize((size_t)slot + 1);
	}
	(*arr)[(size_t)slot] = bone;
	if (self)
	{
		self->addPublicBone(bone);
	}
}

// RW 0x4BCC4D (shared by the ModelConditionState field RW 0x4BCE4E and the AnimationState field RW 0x4BCE37).
void parseParticleSysBoneInto(INI *ini, std::vector<ParticleSysBoneInfo> &out)
{
	ParticleSysBoneInfo info;
	info.boneName = ini->getNextAsciiString();
	AsciiStringUtil::toLower(info.boneName);
	// RW 0x73AECB: the particle system template by name (INI::parseParticleSystemTemplate); the registry is not ported (S-094).
	info.systemName = ini->getNextAsciiString();
	const char *colon = ini->getSepsColon();
	for (const char *token = ini->getNextTokenOrNull(colon); token != nullptr; token = ini->getNextTokenOrNull(colon))
	{
		const std::string key = token;
		if (AsciiStringUtil::compareNoCase(key, "FollowBone") == 0)
		{
			info.followBone = ini->scanBool(ini->getNextToken(colon));
		}
		else if (AsciiStringUtil::compareNoCase(key, "HouseColor") == 0)
		{
			info.houseColor = ini->scanBool(ini->getNextToken(colon));
		}
		else if (AsciiStringUtil::compareNoCase(key, "FXTrigger") == 0)
		{
			info.fxTrigger = INI::scanIndexList(ini->getNextToken(colon), kFXTriggerNames);
		}
		else if (AsciiStringUtil::compareNoCase(key, "Persist") == 0)
		{
			info.persist = INI::scanIndexList(ini->getNextToken(colon), kPersistNames);
		}
		else if (AsciiStringUtil::compareNoCase(key, "PersistID") == 0)
		{
			info.persistID = ini->scanInt(ini->getNextToken(colon));
		}
		else if (AsciiStringUtil::compareNoCase(key, "OnlyIfOnWater") == 0)
		{
			info.onlyIfOnWater = ini->scanBool(ini->getNextToken(colon));
		}
		else if (AsciiStringUtil::compareNoCase(key, "OnlyIfOnLand") == 0)
		{
			info.onlyIfOnLand = ini->scanBool(ini->getNextToken(colon));
		}
		// RW 0x4BCDFA: any other word is skipped
	}
	out.push_back(info);
}

void parseModelStateParticleSysBone(INI *ini, void *instance, void *, const void *)
{
	parseParticleSysBoneInto(ini, static_cast<ModelConditionInfo *>(instance)->particleSysBones);
}

void parseAnimStateParticleSysBone(INI *ini, void *instance, void *, const void *)
{
	parseParticleSysBoneInto(ini, static_cast<AnimationStateInfo *>(instance)->particleSysBones);
}

// RW 0x4C3FD5 (donor FXEventParser007764E0 bfmeTailCHG): key words with the colon separators, in any order; unknown words are
// skipped (the while loop RW 0x4C4127). The value of FireWhenSkipped is not consumed.
void parseFXEventInto(INI *ini, std::vector<FXEventInfo> &out)
{
	FXEventInfo ev;
	const char *colon = ini->getSepsColon();
	for (const char *token = ini->getNextTokenOrNull(colon); token != nullptr; token = ini->getNextTokenOrNull(colon))
	{
		const std::string key = token;
		if (key == "Frame")
		{
			if (const char *v = ini->getNextTokenOrNull(colon))
			{
				ev.frame = ini->scanInt(v);
			}
		}
		else if (key == "FrameStep")
		{
			if (const char *v = ini->getNextTokenOrNull(colon))
			{
				ev.frameStep = ini->scanInt(v);
			}
		}
		else if (key == "FrameStop")
		{
			if (const char *v = ini->getNextTokenOrNull(colon))
			{
				ev.frameStop = ini->scanInt(v);
			}
		}
		else if (key == "FireWhenSkipped")
		{
			ev.fireWhenSkipped = true;
		}
		else if (key == "Name")
		{
			if (const char *v = ini->getNextTokenOrNull(colon))
			{
				ev.fxListName = v;
			}
		}
		else if (key == "Bone")
		{
			if (const char *v = ini->getNextTokenOrNull(colon))
			{
				ev.bone = v;
			}
		}
	}
	out.push_back(ev);
}

void parseModelStateFXEvent(INI *ini, void *instance, void *, const void *)
{
	if (instance)
	{
		parseFXEventInto(ini, static_cast<ModelConditionInfo *>(instance)->fxEvents);
	}
}

void parseAnimStateFXEvent(INI *ini, void *instance, void *, const void *)
{
	if (instance)
	{
		parseFXEventInto(ini, static_cast<AnimationStateInfo *>(instance)->fxEvents);
	}
}

// RW 0x4C3236: LuaEvent = Frame: N Data: TEXT OnStateEnter | OnStateLeave.
void parseLuaEvent(INI *ini, void *instance, void *, const void *)
{
	if (!instance)
	{
		return;
	}
	LuaEventInfo ev;
	const char *colon = ini->getSepsColon();
	for (const char *token = ini->getNextTokenOrNull(colon); token != nullptr; token = ini->getNextTokenOrNull(colon))
	{
		const std::string key = token;
		if (AsciiStringUtil::compareNoCase(key, "Frame") == 0)
		{
			ev.frame = ini->scanInt(ini->getNextToken());
		}
		else if (AsciiStringUtil::compareNoCase(key, "Data") == 0)
		{
			ev.data = ini->getNextAsciiString();
		}
		else if (AsciiStringUtil::compareNoCase(key, "OnStateEnter") == 0)
		{
			ev.when = 1;
		}
		else if (AsciiStringUtil::compareNoCase(key, "OnStateLeave") == 0)
		{
			ev.when = 2;
		}
	}
	static_cast<AnimationStateInfo *>(instance)->luaEvents.push_back(ev);
}

// RW 0x4C2EEC: Texture = A B (two ascii strings, both required).
void parseTexturePair(INI *ini, void *instance, void *, const void *)
{
	ModelConditionInfo *self = static_cast<ModelConditionInfo *>(instance);
	std::string a = ini->getNextAsciiString();
	std::string b = ini->getNextAsciiString();
	self->textures.emplace_back(a, b);
}

// RW 0x4B92D3: Shadow = TYPE (a 16 bit bit string over RW 0xD99EB0). A non-zero type creates the shadow object; later
// ShadowSizeX... fields write into it, and are skipped without reading a token while it does not exist (RW 0x4B265F).
void parseShadowType(INI *ini, void *instance, void *, const void *userData)
{
	unsigned bits = 0;
	INI::parseBitString32(ini, nullptr, &bits, userData);
	if (bits & 0xffff0000u)
	{
		throw INIPlainIntError("Bad bitstring list INI::parseBitString16");
	}
	if (bits != 0)
	{
		ModelConditionInfo *self = static_cast<ModelConditionInfo *>(instance);
		self->hasShadow = true;
		self->shadow = ModelShadowInfo();
		self->shadow.type = (int)bits;
	}
}

#define SHADOW_FIELD_PARSER(Name, Member, Parser)                                                       \
	void Name(INI *ini, void *instance, void *, const void *)                                           \
	{                                                                                                   \
		ModelConditionInfo *self = static_cast<ModelConditionInfo *>(instance);                         \
		if (self->hasShadow)                                                                            \
		{                                                                                               \
			Parser(ini, instance, &self->shadow.Member, nullptr);                                       \
		}                                                                                               \
	}
SHADOW_FIELD_PARSER(parseShadowSizeX, sizeX, INI::parseReal)
SHADOW_FIELD_PARSER(parseShadowSizeY, sizeY, INI::parseReal)
SHADOW_FIELD_PARSER(parseShadowOffsetX, offsetX, INI::parseReal)
SHADOW_FIELD_PARSER(parseShadowOffsetY, offsetY, INI::parseReal)
SHADOW_FIELD_PARSER(parseShadowSunAngle, sunAngle, INI::parseReal)
SHADOW_FIELD_PARSER(parseShadowMaxHeight, maxHeight, INI::parseReal)
SHADOW_FIELD_PARSER(parseShadowTexture, texture, INI::parseAsciiString)
SHADOW_FIELD_PARSER(parseShadowOverrideLODVisibility, overrideLODVisibility, INI::parseBool)
#undef SHADOW_FIELD_PARSER

// RW 0x4C232A / 0x4C236F / 0x4B65FE / 0x4B661E. The retail ones write through the last element of the turret list, which is
// undefined when no Turret came first (the pointer arithmetic runs off an empty vector); here that is an INI error.
void parseTurret(INI *ini, void *instance, void *, const void *)
{
	ModelConditionInfo *self = static_cast<ModelConditionInfo *>(instance);
	std::string bone = ini->getNextToken();
	AsciiStringUtil::toLower(bone);
	self->addPublicBone(bone);
	ModelTurretInfo t;
	t.angleBone = (bone.empty() || isNoneName(bone)) ? std::string() : bone;
	self->turrets.push_back(t);
}

ModelTurretInfo &lastTurret(ModelConditionInfo *self, const char *field)
{
	if (self->turrets.empty())
	{
		throw INIException(3, "%s needs a Turret before it (retail would write through an empty list)", field);
	}
	return self->turrets.back();
}

void parseTurretPitch(INI *ini, void *instance, void *, const void *)
{
	ModelConditionInfo *self = static_cast<ModelConditionInfo *>(instance);
	ModelTurretInfo &t = lastTurret(self, "TurretPitch");
	std::string bone = ini->getNextToken();
	AsciiStringUtil::toLower(bone);
	self->addPublicBone(bone);
	t.pitchBone = (bone.empty() || isNoneName(bone)) ? std::string() : bone;
}

void parseTurretArtAngle(INI *ini, void *instance, void *, const void *)
{
	INI::parseAngleReal(ini, instance, &lastTurret(static_cast<ModelConditionInfo *>(instance), "TurretArtAngle").artAngle, nullptr);
}

void parseTurretArtPitch(INI *ini, void *instance, void *, const void *)
{
	INI::parseAngleReal(ini, instance, &lastTurret(static_cast<ModelConditionInfo *>(instance), "TurretArtPitch").artPitch, nullptr);
}

// RW 0x42DB44: BeginScript. The body is read from the next lines (RW 0x42D400) and assigned to the field.
void parseLuaScript(INI *ini, void *instance, void *, const void *)
{
	AnimationStateInfo *self = static_cast<AnimationStateInfo *>(instance);
	std::string body;
	std::vector<std::string> lines;
	for (;;)
	{
		ini->readLine();
		if (ini->isEOF())
		{
			throw INIException(4, "Missing 'ENDSCRIPT' token.\n\nError parsing block '%s' in file '%s', line %i.\n", ini->getCurBlockStart(),
				ini->getFilename().c_str(), ini->currentSourceLine());
		}
		const std::string line = ini->currentLineText();
		// RW 0x42D42A-0x42D45A: the line is copied BEFORE strtok splits it; the loop ends on a line whose first token equals
		// ENDSCRIPT (stricmp) and does not append that line.
		std::string first = line;
		const size_t b = first.find_first_not_of(" \n\r\t=");
		if (b != std::string::npos)
		{
			const size_t e = first.find_first_of(" \n\r\t=", b);
			first = first.substr(b, e == std::string::npos ? std::string::npos : e - b);
			if (AsciiStringUtil::compareNoCase(first, "ENDSCRIPT") == 0)
			{
				break;
			}
		}
		body += line;
		lines.push_back(line);
	}
	self->beginScript = body;
	self->beginScriptLines = lines;
}

// RW 0x73A302: EnteringStateFX = FXLIST (stored by name; the FXList store is not ported, S-094).
void parseFXListName(INI *ini, void *, void *store, const void *)
{
	*static_cast<std::string *>(store) = ini->getNextToken();
}

// RW 0x4B262A: AnimationSpeedFactorRange = MIN MAX (both required).
void parseSpeedFactorRange(INI *ini, void *instance, void *, const void *)
{
	W3DAnimationInfo *self = static_cast<W3DAnimationInfo *>(instance);
	self->speedFactorMin = ini->scanReal(ini->getNextToken());
	self->speedFactorMax = ini->scanReal(ini->getNextToken());
}

// ---------------------------------------------------------------------------------------------------------------------
// The Animation sub block (RW 0xBE04B8, 11 rows)
// ---------------------------------------------------------------------------------------------------------------------
const FieldParse kAnimationFieldParse[] = {
	{ "AnimationName", INI::parseAsciiStringVector, nullptr, (int)offsetof(W3DAnimationInfo, animationNames) },
	{ "AnimationMode", INI::parseIndexList, kAnimationModeNames, (int)offsetof(W3DAnimationInfo, mode) },
	{ "Distance", INI::parseReal, nullptr, (int)offsetof(W3DAnimationInfo, distance) },
	{ "AnimationBlendTime", INI::parseReal, nullptr, (int)offsetof(W3DAnimationInfo, blendTime) },
	{ "AnimationMustCompleteBlend", INI::parseBool, nullptr, (int)offsetof(W3DAnimationInfo, mustCompleteBlend) },
	{ "AnimationSpeedFactorRange", parseSpeedFactorRange, nullptr, 0 },
	{ "UseWeaponTiming", INI::parseBool, nullptr, (int)offsetof(W3DAnimationInfo, useWeaponTiming) },
	{ "AnimationPriority", INI::parseInt, nullptr, (int)offsetof(W3DAnimationInfo, priority) },
	{ "FadeBeginFrame", INI::parseReal, nullptr, (int)offsetof(W3DAnimationInfo, fadeBeginFrame) },
	{ "FadeEndFrame", INI::parseReal, nullptr, (int)offsetof(W3DAnimationInfo, fadeEndFrame) },
	{ "FadingIn", INI::parseBool, nullptr, (int)offsetof(W3DAnimationInfo, fadingIn) },
	{ nullptr, nullptr, nullptr, 0 },
};

// RW 0x4C7A89 (donor ParseAnimation.cpp). `Animation = LABEL` (the label is optional: 1,753 retail lines have none) opens a block.
void parseAnimation(INI *ini, void *instance, void *, const void *)
{
	AnimationStateInfo *state = static_cast<AnimationStateInfo *>(instance);
	W3DAnimationInfo info;
	info.labelOriginal = ini->getNextAsciiString();
	info.label = lowerCopy(info.labelOriginal);
	if (state->kind == AnimationStateInfo::KIND_IDLE)
	{
		info.mode = W3D_ANIM_MODE_ONCE; // RW 0x4C7AF7: userData == 1 stores mode 2
	}
	ini->initFromINI(&info, kAnimationFieldParse);
	if (info.priority < 0)
	{
		info.priority = 0;
	}
	else if (info.priority > 100)
	{
		info.priority = 100;
	}
	if (!info.animationNames.empty() && !info.animationNames[0].empty() && !isNoneName(info.animationNames[0]))
	{
		state->animations.push_back(info);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// AnimationState (RW 0xD99F60, 12 rows)
// ---------------------------------------------------------------------------------------------------------------------
const FieldParse kAnimationStateFieldParse[] = {
	{ "Animation", parseAnimation, nullptr, 0 },
	{ "StateName", INI::parseAsciiString, nullptr, (int)offsetof(AnimationStateInfo, stateName) },
	{ "Flags", INI::parseBitString32, kAnimationStateFlagNames, (int)offsetof(AnimationStateInfo, flags) },
	{ "ShareAnimation", INI::parseBool, nullptr, (int)offsetof(AnimationStateInfo, shareAnimation) },
	{ "EnteringStateFX", parseFXListName, nullptr, (int)offsetof(AnimationStateInfo, enteringStateFX) },
	{ "BeginScript", parseLuaScript, nullptr, 0 },
	{ "FrameForPristineBonePositions", INI::parseInt, nullptr, (int)offsetof(AnimationStateInfo, frameForPristineBonePositions) },
	{ "AllowRepeatInRandomPick", INI::parseBool, nullptr, (int)offsetof(AnimationStateInfo, allowRepeatInRandomPick) },
	{ "FXEvent", parseAnimStateFXEvent, nullptr, 0 },
	{ "LuaEvent", parseLuaEvent, nullptr, 0 },
	{ "ParticleSysBone", parseAnimStateParticleSysBone, nullptr, 0 },
	{ "SimilarRestart", INI::parseBool, nullptr, (int)offsetof(AnimationStateInfo, similarRestart) },
	{ nullptr, nullptr, nullptr, 0 },
};

// RW 0x4C8840. userData: 0 AnimationState (condition flags on the line), 1 IdleAnimationState (no tokens read; inserted at the front),
// 2 TransitionState (one name token, appended).
void parseAnimationState(INI *ini, void *instance, void *, const void *userData)
{
	W3DModelDrawModuleData *module = static_cast<W3DModelDrawModuleData *>(instance);
	const int mode = (int)(std::uintptr_t)userData;
	AnimationStateInfo state;
	state.kind = (AnimationStateInfo::Kind)mode;
	if (mode == 0)
	{
		ModelCondition::parseFromLine(ini, state.conditions);
	}
	else if (mode == 2)
	{
		if (const char *token = ini->getNextTokenOrNull())
		{
			state.stateName = token;
		}
	}
	ini->initFromINI(&state, kAnimationStateFieldParse);
	if (mode == 1)
	{
		module->m_animationStates.insert(module->m_animationStates.begin(), state);
	}
	else
	{
		module->m_animationStates.push_back(state);
	}
	module->clearMatchCaches();
}

// ---------------------------------------------------------------------------------------------------------------------
// ModelConditionState (RW 0xBE0A78, 32 rows)
// ---------------------------------------------------------------------------------------------------------------------
const FieldParse kModelConditionFieldParse[] = {
	{ "Model", parseModel, nullptr, 0 },
	{ "Skeleton", parseAsciiStringLC, nullptr, (int)offsetof(ModelConditionInfo, skeleton) },
	{ "ModelAnimationPrefix", INI::parseAsciiString, nullptr, (int)offsetof(ModelConditionInfo, modelAnimationPrefix) },
	{ "Texture", parseTexturePair, nullptr, 0 },
	{ "PortraitImageName", INI::parseAsciiString, nullptr, (int)offsetof(ModelConditionInfo, portraitImageName) },
	{ "ButtonImageName", INI::parseAsciiString, nullptr, (int)offsetof(ModelConditionInfo, buttonImageName) },
	{ "WeaponFireFXBone", parseWeaponBoneName, nullptr, (int)offsetof(ModelConditionInfo, weaponFireFXBone) },
	{ "WeaponLaunchBone", parseWeaponBoneName, nullptr, (int)offsetof(ModelConditionInfo, weaponLaunchBone) },
	{ "WeaponRecoilBone", parseWeaponBoneName, nullptr, (int)offsetof(ModelConditionInfo, weaponRecoilBone) },
	{ "WeaponMuzzleFlash", parseWeaponBoneName, nullptr, (int)offsetof(ModelConditionInfo, weaponMuzzleFlash) },
	{ "ParticleSysBone", parseModelStateParticleSysBone, nullptr, 0 },
	{ "OverrideTooltip", INI::parseAsciiString, nullptr, (int)offsetof(ModelConditionInfo, overrideTooltip) },
	{ "FXEvent", parseModelStateFXEvent, nullptr, 0 },
	{ "RetainSubObjects", INI::parseBool, nullptr, (int)offsetof(ModelConditionInfo, retainSubObjects) },
	{ "Shadow", parseShadowType, kShadowTypeNames, 0 },
	{ "ShadowSizeX", parseShadowSizeX, nullptr, 0 },
	{ "ShadowSizeY", parseShadowSizeY, nullptr, 0 },
	{ "ShadowOffsetX", parseShadowOffsetX, nullptr, 0 },
	{ "ShadowOffsetY", parseShadowOffsetY, nullptr, 0 },
	{ "ShadowSunAngle", parseShadowSunAngle, nullptr, 0 },
	{ "ShadowTexture", parseShadowTexture, nullptr, 0 },
	{ "ShadowMaxHeight", parseShadowMaxHeight, nullptr, 0 },
	{ "ShadowOverrideLODVisibility", parseShadowOverrideLODVisibility, nullptr, 0 },
	{ "ShadowOpacityStart", INI::parseUnsignedInt, nullptr, (int)offsetof(ModelConditionInfo, shadowOpacityStart) },
	{ "ShadowOpacityFadeInTime", INI::parseReal, nullptr, (int)offsetof(ModelConditionInfo, shadowOpacityFadeInTime) },
	{ "ShadowOpacityPeak", INI::parseUnsignedInt, nullptr, (int)offsetof(ModelConditionInfo, shadowOpacityPeak) },
	{ "ShadowOpacityFadeOutTime", INI::parseReal, nullptr, (int)offsetof(ModelConditionInfo, shadowOpacityFadeOutTime) },
	{ "ShadowOpacityEnd", INI::parseUnsignedInt, nullptr, (int)offsetof(ModelConditionInfo, shadowOpacityEnd) },
	{ "Turret", parseTurret, nullptr, 0 },
	{ "TurretArtAngle", parseTurretArtAngle, nullptr, 0 },
	{ "TurretPitch", parseTurretPitch, nullptr, 0 },
	{ "TurretArtPitch", parseTurretArtPitch, nullptr, 0 },
	{ nullptr, nullptr, nullptr, 0 },
};

// RW 0x4C800E. userData 0 = ModelConditionState, 1 = DefaultModelConditionState.
void parseModelConditionState(INI *ini, void *instance, void *, const void *userData)
{
	W3DModelDrawModuleData *module = static_cast<W3DModelDrawModuleData *>(instance);
	const int mode = (int)(std::uintptr_t)userData;
	ModelConditionInfo info;
	ModelConditionFlags flags;
	bool checkedFlags = false;

	if (mode == 1)
	{
		if (module->m_defaultState >= 0)
		{
			throw INIException(3, "*** ASSET ERROR: you may have only one default state!");
		}
		if (ini->getNextTokenOrNull() != nullptr)
		{
			throw INIException(3, "*** ASSET ERROR: unknown keyword");
		}
		if (!module->m_conditionStates.empty())
		{
			throw INIException(3, "*** ASSET ERROR: when using DefaultConditionState, it must be the first state listed (%s)\n", "");
		}
		module->m_defaultState = (int)module->m_conditionStates.size();
	}
	else
	{
		// RW 0x4C8133: a normal state starts as a copy of the default state, then the flags on the line are read.
		if (module->m_defaultState >= 0)
		{
			info = module->m_conditionStates[(size_t)module->m_defaultState];
		}
		ModelCondition::parseFromLine(ini, flags);
		const bool noDefault = module->m_defaultState < 0;
		if (noDefault && module->m_conditionStates.empty() && flags.any())
		{
			throw INIException(3, "*** ASSET ERROR: when not using DefaultConditionState, the first ConditionState must be for NONE (%s)", "");
		}
		if (!flags.any() && !noDefault)
		{
			throw INIException(3, "*** ASSET ERROR: you may not specify both a Default state and a Conditions=None state");
		}
		for (const ModelConditionInfo &other : module->m_conditionStates)
		{
			if (other.conditions == flags)
			{
				throw INIException(3, "*** ASSET ERROR: duplicate condition states are not currently allowed (%s)", "");
			}
		}
		checkedFlags = true;
	}
	(void)checkedFlags;

	ini->initFromINI(&info, kModelConditionFieldParse);
	if (info.modelNames.empty())
	{
		throw INIException(3, "*** ASSET ERROR: you must specify a model name");
	}
	if (isNoneName(info.modelNames[0]))
	{
		info.modelNames.clear(); // RW 0x4C821F: Model = NONE means no model
	}
	info.conditions = flags;
	module->m_conditionStates.push_back(info);
	module->clearMatchCaches();
}

// ---------------------------------------------------------------------------------------------------------------------
// LodOptions (RW 0xBDCAA0 -> row table 0xBDCA20) and the module level tables
// ---------------------------------------------------------------------------------------------------------------------
const FieldParse kLodOptionsRowFieldParse[] = {
	{ "AllowMultipleModels", INI::parseBool, nullptr, (int)offsetof(W3DLodOptions, allowMultipleModels) },
	{ "MaxRandomTextures", INI::parseInt, nullptr, (int)offsetof(W3DLodOptions, maxRandomTextures) },
	{ "MaxRandomAnimations", INI::parseInt, nullptr, (int)offsetof(W3DLodOptions, maxRandomAnimations) },
	{ "MaxAnimFrameDelta", INI::parseReal, nullptr, (int)offsetof(W3DLodOptions, maxAnimFrameDelta) },
	{ "RandomStartFramePercent", INI::parseInt, nullptr, (int)offsetof(W3DLodOptions, randomStartFramePercent) },
	{ nullptr, nullptr, nullptr, 0 },
};

// RW 0x478B94: the first token LOW | MEDIUM | HIGH (stricmp; anything else is "Expected LOW, MEDIUM, or HIGH"), then the row.
void parseLodOptions(INI *ini, void *instance, void *, const void *)
{
	W3DHordeModelDrawModuleData *module = static_cast<W3DHordeModelDrawModuleData *>(instance);
	const std::string level = ini->getNextAsciiString();
	int slot;
	if (AsciiStringUtil::compareNoCase(level, "LOW") == 0)
	{
		slot = 0;
	}
	else if (AsciiStringUtil::compareNoCase(level, "MEDIUM") == 0)
	{
		slot = 1;
	}
	else if (AsciiStringUtil::compareNoCase(level, "HIGH") == 0)
	{
		slot = 2;
	}
	else
	{
		throw INIException(1, "Expected LOW, MEDIUM, or HIGH");
	}
	ini->initFromINI(&module->m_lodOptions[slot], kLodOptionsRowFieldParse);
}

const FieldParse kLodOptionsFieldParse[] = {
	{ "LodOptions", parseLodOptions, nullptr, 0 },
	{ nullptr, nullptr, nullptr, 0 },
};

// RW 0x4C7C56: RandomTexture = NAME [WEIGHT [REPLACEMENT]] ; entries with the same NAME (stricmp) share one record.
void parseRandomTexture(INI *ini, void *instance, void *, const void *)
{
	W3DModelDrawModuleData *module = static_cast<W3DModelDrawModuleData *>(instance);
	if (!module)
	{
		return;
	}
	std::string name;
	if (const char *t = ini->getNextTokenOrNull())
	{
		name = t;
	}
	int weight = 0;
	if (const char *t = ini->getNextTokenOrNull())
	{
		weight = ini->scanInt(t);
	}
	std::string replacement;
	if (const char *t = ini->getNextTokenOrNull())
	{
		replacement = t;
	}
	W3DRandomTextureInfo *record = nullptr;
	for (W3DRandomTextureInfo &r : module->m_randomTextures)
	{
		if (AsciiStringUtil::compareNoCase(r.name, name) == 0)
		{
			record = &r;
			break;
		}
	}
	if (!record)
	{
		module->m_randomTextures.push_back(W3DRandomTextureInfo());
		record = &module->m_randomTextures.back();
		record->name = name;
	}
	W3DRandomTextureInfo::Entry e;
	e.replacement = replacement;
	e.weight = weight;
	record->entries.push_back(e);
}

// RW 0x4C42A8 / 0x4C3321 / 0x4C30BD: kept as the words that were written (S-094).
void parseWordsRaw(INI *ini, void *, void *store, const void *)
{
	std::vector<std::string> *words = static_cast<std::vector<std::string> *>(store);
	words->clear();
	for (const char *t = ini->getNextTokenOrNull(); t != nullptr; t = ini->getNextTokenOrNull())
	{
		words->push_back(t);
	}
}

void parseEmbedPortal(INI *ini, void *instance, void *, const void *)
{
	W3DModelDrawModuleData *module = static_cast<W3DModelDrawModuleData *>(instance);
	module->m_embedPortalName = lowerCopy(ini->getNextAsciiString());
	module->m_embedPortalKind = ini->getNextAsciiString();
}

// RW 0x4C7E42 (donor Rva0077C390Parse.cpp): AttachModel [conditions] ... End with Bone / Offset / Model NAME prob:N.
void parseAttachModelEntry(INI *ini, void *instance, void *, const void *)
{
	W3DAttachModelInfo *info = static_cast<W3DAttachModelInfo *>(instance);
	std::string name = ini->getNextAsciiString();
	int probability = 0;
	const char *colon = ini->getSepsColon();
	for (const char *t = ini->getNextTokenOrNull(colon); t != nullptr; t = ini->getNextTokenOrNull(colon))
	{
		if (AsciiStringUtil::compareNoCase(t, "prob") == 0)
		{
			probability = ini->scanInt(ini->getNextToken(colon));
		}
	}
	info->models.emplace_back(name, probability);
}

void parseAttachModel(INI *ini, void *instance, void *, const void *)
{
	W3DModelDrawModuleData *module = static_cast<W3DModelDrawModuleData *>(instance);
	W3DAttachModelInfo info;
	// RW 0x4B5B2A reads the condition list into two sets; their meaning is not decoded (S-094): both hold the listed flags.
	ModelCondition::parseFromLine(ini, info.positive);
	info.all = info.positive;
	static const FieldParse table[] = {
		{ "Bone", INI::parseAsciiString, nullptr, (int)offsetof(W3DAttachModelInfo, bone) },
		{ "Offset", INI::parseCoord3D, nullptr, (int)offsetof(W3DAttachModelInfo, offset) },
		{ "Model", parseAttachModelEntry, nullptr, 0 },
		{ nullptr, nullptr, nullptr, 0 },
	};
	ini->initFromINI(&info, table);
	if (info.bone.empty())
	{
		throw INIException(3, "*** ASSET ERROR: you must specify then bone name");
	}
	if (info.models.empty())
	{
		throw INIException(3, "*** ASSET ERROR: you must specify at least one model name");
	}
	// donor Rva0077C390Parse.cpp: probabilities <= 0 are unassigned and share what the assigned ones leave of 100.
	int total = 0;
	int unassigned = 0;
	for (const auto &m : info.models)
	{
		if (m.second <= 0)
		{
			++unassigned;
		}
		else
		{
			total += m.second;
		}
	}
	if (total > 100)
	{
		throw INIException(3, "*** ASSET ERROR: combined probability may not be higher than 100 (it's %i)", total);
	}
	if (total + unassigned > 100)
	{
		throw INIException(3, "*** ASSET ERROR: can't auto-assign probabilities, specified probabilities must be %i or less", 100 - unassigned);
	}
	if (unassigned != 0)
	{
		int remainder = 100 - total;
		const int share = remainder / unassigned;
		for (auto &m : info.models)
		{
			if (m.second <= 0)
			{
				--unassigned;
				if (unassigned == 0)
				{
					m.second = remainder;
				}
				else
				{
					m.second = share;
					remainder -= share;
				}
				total += m.second;
			}
		}
	}
	if (total > 100)
	{
		throw INIException(3, "*** ASSET ERROR: combined probability must be 100 (it's %i)", total);
	}
	module->m_attachModels.push_back(info);
}

void parseDependencySharedModelFlags(INI *ini, void *, void *store, const void *)
{
	ModelCondition::parseFromLine(ini, *static_cast<ModelConditionFlags *>(store));
}

// RW 0x42F2FA: AlphaRefRange = X:n Y:n.
void parseAlphaRefRange(INI *ini, void *instance, void *, const void *)
{
	W3DModelDrawModuleData *module = static_cast<W3DModelDrawModuleData *>(instance);
	module->m_alphaRefRangeX = ini->scanInt(ini->getNextSubToken("X"));
	module->m_alphaRefRangeY = ini->scanInt(ini->getNextSubToken("Y"));
}

const FieldParse kModelDrawFieldParse[] = {
	{ "InitialRecoilSpeed", INI::parseVelocityReal, nullptr, (int)offsetof(W3DModelDrawModuleData, m_initialRecoil) },
	{ "MaxRecoilDistance", INI::parseReal, nullptr, (int)offsetof(W3DModelDrawModuleData, m_maxRecoil) },
	{ "RecoilDamping", INI::parseReal, nullptr, (int)offsetof(W3DModelDrawModuleData, m_recoilDamping) },
	{ "RecoilSettleSpeed", INI::parseVelocityReal, nullptr, (int)offsetof(W3DModelDrawModuleData, m_recoilSettle) },
	{ "OkToChangeModelColor", INI::parseBool, nullptr, (int)offsetof(W3DModelDrawModuleData, m_okToChangeModelColor) },
	{ "AnimationsRequirePower", INI::parseBool, nullptr, (int)offsetof(W3DModelDrawModuleData, m_animationsRequirePower) },
	{ "MinLODRequired", INI::parseIndexList, kStaticGameLODNames, (int)offsetof(W3DModelDrawModuleData, m_minLODRequired) },
	{ "ProjectileBoneFeedbackEnabledSlots", INI::parseBitString32, kWeaponSlotNames, (int)offsetof(W3DModelDrawModuleData, m_projectileBoneFeedbackEnabledSlots) },
	{ "DefaultModelConditionState", parseModelConditionState, (const void *)(std::uintptr_t)1, 0 },
	{ "ModelConditionState", parseModelConditionState, (const void *)(std::uintptr_t)0, 0 },
	{ "IdleAnimationState", parseAnimationState, (const void *)(std::uintptr_t)1, 0 },
	{ "TransitionState", parseAnimationState, (const void *)(std::uintptr_t)2, 0 },
	{ "AnimationState", parseAnimationState, (const void *)(std::uintptr_t)0, 0 },
	{ "TrackMarks", parseAsciiStringLC, nullptr, (int)offsetof(W3DModelDrawModuleData, m_trackFile) },
	{ "ExtraPublicBone", INI::parseAsciiStringVectorAppend, nullptr, (int)offsetof(W3DModelDrawModuleData, m_extraPublicBones) },
	{ "AttachToBoneInAnotherModule", parseAsciiStringLC, nullptr, (int)offsetof(W3DModelDrawModuleData, m_attachToDrawableBone) },
	{ "DependencySharedModelFlags", parseDependencySharedModelFlags, nullptr, (int)offsetof(W3DModelDrawModuleData, m_dependencySharedModelFlags) },
	{ "TimeOfDayTexture", parseWordsRaw, nullptr, (int)offsetof(W3DModelDrawModuleData, m_timeOfDayTextureRaw) },
	{ "UseProducerTexture", INI::parseBool, nullptr, (int)offsetof(W3DModelDrawModuleData, m_useProducerTexture) },
	{ "NoRotate", INI::parseBool, nullptr, (int)offsetof(W3DModelDrawModuleData, m_noRotate) },
	{ "UseFiringArcRotation", INI::parseBool, nullptr, (int)offsetof(W3DModelDrawModuleData, m_useFiringArcRotation) },
	{ "RandomTexture", parseRandomTexture, nullptr, 0 },
	{ "RandomTextureFixedRandomIndex", INI::parseBool, nullptr, (int)offsetof(W3DModelDrawModuleData, m_randomTextureFixedRandomIndex) },
	{ "BurntTexture", parseWordsRaw, nullptr, (int)offsetof(W3DModelDrawModuleData, m_burntTextureRaw) },
	{ "AttachModel", parseAttachModel, nullptr, 0 },
	{ "ParticlesAttachedToAnimatedBones", INI::parseBool, nullptr, (int)offsetof(W3DModelDrawModuleData, m_particlesAttachedToAnimatedBones) },
	{ "TrackMarksLeftBone", INI::parseAsciiString, nullptr, (int)offsetof(W3DModelDrawModuleData, m_trackMarksLeftBone) },
	{ "TrackMarksRightBone", INI::parseAsciiString, nullptr, (int)offsetof(W3DModelDrawModuleData, m_trackMarksRightBone) },
	{ "RampMesh1", INI::parseAsciiString, nullptr, (int)offsetof(W3DModelDrawModuleData, m_rampMesh1) },
	{ "RampMesh2", INI::parseAsciiString, nullptr, (int)offsetof(W3DModelDrawModuleData, m_rampMesh2) },
	{ "EmbedPortal", parseEmbedPortal, nullptr, 0 },
	{ "WallBoundsMesh", INI::parseAsciiString, nullptr, (int)offsetof(W3DModelDrawModuleData, m_wallBoundsMesh) },
	{ "RaisedWallMesh", INI::parseAsciiString, nullptr, (int)offsetof(W3DModelDrawModuleData, m_raisedWallMesh) },
	{ "GlowEnabled", INI::parseBool, nullptr, (int)offsetof(W3DModelDrawModuleData, m_glowEnabled) },
	{ "GlowEmissive", INI::parseBool, nullptr, (int)offsetof(W3DModelDrawModuleData, m_glowEmissive) },
	{ "ParticleBonesCheckDrawable", INI::parseBool, nullptr, (int)offsetof(W3DModelDrawModuleData, m_particleBonesCheckDrawable) },
	{ "ShadowForceDisable", INI::parseBool, nullptr, (int)offsetof(W3DModelDrawModuleData, m_shadowForceDisable) },
	{ "HighDetailLODThreshold", INI::parseReal, nullptr, (int)offsetof(W3DModelDrawModuleData, m_highDetailLODThreshold) },
	{ "LowDetailLODThreshold", INI::parseReal, nullptr, (int)offsetof(W3DModelDrawModuleData, m_lowDetailLODThreshold) },
	{ "SwitchModelLODMode", INI::parseBool, nullptr, (int)offsetof(W3DModelDrawModuleData, m_switchModelLODMode) },
	{ "StaticModelLODMode", INI::parseBool, nullptr, (int)offsetof(W3DModelDrawModuleData, m_staticModelLODMode) },
	{ "ShowShadowWhileContained", INI::parseBool, nullptr, (int)offsetof(W3DModelDrawModuleData, m_showShadowWhileContained) },
	{ "UseStandardModelNames", INI::parseBool, nullptr, (int)offsetof(W3DModelDrawModuleData, m_useStandardModelNames) },
	{ "UseDefaultAnimation", INI::parseBool, nullptr, (int)offsetof(W3DModelDrawModuleData, m_useDefaultAnimation) },
	{ "AlphaRefAnimated", INI::parseBool, nullptr, (int)offsetof(W3DModelDrawModuleData, m_alphaRefAnimated) },
	{ "AlphaRefRange", parseAlphaRefRange, nullptr, 0 },
	{ "WadingParticleSys", INI::parseAsciiString, nullptr, (int)offsetof(W3DModelDrawModuleData, m_wadingParticleSys) },
	{ "AlphaCameraFadeOuterRadius", INI::parsePositiveNonZeroReal, nullptr, (int)offsetof(W3DModelDrawModuleData, m_alphaCameraFadeOuterRadius) },
	{ "AlphaCameraFadeInnerRadius", INI::parsePositiveNonZeroReal, nullptr, (int)offsetof(W3DModelDrawModuleData, m_alphaCameraFadeInnerRadius) },
	{ "AlphaCameraAtInnerRadius", INI::parsePercentToReal, nullptr, (int)offsetof(W3DModelDrawModuleData, m_alphaCameraAtInnerRadius) },
	{ "StaticSortLevelWhileFading", INI::parseInt, nullptr, (int)offsetof(W3DModelDrawModuleData, m_staticSortLevelWhileFading) },
	{ "BirthFadeTime", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(W3DModelDrawModuleData, m_birthFadeTime) },
	{ "BirthFadeAdditive", INI::parseBool, nullptr, (int)offsetof(W3DModelDrawModuleData, m_birthFadeAdditive) },
	{ "ZWriteDisableOverride", INI::parseBool, nullptr, (int)offsetof(W3DModelDrawModuleData, m_zWriteDisableOverride) },
	{ "MultiPlayerOnly", INI::parseBool, nullptr, (int)offsetof(W3DModelDrawModuleData, m_multiPlayerOnly) },
	{ "AffectedByStealth", INI::parseBool, nullptr, (int)offsetof(W3DModelDrawModuleData, m_affectedByStealth) },
	{ "HighDetailOnly", INI::parseBool, nullptr, (int)offsetof(W3DModelDrawModuleData, m_highDetailOnly) },
	{ nullptr, nullptr, nullptr, 0 },
};
} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// ModelConditionInfo / module data
// ---------------------------------------------------------------------------------------------------------------------
void ModelConditionInfo::addPublicBone(const std::string &lowerBoneName)
{
	// ZH ModelConditionInfo::addPublicBone (W3DModelDraw.cpp:403-410)
	if (lowerBoneName.empty() || isNoneName(lowerBoneName))
	{
		return;
	}
	if (std::find(publicBones.begin(), publicBones.end(), lowerBoneName) == publicBones.end())
	{
		publicBones.push_back(lowerBoneName);
	}
}

// RW 0x4C87DD-0x4C8800 (target): the constructor builds an AnimationState (RW 0x4C6147 defaults: no conditions, no animations), names
// it "<DefaultEmptyIdleAnimationState>" and pushes it onto the animation state list before any INI state is parsed. An explicit
// IdleAnimationState is inserted in FRONT of it (RW 0x4C8840), so it is only reached by RW 0x4B4443's empty-condition pass when the
// module has no IdleAnimationState: the idle fallback of retail.
W3DModelDrawModuleData::W3DModelDrawModuleData()
{
	AnimationStateInfo empty;
	empty.stateName = "<DefaultEmptyIdleAnimationState>";
	m_animationStates.push_back(empty);
}

void W3DModelDrawModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kModelDrawFieldParse);
}

void W3DScriptedModelDrawModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	// RW 0x4C893A: the one table.
	W3DModelDrawModuleData::buildFieldParse(p);
}

// RW 0x478289 (target facts: the stores at +0x188..+0x1C0 and the floats at RW 0xBDC6CC = 15.0, 0xBD89BC = 8.0, 0xBD869C = 0.5)
W3DHordeModelDrawModuleData::W3DHordeModelDrawModuleData()
{
	m_lodOptions[0] = { false, 1, 1, 15.0f, 0 };
	m_lodOptions[1] = { true, 2, 2, 8.0f, 50 };
	m_lodOptions[2] = { true, 999, 999, 0.5f, 100 };
}

void W3DHordeModelDrawModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	// RW 0x478C74: add the LodOptions table, then fall into RW 0x4C893A.
	p.add(kLodOptionsFieldParse);
	W3DScriptedModelDrawModuleData::buildFieldParse(p);
}

void W3DDefaultDrawModuleData::buildFieldParse(MultiIniFieldParse &)
{
	// ModuleData has no fields (RW createData of W3DDefaultDraw is the base ModuleData; the retail INI bodies are empty).
}

// RW 0x4B4379. A state "matches" when every one of its condition bits is in the query (the code ANDs the state's set with the query
// and compares the result with the state's set).
const ModelConditionInfo *W3DModelDrawModuleData::findBestInfo(const ModelConditionFlags &c) const
{
	if (!c.any() && m_defaultState >= 0)
	{
		return &m_conditionStates[(size_t)m_defaultState]; // RW 0x4B43A2
	}
	const ModelConditionInfo *lastNone = nullptr;
	for (const ModelConditionInfo &state : m_conditionStates)
	{
		if (!state.conditions.any())
		{
			if (!c.any())
			{
				return &state; // RW 0x4B43E1: an empty query takes the first state without conditions
			}
			lastNone = &state; // RW 0x4B43F4: remembered, overwritten by later ones
			continue;
		}
		if (c.testForAll(state.conditions))
		{
			return &state;
		}
	}
	if (m_defaultState >= 0)
	{
		return &m_conditionStates[(size_t)m_defaultState]; // RW 0x4B4428
	}
	return lastNone;
}

// RW 0x4B4443
const AnimationStateInfo *W3DModelDrawModuleData::findBestAnimationState(const ModelConditionFlags &c) const
{
	for (const AnimationStateInfo &state : m_animationStates)
	{
		if (state.conditions.any() && c.testForAll(state.conditions))
		{
			return &state;
		}
	}
	for (const AnimationStateInfo &state : m_animationStates)
	{
		if (!state.conditions.any())
		{
			return &state;
		}
	}
	return nullptr;
}

const AnimationStateInfo *W3DModelDrawModuleData::findTransitionState(const std::string &name) const
{
	for (const AnimationStateInfo &s : m_animationStates)
	{
		if (s.kind == AnimationStateInfo::KIND_TRANSITION && AsciiStringUtil::compareNoCase(s.stateName, name) == 0)
		{
			return &s;
		}
	}
	return nullptr;
}

const AnimationStateInfo *W3DModelDrawModuleData::findStateByName(const std::string &name) const
{
	for (const AnimationStateInfo &s : m_animationStates)
	{
		if (AsciiStringUtil::compareNoCase(s.stateName, name) == 0)
		{
			return &s;
		}
	}
	return nullptr;
}

std::vector<std::string> W3DModelDrawModuleData::unverifiedParseItems() const
{
	std::vector<std::string> out;
	size_t particles = 0, fx = 0, fxEvents = 0, extraNames = 0, minLodBeyond = 0;
	for (const ModelConditionInfo &s : m_conditionStates)
	{
		particles += s.particleSysBones.size();
		fxEvents += s.fxEvents.size();
	}
	for (const AnimationStateInfo &s : m_animationStates)
	{
		particles += s.particleSysBones.size();
		fxEvents += s.fxEvents.size();
		fx += s.enteringStateFX.empty() ? 0 : 1;
		for (const W3DAnimationInfo &a : s.animations)
		{
			extraNames += a.animationNames.size() > 1 ? 1 : 0;
		}
	}
	if (m_minLODRequired >= 5)
	{
		++minLodBeyond;
	}
	auto add = [&out](const std::string &detail) { out.push_back("[S-094] " + detail); };
	if (particles || fx || fxEvents)
	{
		add(std::to_string(particles) + " ParticleSysBone, " + std::to_string(fx) + " EnteringStateFX and " + std::to_string(fxEvents) +
			" FXEvent names are stored without checking them against the particle system / FXList registries (not ported)");
	}
	if (extraNames)
	{
		add(std::to_string(extraNames) + " AnimationName lists have words after the first; only the first is played and the others' meaning is unknown");
	}
	if (!m_timeOfDayTextureRaw.empty() || !m_burntTextureRaw.empty() || !m_embedPortalName.empty() || !m_attachModels.empty())
	{
		add("TimeOfDayTexture / BurntTexture / EmbedPortal / AttachModel are stored (their condition flags and the use of the portal kind are not decoded)");
	}
	// Always: the constructor (RW 0x4C85E9) members that are not mapped to a parse result (the container at +0x8C and the 2 x 0x14 byte
	// array at +0x160 belong to the RandomTexture / TimeOfDayTexture / BurntTexture storage, kept raw here). The scalar defaults it
	// sets are ported.
	add("constructor defaults not recovered: the +0x8C container and the +0x160 array of 2 x 0x14 bytes are empty here");
	if (minLodBeyond)
	{
		add("MinLODRequired names an entry past the five StaticGameLODLevel names: retail's list is not NULL terminated there (RW 0xD9E6BC) and runs on into the neighbouring tables");
	}
	return out;
}

std::vector<std::string> W3DModelDrawModuleData::validate() const
{
	std::vector<std::string> problems;
	if (!findBestInfo(ModelConditionFlags()))
	{
		problems.push_back("no model condition state matches the empty condition set (retail: \"all draw modules must have an IDLE state\")");
	}
	return problems;
}

namespace W3DDrawTables
{
const FieldParse *modelCondition() { return kModelConditionFieldParse; }
const FieldParse *animationState() { return kAnimationStateFieldParse; }
const FieldParse *animation() { return kAnimationFieldParse; }
const FieldParse *lodOptionsRow() { return kLodOptionsRowFieldParse; }
const FieldParse *hordeLodOptions() { return kLodOptionsFieldParse; }
const FieldParse *moduleTable() { return kModelDrawFieldParse; }
const char *const *weaponSlotNames() { return kWeaponSlotNames; }
const char *const *animationStateFlagNames() { return kAnimationStateFlagNames; }
const char *const *animationModeNames() { return kAnimationModeNames; }
const char *const *shadowTypeNames() { return kShadowTypeNames; }
const char *const *staticGameLODNames() { return kStaticGameLODNames; }
const char *const *fxTriggerNames() { return kFXTriggerNames; }
const char *const *persistNames() { return kPersistNames; }
const char *const *timeOfDayNames() { return kTimeOfDayNames; }
} // namespace W3DDrawTables

bool W3DNamesEqual(const std::string &a, const std::string &b) { return AsciiStringUtil::compareNoCase(a, b) == 0; }

W3DDrawModuleClass W3DDrawModuleClassFromName(const std::string &className)
{
	// the registered names (strcmp, case sensitive, RW 0xBDBBB0 / 0xBDBB9C / 0xBDBBD8)
	if (className == "W3DScriptedModelDraw")
	{
		return W3D_DRAW_SCRIPTED_MODEL;
	}
	if (className == "W3DHordeModelDraw")
	{
		return W3D_DRAW_HORDE_MODEL;
	}
	if (className == "W3DDefaultDraw")
	{
		return W3D_DRAW_DEFAULT;
	}
	return W3D_DRAW_NONE;
}

std::unique_ptr<W3DModelDrawModuleData> W3DParseDrawModuleBody(INI *ini, W3DDrawModuleClass cls)
{
	switch (cls)
	{
	case W3D_DRAW_SCRIPTED_MODEL:
	{
		std::unique_ptr<W3DScriptedModelDrawModuleData> data(new W3DScriptedModelDrawModuleData());
		ini->initFromINIMultiProc(data.get(), W3DScriptedModelDrawModuleData::buildFieldParse);
		return data;
	}
	case W3D_DRAW_HORDE_MODEL:
	{
		std::unique_ptr<W3DHordeModelDrawModuleData> data(new W3DHordeModelDrawModuleData());
		ini->initFromINIMultiProc(data.get(), W3DHordeModelDrawModuleData::buildFieldParse);
		return data;
	}
	case W3D_DRAW_DEFAULT:
		throw std::logic_error("W3DDefaultDraw has its own module data class: use W3DParseDefaultDrawBody");
	default:
		break;
	}
	throw INIException(3, "not a W3D draw module class");
}

std::unique_ptr<W3DDefaultDrawModuleData> W3DParseDefaultDrawBody(INI *ini)
{
	std::unique_ptr<W3DDefaultDrawModuleData> data(new W3DDefaultDrawModuleData());
	ini->initFromINIMultiProc(data.get(), W3DDefaultDrawModuleData::buildFieldParse);
	return data;
}
