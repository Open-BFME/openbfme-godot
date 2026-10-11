// OpenBFME. GPL-3.0.
// See GodotHud6Draw.h (lane HUD-6).

#include "GodotDevice/GodotHud6Draw.h"

#include "GameClient/CommandButtonHelp.h"
#include "GameClient/ControlBarCommands.h"
#include "GameClient/ControlBarRadialMenu.h"
#include "GameClient/GUI/Image.h"
#include "GameClient/InGameHud.h"
#include "GodotDevice/GodotAptPlayer.h"

#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/theme_db.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>

#include <cmath>

namespace godot
{
namespace
{
Color argb(std::uint32_t c)
{
	return Color(((c >> 16) & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f, (c & 0xFF) / 255.0f, ((c >> 24) & 0xFF) / 255.0f);
}
String toGodot(const std::string &s)
{
	return String::utf8(s.c_str(), (int)s.size());
}
std::string u16ToUtf8(const UnicodeString &s)
{
	std::string out;
	for (char16_t c : s)
	{
		const unsigned v = c;
		if (v < 0x80)
		{
			out += (char)v;
		}
		else if (v < 0x800)
		{
			out += (char)(0xC0 | (v >> 6));
			out += (char)(0x80 | (v & 0x3F));
		}
		else
		{
			out += (char)(0xE0 | (v >> 12));
			out += (char)(0x80 | ((v >> 6) & 0x3F));
			out += (char)(0x80 | (v & 0x3F));
		}
	}
	return out;
}

// the help's text measure with the movie's fonts (the point size is already the window's, InGameHelpBox scales it by min(scale x, y))
class HelpFontMetrics : public FontMetricsSource
{
public:
	explicit HelpFontMetrics(AptMenuPlayer *player) : m_player(player) {}
	Ref<Font> font(const GameFont &f, float &size)
	{
		size = (float)f.pointSize;
		Ref<Font> r = m_player ? m_player->font_for(f.name, (float)f.pointSize, &size) : Ref<Font>();
		return r.is_valid() ? r : ThemeDB::get_singleton()->get_fallback_font();
	}
	int fontHeight(const GameFont &f) override
	{
		float size = 0;
		Ref<Font> r = font(f, size);
		return (int)std::ceil(r->get_height((int)std::lround(size)));
	}
	int textWidth(const GameFont &f, const UnicodeString &text) override
	{
		float size = 0;
		Ref<Font> r = font(f, size);
		return (int)std::ceil(r->get_string_size(toGodot(u16ToUtf8(text)), HORIZONTAL_ALIGNMENT_LEFT, -1, (int)std::lround(size)).x);
	}
	int wrappedHeight(const GameFont &f, const UnicodeString &text, int wrapWidth) override
	{
		float size = 0;
		Ref<Font> r = font(f, size);
		if (wrapWidth <= 0)
		{
			return fontHeight(f);
		}
		return (int)std::ceil(r->get_multiline_string_size(toGodot(u16ToUtf8(text)), HORIZONTAL_ALIGNMENT_LEFT, (float)wrapWidth, (int)std::lround(size)).y);
	}

private:
	AptMenuPlayer *m_player;
};

const char *kGrayShader = R"(shader_type canvas_item;
void fragment() {
	vec4 c = texture(TEXTURE, UV) * COLOR;
	float g = dot(c.rgb, vec3(0.299, 0.587, 0.114));
	COLOR = vec4(vec3(g), c.a);
}
)";
} // namespace

Hud6Draw::Hud6Draw(InGameHud &hud, const MappedImageCollection &images, TextureFn textureFor, AptMenuPlayer *player)
	: m_hud(hud), m_images(images), m_textureFor(std::move(textureFor)), m_player(player), m_metrics(std::make_unique<HelpFontMetrics>(player))
{
}

Hud6Draw::~Hud6Draw()
{
	RenderingServer *rs = RenderingServer::get_singleton();
	if (rs)
	{
		if (m_radialItem.is_valid())
		{
			rs->free_rid(m_radialItem);
		}
		if (m_grayItem.is_valid())
		{
			rs->free_rid(m_grayItem);
		}
	}
}

bool Hud6Draw::handles(const std::string &symbolName) const
{
	return !m_hud.helpBox().renderName().empty() && symbolName == m_hud.helpBox().renderName();
}

bool Hud6Draw::region(const std::string &image, Ref<ImageTexture> &tex, Rect2 &src)
{
	const ::Image *mapped = m_images.findImageByName(image);
	tex = mapped ? m_textureFor(mapped->filename) : Ref<ImageTexture>();
	if (tex.is_null())
	{
		return false;
	}
	const float tw = (float)tex->get_width(), th = (float)tex->get_height();
	src = Rect2(mapped->uvLo[0] * tw, mapped->uvLo[1] * th, (mapped->uvHi[0] - mapped->uvLo[0]) * tw, (mapped->uvHi[1] - mapped->uvLo[1]) * th);
	return true;
}

void Hud6Draw::drawHelp(const Rect2 &rect, RID item)
{
	std::vector<HelpDrawOp> ops;
	m_hud.helpBoxRender(rect.position.x, rect.position.y, rect.size.x, rect.size.y, ops); // RW 0x92E2AC
	RenderingServer *rs = RenderingServer::get_singleton();
	for (const HelpDrawOp &op : ops)
	{
		if (op.kind == HelpDrawOp::IMAGE)
		{
			Ref<ImageTexture> tex;
			Rect2 src;
			if (region(op.image, tex, src))
			{
				rs->canvas_item_add_texture_rect_region(item, Rect2(op.x, op.y, op.w, op.h), tex->get_rid(), src);
			}
			continue;
		}
		GameFont g;
		g.name = op.font.name;
		g.pointSize = op.font.pointSize;
		g.bold = op.font.bold;
		float size = 0;
		Ref<Font> f = static_cast<HelpFontMetrics *>(m_metrics.get())->font(g, size);
		const String text = toGodot(op.text);
		// the word wrap is centred: each line in the string's box (CommandButtonHelp::emit gives the box's left edge and width)
		const float w = f->get_string_size(text, HORIZONTAL_ALIGNMENT_LEFT, -1, (int)std::lround(size)).x;
		const float x = std::floor(op.x + (op.w - w) * 0.5f + 0.5f);
		f->draw_string(item, Vector2(x, op.y + f->get_ascent((int)std::lround(size))), text, HORIZONTAL_ALIGNMENT_LEFT, -1, (int)std::lround(size), argb(op.font.color));
	}
	++m_helpDraws;
}

void Hud6Draw::drawRadial(RID parent)
{
	RenderingServer *rs = RenderingServer::get_singleton();
	if (!rs)
	{
		return;
	}
	if (!m_grayItem.is_valid())
	{
		m_grayShader.instantiate();
		m_grayShader->set_code(kGrayShader);
		m_grayMaterial.instantiate();
		m_grayMaterial->set_shader(m_grayShader);
		m_grayItem = rs->canvas_item_create();
		rs->canvas_item_set_parent(m_grayItem, parent);
		rs->canvas_item_set_draw_index(m_grayItem, -999);
		rs->canvas_item_set_material(m_grayItem, m_grayMaterial->get_rid());
		m_radialItem = rs->canvas_item_create();
		rs->canvas_item_set_parent(m_radialItem, parent);
		rs->canvas_item_set_draw_index(m_radialItem, -998);
	}
	rs->canvas_item_clear(m_grayItem);
	rs->canvas_item_clear(m_radialItem);
	const int kSegments = 32;
	for (const RadialDrawOp &op : m_hud.radialMenu().ops())
	{
		Ref<ImageTexture> tex;
		Rect2 src;
		if (!region(op.image, tex, src))
		{
			continue;
		}
		const float tw = (float)tex->get_width(), th = (float)tex->get_height();
		if (op.kind == RadialDrawOp::OVERLAY)
		{
			rs->canvas_item_add_texture_rect_region(m_radialItem, Rect2(op.x, op.y, op.w, op.h), tex->get_rid(), src, argb(op.color));
			continue;
		}
		const float cx = op.x + op.w * 0.5f, cy = op.y + op.h * 0.5f, rx = op.w * 0.5f, ry = op.h * 0.5f;
		PackedVector2Array pts, uvs;
		PackedColorArray cols;
		if (op.kind == RadialDrawOp::ICON)
		{
			// the image clipped to the window's ellipse (the radial stencil)
			for (int i = 0; i < kSegments; ++i)
			{
				const float a = (float)i / (float)kSegments * 6.2831853f;
				const float ux = std::cos(a) * 0.5f + 0.5f, uy = std::sin(a) * 0.5f + 0.5f;
				pts.push_back(Vector2(op.x + ux * op.w, op.y + uy * op.h));
				uvs.push_back(Vector2((src.position.x + ux * src.size.x) / tw, (src.position.y + uy * src.size.y) / th));
				cols.push_back(Color(1, 1, 1, 1));
			}
			rs->canvas_item_add_polygon(op.grayscale ? m_grayItem : m_radialItem, pts, cols, uvs, tex->get_rid());
			continue;
		}
		// CLOCK: the part still to go, clockwise from the top (INFERENCE, S-2701: the inverse clock's wipe)
		const float done = op.percent / 100.0f;
		if (done >= 1.0f)
		{
			continue;
		}
		pts.push_back(Vector2(cx, cy));
		uvs.push_back(Vector2((src.position.x + src.size.x * 0.5f) / tw, (src.position.y + src.size.y * 0.5f) / th));
		cols.push_back(argb(op.color));
		const int n = kSegments;
		for (int i = 0; i <= n; ++i)
		{
			const float t = done + (1.0f - done) * (float)i / (float)n;
			const float a = t * 6.2831853f - 1.5707963f;
			const float ux = std::cos(a) * 0.5f + 0.5f, uy = std::sin(a) * 0.5f + 0.5f;
			pts.push_back(Vector2(cx + (ux - 0.5f) * 2.0f * rx, cy + (uy - 0.5f) * 2.0f * ry));
			uvs.push_back(Vector2((src.position.x + ux * src.size.x) / tw, (src.position.y + uy * src.size.y) / th));
			cols.push_back(argb(op.color));
		}
		rs->canvas_item_add_polygon(m_radialItem, pts, cols, uvs, tex->get_rid());
	}
	++m_radialDraws;
}

Dictionary Hud6Draw::state() const
{
	Dictionary out;
	const ControlBarRadialMenu &r = m_hud.radialMenu();
	Dictionary radial;
	radial["object"] = (int64_t)r.object();
	radial["count"] = r.layout().count();
	radial["radius"] = r.layout().radius();
	radial["centre"] = Vector2((float)r.layout().centerX(), (float)r.layout().centerY());
	radial["size"] = Vector2((float)r.layout().buttonW(), (float)r.layout().buttonH());
	radial["hilited"] = r.hilitedSlot();
	radial["presses"] = (int64_t)r.presses();
	radial["draws"] = (int64_t)m_radialDraws;
	Array buttons;
	for (const ControlBarRadialMenu::Button &b : r.buttons())
	{
		Dictionary e;
		e["slot"] = b.slot;
		e["rect"] = Rect2((float)b.rect.x, (float)b.rect.y, (float)b.rect.w, (float)b.rect.h);
		e["state"] = (int)b.source.state;
		e["image"] = toGodot(b.source.image);
		e["name"] = toGodot(b.source.button ? b.source.button->m_name : std::string());
		e["command"] = toGodot(b.source.button ? GUICommandName(b.source.button->m_command) : "");
		buttons.push_back(e);
	}
	radial["buttons"] = buttons;
	Array errors;
	for (const std::string &e : r.errors())
	{
		errors.push_back(toGodot(e));
	}
	radial["errors"] = errors;
	out["radial"] = radial;
	Dictionary help;
	const InGameHelpBox &h = m_hud.helpBox();
	help["loaded"] = h.isLoaded();
	help["render_name"] = toGodot(h.renderName());
	help["state"] = h.state();
	help["shown"] = h.isShown();
	help["width"] = h.sampledWidth();
	help["helps_shown"] = (int64_t)h.helpsShown();
	help["draws"] = (int64_t)m_helpDraws;
	help["name"] = h.help() ? toGodot(u16ToUtf8(h.help()->name())) : String();
	Array calls;
	for (const std::string &c : h.calls())
	{
		calls.push_back(toGodot(c));
	}
	help["calls"] = calls;
	out["help"] = help;
	Dictionary side;
	if (const AptPalantir *p = const_cast<InGameHud &>(m_hud).palantir())
	{
		side["shown"] = p->sideBarShown();
		side["count"] = p->sideBarCount();
		Array sc;
		for (const std::string &c : p->sideBarCalls())
		{
			sc.push_back(toGodot(c));
		}
		side["calls"] = sc;
	}
	out["side_bar"] = side;
	return out;
}

} // namespace godot
