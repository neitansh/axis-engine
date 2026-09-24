// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#include "places_screen.h"

#include "client/menu/keys.h"
#include "client/menu/main_menu.h"
#include "client/menu/text.h"
#include "content/crates.h"
#include "filesys.h"
#include "gettext.h"
#include "settings.h"
#include "util/string.h"
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <algorithm>

namespace menu
{

namespace
{

// Обложка — menu/cover.png крейта; без неё — screenshot.png, который
// кладут крейты по старому обычаю Luanti.
std::string coverOf(const CrateSpec &crate)
{
	for (const char *name : {"menu" DIR_DELIM "cover.png", "screenshot.png"}) {
		const std::string path = crate.path + DIR_DELIM + name;
		if (fs::PathExists(path))
			return path;
	}
	return "";
}

std::string descriptionOf(const CrateSpec &crate)
{
	Settings conf;
	std::string description;
	conf.readConfigFile((crate.path + DIR_DELIM "crate.conf").c_str());
	conf.getNoEx("description", description);
	return description;
}

}

void PlacesScreen::bind(Rml::DataModelConstructor &model)
{
	if (auto entry = model.RegisterStruct<PlaceEntry>()) {
		entry.RegisterMember("id", &PlaceEntry::id);
		entry.RegisterMember("title", &PlaceEntry::title);
		entry.RegisterMember("author", &PlaceEntry::author);
		entry.RegisterMember("description", &PlaceEntry::description);
		entry.RegisterMember("cover", &PlaceEntry::cover);
		entry.RegisterMember("cover_decorator", &PlaceEntry::cover_decorator);
		entry.RegisterMember("initial", &PlaceEntry::initial);
		entry.RegisterMember("worlds", &PlaceEntry::worlds);
	}
	model.RegisterArray<std::vector<PlaceEntry>>();
	model.Bind("places", &m_places);
	model.Bind("current", &m_current);
	model.Bind("overflow", &m_overflow);

	auto index_arg = [](const Rml::VariantList &args) {
		return args.empty() ? -1 : args[0].Get<int>(-1);
	};

	model.BindEventCallback("open",
			[this, index_arg](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args) {
				open(index_arg(args));
			});
	model.BindEventCallback("card_key",
			[this, index_arg](Rml::DataModelHandle, Rml::Event &event, const Rml::VariantList &args) {
				if (!isEnter(event))
					return;
				event.StopPropagation();
				open(index_arg(args));
			});
	// Наведение мышью переносит фокус (menu::Sounds), а фокус — это текущая
	// карточка: стрелки клавиатуры идут от той, что под мышью.
	model.BindEventCallback("card_focus",
			[this, index_arg](Rml::DataModelHandle handle, Rml::Event &, const Rml::VariantList &args) {
				m_current = index_arg(args);
				handle.DirtyVariable("current");
			});
	model.BindEventCallback("page",
			[this, index_arg](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args) {
				page(index_arg(args));
			});
}

void PlacesScreen::refresh()
{
	const std::vector<CrateSpec> crates = getAvailableCrates();
	const std::vector<WorldSpec> worlds = getAvailableWorlds();

	std::string last;
	g_settings->getNoEx("menu_last_crate", last);

	m_places.clear();
	m_current = 0;
	for (const CrateSpec &spec : crates) {
		PlaceEntry entry;
		entry.id = spec.id;
		entry.title = spec.title.empty() ? spec.id : spec.title;
		entry.author = spec.author;
		entry.description = descriptionOf(spec);
		entry.cover = coverOf(spec);
		// Декоратор собирается здесь, а не выражением в документе: data-style
		// вычисляется и у карточки без обложки, и «image( cover)» ищет файл cover.
		entry.cover_decorator = entry.cover.empty() ? "none" : "image(" + entry.cover + " cover)";
		const std::wstring wide = utf8_to_wide(entry.title);
		entry.initial = wide.empty() ? "" : uppercase(wide_to_utf8(wide.substr(0, 1)));
		entry.worlds = (int)std::count_if(worlds.begin(), worlds.end(),
				[&spec](const WorldSpec &w) {
					return w.crateid == spec.id || spec.aliases.count(w.crateid) > 0;
				});
		if (spec.id == last)
			m_current = (int)m_places.size();
		m_places.push_back(entry);
	}
	model().DirtyAllVariables();
	refocus();
}

bool PlacesScreen::onEvent(const SEvent &event)
{
	if (event.EventType == EET_MOUSE_INPUT_EVENT
			&& event.MouseInput.Event == EMIE_MOUSE_WHEEL) {
		if (Rml::Element *row = track())
			row->ScrollTo({row->GetScrollLeft() - event.MouseInput.Wheel * 160.0f, 0.0f},
					Rml::ScrollBehavior::Smooth);
		return true;
	}
	if (event.EventType != EET_KEY_INPUT_EVENT || !event.KeyInput.PressedDown)
		return false;
	switch (event.KeyInput.Key) {
	case KEY_ESCAPE:
		menu().navigate("start");
		return true;
	case KEY_LEFT:
		focusCard(m_current - 1);
		return true;
	case KEY_RIGHT:
		focusCard(m_current + 1);
		return true;
	default:
		return false;
	}
}

std::vector<Screen::KeyHint> PlacesScreen::keys() const
{
	return {{"←→", strgettext("Choose")}, {"Enter", strgettext("Open")},
			{"Esc", strgettext("Back")}};
}

void PlacesScreen::open(int index)
{
	if (index < 0 || (size_t)index >= m_places.size())
		return;
	g_settings->set("menu_last_crate", m_places[index].id);
	menu().navigate("worlds");
}

void PlacesScreen::focusCard(int index)
{
	if (m_places.empty())
		return;
	m_current = std::clamp(index, 0, (int)m_places.size() - 1);
	model().DirtyVariable("current");
	Rml::ElementList cards;
	document()->QuerySelectorAll(cards, ".place");
	int seen = 0;
	for (Rml::Element *card : cards) {
		if (!card->IsVisible(true))
			continue;
		if (seen++ == m_current) {
			card->Focus(true);
			card->ScrollIntoView(Rml::ScrollIntoViewOptions(Rml::ScrollAlignment::Nearest,
					Rml::ScrollAlignment::Nearest, Rml::ScrollBehavior::Smooth));
			return;
		}
	}
}

// Стрелки листания нужны, только когда ряд не влез; ширина известна лишь
// после раскладки.
void PlacesScreen::afterUpdate()
{
	Rml::Element *row = track();
	const bool overflow = row && row->GetScrollWidth() > row->GetClientWidth() + 1.0f;
	if (overflow != m_overflow) {
		m_overflow = overflow;
		model().DirtyVariable("overflow");
	}
}

// Стрелки по краям листают на ширину видимого ряда.
void PlacesScreen::page(int direction)
{
	if (Rml::Element *row = track())
		row->ScrollTo({row->GetScrollLeft() + direction * row->GetClientWidth(), 0.0f},
				Rml::ScrollBehavior::Smooth);
}

Rml::Element *PlacesScreen::track()
{
	return document() ? document()->GetElementById("track") : nullptr;
}

}
