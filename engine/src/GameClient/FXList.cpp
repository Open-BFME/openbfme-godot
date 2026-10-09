// OpenBFME. GPL-3.0. See FXList.h for the target / donor facts. Addresses are RotWK game.dat (RW), stop S-001 caveat.

#include "GameClient/FXList.h"

#include "Common/AsciiString.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDrawServices.h"

#include <cmath>
#include <cstring>
#include <stdexcept>

// RW 0x76392f: FXListObjectFilter.cpp (its own translation unit); store is the nugget's shared_ptr<ObjectFilter> member.
void ParseFXNuggetObjectFilter(INI *ini, void *instance, void *store, const void *userData);

namespace
{

const float kTwoPi = 6.2831855f; // RW 0xbdd38c
const float kPi = 3.14159274f;   // RW 0xbdd388

template <class Object, class Member>
int offsetIn(const Object &object, const Member &member)
{
	return (int)((const char *)&member - (const char *)&object);
}

typedef std::vector<FieldParse> Table;
const FieldParse *finish(Table &table)
{
	table.push_back(FieldParse{ nullptr, nullptr, nullptr, 0 });
	return table.data();
}

#define ROW(token, proc, ud, obj, member) FieldParse{ token, proc, ud, offsetIn(obj, (obj).member) }
#define RV_ROW(token, obj, member) ROW(token, GameClientRandomVariable::parseRandomVariable, nullptr, obj, member)

const char *const WeatherNames[] = { "NORMAL", "SNOWY", nullptr };
const char *const DecalShaderNames[] = { "ALPHA", "ADDITIVE", "SUBTRACT", nullptr };
const char *const BuffTypeNames[] = { "DO_NOT_USE_THIS_TYPE", "Healing", "LeaderShip", "GloriousCharge", "Dominate", "Cursed", "Buff", "Debuff", "Poison", nullptr };
const char *const FXTriggerNames[] = { "NONE", "CATAPULT_ROCK", "TREBUCHET_ROCK", nullptr }; // RW 0xd9de5c
const LookupListRec ViewShakeTypes[] = { { "SUBTLE", 0 }, { "NORMAL", 1 }, { "STRONG", 2 }, { "SEVERE", 3 }, { "CINE_EXTREME", 4 }, { "CINE_INSANE", 5 }, { nullptr, 0 } };
const LookupListRec ScorchTypes[] = { { "SCORCH_1", 0 }, { "SCORCH_2", 1 }, { "SCORCH_3", 2 }, { "SCORCH_4", 3 }, { "SCORCH_5", 4 }, { "SCORCH_6", 5 }, { "SCORCH_7", 6 },
	{ "SCORCH_8", 7 }, { "SCORCH_9", 8 }, { "RANDOM", -1 }, { nullptr, 0 } };

// ---- field parsers --------------------------------------------------------------------------------------------------
// RW 0x73b217 -> 0x73aa94: "NoSound" -> empty; else the audio registry is asked (parse-time validation).
void parseSoundName(INI *ini, void *, void *store, const void *)
{
	const std::string name = ini->getNextToken();
	std::string &out = *(std::string *)store;
	if (AsciiStringUtil::compareNoCase(name, "NoSound") == 0)
	{
		out.clear();
		return;
	}
	if (FXListParseServices *services = TheFXListStore ? TheFXListStore->parseServices() : nullptr)
	{
		if (!services->audioEventExists(name))
		{
			throw INIException(3, "Invalid Sound '%s'", name.c_str()); // RW 0xc2500c
		}
	}
	else if (TheFXListStore)
	{
		TheFXListStore->noteUnvalidatedSound();
	}
	out = name;
}

// RW 0x5de588 -> 0x5de0d8: "None" -> -1 (empty here); unknown -> code 3.
void parseEvaName(INI *ini, void *, void *store, const void *)
{
	const std::string name = ini->getNextToken();
	std::string &out = *(std::string *)store;
	if (AsciiStringUtil::compareNoCase(name, "None") == 0)
	{
		out.clear();
		return;
	}
	if (FXListParseServices *services = TheFXListStore ? TheFXListStore->parseServices() : nullptr)
	{
		if (!services->evaEventExists(name))
		{
			throw INIException(3, "Expected a recognized Eva event name or 'None'; got '%s'", name.c_str());
		}
	}
	else if (TheFXListStore)
	{
		TheFXListStore->noteUnvalidatedEva();
	}
	out = name;
}

// RW 0x5df33e: the tint colour accepts -255..255 per component and stores v / 255.
void parseTintColor(INI *ini, void *, void *store, const void *)
{
	const char *names[3] = { "R", "G", "B" };
	float v[3];
	for (int i = 0; i < 3; ++i)
	{
		const int c = ini->scanInt(ini->getNextSubToken(names[i]));
		if (c < -255 || c > 255)
		{
			throw INIException(3, "color value %s=%i out of range (-255..255)", names[i], c);
		}
		v[i] = (float)c / 255.0f;
	}
	RGBColor *out = (RGBColor *)store;
	out->red = v[0];
	out->green = v[1];
	out->blue = v[2];
}

// ---- nugget tables ---------------------------------------------------------------------------------------------
const FieldParse *baseTable()
{
	static Table t;
	if (t.empty())
	{
		static SoundFXNugget d; // any derived instance: the base offsets are the same
		t.push_back(ROW("ObjectFilter", ParseFXNuggetObjectFilter, nullptr, d, m_objectFilter));
		t.push_back(ROW("SourceObjectFilter", ParseFXNuggetObjectFilter, nullptr, d, m_sourceObjectFilter));
		t.push_back(ROW("RequiredSecondaryModelConditions", ModelCondition::parseFromINI, nullptr, d, m_requiredSecondaryModelConditions));
		t.push_back(ROW("ExcludedSecondaryModelConditions", ModelCondition::parseFromINI, nullptr, d, m_excludedSecondaryModelConditions));
		t.push_back(ROW("RequiredSourceModelConditions", ModelCondition::parseFromINI, nullptr, d, m_requiredSourceModelConditions));
		t.push_back(ROW("ExcludedSourceModelConditions", ModelCondition::parseFromINI, nullptr, d, m_excludedSourceModelConditions));
		t.push_back(ROW("StopIfNuggetPlayed", INI::parseBool, nullptr, d, m_stopIfNuggetPlayed));
		t.push_back(ROW("Weather", INI::parseIndexList, WeatherNames, d, m_weather));
		finish(t);
	}
	return t.data();
}

template <class T>
void parseNugget(INI *ini, void *instance, void *, const void *)
{
	FXList *fx = (FXList *)instance;
	std::unique_ptr<T> n = std::make_unique<T>();
	MultiIniFieldParse tables;
	tables.add(n->fieldParse()); // the nugget's own table is searched first, the base table second (RW 0x42b8d7 order)
	tables.add(baseTable());
	ini->initFromINIMulti(n.get(), tables);
	fx->addNugget(std::move(n));
}

} // namespace

// ---- per-nugget tables (RW tables 0xbf2c90 ... ) ----------------------------------------------------------------
const FieldParse *SoundFXNugget::fieldParse() const
{
	static Table t;
	if (t.empty())
	{
		static SoundFXNugget d;
		t.push_back(ROW("Name", parseSoundName, nullptr, d, m_soundName));
		finish(t);
	}
	return t.data();
}

const FieldParse *EvaEventFXNugget::fieldParse() const
{
	static Table t;
	if (t.empty())
	{
		static EvaEventFXNugget d;
		t.push_back(ROW("EvaEventOwner", parseEvaName, nullptr, d, m_owner));
		t.push_back(ROW("EvaEventAlly", parseEvaName, nullptr, d, m_ally));
		t.push_back(ROW("EvaEventEnemy", parseEvaName, nullptr, d, m_enemy));
		finish(t);
	}
	return t.data();
}

const FieldParse *RayEffectFXNugget::fieldParse() const
{
	static Table t;
	if (t.empty())
	{
		static RayEffectFXNugget d;
		t.push_back(ROW("Name", INI::parseAsciiString, nullptr, d, m_templateName));
		t.push_back(ROW("PrimaryOffset", INI::parseCoord3D, nullptr, d, m_primaryOffset));
		t.push_back(ROW("SecondaryOffset", INI::parseCoord3D, nullptr, d, m_secondaryOffset));
		finish(t);
	}
	return t.data();
}

const FieldParse *LightPulseFXNugget::fieldParse() const
{
	static Table t;
	if (t.empty())
	{
		static LightPulseFXNugget d;
		t.push_back(ROW("Color", INI::parseRGBColor, nullptr, d, m_color));
		t.push_back(ROW("Radius", INI::parseReal, nullptr, d, m_radius));
		t.push_back(ROW("RadiusAsPercentOfObjectSize", INI::parsePercentToReal, nullptr, d, m_boundingCirclePct));
		t.push_back(ROW("IncreaseTime", INI::parseDurationUnsignedInt, nullptr, d, m_increaseFrames));
		t.push_back(ROW("DecreaseTime", INI::parseDurationUnsignedInt, nullptr, d, m_decreaseFrames));
		finish(t);
	}
	return t.data();
}

const FieldParse *CameraShakerVolumeFXNugget::fieldParse() const
{
	static Table t;
	if (t.empty())
	{
		static CameraShakerVolumeFXNugget d;
		t.push_back(ROW("Radius", INI::parseReal, nullptr, d, m_radius));
		t.push_back(ROW("Duration_Seconds", INI::parseReal, nullptr, d, m_durationSeconds));
		t.push_back(ROW("Amplitude_Degrees", INI::parseReal, nullptr, d, m_amplitudeDegrees));
		finish(t);
	}
	return t.data();
}

const FieldParse *ViewShakeFXNugget::fieldParse() const
{
	static Table t;
	if (t.empty())
	{
		static ViewShakeFXNugget d;
		t.push_back(ROW("Type", INI::parseLookupList, ViewShakeTypes, d, m_type));
		finish(t);
	}
	return t.data();
}

const FieldParse *AttachedModelFXNugget::fieldParse() const
{
	static Table t;
	if (t.empty())
	{
		static AttachedModelFXNugget d;
		t.push_back(ROW("Modelname", INI::parseAsciiString, nullptr, d, m_modelName));
		t.push_back(ROW("RandomlyRotate", INI::parseBool, nullptr, d, m_randomlyRotate));
		t.push_back(ROW("ExpireTimer", INI::parseInt, nullptr, d, m_expireTimer));
		finish(t);
	}
	return t.data();
}

const FieldParse *TerrainScorchFXNugget::fieldParse() const
{
	static Table t;
	if (t.empty())
	{
		static TerrainScorchFXNugget d;
		t.push_back(ROW("Type", INI::parseLookupList, ScorchTypes, d, m_scorchType));
		t.push_back(ROW("Radius", INI::parseReal, nullptr, d, m_radius));
		t.push_back(ROW("RandomRange", INI::parseICoord2D, nullptr, d, m_randomRange));
		finish(t);
	}
	return t.data();
}

const FieldParse *ParticleSystemFXNugget::fieldParse() const
{
	static Table t;
	if (t.empty())
	{
		static ParticleSystemFXNugget d;
		t.push_back(ROW("Name", INI::parseAsciiString, nullptr, d, m_name));
		t.push_back(ROW("Count", INI::parseInt, nullptr, d, m_count));
		t.push_back(ROW("Offset", INI::parseCoord3D, nullptr, d, m_offset));
		t.push_back(RV_ROW("Radius", d, m_radius));
		t.push_back(RV_ROW("Height", d, m_height));
		t.push_back(RV_ROW("InitialDelay", d, m_initialDelay));
		t.push_back(ROW("RotateX", INI::parseAngleReal, nullptr, d, m_rotateX));
		t.push_back(ROW("RotateY", INI::parseAngleReal, nullptr, d, m_rotateY));
		t.push_back(ROW("RotateZ", INI::parseAngleReal, nullptr, d, m_rotateZ));
		t.push_back(ROW("OrientToObject", INI::parseBool, nullptr, d, m_orientToObject));
		t.push_back(ROW("AttachToObject", INI::parseBool, nullptr, d, m_attachToObject));
		t.push_back(ROW("AttachToBone", INI::parseAsciiString, nullptr, d, m_attachToBone));
		t.push_back(ROW("CreateAtGroundHeight", INI::parseBool, nullptr, d, m_createAtGroundHeight));
		t.push_back(ROW("Ricochet", INI::parseBool, nullptr, d, m_ricochet));
		t.push_back(ROW("CreateBoneOverride", INI::parseAsciiString, nullptr, d, m_createBoneOverride));
		t.push_back(ROW("TargetBoneOverride", INI::parseAsciiString, nullptr, d, m_targetBoneOverride));
		t.push_back(ROW("CreateBoneAtTarget", INI::parseBool, nullptr, d, m_createBoneAtTarget));
		t.push_back(ROW("TargetCoeff", INI::parseReal, nullptr, d, m_targetCoeff));
		t.push_back(ROW("SystemLife", INI::parseInt, nullptr, d, m_systemLife));
		t.push_back(ROW("UseTargetOffset", INI::parseBool, nullptr, d, m_useTargetOffset));
		t.push_back(ROW("SetTargetMatrix", INI::parseBool, nullptr, d, m_setTargetMatrix));
		t.push_back(ROW("OnlyIfOnLand", INI::parseBool, nullptr, d, m_onlyIfOnLand));
		t.push_back(ROW("OnlyIfOnWater", INI::parseBool, nullptr, d, m_onlyIfOnWater));
		t.push_back(ROW("TargetOffset", INI::parseCoord3D, nullptr, d, m_targetOffset));
		finish(t);
	}
	return t.data();
}

// ParticleSysBone (RW 0x5e1118) is a single-line nugget with a fixed token order; it has no field table.
const FieldParse *ParticleSysBoneFXNugget::fieldParse() const
{
	static const FieldParse t[1] = { { nullptr, nullptr, nullptr, 0 } };
	return t;
}

const FieldParse *FXListAtBonePosFXNugget::fieldParse() const
{
	static Table t;
	if (t.empty())
	{
		static FXListAtBonePosFXNugget d;
		t.push_back(ROW("FX", FXListStore::parseFXList, nullptr, d, m_fx));
		t.push_back(ROW("BoneName", INI::parseAsciiString, nullptr, d, m_boneName));
		finish(t);
	}
	return t.data();
}

const FieldParse *CursorParticleSystemFXNugget::fieldParse() const
{
	static Table t;
	if (t.empty())
	{
		static CursorParticleSystemFXNugget d;
		t.push_back(ROW("Anim2DTemplateName", INI::parseAsciiString, nullptr, d, m_anim2DTemplateName));
		t.push_back(ROW("BurstCount", INI::parseUnsignedInt, nullptr, d, m_burstCount));
		t.push_back(RV_ROW("ParticleLife", d, m_particleLife));
		t.push_back(RV_ROW("SystemLife", d, m_systemLife));
		t.push_back(RV_ROW("DriftVelX", d, m_driftVelX));
		t.push_back(RV_ROW("DriftVelY", d, m_driftVelY));
		finish(t);
	}
	return t.data();
}

const FieldParse *DynamicDecalFXNugget::fieldParse() const
{
	static Table t;
	if (t.empty())
	{
		static DynamicDecalFXNugget d;
		t.push_back(ROW("DecalName", INI::parseAsciiString, nullptr, d, m_decalName));
		t.push_back(ROW("Shader", INI::parseIndexList, DecalShaderNames, d, m_shader));
		t.push_back(ROW("Size", INI::parseReal, nullptr, d, m_size));
		t.push_back(ROW("Color", INI::parseRGBColor, nullptr, d, m_color));
		t.push_back(ROW("Offset", INI::parseCoord2D, nullptr, d, m_offset));
		t.push_back(ROW("OrientToObject", INI::parseBool, nullptr, d, m_orientToObject));
		t.push_back(ROW("OpacityStart", INI::parseUnsignedInt, nullptr, d, m_opacityStart));
		t.push_back(ROW("OpacityFadeTimeOne", INI::parseReal, nullptr, d, m_opacityFadeTimeOne));
		t.push_back(ROW("OpacityPeak", INI::parseUnsignedInt, nullptr, d, m_opacityPeak));
		t.push_back(ROW("OpacityPeakTime", INI::parseReal, nullptr, d, m_opacityPeakTime));
		t.push_back(ROW("OpacityFadeTimeTwo", INI::parseReal, nullptr, d, m_opacityFadeTimeTwo));
		t.push_back(ROW("OpacityEnd", INI::parseUnsignedInt, nullptr, d, m_opacityEnd));
		t.push_back(ROW("StartingDelay", INI::parseReal, nullptr, d, m_startingDelay));
		t.push_back(ROW("Lifetime", INI::parseReal, nullptr, d, m_lifetime));
		finish(t);
	}
	return t.data();
}

const FieldParse *LaserFXNugget::fieldParse() const
{
	static Table t;
	if (t.empty())
	{
		static LaserFXNugget d;
		t.push_back(ROW("LaserName", INI::parseAsciiString, nullptr, d, m_laserName));
		t.push_back(ROW("LaserBackwards", INI::parseBool, nullptr, d, m_laserBackwards));
		t.push_back(ROW("TargetPositionOffsetFallback", INI::parseCoord3D, nullptr, d, m_targetPositionOffsetFallback));
		finish(t);
	}
	return t.data();
}

const FieldParse *TintDrawableFXNugget::fieldParse() const
{
	static Table t;
	if (t.empty())
	{
		static TintDrawableFXNugget d;
		t.push_back(ROW("Color", parseTintColor, nullptr, d, m_color));
		t.push_back(ROW("PreColorTime", INI::parseUnsignedInt, nullptr, d, m_preColorTime));
		t.push_back(ROW("PostColorTime", INI::parseUnsignedInt, nullptr, d, m_postColorTime));
		t.push_back(ROW("SustainedColorTime", INI::parseUnsignedInt, nullptr, d, m_sustainedColorTime));
		t.push_back(ROW("Frequency", INI::parseReal, nullptr, d, m_frequency));
		t.push_back(ROW("Amplitude", INI::parseReal, nullptr, d, m_amplitude));
		finish(t);
	}
	return t.data();
}

const FieldParse *BuffFXNugget::fieldParse() const
{
	static Table t;
	if (t.empty())
	{
		static BuffFXNugget d;
		t.push_back(ROW("BuffType", INI::parseIndexList, BuffTypeNames, d, m_buffType));
		t.push_back(ROW("IsComplexBuff", INI::parseBool, nullptr, d, m_isComplexBuff));
		t.push_back(ROW("BuffLifeTime", INI::parseDurationUnsignedInt, nullptr, d, m_buffLifeTime));
		t.push_back(ROW("BuffThingTemplate", INI::parseAsciiString, nullptr, d, m_buffThingTemplate));
		t.push_back(ROW("BuffOrcTemplate", INI::parseAsciiString, nullptr, d, m_buffOrcTemplate));
		t.push_back(ROW("BuffInfantryTemplate", INI::parseAsciiString, nullptr, d, m_buffInfantryTemplate));
		t.push_back(ROW("BuffCavalryTemplate", INI::parseAsciiString, nullptr, d, m_buffCavalryTemplate));
		t.push_back(ROW("BuffTrollTemplate", INI::parseAsciiString, nullptr, d, m_buffTrollTemplate));
		t.push_back(ROW("BuffMumakilTemplate", INI::parseAsciiString, nullptr, d, m_buffMumakilTemplate));
		t.push_back(ROW("BuffShipTemplate", INI::parseAsciiString, nullptr, d, m_buffShipTemplate));
		t.push_back(ROW("BuffMonsterTemplate", INI::parseAsciiString, nullptr, d, m_buffMonsterTemplate));
		t.push_back(ROW("Extrusion", INI::parseReal, nullptr, d, m_extrusion));
		t.push_back(ROW("Color", INI::parseRGBColor, nullptr, d, m_color));
		finish(t);
	}
	return t.data();
}


// ---- FXList-level table (RW 0xbf2898) ---------------------------------------------------------------------------
struct FXListParseAccess
{
	// RW 0x5e1118. Fixed token order: bone, system name, then (third token) FollowBone, then (next) FXTrigger. A token that is not the
	// expected keyword is consumed and dropped (RW calls getNextTokenOrNull and only compares).
	static void parseParticleSysBone(INI *ini, void *instance, void *, const void *)
	{
		FXList *fx = (FXList *)instance;
		std::unique_ptr<ParticleSysBoneFXNugget> n = std::make_unique<ParticleSysBoneFXNugget>();
		n->m_boneName = AsciiStringUtil::lowered(ini->getNextAsciiString());
		const char *ps = ini->getNextTokenOrNull(ini->getSepsColon());
		if (!ps)
		{
			// RW passes NULL to strncpy here (a crash): rule 10, an error that reaches the report
			throw INIException(3, "Expected additional data after '%s'", ini->getSepsColon());
		}
		n->m_particleSystemName = std::string(ps).substr(0, 0x3f);
		const char *tok = ini->getNextTokenOrNull(ini->getSepsColon());
		if (tok && std::strcmp(tok, "FollowBone") == 0)
		{
			n->m_followBone = ini->scanBool(ini->getNextToken(ini->getSepsColon()));
		}
		tok = ini->getNextTokenOrNull(ini->getSepsColon());
		if (tok && std::strcmp(tok, "FXTrigger") == 0)
		{
			n->m_fxTrigger = INI::scanIndexList(ini->getNextToken(ini->getSepsColon()), FXTriggerNames);
		}
		fx->addNugget(std::move(n));
		fx->m_hasParticleSysBone = true; // RW 0x5e1247
	}

	// RW 0x5df46b: CullingInfo = TrackingSeconds:<real> StartCullingAbove:<int> CullAllAbove:<int>
	static void parseCullingInfo(INI *ini, void *instance, void *, const void *)
	{
		FXList *fx = (FXList *)instance;
		for (const char *key = ini->getNextTokenOrNull(ini->getSepsColon()); key; key = ini->getNextTokenOrNull(ini->getSepsColon()))
		{
			if (AsciiStringUtil::compareNoCase(key, "TrackingSeconds") == 0)
			{
				const float seconds = ini->scanReal(ini->getNextToken(ini->getSepsColon()));
				fx->m_cullWindowFrames = (int)(seconds * 5.0f); // fimul LOGICFRAMES_PER_SECOND (RW 0xd9f608), _ftol
			}
			else if (AsciiStringUtil::compareNoCase(key, "StartCullingAbove") == 0)
			{
				fx->m_startCullingAbove = (int)ini->scanUnsignedInt(ini->getNextToken(ini->getSepsColon()));
			}
			else if (AsciiStringUtil::compareNoCase(key, "CullAllAbove") == 0)
			{
				fx->m_cullAllAbove = (int)ini->scanUnsignedInt(ini->getNextToken(ini->getSepsColon()));
			}
			else
			{
				throw INIException(3, "bad colon spacing, or unexpected token in FXList::parseCullingInfo");
			}
		}
		if (fx->m_cullAllAbove == 0)
		{
			throw INIException(3, "m_cullTrackingMax == 0 in FXList::parseCullingInfo");
		}
		if (fx->m_startCullingAbove == 0)
		{
			throw INIException(3, "m_cullTrackingMin == 0 in FXList::parseCullingInfo");
		}
		if ((unsigned)fx->m_cullAllAbove <= (unsigned)fx->m_startCullingAbove)
		{
			fx->m_cullAllAbove = fx->m_startCullingAbove + 1;
		}
	}

	static const FieldParse *table()
	{
		static Table t;
		if (t.empty())
		{
			static FXList d("");
			t.push_back(FieldParse{ "Sound", parseNugget<SoundFXNugget>, nullptr, 0 });
			t.push_back(FieldParse{ "EvaEvent", parseNugget<EvaEventFXNugget>, nullptr, 0 });
			t.push_back(FieldParse{ "RayEffect", parseNugget<RayEffectFXNugget>, nullptr, 0 });
			t.push_back(FieldParse{ "LightPulse", parseNugget<LightPulseFXNugget>, nullptr, 0 });
			t.push_back(FieldParse{ "CameraShakerVolume", parseNugget<CameraShakerVolumeFXNugget>, nullptr, 0 });
			t.push_back(FieldParse{ "ViewShake", parseNugget<ViewShakeFXNugget>, nullptr, 0 });
			t.push_back(FieldParse{ "AttachedModel", parseNugget<AttachedModelFXNugget>, nullptr, 0 });
			t.push_back(FieldParse{ "TerrainScorch", parseNugget<TerrainScorchFXNugget>, nullptr, 0 });
			t.push_back(FieldParse{ "ParticleSystem", parseNugget<ParticleSystemFXNugget>, nullptr, 0 });
			t.push_back(FieldParse{ "ParticleSysBone", parseParticleSysBone, nullptr, 0 });
			t.push_back(FieldParse{ "FXListAtBonePos", parseNugget<FXListAtBonePosFXNugget>, nullptr, 0 });
			t.push_back(FieldParse{ "CursorParticleSystem", parseNugget<CursorParticleSystemFXNugget>, nullptr, 0 });
			t.push_back(FieldParse{ "DynamicDecal", parseNugget<DynamicDecalFXNugget>, nullptr, 0 });
			t.push_back(FieldParse{ "Laser", parseNugget<LaserFXNugget>, nullptr, 0 });
			t.push_back(FieldParse{ "CullingInfo", parseCullingInfo, nullptr, 0 });
			t.push_back(FieldParse{ "TintDrawable", parseNugget<TintDrawableFXNugget>, nullptr, 0 });
			t.push_back(FieldParse{ "BuffNugget", parseNugget<BuffFXNugget>, nullptr, 0 });
			t.push_back(ROW("PlayEvenIfShrouded", INI::parseBool, nullptr, d, m_playEvenIfShrouded));
			finish(t);
		}
		return t.data();
	}
};

const FieldParse *FXList::fieldParse()
{
	return FXListParseAccess::table();
}

// ---- store ------------------------------------------------------------------------------------------------------
thread_local FXListStore *TheFXListStore = nullptr; // SMOOTH-1: per thread (the world context of each thread, RetailObjectWorld::ContextScope)

const FXList *FXListStore::findFXList(const std::string &name) const
{
	if (AsciiStringUtil::compareNoCase(name, "None") == 0)
	{
		return nullptr; // RW 0x5e20a2
	}
	auto it = m_lists.find(name);
	return it == m_lists.end() ? nullptr : it->second.get();
}

const FXList *FXListStore::parseFXListRef(const std::string &token) const
{
	const FXList *fx = findFXList(token);
	if (!fx && AsciiStringUtil::compareNoCase(token, "none") != 0)
	{
		throw INIException(3, "iniParseFXList -- FXList %s not found! Either add the FXList or remove the reference to it.", token.c_str()); // RW 0xc24ed8
	}
	return fx;
}

void FXListStore::parseFXList(INI *ini, void *, void *store, const void *)
{
	const std::string token = ini->getNextToken(); // code 3 when absent
	if (!TheFXListStore)
	{
		throw INIException(3, "TheFXListStore==NULL");
	}
	*(const FXList **)store = TheFXListStore->parseFXListRef(token);
}

void FXListStore::parseDefinition(INI *ini)
{
	const std::string name = ini->getNextToken();
	auto it = m_lists.find(name);
	if (it != m_lists.end() && ini->getLoadType() == INI_LOAD_RELOAD)
	{
		// RW 0x5e2518: only load type 5 erases the entry and flags the old list superseded (kept alive: callers may hold it)
		it->second->m_superseded = true;
		m_superseded.push_back(std::move(it->second));
		m_lists.erase(it);
		it = m_lists.end();
	}
	std::unique_ptr<FXList> fx = std::make_unique<FXList>(name);
	FXList *raw = fx.get();
	if (it != m_lists.end())
	{
		// every other load type: the new list replaces the entry, the old object is leaked in RW (still playable through held pointers)
		m_superseded.push_back(std::move(it->second));
		it->second = std::move(fx);
	}
	else
	{
		m_lists[name] = std::move(fx);
		m_order.push_back(name);
	}
	ini->initFromINI(raw, FXList::fieldParse());
}

std::vector<std::string> FXListStore::unverified() const
{
	std::vector<std::string> out;
	if (m_unvalidatedSounds)
	{
		out.push_back("S-190: " + std::to_string(m_unvalidatedSounds) + " Sound nugget names were not validated against the audio event registry (RW 0x73b217 throws `Invalid Sound` at parse time; the audio lane is not ported)");
	}
	if (m_unvalidatedEva)
	{
		out.push_back("S-190: " + std::to_string(m_unvalidatedEva) + " EvaEvent names were not validated against the Eva registry (RW 0x5de588; the Eva lane is not ported)");
	}
	return out;
}

void FXListStore::clear()
{
	m_lists.clear();
	m_superseded.clear();
	m_order.clear();
	m_unvalidatedSounds = m_unvalidatedEva = 0;
}

void ParseFXListDefinitionGlobal(INI *ini)
{
	if (!TheFXListStore)
	{
		throw INIException(3, "TheFXListStore==NULL");
	}
	TheFXListStore->parseDefinition(ini);
}

// ---- runtime ----------------------------------------------------------------------------------------------------
// RW 0x5df671
bool FXNugget::passesFilters(const FXObject *primary, const FXObject *secondary, const FXServices &services) const
{
	if (primary)
	{
		if (m_sourceObjectFilter && !primary->passesObjectFilter(*m_sourceObjectFilter))
		{
			return false;
		}
		const ModelConditionFlags flags = primary->modelConditions();
		// RW 0x5df56a: for every word (obj & excluded) == 0 && (obj & required) == required
		if (flags.anyIntersectionWith(m_excludedSourceModelConditions) || !flags.testForAll(m_requiredSourceModelConditions))
		{
			return false;
		}
		if (primary->hasDrawable() && primary->drawableState() == 5)
		{
			return false;
		}
	}
	if (secondary)
	{
		if (m_objectFilter && !secondary->passesObjectFilter(*m_objectFilter))
		{
			return false;
		}
		const ModelConditionFlags flags = secondary->modelConditions();
		if (flags.anyIntersectionWith(m_excludedSecondaryModelConditions) || !flags.testForAll(m_requiredSecondaryModelConditions))
		{
			return false;
		}
	}
	return m_weather == 2 || m_weather == services.weather();
}

void FXNugget::doFXPos(FXServices &, const Coord3D *, const Matrix3D *, float, const Coord3D *) const
{
	throw std::logic_error("FXNugget::doFXPos: pure virtual function called (RW 0x43b160)");
}

// RW 0x5df633
void FXNugget::doFXObj(FXServices &s, const FXObject *primary, const FXObject *secondary) const
{
	Coord3D p, sp;
	Matrix3D m;
	if (primary)
	{
		p = primary->position();
		m = primary->transform();
	}
	if (secondary)
	{
		sp = secondary->position();
	}
	doFXPos(s, primary ? &p : nullptr, primary ? &m : nullptr, 0.0f, secondary ? &sp : nullptr);
}

// Sound (RW 0x5df7f3 / 0x5df85e): plays even when pos == NULL
void SoundFXNugget::doFXPos(FXServices &s, const Coord3D *pos, const Matrix3D *, float, const Coord3D *) const
{
	s.playSound(m_soundName, pos, -1);
}

void SoundFXNugget::doFXObj(FXServices &s, const FXObject *primary, const FXObject *) const
{
	if (primary)
	{
		const Coord3D p = primary->position();
		s.playSound(m_soundName, &p, primary->controllingPlayerIndex());
	}
	else
	{
		s.playSound(m_soundName, nullptr, -1);
	}
}

// EvaEvent (RW 0x5df73c)
void EvaEventFXNugget::doFXObj(FXServices &s, const FXObject *primary, const FXObject *) const
{
	if (!primary)
	{
		return;
	}
	const std::string *event = nullptr;
	if (primary->isControlledByLocalPlayer())
	{
		event = &m_owner;
	}
	else if (primary->relationshipToLocalPlayer() == 2)
	{
		event = &m_ally;
	}
	else
	{
		event = &m_enemy;
	}
	if (!event->empty())
	{
		s.playEva(*event);
	}
}

// RayEffect (RW 0x5df95d)
void RayEffectFXNugget::doFXPos(FXServices &s, const Coord3D *pos, const Matrix3D *, float, const Coord3D *secondary) const
{
	if (pos && secondary && !m_templateName.empty())
	{
		const Coord3D a{ pos->x + m_primaryOffset.x, pos->y + m_primaryOffset.y, pos->z + m_primaryOffset.z };
		const Coord3D b{ secondary->x + m_secondaryOffset.x, secondary->y + m_secondaryOffset.y, secondary->z + m_secondaryOffset.z };
		s.createRayEffect(a, b, m_templateName);
	}
}

// LightPulse (RW 0x5dfaa3 / 0x5dfae4)
void LightPulseFXNugget::doFXPos(FXServices &s, const Coord3D *pos, const Matrix3D *, float, const Coord3D *) const
{
	if (pos)
	{
		s.lightPulse(*pos, m_color, m_radius, m_increaseFrames, m_decreaseFrames);
	}
}

void LightPulseFXNugget::doFXObj(FXServices &s, const FXObject *primary, const FXObject *) const
{
	if (!primary)
	{
		return;
	}
	float r = m_radius;
	if (m_boundingCirclePct > 0.0f)
	{
		r = primary->boundingCircleRadius() * m_boundingCirclePct;
	}
	const Coord3D p = primary->position();
	s.lightPulse(p, m_color, r, m_increaseFrames, m_decreaseFrames);
}

// CameraShakerVolume (RW 0x5dfe90 / 0x5dfece; both slots go through the position)
void CameraShakerVolumeFXNugget::doFXPos(FXServices &s, const Coord3D *pos, const Matrix3D *, float, const Coord3D *) const
{
	if (pos)
	{
		s.cameraShaker(*pos, m_radius, m_durationSeconds, m_amplitudeDegrees);
	}
}

// ViewShake (RW 0x5dff32)
void ViewShakeFXNugget::doFXPos(FXServices &s, const Coord3D *pos, const Matrix3D *, float, const Coord3D *) const
{
	if (pos)
	{
		s.viewShake(*pos, m_type);
	}
}

// AttachedModel (RW 0x5dffa8)
void AttachedModelFXNugget::doFXObj(FXServices &s, const FXObject *primary, const FXObject *) const
{
	if (primary && primary->hasDrawable())
	{
		s.attachModel(*primary, m_modelName, m_randomlyRotate, m_expireTimer);
	}
}

// TerrainScorch (RW 0x5e0045)
void TerrainScorchFXNugget::doFXPos(FXServices &s, const Coord3D *pos, const Matrix3D *, float, const Coord3D *) const
{
	if (pos)
	{
		int type = m_scorchType;
		if (type < 0)
		{
			type = s.clientRandom().value(m_randomRange.x, m_randomRange.y); // GameClientRandomValue, RW 0x6d32e4
		}
		s.addScorch(*pos, m_radius, type);
	}
}

// ParticleSystem (RW 0x5e1256 / 0x5e1d64 / 0x5e1270)
void ParticleSystemFXNugget::doFXPos(FXServices &s, const Coord3D *pos, const Matrix3D *mtx, float, const Coord3D *) const
{
	if (pos)
	{
		spawn(s, *pos, mtx, nullptr, nullptr);
	}
}

void ParticleSystemFXNugget::doFXObj(FXServices &s, const FXObject *primary, const FXObject *secondary) const
{
	if (!primary)
	{
		return;
	}
	const Coord3D p = primary->position();
	if (m_ricochet && secondary)
	{
		const Coord3D sp = secondary->position();
		const float a = std::atan2(p.y - sp.y, p.x - sp.x);
		Matrix3D r;
		r.Row[0][0] = std::cos(a);
		r.Row[0][1] = -std::sin(a);
		r.Row[1][0] = std::sin(a);
		r.Row[1][1] = std::cos(a);
		spawn(s, p, &r, primary, secondary);
	}
	else
	{
		const Matrix3D m = primary->transform();
		spawn(s, p, &m, primary, secondary);
	}
}

void ParticleSystemFXNugget::spawn(FXServices &s, const Coord3D &pos, const Matrix3D *mtx, const FXObject *primary, const FXObject *secondary) const
{
	Vector3 off(m_offset.x, m_offset.y, m_offset.z);
	if (mtx)
	{
		off = mtx->Rotate_Vector(off); // RW 0x5e07f7: rotation part only
	}
	if (!s.particleSystemTemplateExists(m_name))
	{
		return; // RW: a missing template is a silent no-op (14 retail names are undefined)
	}
	for (int i = 0; i < m_count; ++i)
	{
		std::unique_ptr<FXParticleSystemRef> sys = s.createParticleSystem(m_name);
		if (!sys)
		{
			continue;
		}
		Coord3D p;
		const float radius = m_radius.getValue(s.clientRandom());                // RNG draw 1
		const float angle = s.clientRandom().real(0.0f, kTwoPi);                  // RNG draw 2: always drawn
		bool usedOffsetPath = true;
		if (!m_createBoneOverride.empty() && primary)
		{
			const FXObject *obj = (m_createBoneAtTarget && secondary) ? secondary : primary;
			Matrix3D bone; // identity; the result code is ignored
			s.boneWorldMatrix(*obj, m_createBoneOverride, bone);
			p = Coord3D{ bone.Row[0][3], bone.Row[1][3], bone.Row[2][3] };
			usedOffsetPath = false;
		}
		else
		{
			p.x = std::cos(angle) * radius + pos.x + off.X;
			p.y = std::sin(angle) * radius + pos.y + off.Y;
		}
		if (m_createAtGroundHeight)
		{
			if (usedOffsetPath)
			{
				p.z = s.groundHeight(p.x, p.y, pos);
			}
		}
		else if (usedOffsetPath)
		{
			p.z = m_height.getValue(s.clientRandom()) + pos.z + off.Z;            // RNG draw 3
		}
		if (m_orientToObject && mtx)
		{
			Matrix3D m = *mtx;
			if (m_setTargetMatrix && secondary)
			{
				const Coord3D sp = secondary->position();
				const float dx = sp.x - p.x, dy = sp.y - p.y, dz = sp.z - p.z;
				const float dist = std::sqrt(dx * dx + dy * dy);
				const float t = kPi - std::atan2(dz + 10.0f, dist);
				const float c = std::cos(t), sn = std::sin(t);
				for (int r = 0; r < 3; ++r)
				{
					const float a = m.Row[r][0], c2 = m.Row[r][2];
					m.Row[r][0] = a * c - c2 * sn;
					m.Row[r][2] = a * sn + c2 * c;
				}
			}
			sys->setLocalTransform(m);
		}
		if (m_rotateX != 0.0f)
		{
			sys->rotateLocalX(m_rotateX);
		}
		if (m_rotateY != 0.0f)
		{
			sys->rotateLocalY(m_rotateY);
		}
		if (m_rotateZ != 0.0f)
		{
			sys->rotateLocalZ(m_rotateZ);
		}
		if (m_onlyIfOnLand || m_onlyIfOnWater)
		{
			const bool water = s.isWater(p.x, p.y);
			if (m_onlyIfOnLand ? water : !water)
			{
				sys->destroy();
				continue;
			}
		}
		if (m_attachToObject && primary)
		{
			sys->attachToObject(primary->objectId());
		}
		else
		{
			sys->setPosition(p);
		}
		if (!m_attachToBone.empty())
		{
			sys->attachToBone(m_attachToBone);
		}
		if (!m_targetBoneOverride.empty() && secondary)
		{
			Matrix3D m2;
			const bool ok = s.boneWorldMatrix(*secondary, m_targetBoneOverride, m2);
			sys->setTarget(ok ? Coord3D{ m2.Row[0][3], m2.Row[1][3], m2.Row[2][3] } : secondary->position());
		}
		if (m_useTargetOffset)
		{
			sys->setTarget(Coord3D{ m_targetOffset.x + p.x, m_targetOffset.y + p.y, m_targetOffset.z + p.z });
		}
		if (m_systemLife >= 0)
		{
			sys->setSystemLife(m_systemLife);
		}
		const float delay = m_initialDelay.getValue(s.clientRandom());            // RNG draw 4
		if (delay >= 0.0f)
		{
			sys->setInitialDelayFrames((int)std::ceil(delay * 0.005f));          // RW 0xd9f610, ceil 0xbd0588
		}
	}
}

// FXListAtBonePos (RW 0x5e261d / 0x5e2640)
void FXListAtBonePosFXNugget::doFXObj(FXServices &s, const FXObject *primary, const FXObject *) const
{
	if (!primary)
	{
		return;
	}
	for (int start = 0; start < 2; ++start)
	{
		const std::vector<FXBoneTransform> bones = s.boneWorldTransforms(*primary, m_boneName, start, 40);
		for (const FXBoneTransform &b : bones)
		{
			// RW 0x5e2721: the cull test then FXList::doFXPos member (no shroud/obj filters beyond the member's own)
			if (m_fx && !m_fx->shouldCull(s))
			{
				if (TheFXListStore)
				{
					m_fx->doFXPos(s, *TheFXListStore, &b.position, &b.transform, 0.0f, nullptr);
				}
			}
		}
	}
}

// CursorParticleSystem (RW 0x5e0441 / 0x5e0487): both slots ignore the position
void CursorParticleSystemFXNugget::doFXPos(FXServices &s, const Coord3D *, const Matrix3D *, float, const Coord3D *) const
{
	s.cursorParticles(m_anim2DTemplateName, m_burstCount, m_particleLife, m_systemLife, m_driftVelX, m_driftVelY);
}

void CursorParticleSystemFXNugget::doFXObj(FXServices &s, const FXObject *, const FXObject *) const
{
	s.cursorParticles(m_anim2DTemplateName, m_burstCount, m_particleLife, m_systemLife, m_driftVelX, m_driftVelY);
}

// DynamicDecal (RW 0x5e05ab / 0x5dfbec)
void DynamicDecalFXNugget::doFXPos(FXServices &s, const Coord3D *pos, const Matrix3D *mtx, float, const Coord3D *) const
{
	if (!pos)
	{
		return;
	}
	FXDecalDesc d;
	d.name = m_decalName;
	d.shader = m_shader;
	d.size = m_size;
	Vector3 off(m_offset.x, m_offset.y, 0.0f);
	if (mtx && m_orientToObject)
	{
		off = mtx->Rotate_Vector(off); // RW 0x5e07f7
	}
	d.position.x = pos->x + off.X;
	d.position.y = pos->y + off.Y;
	d.position.z = s.groundHeight(d.position.x, d.position.y, *pos);
	d.yaw = (mtx && m_orientToObject) ? std::atan2(mtx->Row[1][0], mtx->Row[0][0]) : 0.0f; // RW 0xb261f0 yawFromMatrix
	d.color = ((std::uint32_t)(std::uint8_t)(m_color.red * 255.0f) << 16) | ((std::uint32_t)(std::uint8_t)(m_color.green * 255.0f) << 8) | (std::uint32_t)(std::uint8_t)(m_color.blue * 255.0f);
	d.opacityStart = m_opacityStart;
	d.opacityPeak = m_opacityPeak;
	d.opacityEnd = m_opacityEnd;
	d.startDelayFrames = (int)(m_startingDelay * 0.03f); // client frames: trunc(ms * 0.03) (RW 0xd9f624)
	d.lifetimeFrames = (int)(m_lifetime * 0.03f);
	d.fadeOneFrames = (int)(m_opacityFadeTimeOne * 0.03f);
	d.peakFrames = (int)(m_opacityPeakTime * 0.03f);
	d.fadeTwoFrames = (int)(m_opacityFadeTimeTwo * 0.03f);
	s.createDecal(d);
}

void DynamicDecalFXNugget::doFXObj(FXServices &s, const FXObject *primary, const FXObject *) const
{
	if (primary)
	{
		const Coord3D p = primary->position();
		const Matrix3D m = primary->transform();
		doFXPos(s, &p, &m, 0.0f, nullptr);
	}
}

// Laser (RW 0x5dfc2e / 0x5dfd29)
void LaserFXNugget::doFXPos(FXServices &s, const Coord3D *pos, const Matrix3D *, float, const Coord3D *secondary) const
{
	if (!pos)
	{
		return;
	}
	const Coord3D b = secondary ? *secondary : Coord3D{ pos->x + m_targetPositionOffsetFallback.x, pos->y + m_targetPositionOffsetFallback.y, pos->z + m_targetPositionOffsetFallback.z };
	s.laser(m_laserName, pos, &b, m_laserBackwards, nullptr, nullptr);
}

void LaserFXNugget::doFXObj(FXServices &s, const FXObject *primary, const FXObject *secondary) const
{
	if (!primary)
	{
		return;
	}
	if (secondary)
	{
		s.laser(m_laserName, nullptr, nullptr, m_laserBackwards, primary, secondary);
		return;
	}
	const Coord3D p = primary->position();
	doFXPos(s, &p, nullptr, 0.0f, nullptr);
}

// TintDrawable (RW 0x5e0108)
void TintDrawableFXNugget::doFXObj(FXServices &s, const FXObject *primary, const FXObject *) const
{
	if (primary && primary->hasDrawable())
	{
		s.tintDrawable(*primary, m_color, m_preColorTime, m_postColorTime, m_sustainedColorTime, m_frequency, m_amplitude);
	}
}

// BuffNugget (RW 0x5e2988)
void BuffFXNugget::doFXObj(FXServices &s, const FXObject *primary, const FXObject *) const
{
	if (!primary || !primary->hasDrawable())
	{
		return;
	}
	const int life = (int)m_buffLifeTime;
	if (life <= 0)
	{
		s.removeBuff(*primary, m_buffType);
		return;
	}
	if (m_isComplexBuff)
	{
		s.addBuff(*primary, m_buffType, nullptr, life, m_color, m_extrusion);
		return;
	}
	if (primary->isHorde())
	{
		return;
	}
	const std::string *name = &m_buffThingTemplate;
	if (primary->hasKindOf("CAVALRY"))
	{
		name = &m_buffCavalryTemplate;
	}
	else if (primary->hasKindOf("ORC"))
	{
		name = &m_buffOrcTemplate;
	}
	else if (primary->hasKindOf("INFANTRY"))
	{
		name = &m_buffInfantryTemplate;
	}
	else if (primary->hasKindOf("TROLL_BUFF_NUGGET"))
	{
		name = &m_buffTrollTemplate;
	}
	else if (primary->hasKindOf("MUMAKIL_BUFF_NUGGET"))
	{
		name = &m_buffMumakilTemplate;
	}
	else if (primary->hasKindOf("SHIP"))
	{
		name = &m_buffShipTemplate;
	}
	else if (primary->hasKindOf("MONSTER"))
	{
		name = &m_buffMonsterTemplate;
	}
	s.addBuff(*primary, m_buffType, name, life, m_color, m_extrusion);
}

// ---- FXList dispatch --------------------------------------------------------------------------------------------
namespace
{
const FXList *resolveSuperseded(const FXList *fx, const FXListStore &store)
{
	// RW 0x5e21d8: while (fx->+0x24) { n = findFXList(fx->name); if (!n) break; fx = n; }
	while (fx->superseded())
	{
		const FXList *n = store.findFXList(fx->name());
		if (!n)
		{
			break;
		}
		fx = n;
	}
	return fx;
}
}

void FXList::doFXPos(FXServices &s, const FXListStore &store, const Coord3D *pos, const Matrix3D *mtx, float speed, const Coord3D *secondary) const
{
	const FXList *fx = resolveSuperseded(this, store);
	if (!fx->m_playEvenIfShrouded && pos)
	{
		if (s.shroudStatusAt(*pos) != FX_SHROUD_CLEAR)
		{
			return;
		}
	}
	for (const std::unique_ptr<FXNugget> &n : fx->m_nuggets)
	{
		if (n->passesFilters(nullptr, nullptr, s))
		{
			n->doFXPos(s, pos, mtx, speed, secondary);
			if (n->m_stopIfNuggetPlayed)
			{
				break;
			}
		}
	}
}

void FXList::doFXObj(FXServices &s, const FXListStore &store, const FXObject *primary, const FXObject *secondary) const
{
	const FXList *fx = resolveSuperseded(this, store);
	if (!fx->m_playEvenIfShrouded && primary)
	{
		if (s.objectShroudedForLocalPlayer(*primary))
		{
			return;
		}
	}
	for (const std::unique_ptr<FXNugget> &n : fx->m_nuggets)
	{
		if (n->passesFilters(primary, secondary, s))
		{
			n->doFXObj(s, primary, secondary);
			if (n->m_stopIfNuggetPlayed)
			{
				break;
			}
		}
	}
}

// RW 0x5e275b
bool FXList::shouldCull(FXServices &s) const
{
	if (m_cullWindowFrames == 0)
	{
		return false;
	}
	const std::uint32_t now = s.logicFrame();
	const std::uint32_t oldest = now - (std::uint32_t)m_cullWindowFrames;
	while (!m_cullStamps.empty() && m_cullStamps.front() < oldest)
	{
		m_cullStamps.pop_front();
	}
	const int n = (int)m_cullStamps.size();
	if (n > m_cullAllAbove)
	{
		return true;
	}
	if (n > m_startCullingAbove)
	{
		const int q = (n - m_startCullingAbove) / (m_cullAllAbove - m_startCullingAbove);
		if (s.clientRandom().value(0, q) == 0)
		{
			return true;
		}
	}
	m_cullStamps.push_back(now);
	return false;
}

void FXList::doFXPos(const FXList *fx, FXServices &s, const FXListStore &store, const Coord3D *pos, const Matrix3D *mtx, float speed, const Coord3D *secondary)
{
	if (fx && !fx->shouldCull(s))
	{
		fx->doFXPos(s, store, pos, mtx, speed, secondary);
	}
}

void FXList::doFXObj(const FXList *fx, FXServices &s, const FXListStore &store, const FXObject *primary, const FXObject *secondary)
{
	if (fx && !fx->shouldCull(s))
	{
		fx->doFXObj(s, store, primary, secondary);
	}
}
