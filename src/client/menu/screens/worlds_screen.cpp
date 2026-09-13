// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#include "worlds_screen.h"

#include "client/menu/main_menu.h"
#include "client/menu/text.h"
#include "filesys.h"
#include "gettext.h"
#include "mapgen/mapgen.h"
#include "settings.h"
#include "util/string.h"
#include <algorithm>

namespace menu
{

namespace
{

bool belongsTo(const WorldSpec &world, const CrateSpec &crate)
{
	return world.crateid == crate.id || crate.aliases.count(world.crateid) > 0;
}

std::string worldMapgen(const WorldSpec &world)
{
	Settings meta;
	std::string name;
	if (meta.readConfigFile((world.path + DIR_DELIM "map_meta.txt").c_str()))
		meta.getNoEx("mg_name", name);
	return name;
}

}

void WorldsScreen::bind(Rml::DataModelConstructor &model)
{
	if (auto entry = model.RegisterStruct<CrateEntry>()) {
		entry.RegisterMember("id", &CrateEntry::id);
		entry.RegisterMember("title", &CrateEntry::title);
		entry.RegisterMember("author", &CrateEntry::author);
		entry.RegisterMember("icon", &CrateEntry::icon);
		entry.RegisterMember("initial", &CrateEntry::initial);
		entry.RegisterMember("worlds", &CrateEntry::worlds);
	}
	if (auto entry = model.RegisterStruct<WorldEntry>()) {
		entry.RegisterMember("name", &WorldEntry::name);
		entry.RegisterMember("mapgen", &WorldEntry::mapgen);
	}
	model.RegisterArray<std::vector<CrateEntry>>();
	model.RegisterArray<std::vector<WorldEntry>>();
	model.RegisterArray<std::vector<Rml::String>>();

	m_crate_heading = strgettext("Crate");
	m_worlds_heading = strgettext("Worlds");
	model.Bind("crate_heading", &m_crate_heading);
	model.Bind("worlds_heading", &m_worlds_heading);
	model.Bind("crates", &m_crates);
	model.Bind("crate", &m_crate);
	model.Bind("worlds", &m_entries);
	model.Bind("mapgens", &m_mapgens);
	model.Bind("selected", &m_selected);
	model.Bind("selected_name", &m_selected_name);
	model.Bind("selected_mapgen", &m_selected_mapgen);
	model.Bind("delete_question", &m_delete_question);
	model.Bind("creating", &m_creating);
	model.Bind("confirm_delete", &m_confirm_delete);
	model.Bind("new_name", &m_new_name);
	model.Bind("new_seed", &m_new_seed);
	model.Bind("new_mapgen", &m_new_mapgen);
	model.Bind("error", &m_error);

	auto index_arg = [](const Rml::VariantList &args) {
		return args.empty() ? -1 : args[0].Get<int>(-1);
	};

	model.BindEventCallback("select_crate",
			[this, index_arg](Rml::DataModelHandle handle, Rml::Event &, const Rml::VariantList &args) {
				selectCrate(index_arg(args));
				handle.DirtyAllVariables();
			});
	model.BindEventCallback("select",
			[this, index_arg](Rml::DataModelHandle handle, Rml::Event &, const Rml::VariantList &args) {
				selectWorld(index_arg(args));
				m_confirm_delete = false;
				m_creating = false;
				handle.DirtyAllVariables();
			});
	model.BindEventCallback("play",
			[this](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
				if (m_selected >= 0 && (size_t)m_selected < m_worlds.size())
					menu().startSingleplayer(m_worlds[m_selected]);
			});
	model.BindEventCallback("create_begin",
			[this](Rml::DataModelHandle handle, Rml::Event &, const Rml::VariantList &) {
				beginCreate();
				handle.DirtyAllVariables();
			});
	model.BindEventCallback("create_cancel",
			[this](Rml::DataModelHandle handle, Rml::Event &, const Rml::VariantList &) {
				m_creating = false;
				m_error.clear();
				handle.DirtyAllVariables();
			});
	model.BindEventCallback("create",
			[this](Rml::DataModelHandle handle, Rml::Event &, const Rml::VariantList &) {
				create();
				handle.DirtyAllVariables();
			});
	model.BindEventCallback("delete_begin",
			[this](Rml::DataModelHandle handle, Rml::Event &, const Rml::VariantList &) {
				m_confirm_delete = m_selected >= 0;
				handle.DirtyAllVariables();
			});
	model.BindEventCallback("delete_cancel",
			[this](Rml::DataModelHandle handle, Rml::Event &, const Rml::VariantList &) {
				m_confirm_delete = false;
				handle.DirtyAllVariables();
			});
	model.BindEventCallback("delete",
			[this](Rml::DataModelHandle handle, Rml::Event &, const Rml::VariantList &) {
				deleteSelected();
				handle.DirtyAllVariables();
			});
}

void WorldsScreen::refresh()
{
	m_crate_specs = getAvailableCrates();
	m_all_worlds = getAvailableWorlds();

	m_crates.clear();
	for (const CrateSpec &spec : m_crate_specs) {
		CrateEntry entry;
		entry.id = spec.id;
		entry.title = spec.title.empty() ? spec.id : spec.title;
		entry.author = spec.author;
		const std::string icon = spec.path + DIR_DELIM "menu" DIR_DELIM "icon.png";
		if (fs::PathExists(icon))
			entry.icon = icon;
		const std::wstring wide = utf8_to_wide(entry.title);
		entry.initial = wide.empty() ? "" : uppercase(wide_to_utf8(wide.substr(0, 1)));
		entry.worlds = (int)std::count_if(m_all_worlds.begin(), m_all_worlds.end(),
				[&spec](const WorldSpec &w) { return belongsTo(w, spec); });
		m_crates.push_back(entry);
	}

	std::string last;
	g_settings->getNoEx("menu_last_crate", last);
	int crate = -1;
	for (size_t i = 0; i < m_crate_specs.size(); i++) {
		if (m_crate_specs[i].id == last)
			crate = (int)i;
	}
	if (crate < 0 && !m_crate_specs.empty())
		crate = 0;
	selectCrate(crate);

	m_creating = false;
	m_confirm_delete = false;
	m_error.clear();
	model().DirtyAllVariables();
}

bool WorldsScreen::onEvent(const SEvent &event)
{
	if (event.EventType != EET_KEY_INPUT_EVENT || !event.KeyInput.PressedDown
			|| event.KeyInput.Key != KEY_ESCAPE)
		return false;
	if (m_creating || m_confirm_delete) {
		m_creating = false;
		m_confirm_delete = false;
		m_error.clear();
		model().DirtyAllVariables();
	} else {
		menu().navigate("start");
	}
	return true;
}

void WorldsScreen::selectCrate(int index)
{
	m_crate = index >= 0 && (size_t)index < m_crate_specs.size() ? index : -1;
	m_creating = false;
	m_confirm_delete = false;
	m_error.clear();
	if (m_crate >= 0)
		g_settings->set("menu_last_crate", m_crate_specs[m_crate].id);
	m_mapgens = m_crate >= 0 ? mapgensFor(m_crate_specs[m_crate]) : std::vector<Rml::String>();
	reloadWorlds();
}

void WorldsScreen::reloadWorlds()
{
	m_worlds.clear();
	m_entries.clear();
	if (m_crate >= 0) {
		for (const WorldSpec &world : m_all_worlds) {
			if (belongsTo(world, m_crate_specs[m_crate]))
				m_worlds.push_back(world);
		}
	}
	for (const WorldSpec &world : m_worlds)
		m_entries.push_back({world.name, worldMapgen(world)});
	selectWorld(m_worlds.empty() ? -1 : 0);
}

void WorldsScreen::selectWorld(int index)
{
	m_selected = index >= 0 && (size_t)index < m_worlds.size() ? index : -1;
	m_selected_name = m_selected >= 0 ? m_worlds[m_selected].name : "";
	m_selected_mapgen = m_selected >= 0 ? m_entries[m_selected].mapgen : "";
	m_delete_question = m_selected >= 0
			? fmtgettext("Delete the world \"%s\" for good?", m_selected_name.c_str())
			: "";
}

// Крейт может ограничить генераторы в crate.conf: allowed_mapgens — только
// эти, disallowed_mapgens — все, кроме этих.
std::vector<Rml::String> WorldsScreen::mapgensFor(const CrateSpec &crate) const
{
	std::vector<const char *> names;
	Mapgen::getMapgenNames(&names, false);

	Settings conf;
	conf.readConfigFile((crate.path + DIR_DELIM "crate.conf").c_str());
	auto listOf = [&conf](const char *key) {
		std::vector<std::string> out;
		std::string raw;
		if (conf.getNoEx(key, raw)) {
			for (const std::string &item : str_split(raw, ','))
				out.emplace_back(trim(item));
		}
		return out;
	};
	const std::vector<std::string> allowed = listOf("allowed_mapgens");
	const std::vector<std::string> disallowed = listOf("disallowed_mapgens");

	std::vector<Rml::String> out;
	for (const char *name : names) {
		const bool in_allowed = std::find(allowed.begin(), allowed.end(), name) != allowed.end();
		const bool in_disallowed = std::find(disallowed.begin(), disallowed.end(), name)
				!= disallowed.end();
		if ((allowed.empty() || in_allowed) && !in_disallowed)
			out.emplace_back(name);
	}
	return out;
}

void WorldsScreen::beginCreate()
{
	m_creating = true;
	m_confirm_delete = false;
	m_error.clear();
	m_new_name.clear();
	m_new_seed.clear();

	std::string mapgen;
	g_settings->getNoEx("mg_name", mapgen);
	const bool mapgen_exists = std::find(m_mapgens.begin(), m_mapgens.end(), mapgen)
			!= m_mapgens.end();
	m_new_mapgen = mapgen_exists ? mapgen : (m_mapgens.empty() ? "" : m_mapgens.front());
}

void WorldsScreen::create()
{
	if (m_crate < 0) {
		m_error = strgettext("No crate selected");
		return;
	}
	std::string name(trim(m_new_name));
	if (name.empty()) {
		// Безымянным мирам достаётся world<N> со следующим свободным номером.
		int max_num = 0;
		for (const WorldSpec &world : m_all_worlds) {
			if (world.name.rfind("world", 0) == 0)
				max_num = std::max(max_num, mystoi(world.name.substr(5), 0, 1 << 30));
		}
		name = "world" + std::to_string(max_num + 1);
	}
	for (const WorldSpec &world : m_all_worlds) {
		if (world.name == name) {
			m_error = fmtgettext("A world named \"%s\" already exists", name.c_str());
			return;
		}
	}

	std::unordered_map<std::string, std::string> settings;
	settings["fixed_map_seed"] = m_new_seed;
	settings["mg_name"] = m_new_mapgen;
	m_error = createWorld(name, m_crate_specs[m_crate].id, settings);
	if (!m_error.empty())
		return;

	refresh();
	for (size_t i = 0; i < m_worlds.size(); i++) {
		if (m_worlds[i].name == name)
			selectWorld((int)i);
	}
}

void WorldsScreen::deleteSelected()
{
	m_confirm_delete = false;
	if (m_selected < 0 || (size_t)m_selected >= m_worlds.size())
		return;
	const std::string error = deleteWorld(m_worlds[m_selected]);
	refresh();
	m_error = error;
}

}
