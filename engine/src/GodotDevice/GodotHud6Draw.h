// OpenBFME. GPL-3.0.
//
// Device layer, lane HUD-6: draws what the core's HUD-6 parts describe (GameClient/ControlBarRadialMenu.h, CommandButtonHelp.h, InGameHelpBox.h): the radial
// command bubbles (the button image in the window's ellipse, grey when disabled, the RadialBorder / RadialOver / RadialPush overlays, the clock) and the help
// box's content clip (the help's lines in the INI fonts, the resource icons). Owned by the HUD node (GodotInGameHud.cpp).

#pragma once

#include "GameClient/GUI/GameWindowManager.h"

#include <godot_cpp/classes/font.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/rect2.hpp>
#include <godot_cpp/variant/rid.hpp>

#include <functional>
#include <memory>
#include <string>

class InGameHud;
class MappedImageCollection;
struct Image;

namespace godot
{
class AptMenuPlayer;

class Hud6Draw
{
public:
	typedef std::function<Ref<ImageTexture>(const std::string &file)> TextureFn;
	Hud6Draw(InGameHud &hud, const MappedImageCollection &images, TextureFn textureFor, AptMenuPlayer *player);
	~Hud6Draw();

	// the help's text measure (the fonts the movie's texts use; S-763's display string inference)
	FontMetricsSource *metrics() { return m_metrics.get(); }
	// the help box's content clip: its `_type` is the help box's render name (InGameHelpBox::renderName)
	bool handles(const std::string &symbolName) const;
	void drawHelp(const Rect2 &rect, RID item);
	// the ring, under the Palantir movie (after the drawable decorations)
	void drawRadial(RID parent);
	// { radial: { object, count, radius, centre, buttons: [{ slot, rect, state, hilited }] }, help: { state, shown, name, width, calls } }
	Dictionary state() const;

private:
	bool region(const std::string &image, Ref<ImageTexture> &tex, Rect2 &src);
	InGameHud &m_hud;
	const MappedImageCollection &m_images;
	TextureFn m_textureFor;
	AptMenuPlayer *m_player;
	std::unique_ptr<FontMetricsSource> m_metrics;
	RID m_radialItem, m_grayItem;
	Ref<Shader> m_grayShader;
	Ref<ShaderMaterial> m_grayMaterial;
	unsigned m_helpDraws = 0, m_radialDraws = 0;
};

} // namespace godot
