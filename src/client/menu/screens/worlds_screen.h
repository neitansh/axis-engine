// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#pragma once

#include "client/menu/screen.h"
#include "content/crates.h"
#include <vector>

namespace menu
{

// Своя игра: список миров, запуск, создание и удаление. Форма создания и
// подтверждение удаления живут в том же документе и показываются по
// переменным модели, а не отдельными диалогами.
class WorldsScreen final : public Screen
{
public:
	explicit WorldsScreen(MainMenu &menu) : Screen(menu, "worlds") {}

	void refresh() override;

protected:
	void bind(Rml::DataModelConstructor &model) override;

private:
	struct WorldEntry
	{
		Rml::String name;
		Rml::String crate;
	};

	struct CrateEntry
	{
		Rml::String id;
		Rml::String title;
	};

	void reloadWorlds();
	void select(int index);
	void beginCreate();
	void create();
	void deleteSelected();

	std::vector<WorldSpec> m_worlds;
	std::vector<WorldEntry> m_entries;
	std::vector<CrateEntry> m_crates;
	std::vector<Rml::String> m_mapgens;

	int m_selected = -1;
	Rml::String m_delete_question;
	bool m_creating = false;
	bool m_confirm_delete = false;
	Rml::String m_new_name;
	Rml::String m_new_seed;
	Rml::String m_new_crate;
	Rml::String m_new_mapgen;
	Rml::String m_error;
};

}
