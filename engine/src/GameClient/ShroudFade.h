#pragma once
// Lane FOGFX-1 (FB-0061: "the fog reveals in choppy chunks"): W3DShroud's fog level interpolation, the client's fade of every shroud texel toward
// the level its cell's status asks for. CLIENT ONLY: it reads the levels the presented snapshot's ShroudView gives and never touches the logic's
// shroud (the revealed cells, a 40 ft PartitionCellSize grid re-looked when a unit changes cell, RW 0xB4EF50, are the simulation's and unchanged).
//
// TARGET FACTS (RotWK 2.01, read in the binary):
// - the cell setter RW 0x472A96 (W3DShroud::setShroudLevel(x, y, level, textureOnly)): the level is raised to at least GlobalData ShroudAlpha
//   (+0xBEA); textureOnly == 0 (the display callback RW 0x444913, the status's ShroudAlpha / FogAlpha / ClearAlpha) stores the FINAL level
//   (W3DShroud + 0x38) and, with interpolation's dirty set on (+0x48), queues the cell (+0x4C); the texel is written either way.
// - the render body RW 0x472EF4 calls RW 0x472CF2 (interpolateFogLevels) on every shroud render, before the cell rows are copied to the texture:
//   ms = timeGetTime() - last (nothing when 0; last += ms), step = _ftol(ms * 0.255f) (RW 0xBDC530 = 0x3E828F5C), at most 255; every cell whose
//   CURRENT level (+0x3C) differs from its final one moves toward it by step, or lands on it when it is less than step away, and its texel is
//   written with the current level (RW 0x472A96, textureOnly 1); a cell that lands leaves the dirty set. 0.255 levels per ms: fog (127) to clear
//   (255) in about 0.5 s; the current levels start at 0 (a new game fades in from black).
// DONOR: ZH W3DShroud::interpolateFogLevels (same algorithm, "one second black to lit"); BFME2 1.06 identical (tools/re/rw2decomp.py tier A).
// PRESENTATION CORRECTION (not retail): retail truncates the step of every render and drops the fraction, so its fade slows above 30 fps and stops
// below 4 ms a frame; Godot renders at any rate, so the milliseconds below one step are carried to the next frame (the fade takes the same wall
// time at every frame rate, retail's at 30 fps).
#include <cstdint>
#include <vector>

class ShroudFade
{
public:
	static constexpr float kLevelsPerMs = 0.255f; // RW 0xBDC530

	// a grid of w * h cells, every current and final level `level` (retail: 0)
	void reset(int w, int h, std::uint8_t level);
	int width() const { return m_w; }
	int height() const { return m_h; }
	// RW 0x472A96 with textureOnly 0: the cell's final level, raised to at least `floor` (GlobalData ShroudAlpha)
	void setFinal(int x, int y, std::uint8_t level, std::uint8_t floor);
	// RW 0x472CF2 for `ms` elapsed milliseconds (fractional; the part below one step is carried); true when a current level changed
	bool step(double ms);
	// every current level moved to its final one (a forced rebuild: a load, the shroud drawn again)
	void snap();
	std::uint8_t current(int x, int y) const { return m_current[(size_t)y * (size_t)m_w + (size_t)x]; }
	std::uint8_t finalLevel(int x, int y) const { return m_final[(size_t)y * (size_t)m_w + (size_t)x]; }
	const std::vector<std::uint8_t> &currentLevels() const { return m_current; }
	bool fading() const { return !m_dirty.empty(); }

private:
	int m_w = 0, m_h = 0;
	std::vector<std::uint8_t> m_current, m_final;
	std::vector<std::uint32_t> m_dirty; ///< cells whose current level differs from the final one (retail's set at + 0x4C), in insertion order
	std::vector<std::uint8_t> m_inDirty;
	double m_carryMs = 0.0;
};
