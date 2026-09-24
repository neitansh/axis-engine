// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#pragma once

#include "client/menu/main_menu.h"
#include <vector>

namespace menu
{

// Одиночная игра начинается с выбора плейса — крейта, во что играть:
// карточки с обложкой и описанием в ряд, лишние уходят за край и
// листаются. Выбранная карточка ведёт к мирам этого крейта (WorldsScreen
// берёт его из menu_last_crate).
class PlacesScreen final : public Screen
{
public:
	explicit PlacesScreen(MainMenu &menu) : Screen(menu, "places") {}

	void refresh() override;
	bool onEvent(const SEvent &event) override;
	std::vector<KeyHint> keys() const override;
	void afterUpdate() override;

protected:
	void bind(Rml::DataModelConstructor &model) override;

private:
	struct PlaceEntry
	{
		Rml::String id;
		Rml::String title;
		Rml::String author;
		Rml::String description;
		Rml::String cover;
		Rml::String initial;
		int worlds = 0;
	};

	void open(int index);
	void focusCard(int index);
	void page(int direction);
	Rml::Element *track();

	std::vector<PlaceEntry> m_places;
	int m_current = 0;
	bool m_overflow = false;
};

}
