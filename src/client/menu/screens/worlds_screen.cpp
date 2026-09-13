// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#include "worlds_screen.h"

#include "client/menu/main_menu.h"
#include "gettext.h"
#include "mapgen/mapgen.h"
#include "settings.h"
#include <algorithm>

namespace menu
{

void WorldsScreen::bind(Rml::DataModelConstructor &model)
{
	if (auto entry = model.RegisterStruct<WorldEntry>()) {
		entry.RegisterMember("name", &WorldEntry::name);
		entry.RegisterMember("crate", &WorldEntry::crate);
	}
	if (auto entry = model.RegisterStruct<CrateEntry>()) {
		entry.RegisterMember("id", &CrateEntry::id);
		entry.RegisterMember("title", &CrateEntry::title);
	}
	model.RegisterArray<std::vector<WorldEntry>>();
	model.RegisterArray<std::vector<CrateEntry>>();
	model.RegisterArray<std::vector<Rml::String>>();

	model.Bind("worlds", &m_entries);
	model.Bind("crates", &m_crates);
	model.Bind("mapgens", &m_mapgens);
	model.Bind("selected", &m_selected);
	model.Bind("delete_question", &m_delete_question);
	model.Bind("creating", &m_creating);
	model.Bind("confirm_delete", &m_confirm_delete);
	model.Bind("new_name", &m_new_name);
	model.Bind("new_seed", &m_new_seed);
	model.Bind("new_crate", &m_new_crate);
	model.Bind("new_mapgen", &m_new_mapgen);
	model.Bind("error", &m_error);

	model.BindEventCallback("select",
			[this](Rml::DataModelHandle handle, Rml::Event &, const Rml::VariantList &args) {
				if (args.empty())
					return;
				select(args[0].Get<int>(-1));
				m_confirm_delete = false;
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
	reloadWorlds();

	m_crates.clear();
	for (const CrateSpec &crate : getAvailableCrates())
		m_crates.push_back({crate.id, crate.title.empty() ? crate.id : crate.title});

	std::vector<const char *> names;
	Mapgen::getMapgenNames(&names, false);
	m_mapgens.assign(names.begin(), names.end());

	m_creating = false;
	m_confirm_delete = false;
	m_error.clear();
	model().DirtyAllVariables();
}

void WorldsScreen::reloadWorlds()
{
	m_worlds = getAvailableWorlds();
	m_entries.clear();
	for (const WorldSpec &world : m_worlds)
		m_entries.push_back({world.name, world.crateid});
	select(m_selected < (int)m_worlds.size() ? m_selected : (m_worlds.empty() ? -1 : 0));
}

void WorldsScreen::select(int index)
{
	m_selected = index >= 0 && (size_t)index < m_worlds.size() ? index : -1;
	m_delete_question = m_selected >= 0
			? fmtgettext("Delete the world \"%s\" for good?", m_worlds[m_selected].name.c_str())
			: "";
}

void WorldsScreen::beginCreate()
{
	m_creating = true;
	m_confirm_delete = false;
	m_error.clear();
	m_new_name.clear();
	m_new_seed.clear();

	std::string last;
	g_settings->getNoEx("menu_last_crate", last);
	const bool last_exists = std::any_of(m_crates.begin(), m_crates.end(),
			[&last](const CrateEntry &c) { return c.id == last; });
	m_new_crate = last_exists ? last : (m_crates.empty() ? "" : m_crates.front().id);

	std::string mapgen;
	g_settings->getNoEx("mg_name", mapgen);
	const bool mapgen_exists = std::find(m_mapgens.begin(), m_mapgens.end(), mapgen)
			!= m_mapgens.end();
	m_new_mapgen = mapgen_exists ? mapgen : (m_mapgens.empty() ? "" : m_mapgens.front());
}

void WorldsScreen::create()
{
	std::string name(trim(m_new_name));
	if (name.empty()) {
		// Безымянным мирам достаётся world<N> со следующим свободным номером.
		int max_num = 0;
		for (const WorldSpec &world : m_worlds) {
			if (world.name.rfind("world", 0) == 0)
				max_num = std::max(max_num, mystoi(world.name.substr(5), 0, 1 << 30));
		}
		name = "world" + std::to_string(max_num + 1);
	}
	for (const WorldSpec &world : m_worlds) {
		if (world.name == name) {
			m_error = fmtgettext("A world named \"%s\" already exists", name.c_str());
			return;
		}
	}
	if (m_new_crate.empty()) {
		m_error = strgettext("No crate selected");
		return;
	}

	std::unordered_map<std::string, std::string> settings;
	settings["fixed_map_seed"] = m_new_seed;
	settings["mg_name"] = m_new_mapgen;
	m_error = createWorld(name, m_new_crate, settings);
	if (!m_error.empty())
		return;

	g_settings->set("menu_last_crate", m_new_crate);
	m_creating = false;
	reloadWorlds();
	for (size_t i = 0; i < m_worlds.size(); i++) {
		if (m_worlds[i].name == name)
			select((int)i);
	}
}

void WorldsScreen::deleteSelected()
{
	m_confirm_delete = false;
	if (m_selected < 0 || (size_t)m_selected >= m_worlds.size())
		return;
	m_error = deleteWorld(m_worlds[m_selected]);
	reloadWorlds();
}

}
