// OpenBFME. GPL-3.0.
//
// Godot's native cursor adapter for the retail Mouse/Win32Mouse cursor resources.

#pragma once

#include "GameClient/CursorFile.h"

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include <map>
#include <string>
#include <vector>

class ArchiveFileSystem;

namespace godot
{

class GodotMouse
{
public:
	void loadCursorTable(ArchiveFileSystem &fs, const String &installRoot);
	void updateCursor(double delta, const std::string &wanted, float scrollX = 0.0f, float scrollY = 0.0f, bool scrolling = false);
	Dictionary getCursorImage(bool includeImage = true) const;
	const std::vector<std::string> &cursorErrors() const { return m_cursorErrors; }

private:
	std::map<std::string, MouseCursorEntry> m_cursorTable;
	struct CursorSet
	{
		bool loaded = false;
		CursorImage image;
		std::vector<Ref<Image>> frames;
	};
	std::map<std::string, CursorSet> m_cursors;
	std::string m_cursorDir, m_cursorName;
	int m_cursorStep = 0, m_cursorDirection = 0;
	double m_cursorClock = 0.0;
	bool m_cursorHidden = false;
	Ref<Image> m_blankCursor;
	Ref<Image> m_cursorShown;
	Vector2 m_cursorHot;
	std::vector<std::string> m_cursorErrors;
};

}
