// OpenBFME. GPL-3.0.
//
// Godot's native cursor adapter for the retail Mouse/Win32Mouse cursor resources.

#include "GodotDevice/GodotMouse.h"

#include "Common/ArchiveFileSystem.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cstring>

namespace godot
{

namespace
{

String toGodot(const std::string &s)
{
	return String::utf8(s.c_str(), (int64_t)s.size());
}

constexpr const char *kArrowCursorName = "Arrow";

}

void GodotMouse::loadCursorTable(ArchiveFileSystem &fs, const String &installRoot)
{
	m_cursorTable.clear();
	m_cursors.clear();
	m_cursorName.clear();
	m_cursorStep = 0;
	m_cursorDirection = 0;
	m_cursorClock = 0.0;
	m_cursorHidden = false;
	m_blankCursor.unref();
	m_cursorShown.unref();
	m_cursorHot = Vector2();
	m_cursorErrors.clear();

	const std::string root = installRoot.utf8().get_data();
	m_cursorDir = root.empty() ? std::string() : root + "/data/cursors";
	if (root.empty())
	{
		m_cursorErrors.push_back("cursor install root is empty");
	}

	std::vector<std::uint8_t> ini;
	std::string error;
	if (!fs.readFile("data/ini/mouse.ini", ini, &error) || !ParseMouseCursors(std::string(ini.begin(), ini.end()), m_cursorTable, &error))
	{
		m_cursorErrors.push_back("mouse.ini: " + error);
	}
	else
	{
		auto arrow = m_cursorTable.find(kArrowCursorName);
		if (arrow == m_cursorTable.end() || arrow->second.image.empty())
		{
			m_cursorErrors.push_back("mouse.ini: missing image for cursor Arrow");
		}
	}
	// Target: RW 0x5EDC1F initializes and resets the mouse to cursor 2 (Arrow).
	// Donor: BFME2 GameClient/Input/Mouse.cpp, RVA 0x1EE4E6 (tier A).
}

void GodotMouse::updateCursor(double delta, const std::string &wanted, float scrollX, float scrollY, bool scrolling)
{
	if (m_cursorDir.empty())
	{
		return;
	}
	auto it = m_cursorTable.find(wanted);
	if (it == m_cursorTable.end() || it->second.image.empty())
	{
		// Target: RW 0x4413E3 uses SetCursor(null) for Mouse.ini entries without a resource.
		// Donor: BFME2 Win32Mouse.cpp::setCursor, RVA 0x41A83 (tier A).
		if (!m_cursorHidden)
		{
			if (m_blankCursor.is_null())
			{
				m_blankCursor = Image::create_empty(1, 1, false, Image::FORMAT_RGBA8);
				m_blankCursor->fill(Color(0, 0, 0, 0));
			}
			Input::get_singleton()->set_custom_mouse_cursor(m_blankCursor, Input::CURSOR_ARROW, Vector2());
			m_cursorHidden = true;
			m_cursorShown.unref();
		}
		m_cursorName = wanted;
		return;
	}
	// lane BUILD-5: the frame of a directional cursor (W3DMouse::setCursorDirection RW 0x498B1A, refreshed every drawn frame)
	const int dirs = it->second.directions > 1 ? it->second.directions : 1;
	const int direction = ScrollCursorDirection(scrollX, scrollY, dirs, scrolling);
	CursorSet &set = m_cursors[wanted + "#" + std::to_string(direction)];
	if (!set.loaded)
	{
		set.loaded = true;
		// Donor: BFME2 Win32Mouse.cpp::initCursorResources, RVA 0x41BA6 (tier A); target call site RW 0x441506.
		const std::string file = CursorFileName(it->second.image, dirs, direction);
		const std::string path = FindFileNoCase(m_cursorDir, file);
		std::string error;
		std::vector<std::uint8_t> bytes;
		if (path.empty())
		{
			m_cursorErrors.push_back("cursor " + wanted + ": no file " + file);
			UtilityFunctions::push_error(toGodot(m_cursorErrors.back()));
		}
		else
		{
			Ref<FileAccess> f = FileAccess::open(String::utf8(path.c_str()), FileAccess::READ);
			if (f.is_null())
			{
				m_cursorErrors.push_back("cursor " + wanted + ": cannot read " + path);
				UtilityFunctions::push_error(toGodot(m_cursorErrors.back()));
			}
			else
			{
				const PackedByteArray all = f->get_buffer((int64_t)f->get_length());
				bytes.assign(all.ptr(), all.ptr() + all.size());
				if (!DecodeCursorFile(bytes, set.image, &error))
				{
					m_cursorErrors.push_back("cursor " + wanted + ": " + error);
					UtilityFunctions::push_error(toGodot(m_cursorErrors.back()));
				}
				else
				{
					for (const std::vector<std::uint8_t> &frame : set.image.frames)
					{
						PackedByteArray px;
						px.resize((int64_t)frame.size());
						std::memcpy(px.ptrw(), frame.data(), frame.size());
						set.frames.push_back(Image::create_from_data(set.image.width, set.image.height, false, Image::FORMAT_RGBA8, px));
					}
				}
			}
		}
	}
	if (set.frames.empty())
	{
		// A missing or invalid cursor reports an error and shows the engine pointer, never the previous cursor.
		if (wanted != m_cursorName || direction != m_cursorDirection || m_cursorHidden || m_cursorShown.is_valid())
		{
			Input::get_singleton()->set_custom_mouse_cursor(Ref<Resource>(), Input::CURSOR_ARROW, Vector2());
			m_cursorName = wanted;
			m_cursorDirection = direction;
			m_cursorHidden = false;
			m_cursorShown.unref();
		}
		return;
	}
	if (wanted != m_cursorName || direction != m_cursorDirection || m_cursorHidden)
	{
		m_cursorName = wanted;
		m_cursorDirection = direction;
		m_cursorHidden = false;
		m_cursorStep = 0;
		m_cursorClock = 0.0;
	}
	else
	{
		m_cursorClock += delta;
		const double step = (double)set.image.jiffies[(size_t)m_cursorStep % set.image.jiffies.size()] / 60.0;
		if (set.image.sequence.size() <= 1 || m_cursorClock < step)
		{
			return;
		}
		m_cursorClock = 0.0;
		m_cursorStep = (m_cursorStep + 1) % (int)set.image.sequence.size();
	}
	const int frame = set.image.sequence[(size_t)m_cursorStep % set.image.sequence.size()];
	const int hx = it->second.hotX >= 0 ? it->second.hotX : set.image.hotX;
	const int hy = it->second.hotY >= 0 ? it->second.hotY : set.image.hotY;
	Input::get_singleton()->set_custom_mouse_cursor(set.frames[(size_t)frame], Input::CURSOR_ARROW, Vector2((float)hx, (float)hy));
	m_cursorShown = set.frames[(size_t)frame];
	m_cursorHot = Vector2((float)hx, (float)hy);
}

Dictionary GodotMouse::getCursorImage(bool includeImage) const
{
	Dictionary cursor;
	cursor["name"] = toGodot(m_cursorName);
	cursor["direction"] = m_cursorDirection;
	cursor["hidden"] = m_cursorHidden;
	if (includeImage)
	{
		cursor["texture"] = m_cursorShown.is_valid() ? ImageTexture::create_from_image(m_cursorShown) : Ref<ImageTexture>();
	}
	cursor["hot"] = m_cursorHot;
	return cursor;
}

}
