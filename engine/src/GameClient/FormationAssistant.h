// OpenBFME. GPL-3.0. BFME2 FormationAssistant data and row placement.
#pragma once
#include "Common/INIDataTypes.h"
#include "GameLogic/ObjectFilter.h"
#include <map>
#include <string>
#include <vector>
class ArchiveFileSystem;
class GameLogic;
class Object;
class INI;
class FormationAssistant
{
  public:
	struct Decal
	{
		std::string texture;
		float width = 0, height = 0;
	};
	struct Preview
	{
		Decal large, item;
	};
	struct Unit
	{
		std::string name, preview;
		ObjectFilter filter;
	};
	using Row = std::vector<std::vector<std::string>>;
	struct Template
	{
		std::string name;
		std::vector<Row> rows;
	};
	struct Selection
	{
		int type = -1;
		float distance = 0;
		int count = 0;
	};
	struct Slot
	{
		unsigned object;
		std::string preview;
		Coord3D offset;
	};
	float rowPadding = 0, columnPadding = 0, activationDistance = 0, activationTime = 0;
	std::string defaultPreview, head, body, base;
	ObjectFilter valid;
	std::vector<Unit> units;
	std::vector<Template> templates;
	std::vector<Selection> selections;
	std::map<std::string, Preview> previews;
	bool load(ArchiveFileSystem &fs, std::string *error);
	bool scan(const std::string &text, std::string *error);
	int choose(float distance, size_t count) const;
	std::vector<Slot> layout(GameLogic &logic, const std::vector<unsigned> &ids, int type, const Coord3D &origin) const;
	static std::vector<Coord2D> rankOffsets(const Object &object);
	static Coord2D rankCenter(const Object &object);
	const Decal *decal(const std::string &name, bool item = false) const;
};
