// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#pragma once

#include "client/menu/screen.h"
#include "content/crates.h"
#include <vector>

namespace menu
{

// Свои миры: список и запуск одиночной игры.
class WorldsScreen final : public Screen
{
public:
	explicit WorldsScreen(MainMenu &menu) : Screen(menu, "worlds") {}

	void refresh() override;

protected:
	void bind(Rml::DataModelConstructor &model) override;

private:
	struct Entry
	{
		Rml::String name;
		Rml::String crate;
	};

	std::vector<WorldSpec> m_worlds;
	std::vector<Entry> m_entries;
};

}
