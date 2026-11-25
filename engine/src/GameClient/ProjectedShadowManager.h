// OpenBFME. GPL-3.0.
//
// ProjectedShadowManager (editions: both RotWK 2.01 and BFME2 1.06):
// Manages projected decals and shadows on the terrain.
//
// TARGET FACTS:
// - Retail singleton VA: RW 0xDD1D14 / BFME2 0xDEC2D4 (TheProjectedShadowManager).
// - createEmitter: RW 0x8B01BC -> vtable slot 4 (offset 0x10) / BFME2 0x004ACADA -> slot 4.
// - Shadow:
//   * position at +0x08 (Coord3D)
//   * setColor: RW 0x73232B / BFME2 0x0073232B
//   * setOpacity: RW 0x73228B / BFME2 0x0073228B
//   * release (vtable slot 2, offset 0x08): RW 0x8AFF8B / BFME2 0x004AC8EF
//   * children at +0x58 (child 1) and +0x5C (child 2):
//     each child has angle (+0x20), sizeX (+0x58), sizeY (+0x5C).

#pragma once

#include "Common/INIDataTypes.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class ShadowChild
{
public:
	float m_angle = 0.0f;  ///< +0x20 (radians)
	float m_sizeX = 0.0f;  ///< +0x58 (world units)
	float m_sizeY = 0.0f;  ///< +0x5C (world units)
};

class Shadow
{
public:
	struct ShadowTypeInfo
	{
		std::string m_first;
		std::string m_second;
		int m_unknown8 = 0x40;  ///< 0x40 = SHADOW_ADDITIVE_DECAL
		float m_floatC = 0.0f;  ///< sizeX
		float m_float10 = 0.0f; ///< sizeY
		float m_float14 = 0.0f;
		float m_float18 = 0.0f;
		float m_float1C = 0.0f;
		float m_float20 = 0.0f;
		std::uint8_t m_byte24 = 0;
		std::uint8_t m_byte25 = 0; ///< allowUpdates
		std::uint8_t m_byte26 = 1; ///< worldAlign
	};

	Shadow() = default;
	virtual ~Shadow() = default;
	virtual void vslot0() {}
	virtual void vslot1() {}
	virtual void release();
	virtual void setColor(std::uint32_t c);
	virtual void setOpacity(int o);

	Coord3D m_pos08{ 0.0f, 0.0f, 0.0f }; ///< +0x08
	std::uint32_t m_color = 0xFFFFFFFF;
	int m_opacity = 255;
	std::string m_texture;
	int m_shadowType = 0x40;
	bool m_active = true;

	ShadowChild m_child1; ///< first child
	ShadowChild m_child2; ///< second child
	ShadowChild *m_58 = &m_child1;
	ShadowChild *m_5c = &m_child2;
};

class ProjectedShadowManager
{
public:
	ProjectedShadowManager();
	virtual ~ProjectedShadowManager();

	virtual void m0() {}
	virtual void m1() {}
	virtual void m2() {}
	virtual void m3() {}
	virtual Shadow *createEmitter(int type, const Shadow::ShadowTypeInfo *b, const Shadow::ShadowTypeInfo *c);
	virtual void removeShadow(Shadow *s);

	const std::vector<std::unique_ptr<Shadow>> &shadows() const { return m_shadows; }
	void prune();

private:
	std::vector<std::unique_ptr<Shadow>> m_shadows;
};

extern ProjectedShadowManager *TheProjectedShadowManager;

