// OpenBFME - lane VISUAL-2: the tree sway of the W3DTreeDraw trees (W3DTreeBuffer's vegetation sway; client only, never simulation state).
//
// TARGET FACTS (RotWK 2.01, each tier A = byte-identical to BFME2 1.06, whose decomp C++ is byte-matched; tools/re/rw2decomp.py):
// - RW 0x4E5D2C (BFME2 0x4E6406, decomp W3DVegetationBufferBase.cpp): when the breeze version changed, the 100-entry offset table
//   offsets[i] = (dirX * S, dirY * S, C - 1) with S / C the sine / cosine of lean + intensity * cos(i * 0.06220976), and for each of the 10 sway
//   types step = 100 / (breezePeriod * swayRate) * GameClientRandomValueReal(1 - r/2, 1 + r/2) (at least 0), offset 0 and factor
//   GameClientRandomValueReal(1 - r/2, 1 + r/2) (r = randomness; per type the step's draw, then the factor's); then every type's offset advances by its
//   step and wraps above 100 (minus 100).
// - RW 0x4E63E9 / 0x4E5EDA (the buffer's constructor / reset): swayRate = (float)TheGameEngine + 0x38 (the client frames per logic frame, 30 / 5 = 6),
//   1.0 without an engine; the version starts at -1 (the first update builds the table).
// - RW 0x4ED508 (the tree buffer's per-frame update, RotWK only; BFME2 0x4EDB47's body): the sway update runs unless the script engine's time is
//   frozen (RW 0x603418 / 0x441E23) or the game is paused (RW 0x90F92C).
// - RW 0x4E61EE (the vegetation effect's "SwayOffsets" parameter setter, RW 0x4E610C): 11 vectors, entry 0 zero (the memset), entry 1 + i the lerp of
//   offsets[floor(offset_i)] and offsets[floor(offset_i) + 1] by the fraction, times factor_i; left zero when floor(offset_i) < 0 or + 1 >= 100.
// - vegetation.fxo vertex shader (blob 5, Shaders.big): position += (position.z - base z) * SwayOffsets[sway type] (texcoord1.x the type,
//   texcoord1.y the tree's base z), so a vertex moves by its height above the tree's base.
// - RW 0x4EC93A (BFME2 0x4ECF79 W3DTreeBuffer::addTree): a tree's sway type is 0 when its W3DTreeDraw data byte +0x54 is set, else
//   GameClientRandomValue(1, 10) (W3DTreeBuffer.cpp line 0x2C3). +0x54 is TaintedTree in RotWK's W3DTreeDraw FieldParse table (RW 0xBE2E80).
// INFERENCE (stop S-4973): the update runs once per retail client frame (30 per second); the device layer advances it by the elapsed client frames
// (fractions included) so the sway is smooth at any frame rate, the wrap applied after each whole or partial step.
#pragma once

#include <cstdint>

class W3DDrawRandom;

class W3DVegetationSway
{
public:
	enum
	{
		NUM_SWAY_ENTRIES = 100,
		MAX_SWAY_TYPES = 10
	};
	struct Breeze
	{
		float directionX = 0.0f, directionY = 0.0f;
		float intensity = 0.0f, lean = 0.0f, randomness = 0.0f;
		int breezePeriod = 1;
		int breezeVersion = 0;
	};
	struct Vec3
	{
		float x = 0.0f, y = 0.0f, z = 0.0f;
	};

	// RW 0xD9F60C (BfmeClientFramesPerSecond, 30) and RW 0x4E63E9's sway rate: TheGameEngine + 0x38 = client frames per logic frame (30 / 5)
	static constexpr int kClientFramesPerSecond = 30;
	static constexpr float kRetailSwayRate = 6.0f;

	explicit W3DVegetationSway(float swayRate = kRetailSwayRate) : m_swayRate(swayRate) {}
	// the device layer's call: `seconds` of presentation time are seconds * kClientFramesPerSecond retail client frames
	void advanceSeconds(const Breeze &breeze, W3DDrawRandom &random, double seconds);

	// RW 0x4E5D2C with `clientFrames` retail client frames elapsed (1 = one retail call)
	void update(const Breeze &breeze, W3DDrawRandom &random, double clientFrames = 1.0);
	// RW 0x4E61EE: the 11 SwayOffsets vectors (entry 0 is no sway)
	// Smooth presentation closes the final interval instead of the retail zero-vector gap.
	void swayOffsets(Vec3 out[MAX_SWAY_TYPES + 1], bool smoothLoop = false) const;

	int version() const { return m_curSwayVersion; }
	float offset(int type) const { return m_curSwayOffset[type]; }
	float step(int type) const { return m_curSwayStep[type]; }
	float factor(int type) const { return m_curSwayFactor[type]; }
	const Vec3 &tableEntry(int i) const { return m_swayOffsets[i]; }

	// RW 0x4EC93A: a new tree's sway type
	static int treeSwayType(bool taintedTree, W3DDrawRandom &random);

private:
	Vec3 m_swayOffsets[NUM_SWAY_ENTRIES];
	int m_curSwayVersion = -1;
	float m_curSwayOffset[MAX_SWAY_TYPES] = {};
	float m_curSwayStep[MAX_SWAY_TYPES] = {};
	float m_curSwayFactor[MAX_SWAY_TYPES] = {};
	float m_swayRate = 1.0f;
};
