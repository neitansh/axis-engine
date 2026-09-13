// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#pragma once

#include "client/menu/screen.h"
#include "client/menu/settings_catalog.h"
#include <vector>

namespace Rml
{
class Element;
class Event;
}

namespace menu
{

// Настройки: страницы слева, строки справа. Значение пишется в g_settings
// сразу, как в прежнем меню. Поиск ищет по всем страницам, «Расширенные»
// раскрывают остальное из разделов страницы.
//
// Виджет строки приходит в документ готовой разметкой (data-rml), а не
// собирается из data-attr: у RmlUi порядок применения атрибутов не задан, и
// ползунок с min/max/step, выставленными вразнобой, сам подгонял значение и
// присылал change — настройки менялись от одного показа экрана.
class SettingsScreen final : public Screen
{
public:
	explicit SettingsScreen(MainMenu &menu);

	void refresh() override;
	bool onEvent(const SEvent &event) override;
	void afterUpdate() override { m_armed = true; }

protected:
	void bind(Rml::DataModelConstructor &model) override;

private:
	struct PageEntry
	{
		Rml::String id;
		Rml::String title;
	};

	struct Section
	{
		int row = 0;
		Rml::String title;
	};

	struct Row
	{
		int index = 0;
		Rml::String name;
		Rml::String label;
		Rml::String help;
		Rml::String kind;
		Rml::String value;
		Rml::String widget;
		bool changed = false;
	};

	void rebuild();
	void appendItems(const std::vector<SettingsPage::Item> &items);
	bool matchesSearch(const SettingDef &def) const;
	bool isShown(const SettingDef &def) const;
	Row makeRow(const SettingDef &def) const;
	std::string makeWidget(const SettingDef &def, const std::string &value) const;
	std::string makeStepper(const std::vector<std::string> &labels, size_t current) const;
	void write(Row &row, const std::string &value, bool refresh_widget);
	void changed(int index, Rml::Event &event);
	void clicked(int index, Rml::Event &event);
	void step(Row &row, const SettingDef &def, int direction);
	void reset(int index);
	void resetPage();
	void focus(int index);
	void jump(int row);

	SettingsCatalog m_catalog;
	std::vector<PageEntry> m_pages;
	Rml::String m_page;
	Rml::String m_search;
	bool m_advanced = false;
	bool m_has_advanced = false;
	std::vector<Row> m_rows;
	std::vector<Section> m_sections;
	int m_section = -1;
	int m_capturing = -1;
	int m_focus = -1;
	Rml::String m_focus_label;
	Rml::String m_focus_help;
	Rml::String m_focus_value;
	Rml::String m_focus_options;
	// Пока строки строятся, виджеты шлют change сами по себе; до первого
	// обновления после rebuild() события не считаются.
	bool m_armed = false;
};

}
