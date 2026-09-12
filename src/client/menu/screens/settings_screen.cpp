// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#include "settings_screen.h"

#include "client/keycode.h"
#include "client/menu/main_menu.h"
#include "client/renderingengine.h"
#include "client/shadows/dynamicshadowsrender.h"
#include "gettext.h"
#include "settings.h"
#include "util/string.h"
#include <IrrlichtDevice.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementUtilities.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/StringUtilities.h>
#include <algorithm>
#include <cmath>

namespace menu
{

namespace
{

std::string readSetting(const SettingDef &def)
{
	std::string value;
	if (!g_settings->getNoEx(def.name, value))
		value = def.default_value;
	return value;
}

// Умолчание движка (defaultsettings.cpp), а не settingtypes.txt: они
// расходятся там, где Axis выставил своё, и строка не должна выглядеть
// изменённой из-за этого.
std::string defaultOf(const SettingDef &def)
{
	std::string value;
	Settings *defaults = Settings::getLayer(SL_DEFAULTS);
	if (defaults && defaults->getNoEx(def.name, value))
		return value;
	return def.default_value;
}

std::string formatNumber(double v, bool integer)
{
	if (integer)
		return std::to_string((long long)std::llround(v));
	char buf[64];
	snprintf(buf, sizeof(buf), "%.4g", v);
	return buf;
}

std::string attr(const std::string &value)
{
	return "\"" + Rml::StringUtilities::EncodeRml(value) + "\"";
}

bool isRanged(const SettingDef &def)
{
	// Ползунок только для диапазона, по которому им реально попасть: у
	// fps_max верхняя граница — 2^32, и ползунок там бесполезен.
	return def.min && def.max && *def.max - *def.min <= 100000;
}

double sliderStep(const SettingDef &def)
{
	if (def.kind == SettingDef::Kind::Int)
		return 1;
	const double span = *def.max - *def.min;
	if (span <= 2)
		return 0.01;
	if (span <= 20)
		return 0.1;
	return std::max(1.0, std::floor(span / 100));
}

}

SettingsScreen::SettingsScreen(MainMenu &menu) : Screen(menu, "settings")
{
	m_catalog.load();
	for (const SettingsPage &page : m_catalog.pages())
		m_pages.push_back({page.id, strgettext(page.title)});
	if (!m_pages.empty())
		m_page = m_pages.front().id;
}

void SettingsScreen::bind(Rml::DataModelConstructor &model)
{
	if (auto page = model.RegisterStruct<PageEntry>()) {
		page.RegisterMember("id", &PageEntry::id);
		page.RegisterMember("title", &PageEntry::title);
	}
	if (auto row = model.RegisterStruct<Row>()) {
		row.RegisterMember("index", &Row::index);
		row.RegisterMember("name", &Row::name);
		row.RegisterMember("label", &Row::label);
		row.RegisterMember("help", &Row::help);
		row.RegisterMember("kind", &Row::kind);
		row.RegisterMember("value", &Row::value);
		row.RegisterMember("widget", &Row::widget);
		row.RegisterMember("changed", &Row::changed);
	}
	if (auto section = model.RegisterStruct<Section>()) {
		section.RegisterMember("row", &Section::row);
		section.RegisterMember("title", &Section::title);
	}
	model.RegisterArray<std::vector<PageEntry>>();
	model.RegisterArray<std::vector<Row>>();
	model.RegisterArray<std::vector<Section>>();
	model.RegisterArray<std::vector<Rml::String>>();

	model.Bind("pages", &m_pages);
	model.Bind("page", &m_page);
	model.Bind("search", &m_search);
	model.Bind("advanced", &m_advanced);
	model.Bind("has_advanced", &m_has_advanced);
	model.Bind("rows", &m_rows);
	model.Bind("sections", &m_sections);
	model.Bind("section", &m_section);
	model.Bind("capturing", &m_capturing);
	model.Bind("focus", &m_focus);
	model.Bind("focus_label", &m_focus_label);
	model.Bind("focus_help", &m_focus_help);
	model.Bind("focus_value", &m_focus_value);
	model.Bind("focus_options", &m_focus_options);

	auto index_arg = [](const Rml::VariantList &args) {
		return args.empty() ? -1 : args[0].Get<int>(-1);
	};

	model.BindEventCallback("open",
			[this](Rml::DataModelHandle handle, Rml::Event &, const Rml::VariantList &args) {
				if (args.empty())
					return;
				m_page = args[0].Get<Rml::String>();
				m_search.clear();
				m_advanced = false;
				rebuild();
				handle.DirtyAllVariables();
			});
	model.BindEventCallback("search",
			[this](Rml::DataModelHandle handle, Rml::Event &, const Rml::VariantList &) {
				rebuild();
				handle.DirtyAllVariables();
			});
	model.BindEventCallback("toggle_advanced",
			[this](Rml::DataModelHandle handle, Rml::Event &, const Rml::VariantList &) {
				m_advanced = !m_advanced;
				rebuild();
				handle.DirtyAllVariables();
			});
	model.BindEventCallback("changed",
			[this, index_arg](Rml::DataModelHandle handle, Rml::Event &event, const Rml::VariantList &args) {
				changed(index_arg(args), event);
				handle.DirtyVariable("rows");
			});
	model.BindEventCallback("clicked",
			[this, index_arg](Rml::DataModelHandle handle, Rml::Event &event, const Rml::VariantList &args) {
				clicked(index_arg(args), event);
				handle.DirtyAllVariables();
			});
	model.BindEventCallback("reset",
			[this, index_arg](Rml::DataModelHandle handle, Rml::Event &, const Rml::VariantList &args) {
				reset(index_arg(args));
				handle.DirtyAllVariables();
			});
	model.BindEventCallback("reset_page",
			[this](Rml::DataModelHandle handle, Rml::Event &, const Rml::VariantList &) {
				resetPage();
				handle.DirtyAllVariables();
			});
	model.BindEventCallback("focus",
			[this, index_arg](Rml::DataModelHandle handle, Rml::Event &, const Rml::VariantList &args) {
				focus(index_arg(args));
				handle.DirtyAllVariables();
			});
	model.BindEventCallback("jump",
			[this, index_arg](Rml::DataModelHandle handle, Rml::Event &, const Rml::VariantList &args) {
				jump(index_arg(args));
				handle.DirtyVariable("section");
			});
	model.BindEventCallback("capture",
			[this, index_arg](Rml::DataModelHandle handle, Rml::Event &, const Rml::VariantList &args) {
				m_capturing = index_arg(args);
				handle.DirtyVariable("capturing");
			});
}

void SettingsScreen::refresh()
{
	m_capturing = -1;
	rebuild();
	model().DirtyAllVariables();
}

bool SettingsScreen::onEvent(const SEvent &event)
{
	if (m_capturing < 0 || (size_t)m_capturing >= m_rows.size())
		return false;

	KeyPress key;
	if (event.EventType == EET_KEY_INPUT_EVENT) {
		if (!event.KeyInput.PressedDown)
			return true;
		if (event.KeyInput.Key == KEY_ESCAPE) {
			m_capturing = -1;
			model().DirtyVariable("capturing");
			return true;
		}
		key = KeyPress(event.KeyInput);
	} else if (event.EventType == EET_MOUSE_INPUT_EVENT) {
		const auto e = event.MouseInput.Event;
		if (e != EMIE_LMOUSE_PRESSED_DOWN && e != EMIE_RMOUSE_PRESSED_DOWN
				&& e != EMIE_MMOUSE_PRESSED_DOWN && e != EMIE_XMOUSE_PRESSED_DOWN)
			return e != EMIE_MOUSE_MOVED;
		key = KeyPress(event.MouseInput);
	} else {
		return false;
	}

	Row &row = m_rows[m_capturing];
	m_capturing = -1;
	if (key)
		write(row, key.sym(), true);
	model().DirtyAllVariables();
	return true;
}

void SettingsScreen::rebuild()
{
	m_armed = false;
	m_rows.clear();
	m_has_advanced = false;

	const bool searching = !trim(m_search).empty();
	for (const SettingsPage &page : m_catalog.pages()) {
		if (!searching && page.id != m_page)
			continue;
		if (searching) {
			appendItems(page.basic);
			appendItems(page.advanced);
			continue;
		}
		appendItems(page.basic);
		for (const SettingsPage::Item &item : page.advanced) {
			if (item.setting.empty())
				continue;
			const SettingDef *def = m_catalog.find(item.setting);
			if (def && isShown(*def)) {
				m_has_advanced = true;
				break;
			}
		}
		if (m_advanced)
			appendItems(page.advanced);
	}
	m_sections.clear();
	for (size_t i = 0; i < m_rows.size(); i++) {
		m_rows[i].index = (int)i;
		if (m_rows[i].kind == "heading")
			m_sections.push_back({(int)i, m_rows[i].label});
	}
	m_section = m_sections.empty() ? -1 : m_sections.front().row;
	focus(m_focus);
}

void SettingsScreen::appendItems(const std::vector<SettingsPage::Item> &items)
{
	const bool searching = !trim(m_search).empty();
	const Row *pending_heading = nullptr;
	Row heading;
	for (const SettingsPage::Item &item : items) {
		if (!item.heading.empty()) {
			heading = Row();
			heading.kind = "heading";
			heading.label = strgettext(item.heading);
			pending_heading = &heading;
			continue;
		}
		const SettingDef *def = m_catalog.find(item.setting);
		if (!def || !isShown(*def))
			continue;
		if (searching && !matchesSearch(*def))
			continue;
		// Заголовок ставится только перед первой видимой строкой под ним;
		// при поиске заголовки не нужны.
		if (pending_heading && !searching) {
			m_rows.push_back(*pending_heading);
			pending_heading = nullptr;
		}
		m_rows.push_back(makeRow(*def));
	}
}

bool SettingsScreen::matchesSearch(const SettingDef &def) const
{
	const std::string needle = lowercase(std::string(trim(m_search)));
	if (needle.empty())
		return true;
	return lowercase(def.name).find(needle) != std::string::npos
			|| lowercase(strgettext(def.readable)).find(needle) != std::string::npos
			|| lowercase(def.readable).find(needle) != std::string::npos;
}

bool SettingsScreen::isShown(const SettingDef &def) const
{
	if (!def.context.empty() && def.context != "common" && def.context != "client"
			&& def.context != "server" && def.context != "world_creation")
		return false;

	IrrlichtDevice *device = RenderingEngine::get_raw_device();
	const bool touch_support = device->supportsTouchEvents();
	std::string touch_controls;
	g_settings->getNoEx("touch_controls", touch_controls);
	std::string touch_style;
	g_settings->getNoEx("touch_interaction_style", touch_style);
	const bool touch_auto = touch_controls == "auto";

	for (const auto &[req, wanted] : def.requires) {
		bool actual;
		if (req == "android")
			actual = false;
		else if (req == "desktop")
			actual = true;
		else if (req == "touch_support")
			actual = touch_support;
		else if (req == "touchscreen")
			actual = touch_support && (touch_auto || is_yes(touch_controls));
		else if (req == "keyboard_mouse")
			actual = !touch_support || touch_auto || !is_yes(touch_controls);
		else if (req == "touch_interaction_style_tap")
			actual = touch_style != "buttons_crosshair";
		else if (req == "shadows_support")
			actual = ShadowRenderer::isSupported(device->getVideoDriver());
		else {
			const SettingDef *other = m_catalog.find(req);
			std::string value;
			if (!g_settings->getNoEx(req, value))
				value = other ? other->default_value : "false";
			actual = is_yes(value);
		}
		if (actual != wanted)
			return false;
	}
	return true;
}

std::string SettingsScreen::makeWidget(const SettingDef &def, const std::string &value) const
{
	std::string rml;
	switch (def.kind) {
	case SettingDef::Kind::Bool:
		rml = makeStepper({strgettext("Disabled"), strgettext("Enabled")}, is_yes(value) ? 1 : 0);
		break;
	case SettingDef::Kind::Int:
	case SettingDef::Kind::Float:
		if (isRanged(def)) {
			rml += "<input type=\"range\" min=" + attr(formatNumber(*def.min, false))
					+ " max=" + attr(formatNumber(*def.max, false))
					+ " step=" + attr(formatNumber(sliderStep(def), false))
					+ " value=" + attr(value) + "/>";
		}
		rml += "<input type=\"text\" class=\"num\" value=" + attr(value) + "/>";
		break;
	case SettingDef::Kind::Enum: {
		const auto it = std::find(def.values.begin(), def.values.end(), value);
		rml = makeStepper(def.values,
				it == def.values.end() ? 0 : (size_t)(it - def.values.begin()));
		break;
	}
	case SettingDef::Kind::Flags: {
		const std::vector<std::string> set = str_split(value, ',');
		rml = "<div class=\"flags\">";
		for (const std::string &flag : def.values) {
			const bool on = std::any_of(set.begin(), set.end(),
					[&flag](const std::string &s) { return trim(s) == flag; });
			rml += "<label><input type=\"checkbox\" value=" + attr(flag)
					+ (on ? " checked" : "") + "/>"
					+ Rml::StringUtilities::EncodeRml(flag) + "</label>";
		}
		rml += "</div>";
		break;
	}
	case SettingDef::Kind::Key:
		break;
	case SettingDef::Kind::Path:
	case SettingDef::Kind::FilePath:
	case SettingDef::Kind::String:
	case SettingDef::Kind::V3f:
	case SettingDef::Kind::NoiseParams:
		rml = "<input type=\"text\" class=\"wide\" value=" + attr(value) + "/>";
		break;
	}
	return rml;
}

std::string SettingsScreen::makeStepper(const std::vector<std::string> &labels,
		size_t current) const
{
	std::string rml = "<div class=\"stepper\"><div class=\"arrow prev\">‹</div>"
			"<div class=\"middle\"><div class=\"val\">";
	if (current < labels.size())
		rml += Rml::StringUtilities::EncodeRml(labels[current]);
	rml += "</div><div class=\"segments\">";
	for (size_t i = 0; i < labels.size(); i++)
		rml += i == current ? "<i class=\"on\"/>" : "<i/>";
	rml += "</div></div><div class=\"arrow next\">›</div></div>";
	return rml;
}

SettingsScreen::Row SettingsScreen::makeRow(const SettingDef &def) const
{
	Row row;
	row.name = def.name;
	row.label = def.readable.empty() ? def.name : strgettext(def.readable);
	row.help = def.comment.empty() ? "" : strgettext(def.comment);
	row.value = readSetting(def);
	row.changed = row.value != defaultOf(def);
	row.widget = makeWidget(def, row.value);

	switch (def.kind) {
	case SettingDef::Kind::Bool: row.kind = "bool"; break;
	case SettingDef::Kind::Int:
	case SettingDef::Kind::Float: row.kind = "number"; break;
	case SettingDef::Kind::Enum: row.kind = "enum"; break;
	case SettingDef::Kind::Flags: row.kind = "flags"; break;
	case SettingDef::Kind::Key:
		row.kind = "key";
		row.value = KeyPress(row.value).name();
		break;
	default: row.kind = "text"; break;
	}
	return row;
}

void SettingsScreen::write(Row &row, const std::string &value, bool refresh_widget)
{
	const SettingDef *def = m_catalog.find(row.name);
	if (!def)
		return;
	g_settings->set(def->name, value);
	if (def->kind == SettingDef::Kind::Key)
		clearKeyCache();

	const int index = row.index;
	// Виджет, который сам прислал новое значение, уже его показывает;
	// пересобирать его разметку значило бы уронить фокус из-под пальцев.
	const std::string widget = row.widget;
	row = makeRow(*def);
	row.index = index;
	if (!refresh_widget)
		row.widget = widget;
}

void SettingsScreen::changed(int index, Rml::Event &event)
{
	if (!m_armed || index < 0 || (size_t)index >= m_rows.size())
		return;
	Row &row = m_rows[index];
	const SettingDef *def = m_catalog.find(row.name);
	Rml::Element *target = event.GetTargetElement();
	if (!def || !target)
		return;

	const std::string tag = target->GetTagName();
	const std::string type = target->GetAttribute<Rml::String>("type", "");
	const std::string current = readSetting(*def);

	if (def->kind == SettingDef::Kind::Bool) {
		// Значение берётся из настроек, а не из виджета: порядок событий
		// у RmlUi не обещан, а инверсия всегда верна.
		write(row, is_yes(current) ? "false" : "true", false);
		// От логического флага могут зависеть другие строки (requires).
		rebuild();
		return;
	}

	if (def->kind == SettingDef::Kind::Flags) {
		const std::string flag = target->GetAttribute<Rml::String>("value", "");
		std::vector<std::string> set;
		for (const std::string &s : str_split(current, ','))
			if (!trim(s).empty())
				set.emplace_back(trim(s));
		auto it = std::find(set.begin(), set.end(), flag);
		if (it == set.end())
			set.push_back(flag);
		else
			set.erase(it);
		std::string joined;
		for (const std::string &s : set) {
			if (!joined.empty())
				joined += ',';
			joined += s;
		}
		write(row, joined, false);
		return;
	}

	std::string value(trim(event.GetParameter<Rml::String>("value", "")));
	if (def->kind == SettingDef::Kind::Int || def->kind == SettingDef::Kind::Float) {
		const bool from_slider = type == "range";
		const bool submitted = from_slider || event.GetParameter<bool>("linebreak", false);
		char *end = nullptr;
		double v = strtod(value.c_str(), &end);
		if (value.empty() || !end || *end != '\0')
			return;
		const bool below = def->min && v < *def->min;
		const bool above = def->max && v > *def->max;
		// Пока текст набирают, промежуточные значения вне диапазона не
		// записываются и не поправляются: «1» на пути к «16» — не ошибка.
		if ((below || above) && !submitted)
			return;
		if (below)
			v = *def->min;
		if (above)
			v = *def->max;
		value = formatNumber(v, def->kind == SettingDef::Kind::Int);

		// Соседний виджет той же строки показывает то же число: ползунок
		// правит поле и наоборот.
		Rml::Element *box = target->GetParentNode();
		if (box) {
			for (int i = 0; i < box->GetNumChildren(); i++) {
				Rml::Element *sibling = box->GetChild(i);
				if (sibling == target)
					continue;
				if (sibling->GetTagName() == "input")
					sibling->SetAttribute("value", value);
			}
		}
		if (submitted && !from_slider)
			target->SetAttribute("value", value);
	} else if (def->kind == SettingDef::Kind::Enum) {
		if (std::find(def->values.begin(), def->values.end(), value) == def->values.end())
			return;
	}

	if (value == current)
		return;
	write(row, value, false);
}

void SettingsScreen::reset(int index)
{
	if (index < 0 || (size_t)index >= m_rows.size())
		return;
	Row &row = m_rows[index];
	const SettingDef *def = m_catalog.find(row.name);
	if (!def)
		return;
	g_settings->remove(def->name);
	if (def->kind == SettingDef::Kind::Key)
		clearKeyCache();
	row = makeRow(*def);
	row.index = index;
	if (def->kind == SettingDef::Kind::Bool)
		rebuild();
	focus(index);
}

void SettingsScreen::clicked(int index, Rml::Event &event)
{
	if (index < 0 || (size_t)index >= m_rows.size())
		return;
	Rml::Element *target = event.GetTargetElement();
	if (!target)
		return;
	Row &row = m_rows[index];
	const SettingDef *def = m_catalog.find(row.name);
	if (!def)
		return;
	if (target->IsClassSet("prev"))
		step(row, *def, -1);
	else if (target->IsClassSet("next"))
		step(row, *def, 1);
}

void SettingsScreen::step(Row &row, const SettingDef &def, int direction)
{
	const std::string current = readSetting(def);
	if (def.kind == SettingDef::Kind::Bool) {
		write(row, is_yes(current) ? "false" : "true", true);
		rebuild();
		return;
	}
	if (def.kind != SettingDef::Kind::Enum || def.values.empty())
		return;
	const auto it = std::find(def.values.begin(), def.values.end(), current);
	int pos = it == def.values.end() ? 0 : (int)(it - def.values.begin());
	const int count = (int)def.values.size();
	pos = ((pos + direction) % count + count) % count;
	write(row, def.values[pos], true);
}

void SettingsScreen::resetPage()
{
	for (const Row &row : m_rows) {
		if (row.kind == "heading")
			continue;
		g_settings->remove(row.name);
	}
	clearKeyCache();
	rebuild();
}

void SettingsScreen::focus(int index)
{
	if (index < 0 || (size_t)index >= m_rows.size() || m_rows[index].kind == "heading") {
		m_focus = -1;
		m_focus_label.clear();
		m_focus_help.clear();
		m_focus_value.clear();
		m_focus_options.clear();
		return;
	}
	const Row &row = m_rows[index];
	const SettingDef *def = m_catalog.find(row.name);
	m_focus = index;
	m_focus_label = row.label;
	m_focus_help = row.help;
	m_focus_value = row.value;
	m_focus_options.clear();
	if (!def)
		return;
	if (def->kind == SettingDef::Kind::Bool) {
		m_focus_options = {strgettext("Disabled"), strgettext("Enabled")};
		m_focus_value = is_yes(row.value) ? m_focus_options[1] : m_focus_options[0];
	} else if (def->kind == SettingDef::Kind::Enum) {
		m_focus_options.assign(def->values.begin(), def->values.end());
	}
	// Раздел слева подсвечивается по ближайшему заголовку сверху.
	for (const Section &section : m_sections) {
		if (section.row < index)
			m_section = section.row;
	}
}

void SettingsScreen::jump(int row)
{
	m_section = row;
	if (!document())
		return;
	Rml::Element *heading = document()->GetElementById("row-" + std::to_string(row));
	if (heading)
		heading->ScrollIntoView(Rml::ScrollAlignment::Start);
}

}
