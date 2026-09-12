// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#include "worlds_screen.h"

#include "client/menu/main_menu.h"

namespace menu
{

void WorldsScreen::bind(Rml::DataModelConstructor &model)
{
	if (auto entry = model.RegisterStruct<Entry>()) {
		entry.RegisterMember("name", &Entry::name);
		entry.RegisterMember("crate", &Entry::crate);
	}
	model.RegisterArray<std::vector<Entry>>();
	model.Bind("worlds", &m_entries);

	model.BindEventCallback("play",
			[this](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args) {
				if (args.empty())
					return;
				const int index = args[0].Get<int>(-1);
				if (index >= 0 && (size_t)index < m_worlds.size())
					menu().startSingleplayer(m_worlds[index]);
			});
}

void WorldsScreen::refresh()
{
	m_worlds = getAvailableWorlds();
	m_entries.clear();
	for (const WorldSpec &world : m_worlds)
		m_entries.push_back({world.name, world.crateid});
	model().DirtyVariable("worlds");
}

}
