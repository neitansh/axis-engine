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

// Настройки: вкладки страниц сверху, строки в середине, описание строки под
// курсором справа. Значение пишется в g_settings сразу, как в прежнем меню.
// Поиск ищет по всем страницам, «Расширенные» раскрывают остальное из
// разделов страницы.
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
	void afterUpdate() override;

protected:
	void bind(Rml::DataModelConstructor &model) override;

private:
	struct PageEntry
	{
		Rml::String id;
		Rml::String title;
	};

	struct Row
	{
		int index = 0;
		Rml::String name;
		Rml::String label;
		Rml::String kind;
		Rml::String value;
		Rml::String widget;
		bool changed = false;
	};

	void rebuild();
	void appendItems(const std::vector<SettingsPage::Item> &items);
	bool isShown(const SettingDef &def) const;
	Row makeRow(const SettingDef &def) const;
	Row makeSpecialRow(const std::string &name) const;
	std::string makeWidget(const SettingDef &def, const std::string &value) const;
	std::string makeStepper(const std::vector<std::string> &labels, size_t current) const;
	std::string makeCrosshairWidget() const;
	std::string optionLabel(const SettingDef &def, const std::string &value) const;
	void write(Row &row, const std::string &value, bool refresh_widget);
	void refreshRow(Row &row);
	void refreshAll();
	void changed(int index, Rml::Event &event);
	void clicked(int index, Rml::Event &event);
	void step(Row &row, const SettingDef &def, int direction);
	void stepSpecial(Row &row, int direction);
	void crosshairChanged(Rml::Element *target, const std::string &value);
	void crosshairClicked(Rml::Element *target);
	void reset(int index);
	void focus(int index);
	void animateNewRows();

	SettingsCatalog m_catalog;
	std::vector<PageEntry> m_pages;
	Rml::String m_page;
	Rml::String m_tab_indicator;
	std::vector<Row> m_rows;
	int m_capturing = -1;
	int m_focus = -1;
	Rml::String m_focus_label;
	Rml::String m_focus_help;
	Rml::String m_focus_load;
	Rml::String m_focus_load_label;
	Rml::String m_focus_load_class;
	Rml::String m_focus_note;
	Rml::String m_crosshair_status;
	// Пока строки строятся, виджеты шлют change сами по себе; до первого
	// обновления после rebuild() события не считаются.
	bool m_armed = false;
};

}
