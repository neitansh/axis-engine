// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#pragma once

#include "client/menu/screen.h"
#include "content/crates.h"
#include <vector>

namespace menu
{

// Одиночная игра: сначала крейт, под ним его миры, справа — что делать с
// выбранным. Форма создания и подтверждение удаления живут в правой колонке
// и показываются по переменным модели, а не отдельными диалогами.
class WorldsScreen final : public Screen
{
public:
	explicit WorldsScreen(MainMenu &menu) : Screen(menu, "worlds") {}

	void refresh() override;
	bool onEvent(const SEvent &event) override;

protected:
	void bind(Rml::DataModelConstructor &model) override;

private:
	struct CrateEntry
	{
		Rml::String id;
		Rml::String title;
		Rml::String author;
		Rml::String icon;
		Rml::String initial;
		int worlds = 0;
	};

	struct WorldEntry
	{
		Rml::String name;
		Rml::String mapgen;
	};

	void reloadWorlds();
	void selectCrate(int index);
	void selectWorld(int index);
	void beginCreate();
	void create();
	void deleteSelected();
	std::vector<Rml::String> mapgensFor(const CrateSpec &crate) const;

	std::vector<CrateSpec> m_crate_specs;
	std::vector<WorldSpec> m_all_worlds;
	std::vector<WorldSpec> m_worlds;

	std::vector<CrateEntry> m_crates;
	std::vector<WorldEntry> m_entries;
	std::vector<Rml::String> m_mapgens;

	Rml::String m_crate_heading;
	Rml::String m_worlds_heading;
	int m_crate = -1;
	int m_selected = -1;
	Rml::String m_selected_name;
	Rml::String m_selected_mapgen;
	Rml::String m_delete_question;
	bool m_creating = false;
	bool m_confirm_delete = false;
	Rml::String m_new_name;
	Rml::String m_new_seed;
	Rml::String m_new_mapgen;
	Rml::String m_error;
};

}
