// OpenBFME. GPL-3.0.
// See GodotHud6Draw.h (lane HUD-6).

#include "GodotDevice/GodotHud6Draw.h"

#include "GameClient/GUI/AptScreens/AptImageMode.h"

#include "GameClient/CommandButtonHelp.h"
#include "GameClient/ControlBarCommands.h"
#include "GameClient/ControlBarRadialMenu.h"
#include "GameClient/GUI/Image.h"
#include "GameClient/ControlBar.h"
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
		if (r.is_valid())
		{
			r = m_player->font_cell(r, (int)std::lround(size)); // lane UI-6: RotWK's GDI text cell
		}
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
		if (m_messageItem.is_valid()) rs->free_rid(m_messageItem);
		if (m_radialItem.is_valid())
		{
			rs->free_rid(m_radialItem);
		}
		if (m_iconItem.is_valid())
		{
			rs->free_rid(m_iconItem);
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
	m_helpTextOps = 0;
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
		++m_helpTextOps;
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
		// INFERENCE (S-2701): radial cameos use the ellipse as their mask. Their texture alpha masks foreground art;
		// the RGB underneath contains the full button background (notably builders and heroes).
		// Keep the rim/clock on the ordinary alpha-blended item.
		m_grayShader->set_code(String(aptImageModeShader(APT_IMAGE_SOLID_GRAYSCALE).c_str()));
		m_iconShader.instantiate();
		m_iconShader->set_code(String(aptImageModeShader(APT_IMAGE_SOLID).c_str()));
		m_iconMaterial.instantiate();
		m_iconMaterial->set_shader(m_iconShader);
		m_iconItem = rs->canvas_item_create();
		rs->canvas_item_set_parent(m_iconItem, parent);
		rs->canvas_item_set_draw_index(m_iconItem, -999);
		rs->canvas_item_set_material(m_iconItem, m_iconMaterial->get_rid());
		rs->canvas_item_set_z_index(m_iconItem, -1);
		m_grayMaterial.instantiate();
		m_grayMaterial->set_shader(m_grayShader);
		m_grayItem = rs->canvas_item_create();
		rs->canvas_item_set_parent(m_grayItem, parent);
		rs->canvas_item_set_draw_index(m_grayItem, -999);
		rs->canvas_item_set_material(m_grayItem, m_grayMaterial->get_rid());
		m_radialItem = rs->canvas_item_create();
		rs->canvas_item_set_parent(m_radialItem, parent);
		rs->canvas_item_set_draw_index(m_radialItem, -998);
		// lane MENU-1 (the tester's report: the bubbles over the pause menu): the radial menu is in-game UI, which is drawn before the menus (ZH
		// W3DDisplay::draw: TheInGameUI->draw(), then TheWindowManager->winRepaint(); RotWK's Apt is the window manager's). The HUD node comes after
		// the shell in the scene, so its bubbles go one z step below the shell's items: under the quit menu, Options and the Palantir.
		// INFERENCE (S-3507): RotWK's own order of the radial draw and the Apt render was not read
		rs->canvas_item_set_z_index(m_grayItem, -1);
		rs->canvas_item_set_z_index(m_radialItem, -1);
	}
	rs->canvas_item_clear(m_iconItem);
	rs->canvas_item_clear(m_grayItem);
	rs->canvas_item_clear(m_radialItem);
	const int kSegments = 32;
	for (const RadialDrawOp &op : m_hud.radialMenu().ops())
	{
		if (op.kind == RadialDrawOp::TEXT)
		{
			// lane OWNERFIX-3: the count (RW 0x4A4CD3): DisplayString::draw(x, y, 1, 1) puts the drop copy at (x + 1, y + 1) first, then the text; (x, y)
			// is the text cell's top-left (the core placed it with these fonts' measure, HelpFontMetrics)
			float size = 0;
			Ref<Font> f = static_cast<HelpFontMetrics *>(m_metrics.get())->font(op.font, size);
			const String text = toGodot(op.text);
			const int px = (int)std::lround(size);
			const float base = op.y + f->get_ascent(px);
			f->draw_string(m_radialItem, Vector2(op.x + 1.0f, base + 1.0f), text, HORIZONTAL_ALIGNMENT_LEFT, -1, px, argb(op.dropColor));
			f->draw_string(m_radialItem, Vector2(op.x, base), text, HORIZONTAL_ALIGNMENT_LEFT, -1, px, argb(op.color));
			++m_radialTexts;
			continue;
		}
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
				cols.push_back(argb(op.color)); // lane HUD-7: the ALWAYS_COLOR multiplier (white otherwise)
			}
			rs->canvas_item_add_polygon(op.grayscale ? m_grayItem : m_iconItem, pts, cols, uvs, tex->get_rid());
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
	// lane DEVPORT-8 (FALSEDEF-1): the under-attack blink's window (ControlBar.wnd:WinUAttack, InGameHud::AttackWindow): the default draw's image for
	// its enabled state at its rect; with RotWK's data (NoImage in both states) nothing is drawn, as in retail
	m_attackImage = m_hud.attackWindowImage();
	int ax, ay, aw, ah;
	Ref<ImageTexture> attackTex;
	Rect2 attackSrc;
	if (!m_attackImage.empty() && m_hud.attackWindowRect(m_hud.windowWidth(), m_hud.windowHeight(), ax, ay, aw, ah) && region(m_attackImage, attackTex, attackSrc))
	{
		rs->canvas_item_add_texture_rect_region(m_radialItem, Rect2((float)ax, (float)ay, (float)aw, (float)ah), attackTex->get_rid(), attackSrc);
		++m_attackDraws;
	}
	++m_radialDraws;
}

Dictionary Hud6Draw::state() const
{
	Dictionary out;
	{
		// lane DEVPORT-8 (FALSEDEF-1): the blink (ControlBar::AttackFlash) and its window
		const ControlBar::AttackFlash &f = m_hud.controlBar().attackFlash();
		const InGameHud::AttackWindow &w = m_hud.attackWindow();
		Dictionary flash;
		flash["window"] = w.present;
		flash["window_enabled"] = f.windowEnabled;
		flash["on"] = f.on;
		flash["count"] = f.count;
		flash["starts"] = (int64_t)f.starts;
		flash["toggles"] = (int64_t)f.toggles;
		flash["requests"] = (int64_t)m_hud.attackFlashRequests();
		flash["enabled_image"] = String(w.enabledImage.c_str());
		flash["disabled_image"] = String(w.disabledImage.c_str());
		flash["image_status"] = w.imageStatus;
		flash["rect"] = Rect2((float)w.x0, (float)w.y0, (float)(w.x1 - w.x0), (float)(w.y1 - w.y0));
		flash["resolution"] = Vector2((float)w.resX, (float)w.resY);
		flash["drawn_image"] = String(m_attackImage.c_str());
		flash["image_draws"] = (int64_t)m_attackDraws;
		out["attack_flash"] = flash;
	}
	const ControlBarRadialMenu &r = m_hud.radialMenu();
	Dictionary radial;
	radial["object"] = (int64_t)r.object();
	radial["count"] = r.layout().count();
	radial["radius"] = r.layout().radius();
	radial["centre"] = Vector2((float)r.layout().centerX(), (float)r.layout().centerY());
	radial["size"] = Vector2((float)r.layout().buttonW(), (float)r.layout().buttonH());
	radial["hilited"] = r.hilitedSlot();
	radial["unverified"] = "[S-3507] the bubbles are drawn under the menus (ZH's in-game UI before the window manager); RotWK's own order was not read"; // lane MENU-1
	radial["presses"] = (int64_t)r.presses();
	radial["draws"] = (int64_t)m_radialDraws;
	radial["texts_drawn"] = (int64_t)m_radialTexts; // lane OWNERFIX-3
	Array texts;
	for (const RadialDrawOp &op : r.ops())
	{
		if (op.kind == RadialDrawOp::TEXT)
		{
			Dictionary t;
			t["slot"] = op.slot;
			t["text"] = toGodot(op.text);
			t["pos"] = Vector2(op.x, op.y);
			t["color"] = (int64_t)op.color;
			texts.push_back(t);
		}
	}
	radial["texts"] = texts;
	Array buttons;
	for (const ControlBarRadialMenu::Button &b : r.buttons())
	{
		Dictionary e;
		e["slot"] = b.slot;
		e["rect"] = Rect2((float)b.rect.x, (float)b.rect.y, (float)b.rect.w, (float)b.rect.h);
		e["state"] = (int)b.source.state;
		e["production_count"] = b.source.productionCount; // lane OWNERFIX-3
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
	help["text_ops"] = (int64_t)m_helpTextOps;
	help["name"] = h.help() ? toGodot(u16ToUtf8(h.help()->name())) : String();
	// lane HOVER-1: the rest of what the box shows (the cost line, the command points line, the long text), its kind and the provider's bar
	help["title"] = h.help() ? toGodot(u16ToUtf8(h.help()->title())) : String();
	help["points"] = h.help() ? toGodot(u16ToUtf8(h.help()->description())) : String();
	help["text"] = h.help() ? toGodot(u16ToUtf8(h.help()->longText())) : String();
	help["simple"] = h.help() ? h.help()->isSimple() : false;
	help["bar"] = h.provider().bar;
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

void Hud6Draw::drawMessages(RID parent)
{
	auto *rs = RenderingServer::get_singleton();
	if (!rs) return;
	if (!m_messageItem.is_valid())
	{
		m_messageItem = rs->canvas_item_create();
		rs->canvas_item_set_parent(m_messageItem, parent);
	}
	rs->canvas_item_clear(m_messageItem);
	if (m_hud.menuCoversStage()) return;
	const auto &log = m_hud.playerMessages();
	const auto &settings = log.settings;
	const int px = GlobalLanguage::adjustFontSize(settings.pointSize, m_hud.windowWidth());
	float drawSize = (float)px;
	Ref<Font> font = m_player ? m_player->font_for(settings.font, drawSize, &drawSize, settings.bold) : Ref<Font>();
	if (font.is_null()) return; // font_for reports unavailable corpus fonts.
	font = m_player->font_cell(font, (int)std::lround(drawSize));
	const int size = (int)std::lround(drawSize);
	const int x = (int)std::floor(m_hud.windowWidth() * settings.position.x / 1024.0f + .5f);
	float y = std::floor(m_hud.windowHeight() * settings.position.y / 768.0f + .5f);
	for (auto it = log.entries().rbegin(); it != log.entries().rend(); ++it)
	{
		const String text = toGodot(u16ToUtf8(it->text));
		const float width = font->get_string_size(text, HORIZONTAL_ALIGNMENT_LEFT, -1, size).x;
		const float left = x < 0 ? m_hud.windowWidth() + x - width : x;
		const Vector2 at(left, y + font->get_ascent(size));
		font->draw_string(m_messageItem, at + Vector2(1, 1), text, HORIZONTAL_ALIGNMENT_LEFT, -1, size, argb(it->color & 0xff000000));
		font->draw_string(m_messageItem, at, text, HORIZONTAL_ALIGNMENT_LEFT, -1, size, argb(it->color));
		y += font->get_height(size);
	}
}

} // namespace godot
