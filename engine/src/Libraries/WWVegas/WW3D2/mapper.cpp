// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// See mapper.h for the sources.

#include "Libraries/WWVegas/WW3D2/mapper.h"

#include <cctype>
#include <cmath>
#include <cstdlib>

namespace
{
const float kPi = 3.141592654f; // WWMATH_PI

std::string lowerCopy(std::string s)
{
	for (char &c : s)
	{
		c = (char)std::tolower((unsigned char)c);
	}
	return s;
}

std::string trim(const std::string &s)
{
	size_t b = 0, e = s.size();
	while (b < e && std::isspace((unsigned char)s[b])) ++b;
	while (e > b && std::isspace((unsigned char)s[e - 1])) --e;
	return s.substr(b, e - b);
}

float floorF(float v) { return std::floor(v); }
float clampF(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

struct Vec2
{
	float X = 0.0f, Y = 0.0f;
};

// ---- ZH ScaleTextureMapperClass (mapper.cpp:60-110); BFME2 RVA 0x00182270 -------------------------------------------
class ScaleMapper : public TextureMapperClass
{
public:
	ScaleMapper(const MapperArgs &args, unsigned stage) : TextureMapperClass(stage)
	{
		Scale.X = args.Get_Float("UScale", 1.0f);
		Scale.Y = args.Get_Float("VScale", 1.0f);
	}
	int Get_Type() const override { return W3D_MAPPING_SCALE; }
	void Calculate_Texture_Matrix(W3DTexMatrix &m, std::uint32_t) override
	{
		m.Make_Identity();
		m.M[0][0] = Scale.X;
		m.M[1][1] = Scale.Y;
	}

protected:
	Vec2 Scale;
};

// ---- LinearOffsetTextureMapperClass (mapper.cpp:125-195); BFME2 RVA 0x001822E0 ---------------------------------------
class LinearOffsetMapper : public ScaleMapper
{
public:
	LinearOffsetMapper(const MapperArgs &args, unsigned stage, std::uint32_t now) : ScaleMapper(args, stage), LastUsedSyncTime(now)
	{
		float u_offset_per_sec = args.Get_Float("UPerSec", 0.0f);
		float v_offset_per_sec = args.Get_Float("VPerSec", 0.0f);
		// "legacy from the API we used before": the per-second speed is negated, artists have worked around it.
		UVOffsetDeltaPerMS.X = u_offset_per_sec * -0.001f;
		UVOffsetDeltaPerMS.Y = v_offset_per_sec * -0.001f;
		StartingUVOffset.X = args.Get_Float("UOffset", 0.0f);
		StartingUVOffset.Y = args.Get_Float("VOffset", 0.0f);
		CurrentUVOffset = StartingUVOffset;
		ClampFix = args.Get_Bool("ClampFix", false);
	}
	int Get_Type() const override { return W3D_MAPPING_LINEAR_OFFSET; }
	bool Is_Time_Variant() const override { return true; }
	void Reset(std::uint32_t now) override
	{
		CurrentUVOffset = Vec2(); // ZH Set_Current_UV_Offset(0, 0)
		LastUsedSyncTime = now;
	}
	void Calculate_Texture_Matrix(W3DTexMatrix &m, std::uint32_t now) override
	{
		std::uint32_t delta = now - LastUsedSyncTime;
		float del = (float)delta;
		float offset_u = CurrentUVOffset.X + UVOffsetDeltaPerMS.X * del;
		float offset_v = CurrentUVOffset.Y + UVOffsetDeltaPerMS.Y * del;
		if (!ClampFix)
		{
			offset_u = offset_u - floorF(offset_u);
			offset_v = offset_v - floorF(offset_v);
		}
		else
		{
			offset_u = clampF(offset_u, -Scale.X, Scale.X);
			offset_v = clampF(offset_v, -Scale.Y, Scale.Y);
		}
		m.Make_Identity();
		m.M[0][2] = offset_u;
		m.M[0][0] = Scale.X;
		m.M[1][2] = offset_v;
		m.M[1][1] = Scale.Y;
		CurrentUVOffset.X = offset_u;
		CurrentUVOffset.Y = offset_v;
		LastUsedSyncTime = now;
	}

protected:
	Vec2 UVOffsetDeltaPerMS, StartingUVOffset, CurrentUVOffset;
	bool ClampFix = false;
	std::uint32_t LastUsedSyncTime;
};

// ---- GridTextureMapperClass (BFME2 matched initialize / update_temporal_state / calculate_uv_offset) -------------------
class GridMapper : public TextureMapperClass
{
public:
	GridMapper(const MapperArgs &args, unsigned stage, std::uint32_t now) : TextureMapperClass(stage)
	{
		float fps = args.Get_Float("FPS", 1.0f);
		unsigned gridwidth_log2 = (unsigned)args.Get_Int("Log2Width", 1);
		LastFrame = (unsigned)args.Get_Int("Last", 0);
		Offset = (unsigned)args.Get_Int("Offset", 0);
		initialize(fps, gridwidth_log2, now);
	}
	int Get_Type() const override { return W3D_MAPPING_GRID; }
	bool Is_Time_Variant() const override { return true; }
	void Reset(std::uint32_t now) override
	{
		Remainder = 0;
		CurrentFrame = Sign >= 0 ? Offset : (LastFrame - 1) - Offset;
		LastUsedSyncTime = now;
	}
	void Calculate_Texture_Matrix(W3DTexMatrix &m, std::uint32_t now) override
	{
		update_temporal_state(now);
		float u_offset, v_offset;
		calculate_uv_offset(&u_offset, &v_offset);
		m.Make_Identity();
		m.M[0][2] = u_offset;
		m.M[1][2] = v_offset;
	}
	unsigned Get_Current_Frame() const { return CurrentFrame; }

protected:
	void initialize(float fps, unsigned gridwidth_log2, std::uint32_t now)
	{
		unsigned grid_width = (1u << gridwidth_log2);
		if (LastFrame == 0)
		{
			LastFrame = grid_width * grid_width;
		}
		Offset = Offset % LastFrame;
		LastUsedSyncTime = now;
		GridWidthLog2 = gridwidth_log2;
		OOGridWidth = 1.0f / (float)grid_width;
		if (fps == 0.0f)
		{
			Sign = 0;
			MSPerFrame = 1;
			CurrentFrame = Offset;
		}
		else if (fps < 0.0f)
		{
			Sign = -1;
			MSPerFrame = (unsigned)(1000.0f / std::fabs(fps));
			CurrentFrame = (LastFrame - 1) - Offset;
		}
		else
		{
			Sign = 1;
			MSPerFrame = (unsigned)(1000.0f / std::fabs(fps));
			CurrentFrame = Offset;
		}
		Remainder = 0;
	}
	// BFME2 RVA 0x00182590. The modulo is taken unsigned, as retail does: a negative frame wraps through 2^32.
	void update_temporal_state(std::uint32_t now)
	{
		unsigned delta = now - LastUsedSyncTime;
		Remainder += delta;
		LastUsedSyncTime = now;
		int new_frame = (int)CurrentFrame + ((int)(Remainder / MSPerFrame) * Sign);
		new_frame = (int)((unsigned)new_frame % LastFrame);
		if (new_frame < 0)
		{
			CurrentFrame = LastFrame + new_frame;
		}
		else
		{
			CurrentFrame = (unsigned)new_frame;
		}
		Remainder = Remainder % MSPerFrame;
	}
	// BFME2 RVA 0x001825E0.
	void calculate_uv_offset(float *u_offset, float *v_offset) const
	{
		unsigned row_mask = ~(0xFFFFFFFFu << GridWidthLog2);
		unsigned col_mask = row_mask << GridWidthLog2;
		unsigned x = CurrentFrame & row_mask;
		unsigned y = (CurrentFrame & col_mask) >> GridWidthLog2;
		*u_offset = x * OOGridWidth;
		*v_offset = y * OOGridWidth;
	}

	int Sign = 1;
	unsigned MSPerFrame = 1;
	float OOGridWidth = 1.0f;
	unsigned GridWidthLog2 = 1;
	unsigned LastFrame = 0;
	unsigned Offset = 0;
	unsigned Remainder = 0;
	unsigned CurrentFrame = 0;
	std::uint32_t LastUsedSyncTime = 0;
};

// ---- RotateTextureMapperClass (BFME2 RVA 0x00182800) ---------------------------------------------------------------
class RotateMapper : public ScaleMapper
{
public:
	RotateMapper(const MapperArgs &args, unsigned stage, std::uint32_t now) : ScaleMapper(args, stage), LastUsedSyncTime(now)
	{
		RadiansPerMilliSec = 2 * kPi * args.Get_Float("Speed", 0.1f) / 1000.0f;
		Center.X = args.Get_Float("UCenter", 0.0f);
		Center.Y = args.Get_Float("VCenter", 0.0f);
	}
	int Get_Type() const override { return W3D_MAPPING_ROTATE; }
	bool Is_Time_Variant() const override { return true; }
	void Reset(std::uint32_t now) override
	{
		CurrentAngle = 0.0f;
		LastUsedSyncTime = now;
	}
	void Calculate_Texture_Matrix(W3DTexMatrix &m, std::uint32_t now) override
	{
		unsigned delta = now - LastUsedSyncTime;
		LastUsedSyncTime = now;
		CurrentAngle += RadiansPerMilliSec * delta;
		CurrentAngle = std::fmod(CurrentAngle, 2 * kPi);
		if (CurrentAngle < 0.0f) CurrentAngle += 2 * kPi;
		float c = std::cos(CurrentAngle);
		float s = std::sin(CurrentAngle);
		m.Make_Identity();
		// subtract center, rotate, add center, then scale
		m.M[0][0] = Scale.X * c;
		m.M[0][1] = -Scale.X * s;
		m.M[0][2] = -Scale.X * (c * Center.X - s * Center.Y - Center.X);
		m.M[0][3] = 0.0f;
		m.M[1][0] = Scale.Y * s;
		m.M[1][1] = Scale.Y * c;
		m.M[1][2] = -Scale.Y * (s * Center.X + c * Center.Y - Center.Y);
		m.M[1][3] = 0.0f;
	}

private:
	std::uint32_t LastUsedSyncTime;
	float CurrentAngle = 0.0f;
	float RadiansPerMilliSec = 0.0f;
	Vec2 Center;
};

// ---- SineLinearOffsetTextureMapperClass (BFME2 RVA 0x00182BA0) -----------------------------------------------------
class SineLinearOffsetMapper : public ScaleMapper
{
public:
	SineLinearOffsetMapper(const MapperArgs &args, unsigned stage, std::uint32_t now) : ScaleMapper(args, stage), LastUsedSyncTime(now)
	{
		UAFP[0] = args.Get_Float("UAmp", 1.0f);
		UAFP[1] = args.Get_Float("UFreq", 1.0f);
		UAFP[2] = args.Get_Float("UPhase", 0.0f);
		VAFP[0] = args.Get_Float("VAmp", 1.0f);
		VAFP[1] = args.Get_Float("VFreq", 1.0f);
		VAFP[2] = args.Get_Float("VPhase", 0.0f);
	}
	int Get_Type() const override { return W3D_MAPPING_SINE_LINEAR_OFFSET; }
	bool Is_Time_Variant() const override { return true; }
	void Reset(std::uint32_t now) override
	{
		CurrentAngle = 0.0f;
		LastUsedSyncTime = now;
	}
	void Calculate_Texture_Matrix(W3DTexMatrix &m, std::uint32_t now) override
	{
		unsigned delta = now - LastUsedSyncTime;
		LastUsedSyncTime = now;
		const float ms_to_radians = 2 * kPi / 1000.0f;
		CurrentAngle += delta * ms_to_radians;
		float offset_u = UAFP[0] * std::sin(UAFP[1] * CurrentAngle + UAFP[2] * kPi);
		float offset_v = VAFP[0] * std::sin(VAFP[1] * CurrentAngle + VAFP[2] * kPi);
		m.Make_Identity();
		m.M[0][2] = offset_u;
		m.M[0][0] = Scale.X;
		m.M[1][2] = offset_v;
		m.M[1][1] = Scale.Y;
	}

private:
	std::uint32_t LastUsedSyncTime;
	float UAFP[3] = { 1, 1, 0 }, VAFP[3] = { 1, 1, 0 };
	float CurrentAngle = 0.0f;
};

// ---- StepLinearOffsetTextureMapperClass (BFME2 RVA 0x00182E80) ---------------------------------------------------
class StepLinearOffsetMapper : public ScaleMapper
{
public:
	StepLinearOffsetMapper(const MapperArgs &args, unsigned stage, std::uint32_t now) : ScaleMapper(args, stage), LastUsedSyncTime(now)
	{
		Step.X = args.Get_Float("UStep", 0.0f);
		Step.Y = args.Get_Float("VStep", 0.0f);
		StepsPerMilliSec = args.Get_Float("SPS", 0.0f) / 1000.0f;
		ClampFix = args.Get_Bool("ClampFix", false);
	}
	int Get_Type() const override { return W3D_MAPPING_STEP_LINEAR_OFFSET; }
	bool Is_Time_Variant() const override { return true; }
	void Reset(std::uint32_t now) override
	{
		LastUsedSyncTime = now;
		CurrentStep = Vec2();
		Remainder = 0.0f;
	}
	void Calculate_Texture_Matrix(W3DTexMatrix &m, std::uint32_t now) override
	{
		unsigned delta = now - LastUsedSyncTime;
		LastUsedSyncTime = now;
		Remainder += delta;
		int num_steps = (int)(StepsPerMilliSec * Remainder);
		if (num_steps != 0)
		{
			CurrentStep.X += Step.X * num_steps;
			CurrentStep.Y += Step.Y * num_steps;
			Remainder -= num_steps / (float)StepsPerMilliSec;
		}
		if (!ClampFix)
		{
			CurrentStep.X -= floorF(CurrentStep.X);
			CurrentStep.Y -= floorF(CurrentStep.Y);
		}
		else
		{
			CurrentStep.X = clampF(CurrentStep.X, -Scale.X, Scale.X);
			CurrentStep.Y = clampF(CurrentStep.Y, -Scale.Y, Scale.Y);
		}
		m.Make_Identity();
		m.M[0][2] = CurrentStep.X;
		m.M[0][0] = Scale.X;
		m.M[1][2] = CurrentStep.Y;
		m.M[1][1] = Scale.Y;
	}

private:
	std::uint32_t LastUsedSyncTime;
	Vec2 Step, CurrentStep;
	float StepsPerMilliSec = 0.0f;
	float Remainder = 0.0f;
	bool ClampFix = false;
};

// ---- ZigZagLinearOffsetTextureMapperClass (BFME2 RVA 0x00183240) -------------------------------------------------
class ZigZagLinearOffsetMapper : public ScaleMapper
{
public:
	ZigZagLinearOffsetMapper(const MapperArgs &args, unsigned stage, std::uint32_t now) : ScaleMapper(args, stage), LastUsedSyncTime(now)
	{
		Speed.X = args.Get_Float("UPerSec", 0.0f) / 1000.0f;
		Speed.Y = args.Get_Float("VPerSec", 0.0f) / 1000.0f;
		Period = args.Get_Float("Period", 0.0f) * 1000.0f;
		if (Period < 0.0f) Period = -Period;
		Half_Period = 0.5f * Period;
	}
	int Get_Type() const override { return W3D_MAPPING_ZIGZAG_LINEAR_OFFSET; }
	bool Is_Time_Variant() const override { return true; }
	void Reset(std::uint32_t now) override
	{
		LastUsedSyncTime = now;
		Remainder = 0.0f;
	}
	void Calculate_Texture_Matrix(W3DTexMatrix &m, std::uint32_t now) override
	{
		unsigned delta = now - LastUsedSyncTime;
		LastUsedSyncTime = now;
		Remainder += delta;
		float offset_u = 0.0f;
		float offset_v = 0.0f;
		if (Period > 0.0f)
		{
			int num_periods = (int)(Remainder / Period);
			Remainder -= num_periods * Period;
			float time = 0.0f;
			if (Remainder > Half_Period)
			{
				time = Period - Remainder;
			}
			else
			{
				time = Remainder;
			}
			offset_u = Speed.X * time;
			offset_v = Speed.Y * time;
		}
		m.Make_Identity();
		m.M[0][2] = offset_u;
		m.M[0][0] = Scale.X;
		m.M[1][2] = offset_v;
		m.M[1][1] = Scale.Y;
	}

private:
	std::uint32_t LastUsedSyncTime;
	Vec2 Speed;
	float Period = 0.0f, Half_Period = 0.0f, Remainder = 0.0f;
};

// ---- RandomTextureMapperClass (BFME2 RVA 0x001838B0) -----------------------------------------------------------
class RandomMapper : public ScaleMapper
{
public:
	RandomMapper(const MapperArgs &args, unsigned stage, std::uint32_t now, const MapperRandom &rnd) : ScaleMapper(args, stage), Rand(rnd), LastUsedSyncTime(now)
	{
		FPMS = args.Get_Float("FPS", 0.0f) / 1000.0f;
		Speed.X = args.Get_Float("UPerSec", 0.0f) / 1000.0f;
		Speed.Y = args.Get_Float("VPerSec", 0.0f) / 1000.0f;
		randomize();
	}
	int Get_Type() const override { return W3D_MAPPING_RANDOM; }
	bool Is_Time_Variant() const override { return true; }
	void Reset(std::uint32_t now) override
	{
		LastUsedSyncTime = now;
		Remainder = 0.0f;
	}
	void Calculate_Texture_Matrix(W3DTexMatrix &m, std::uint32_t now) override
	{
		unsigned delta = now - LastUsedSyncTime;
		LastUsedSyncTime = now;
		Remainder += delta;
		if (FPMS != 0.0f)
		{
			int num_frames = (int)(Remainder * FPMS);
			if (num_frames != 0)
			{
				randomize();
				Remainder -= num_frames / FPMS;
			}
		}
		// [ c Sx  -s Sy  uoff ]  rotation about Z with the scale applied to the right, so it does not scale the offset
		float c = std::cos(CurrentAngle);
		float s = std::sin(CurrentAngle);
		m.Make_Identity();
		m.M[0][0] = c * Scale.X;
		m.M[0][1] = -s * Scale.Y;
		m.M[1][0] = s * Scale.X;
		m.M[1][1] = c * Scale.Y;
		float uoff = Center.X + Remainder * Speed.X;
		float voff = Center.Y + Remainder * Speed.Y;
		uoff = std::fmod(uoff, 1.0f);
		voff = std::fmod(voff, 1.0f);
		m.M[0][2] = uoff;
		m.M[1][2] = voff;
	}

private:
	void randomize()
	{
		CurrentAngle = 2 * kPi * Rand();
		Center.X = Rand();
		Center.Y = Rand();
	}
	MapperRandom Rand;
	std::uint32_t LastUsedSyncTime;
	float FPMS = 0.0f, Remainder = 0.0f, CurrentAngle = 0.0f;
	Vec2 Speed, Center;
};

// ---- environment mappers ---------------------------------------------------------------------------------------------
// ZH ClassicEnvironmentMapperClass / EnvironmentMapperClass: the canonical environment map; BFME2 0x00183340.
class EnvMapper : public TextureMapperClass
{
public:
	EnvMapper(int type, W3DTexGen gen, unsigned stage) : TextureMapperClass(stage), Type(type), Gen(gen) {}
	int Get_Type() const override { return Type; }
	W3DTexGen Get_TexGen() const override { return Gen; }
	void Calculate_Texture_Matrix(W3DTexMatrix &m, std::uint32_t) override
	{
		m.Init(0.5f, 0.0f, 0.0f, 0.5f, 0.0f, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
	}

private:
	int Type;
	W3DTexGen Gen;
};

enum AxisType { AXIS_X, AXIS_Y, AXIS_Z };

AxisType parseAxis(const MapperArgs &args)
{
	std::string a = args.Get_String("Axis", "Z");
	switch (a.empty() ? 'Z' : a[0])
	{
	case 'X': case 'x': return AXIS_X;
	case 'Y': case 'y': return AXIS_Y;
	default: return AXIS_Z;
	}
}

// ZH WSEnvMapperClass::Calculate_Texture_Matrix: the matrix BEFORE the multiplication by the view rotation transpose.
class WSEnvMapper : public TextureMapperClass
{
public:
	WSEnvMapper(int type, W3DTexGen gen, const MapperArgs &args, unsigned stage) : TextureMapperClass(stage), Type(type), Gen(gen), Axis(parseAxis(args)) {}
	int Get_Type() const override { return Type; }
	W3DTexGen Get_TexGen() const override { return Gen; }
	bool View_Dependent() const override { return true; }
	void Calculate_Texture_Matrix(W3DTexMatrix &m, std::uint32_t) override
	{
		switch (Axis)
		{
		case AXIS_X: m.Init(0.0f, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f, 0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f); break;
		case AXIS_Y: m.Init(0.5f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f); break;
		default: m.Init(0.5f, 0.0f, 0.0f, 0.5f, 0.0f, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f); break;
		}
	}

private:
	int Type;
	W3DTexGen Gen;
	AxisType Axis;
};

// ZH GridClassicEnvironmentMapperClass / GridEnvironmentMapperClass (BFME2 RVA 0x00183740, 260 bytes).
class GridEnvMapper : public GridMapper
{
public:
	GridEnvMapper(int type, W3DTexGen gen, const MapperArgs &args, unsigned stage, std::uint32_t now) : GridMapper(args, stage, now), Type(type), Gen(gen) {}
	int Get_Type() const override { return Type; }
	W3DTexGen Get_TexGen() const override { return Gen; }
	void Calculate_Texture_Matrix(W3DTexMatrix &m, std::uint32_t now) override
	{
		update_temporal_state(now);
		float u_offset, v_offset;
		calculate_uv_offset(&u_offset, &v_offset);
		float del = 0.5f * OOGridWidth;
		m.Init(del, 0.0f, 0.0f, u_offset + del, 0.0f, del, 0.0f, v_offset + del, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
	}

private:
	int Type;
	W3DTexGen Gen;
};

// ZH GridWSEnvMapperClass::Calculate_Texture_Matrix, minus the multiplication by the view rotation transpose.
class GridWSEnvMapper : public GridMapper
{
public:
	GridWSEnvMapper(int type, W3DTexGen gen, const MapperArgs &args, unsigned stage, std::uint32_t now) : GridMapper(args, stage, now), Type(type), Gen(gen), Axis(parseAxis(args)) {}
	int Get_Type() const override { return Type; }
	W3DTexGen Get_TexGen() const override { return Gen; }
	bool View_Dependent() const override { return true; }
	void Calculate_Texture_Matrix(W3DTexMatrix &m, std::uint32_t now) override
	{
		update_temporal_state(now);
		float u_offset, v_offset;
		calculate_uv_offset(&u_offset, &v_offset);
		float del = 0.5f * OOGridWidth;
		switch (Axis)
		{
		case AXIS_X: m.Init(0.0f, del, 0.0f, u_offset + del, 0.0f, 0.0f, del, v_offset + del, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f); break;
		case AXIS_Y: m.Init(del, 0.0f, 0.0f, u_offset + del, 0.0f, 0.0f, del, v_offset + del, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f); break;
		default: m.Init(del, 0.0f, 0.0f, u_offset + del, 0.0f, del, 0.0f, v_offset + del, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f); break;
		}
	}

private:
	int Type;
	W3DTexGen Gen;
	AxisType Axis;
};

// ---- EdgeMapperClass (BFME2 RVA 0x00183540) ---------------------------------------------------------------------------
class EdgeMapper : public TextureMapperClass
{
public:
	EdgeMapper(const MapperArgs &args, unsigned stage, std::uint32_t now) : TextureMapperClass(stage), LastUsedSyncTime(now)
	{
		VSpeed = args.Get_Float("VPerSec", 0.0f);
		VOffset = args.Get_Float("VStart", 0.0f);
		UseReflect = args.Get_Bool("UseReflect", false);
	}
	int Get_Type() const override { return W3D_MAPPING_EDGE; }
	W3DTexGen Get_TexGen() const override { return UseReflect ? W3D_TEXGEN_CAMERA_REFLECTION : W3D_TEXGEN_CAMERA_NORMAL; }
	bool Is_Time_Variant() const override { return true; }
	void Reset(std::uint32_t now) override
	{
		LastUsedSyncTime = now;
		VOffset = 0.0f;
	}
	void Calculate_Texture_Matrix(W3DTexMatrix &m, std::uint32_t now) override
	{
		float delta = (now - LastUsedSyncTime) * 0.001f;
		LastUsedSyncTime = now;
		VOffset += delta * VSpeed;
		VOffset -= floorF(VOffset);
		float vo = VOffset;
		// takes the Z component and uses it to index the texture
		m.Init(0.0f, 0.0f, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, vo, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
	}

private:
	std::uint32_t LastUsedSyncTime;
	float VSpeed = 0.0f, VOffset = 0.0f;
	bool UseReflect = false;
};

// ---- ScreenMapperClass (ZH mapper.cpp:893-960). The matrix here is S' with u' = Sx * s + offU * w; the projection is
// multiplied in by the shader (Projection_Dependent). The offset logic is LinearOffset's. -------------------------------
class ScreenMapper : public LinearOffsetMapper
{
public:
	ScreenMapper(const MapperArgs &args, unsigned stage, std::uint32_t now) : LinearOffsetMapper(args, stage, now) {}
	int Get_Type() const override { return W3D_MAPPING_SCREEN; }
	W3DTexGen Get_TexGen() const override { return W3D_TEXGEN_CAMERA_POSITION_PROJECTED; }
	bool Projection_Dependent() const override { return true; }
};

// ---- BumpEnvTextureMapperClass (ZH mapper.cpp:1043-1070): the linear offset matrix; the bump environment matrix feeds
// D3D's BUMPENVMAP stage op, which the translation does not provide (shader.cpp falls back to the diffuse colour).
class BumpEnvMapper : public LinearOffsetMapper
{
public:
	BumpEnvMapper(const MapperArgs &args, unsigned stage, std::uint32_t now) : LinearOffsetMapper(args, stage, now)
	{
		RadiansPerSecond = 2 * kPi * args.Get_Float("BumpRotation", 0.0f);
		ScaleFactor = args.Get_Float("BumpScale", 1.0f);
	}
	int Get_Type() const override { return W3D_MAPPING_BUMPENV; }

private:
	float RadiansPerSecond = 0.0f, ScaleFactor = 1.0f;
};
} // namespace

void W3DTexMatrix::Make_Identity()
{
	for (int r = 0; r < 4; ++r)
		for (int c = 0; c < 4; ++c)
			M[r][c] = r == c ? 1.0f : 0.0f;
}

void W3DTexMatrix::Init(float m00, float m01, float m02, float m03, float m10, float m11, float m12, float m13, float m20, float m21,
	float m22, float m23, float m30, float m31, float m32, float m33)
{
	const float v[16] = { m00, m01, m02, m03, m10, m11, m12, m13, m20, m21, m22, m23, m30, m31, m32, m33 };
	for (int i = 0; i < 16; ++i)
		M[i / 4][i % 4] = v[i];
}

bool MapperArgs::Parse(const std::string &text, std::string *error)
{
	Values.clear();
	Read.clear();
	size_t pos = 0;
	int lineNo = 0;
	while (pos <= text.size())
	{
		size_t nl = text.find_first_of("\r\n", pos);
		std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
		pos = nl == std::string::npos ? text.size() + 1 : nl + 1;
		++lineNo;
		size_t semi = line.find(';');
		if (semi != std::string::npos) line.resize(semi);
		line = trim(line);
		if (line.empty()) continue;
		size_t eq = line.find('=');
		if (eq == std::string::npos)
		{
			if (error) *error = "mapper args line " + std::to_string(lineNo) + " is not Key=Value: '" + line + "'";
			return false;
		}
		std::string key = lowerCopy(trim(line.substr(0, eq)));
		if (key.empty())
		{
			if (error) *error = "mapper args line " + std::to_string(lineNo) + " has an empty key";
			return false;
		}
		Values[key] = trim(line.substr(eq + 1));
	}
	return true;
}

bool MapperArgs::Has(const std::string &key) const { return Values.count(lowerCopy(key)) != 0; }

void MapperArgs::Mark_Read(const std::string &key) const { Read[lowerCopy(key)] = true; }

float MapperArgs::Get_Float(const std::string &key, float def) const
{
	Mark_Read(key);
	auto it = Values.find(lowerCopy(key));
	return it == Values.end() ? def : (float)std::atof(it->second.c_str());
}

int MapperArgs::Get_Int(const std::string &key, int def) const
{
	Mark_Read(key);
	auto it = Values.find(lowerCopy(key));
	return it == Values.end() ? def : std::atoi(it->second.c_str());
}

bool MapperArgs::Get_Bool(const std::string &key, bool def) const
{
	Mark_Read(key);
	auto it = Values.find(lowerCopy(key));
	if (it == Values.end() || it->second.empty()) return def;
	switch (it->second[0])
	{
	case 'Y': case 'y': case 'T': case 't': case '1': return true;
	case 'N': case 'n': case 'F': case 'f': case '0': return false;
	}
	return def;
}

std::string MapperArgs::Get_String(const std::string &key, const std::string &def) const
{
	Mark_Read(key);
	auto it = Values.find(lowerCopy(key));
	return it == Values.end() ? def : it->second;
}

std::vector<std::string> MapperArgs::Unread_Keys() const
{
	std::vector<std::string> out;
	for (const auto &kv : Values)
	{
		if (!Read.count(kv.first)) out.push_back(kv.first);
	}
	return out;
}

const char *W3D_Mapper_Type_Name(int type)
{
	switch (type)
	{
	case W3D_MAPPING_UV: return "UV";
	case W3D_MAPPING_ENVIRONMENT: return "ENVIRONMENT";
	case W3D_MAPPING_CHEAP_ENVIRONMENT: return "CHEAP_ENVIRONMENT";
	case W3D_MAPPING_SCREEN: return "SCREEN";
	case W3D_MAPPING_LINEAR_OFFSET: return "LINEAR_OFFSET";
	case W3D_MAPPING_SILHOUETTE: return "SILHOUETTE";
	case W3D_MAPPING_SCALE: return "SCALE";
	case W3D_MAPPING_GRID: return "GRID";
	case W3D_MAPPING_ROTATE: return "ROTATE";
	case W3D_MAPPING_SINE_LINEAR_OFFSET: return "SINE_LINEAR_OFFSET";
	case W3D_MAPPING_STEP_LINEAR_OFFSET: return "STEP_LINEAR_OFFSET";
	case W3D_MAPPING_ZIGZAG_LINEAR_OFFSET: return "ZIGZAG_LINEAR_OFFSET";
	case W3D_MAPPING_WS_CLASSIC_ENV: return "WS_CLASSIC_ENV";
	case W3D_MAPPING_WS_ENVIRONMENT: return "WS_ENVIRONMENT";
	case W3D_MAPPING_GRID_CLASSIC_ENV: return "GRID_CLASSIC_ENV";
	case W3D_MAPPING_GRID_ENVIRONMENT: return "GRID_ENVIRONMENT";
	case W3D_MAPPING_RANDOM: return "RANDOM";
	case W3D_MAPPING_EDGE: return "EDGE";
	case W3D_MAPPING_BUMPENV: return "BUMPENV";
	case W3D_MAPPING_GRID_WS_CLASSIC_ENV: return "GRID_WS_CLASSIC_ENV";
	case W3D_MAPPING_GRID_WS_ENVIRONMENT: return "GRID_WS_ENVIRONMENT";
	}
	return "?";
}

std::unique_ptr<TextureMapperClass> Create_Texture_Mapper(int type, unsigned stage, const std::string &argsText, std::uint32_t now,
	const MapperRandom &random, std::string *error, bool *unknownType)
{
	if (unknownType) *unknownType = false;
	if (error) error->clear();

	// ZH vertmaterial.cpp:593-940: the cases that build a mapper. Everything else, SILHOUETTE included, is `default: break`.
	if (type < 0 || type > W3D_MAPPING_GRID_WS_ENVIRONMENT)
	{
		if (unknownType) *unknownType = true;
		return nullptr;
	}
	if (type == W3D_MAPPING_UV || type == W3D_MAPPING_SILHOUETTE)
	{
		return nullptr;
	}

	MapperArgs args;
	std::string parseError;
	if (!args.Parse(argsText, &parseError))
	{
		if (error) *error = parseError;
		return nullptr;
	}

	std::unique_ptr<TextureMapperClass> mapper;
	switch (type)
	{
	case W3D_MAPPING_ENVIRONMENT: mapper.reset(new EnvMapper(type, W3D_TEXGEN_CAMERA_REFLECTION, stage)); break;
	case W3D_MAPPING_CHEAP_ENVIRONMENT: mapper.reset(new EnvMapper(type, W3D_TEXGEN_CAMERA_NORMAL, stage)); break;
	case W3D_MAPPING_SCREEN: mapper.reset(new ScreenMapper(args, stage, now)); break;
	case W3D_MAPPING_LINEAR_OFFSET: mapper.reset(new LinearOffsetMapper(args, stage, now)); break;
	case W3D_MAPPING_SCALE: mapper.reset(new ScaleMapper(args, stage)); break;
	case W3D_MAPPING_GRID: mapper.reset(new GridMapper(args, stage, now)); break;
	case W3D_MAPPING_ROTATE: mapper.reset(new RotateMapper(args, stage, now)); break;
	case W3D_MAPPING_SINE_LINEAR_OFFSET: mapper.reset(new SineLinearOffsetMapper(args, stage, now)); break;
	case W3D_MAPPING_STEP_LINEAR_OFFSET: mapper.reset(new StepLinearOffsetMapper(args, stage, now)); break;
	case W3D_MAPPING_ZIGZAG_LINEAR_OFFSET: mapper.reset(new ZigZagLinearOffsetMapper(args, stage, now)); break;
	case W3D_MAPPING_WS_CLASSIC_ENV: mapper.reset(new WSEnvMapper(type, W3D_TEXGEN_CAMERA_NORMAL, args, stage)); break;
	case W3D_MAPPING_WS_ENVIRONMENT: mapper.reset(new WSEnvMapper(type, W3D_TEXGEN_CAMERA_REFLECTION, args, stage)); break;
	case W3D_MAPPING_GRID_CLASSIC_ENV: mapper.reset(new GridEnvMapper(type, W3D_TEXGEN_CAMERA_NORMAL, args, stage, now)); break;
	case W3D_MAPPING_GRID_ENVIRONMENT: mapper.reset(new GridEnvMapper(type, W3D_TEXGEN_CAMERA_REFLECTION, args, stage, now)); break;
	case W3D_MAPPING_RANDOM:
	{
		MapperRandom rnd = random;
		if (!rnd)
		{
			// Fixed-seed fallback so an unconfigured caller is still deterministic (render-only; not retail's Random4Class).
			auto state = std::make_shared<std::uint32_t>(0x9E3779B9u + stage);
			rnd = [state]() {
				std::uint32_t x = *state;
				x ^= x << 13;
				x ^= x >> 17;
				x ^= x << 5;
				*state = x;
				return (float)(x >> 8) * (1.0f / 16777216.0f);
			};
		}
		mapper.reset(new RandomMapper(args, stage, now, rnd));
		break;
	}
	case W3D_MAPPING_EDGE: mapper.reset(new EdgeMapper(args, stage, now)); break;
	case W3D_MAPPING_BUMPENV: mapper.reset(new BumpEnvMapper(args, stage, now)); break;
	case W3D_MAPPING_GRID_WS_CLASSIC_ENV: mapper.reset(new GridWSEnvMapper(type, W3D_TEXGEN_CAMERA_NORMAL, args, stage, now)); break;
	case W3D_MAPPING_GRID_WS_ENVIRONMENT: mapper.reset(new GridWSEnvMapper(type, W3D_TEXGEN_CAMERA_REFLECTION, args, stage, now)); break;
	default: break;
	}

	std::vector<std::string> unread = args.Unread_Keys();
	if (!unread.empty())
	{
		std::string list;
		for (const std::string &k : unread) list += (list.empty() ? "" : ", ") + k;
		if (error) *error = std::string("mapper ") + W3D_Mapper_Type_Name(type) + " has arguments it does not read: " + list;
		return nullptr;
	}
	return mapper;
}
