// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#include "settings_screen.h"

#include "client/keycode.h"
#include "client/menu/main_menu.h"
#include "client/menu/settings_presets.h"
#include "client/menu/text.h"
#include "client/renderingengine.h"
#include "client/shadows/dynamicshadowsrender.h"
#include "crosshair.h"
#include "gettext.h"
#include "settings.h"
#include "util/string.h"
#include <IOSOperator.h>
#include <IrrlichtDevice.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
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

std::string text(const std::string &value)
{
	return Rml::StringUtilities::EncodeRml(value);
}

// Ползунок только для диапазона, по которому им реально попасть: порт с
// верхней границей 65535 ползунком не выставить.
bool isRanged(const SettingDef &def)
{
	return def.min && def.max && *def.max - *def.min <= 1000;
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
	return 1;
}

// Описание для игрока: своё из descriptions.lua, иначе подсказка движка со
// сшитыми переносами — они расставлены под ширину файла, а не окна.
std::string describe(const SettingDef &def)
{
	if (!def.description.empty())
		return strgettext(def.description);
	if (def.comment.empty())
		return "";
	std::string out;
	for (const std::string &line : str_split(strgettext(def.comment), '\n')) {
		const std::string_view piece = trim(line);
		if (piece.empty())
			continue;
		if (!out.empty())
			out += ' ';
		out += piece;
	}
	return out;
}

const char *loadWord(const std::string &load)
{
	if (load == "high")
		return "high";
	if (load == "medium")
		return "moderate";
	if (load == "low")
		return "light";
	return "";
}

// Перекрестье. Наборы задают форму, цвет игрок выбирает отдельно и при смене
// формы его не теряет.
struct CrosshairPreset
{
	const char *name;
	const char *code;
};

const CrosshairPreset CROSSHAIR_PRESETS[] = {
	{"Cross", "AXCH1-cross-6-2-3-0-0"},
	{"Cross, no gap", "AXCH1-cross-7-2-0-0-0"},
	{"Thick cross", "AXCH1-cross-4-3-0-0-0"},
	{"Ticks", "AXCH1-cross-3-2-5-0-0"},
	{"Cross with dot", "AXCH1-cross-6-2-3-2-0"},
	{"Dot", "AXCH1-dot-3-1-0-0-0"},
	{"Small dot", "AXCH1-dot-1-1-0-0-0"},
	{"Ring with dot", "AXCH1-circle-5-1-0-1-0"},
	{"Frame with dot", "AXCH1-square-5-1-0-1-0"},
	{"Brackets", "AXCH1-brackets-4-2-4-0-0"},
	{"Diagonal cross", "AXCH1-x-5-2-2-0-0"},
	{"Nothing", "AXCH1-none-0-1-0-0-0"},
};

// Первые семь полей кода — форма, дальше цвета.
std::string codeShape(const std::string &code)
{
	size_t pos = 0;
	for (int i = 0; i < 7 && pos != std::string::npos; i++)
		pos = code.find('-', pos + 1);
	return pos == std::string::npos ? code : code.substr(0, pos);
}

std::string codeColors(const std::string &code)
{
	const std::string shape = codeShape(code);
	return code.size() > shape.size() ? code.substr(shape.size()) : "";
}

int crosshairPreset(const std::string &code)
{
	const std::string shape = codeShape(code);
	for (size_t i = 0; i < std::size(CROSSHAIR_PRESETS); i++) {
		if (shape == CROSSHAIR_PRESETS[i].code)
			return (int)i + 1;
	}
	return 0;
}

std::string hexOf(video::SColor c)
{
	char buf[16];
	snprintf(buf, sizeof(buf), "#%02x%02x%02x", (unsigned)c.getRed(),
			(unsigned)c.getGreen(), (unsigned)c.getBlue());
	return buf;
}

// Принимается то, что люди пишут: #ff8800, ff8800, #f80. Остальное — отказ.
bool parseHex(std::string in, unsigned &r, unsigned &g, unsigned &b)
{
	in = std::string(trim(in));
	if (!in.empty() && in[0] == '#')
		in.erase(0, 1);
	if (in.size() == 3) {
		std::string full;
		for (char c : in)
			full += std::string(2, c);
		in = full;
	}
	if (in.size() != 6)
		return false;
	for (char c : in) {
		if (!isxdigit((unsigned char)c))
			return false;
	}
	r = std::stoul(in.substr(0, 2), nullptr, 16);
	g = std::stoul(in.substr(2, 2), nullptr, 16);
	b = std::stoul(in.substr(4, 2), nullptr, 16);
	return true;
}

}

SettingsScreen::SettingsScreen(MainMenu &menu) : Screen(menu, "settings")
{
	m_catalog.load();
	for (const SettingsPage &page : m_catalog.pages())
		m_pages.push_back({page.id, strgettext(page.title)});
	if (!m_pages.empty())
		m_page = m_pages.front().id;
	m_tab_indicator = "translateX(0dp)";
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
		row.RegisterMember("kind", &Row::kind);
		row.RegisterMember("value", &Row::value);
		row.RegisterMember("widget", &Row::widget);
		row.RegisterMember("long_label", &Row::long_label);
		row.RegisterMember("changed", &Row::changed);
	}
	model.RegisterArray<std::vector<PageEntry>>();
	model.RegisterArray<std::vector<Row>>();

	model.Bind("pages", &m_pages);
	model.Bind("page", &m_page);
	model.Bind("tab_indicator", &m_tab_indicator);
	model.Bind("rows", &m_rows);
	model.Bind("capturing", &m_capturing);
	model.Bind("key_prompt", &m_key_prompt);
	model.Bind("focus", &m_focus);
	model.Bind("focus_label", &m_focus_label);
	model.Bind("focus_help", &m_focus_help);
	model.Bind("focus_load", &m_focus_load);
	model.Bind("focus_load_label", &m_focus_load_label);
	model.Bind("focus_load_class", &m_focus_load_class);
	model.Bind("focus_note", &m_focus_note);
	model.Bind("crosshair_status", &m_crosshair_status);

	auto index_arg = [](const Rml::VariantList &args) {
		return args.empty() ? -1 : args[0].Get<int>(-1);
	};

	model.BindEventCallback("open",
			[this](Rml::DataModelHandle handle, Rml::Event &, const Rml::VariantList &args) {
				if (args.empty())
					return;
				m_page = args[0].Get<Rml::String>();
				rebuild();
				handle.DirtyAllVariables();
			});
	model.BindEventCallback("changed",
			[this, index_arg](Rml::DataModelHandle handle, Rml::Event &event, const Rml::VariantList &args) {
				changed(index_arg(args), event);
				handle.DirtyAllVariables();
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
	model.BindEventCallback("focus",
			[this, index_arg](Rml::DataModelHandle handle, Rml::Event &, const Rml::VariantList &args) {
				focus(index_arg(args));
				handle.DirtyAllVariables();
			});
	model.BindEventCallback("capture",
			[this, index_arg](Rml::DataModelHandle handle, Rml::Event &, const Rml::VariantList &args) {
				m_capturing = index_arg(args);
				handle.DirtyVariable("capturing");
			});
}

void SettingsScreen::afterUpdate()
{
	m_armed = true;
}

void SettingsScreen::refresh()
{
	m_capturing = -1;
	m_crosshair_status.clear();
	rebuild();
	model().DirtyAllVariables();
}

bool SettingsScreen::onEvent(const SEvent &event)
{
	const bool capturing = m_capturing >= 0 && (size_t)m_capturing < m_rows.size();
	if (!capturing) {
		if (event.EventType == EET_KEY_INPUT_EVENT && event.KeyInput.PressedDown
				&& event.KeyInput.Key == KEY_ESCAPE) {
			menu().navigate("start");
			return true;
		}
		return false;
	}

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

	// Подсветка едет к открытой вкладке: шаг — ширина вкладки с полями
	// (settings.rcss, .tabs .tab).
	for (size_t i = 0; i < m_pages.size(); i++) {
		if (m_pages[i].id == m_page)
			m_tab_indicator = "translateX(" + std::to_string(i * 56) + "dp)";
	}

	for (const SettingsPage &page : m_catalog.pages()) {
		if (page.id != m_page)
			continue;
		appendItems(page.basic);
		// Расширенные идут следом под своим заголовком, без раскрытия; у
		// страницы без избранного они и есть всё содержимое.
		bool has_basic = false;
		for (const SettingsPage::Item &item : page.basic)
			has_basic = has_basic || !item.setting.empty();
		if (has_basic) {
			std::vector<SettingsPage::Item> advanced = page.advanced;
			advanced.insert(advanced.begin(), {"Advanced settings", ""});
			appendItems(advanced);
		} else {
			appendItems(page.advanced);
		}
	}
	for (size_t i = 0; i < m_rows.size(); i++)
		m_rows[i].index = (int)i;
	focus(m_focus);
}

void SettingsScreen::appendItems(const std::vector<SettingsPage::Item> &items)
{
	const Row *pending_heading = nullptr;
	Row heading;
	for (const SettingsPage::Item &item : items) {
		if (!item.heading.empty()) {
			heading = Row();
			heading.kind = "heading";
			heading.label = uppercase(strgettext(item.heading));
			pending_heading = &heading;
			continue;
		}

		Row row;
		if (!item.setting.empty() && item.setting[0] == '@') {
			row = makeSpecialRow(item.setting);
			if (row.kind.empty())
				continue;
		} else {
			const SettingDef *def = m_catalog.find(item.setting);
			if (!def || !isShown(*def))
				continue;
			row = makeRow(*def);
		}
		// Заголовок ставится только перед первой видимой строкой под ним.
		if (pending_heading) {
			m_rows.push_back(*pending_heading);
			pending_heading = nullptr;
		}
		m_rows.push_back(std::move(row));
	}
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

std::string SettingsScreen::optionLabel(const SettingDef &def, const std::string &value) const
{
	auto it = def.option_labels.find(value);
	if (it != def.option_labels.end())
		return strgettext(it->second);
	return value;
}

std::string SettingsScreen::makeStepper(const std::vector<std::string> &labels,
		size_t current) const
{
	std::string rml = "<div class=\"stepper\"><div class=\"arrow prev\"/>"
			"<div class=\"middle\"><div class=\"val\">";
	if (current < labels.size())
		rml += text(labels[current]);
	rml += "</div><div class=\"segments\">";
	// Полсотни языков чертой из отрезков не показать: отрезки сливаются.
	if (labels.size() <= 12) {
		for (size_t i = 0; i < labels.size(); i++)
			rml += i == current ? "<i class=\"on\"/>" : "<i/>";
	}
	rml += "</div></div><div class=\"arrow next\"/></div>";
	return rml;
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
		// Длинный ряд (языки) стрелками не пролистать — ему список.
		if (def.values.size() > 8) {
			rml = "<select>";
			for (const std::string &option : def.values) {
				rml += "<option value=" + attr(option) + (option == value ? " selected" : "")
						+ ">" + text(optionLabel(def, option)) + "</option>";
			}
			rml += "</select>";
			break;
		}
		std::vector<std::string> labels;
		for (const std::string &option : def.values)
			labels.push_back(optionLabel(def, option));
		const auto it = std::find(def.values.begin(), def.values.end(), value);
		rml = makeStepper(labels, it == def.values.end() ? 0 : (size_t)(it - def.values.begin()));
		break;
	}
	case SettingDef::Kind::Flags: {
		const std::vector<std::string> set = str_split(value, ',');
		rml = "<div class=\"flags\">";
		for (const std::string &flag : def.values) {
			const bool on = std::any_of(set.begin(), set.end(),
					[&flag](const std::string &s) { return trim(s) == flag; });
			rml += "<label><input type=\"checkbox\" value=" + attr(flag)
					+ (on ? " checked" : "") + "/>" + text(flag) + "</label>";
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

std::string SettingsScreen::makeCrosshairWidget() const
{
	const CrosshairStyle style = CrosshairStyle::fromSettings(g_settings);
	const std::string code = style.toCode();

	// Предпросмотр крупнее настоящего перекрестья: его разглядывают, а не
	// целятся им. Геометрию в увеличенном масштабе считает сам движок, как
	// для большого экрана: масштабировать готовые прямоугольники нельзя —
	// штрих нечётной толщины уехал бы от центра на половину масштаба.
	const int box = 120;
	const float scale = 3.0f;
	// Середина бокса из 120 пикселей лежит между 59-м и 60-м, а не на 60-м.
	const float origin = box / 2.0f - 0.5f;
	std::string rml = "<div class=\"xhair\"><div class=\"xhair-preview\">";
	auto dp = [](float v) {
		char buf[32];
		snprintf(buf, sizeof(buf), "%.1fdp", v);
		return std::string(buf);
	};
	auto pieces = [&](const std::vector<CrosshairStyle::Piece> &list, video::SColor color) {
		for (const auto &p : list) {
			const float x = std::max(origin + p.x, 0.0f), y = std::max(origin + p.y, 0.0f);
			const float x2 = std::min(origin + p.x + p.w, (float)box);
			const float y2 = std::min(origin + p.y + p.h, (float)box);
			if (x2 <= x || y2 <= y)
				continue;
			rml += "<div class=\"piece\" style=\"left:" + dp(x) + ";top:" + dp(y)
					+ ";width:" + dp(x2 - x) + ";height:" + dp(y2 - y)
					+ ";background-color:" + hexOf(color) + ";\"/>";
		}
	};
	pieces(style.outlinePieces(scale), style.outline_color);
	pieces(style.pieces(scale), style.color);
	rml += "</div><div class=\"xhair-fields\">";

	std::vector<std::string> names = {strgettext("Custom")};
	for (const CrosshairPreset &preset : CROSSHAIR_PRESETS)
		names.push_back(strgettext(preset.name));
	rml += "<div class=\"field\"><span>" + text(strgettext("Preset")) + "</span>"
			+ makeStepper(names, crosshairPreset(code)) + "</div>";

	auto color_field = [&](const char *label, const char *setting, video::SColor color) {
		rml += "<div class=\"field\"><span>" + text(strgettext(label)) + "</span>"
				"<input type=\"text\" class=\"color\" name=" + attr(setting)
				+ " value=" + attr(hexOf(color)) + "/>"
				"<div class=\"swatch\" style=\"background-color:" + hexOf(color) + ";\"/></div>";
	};
	color_field("Color", "crosshair_color", style.color);
	color_field("Outline", "crosshair_outline_color", style.outline_color);
	color_field("On target", "crosshair_object_color", style.object_color);

	rml += "<div class=\"field share\"><span>" + text(strgettext("Share")) + "</span>"
			"<div class=\"button copy\">" + text(strgettext("Copy")) + "</div>"
			"<div class=\"button paste\">" + text(strgettext("Paste")) + "</div></div>";
	rml += "</div></div>";
	return rml;
}

SettingsScreen::Row SettingsScreen::makeRow(const SettingDef &def) const
{
	Row row;
	row.name = def.name;
	row.label = def.readable.empty() ? def.name : strgettext(def.readable);
	row.long_label = utf8_to_wide(row.label).size() > 34;
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

SettingsScreen::Row SettingsScreen::makeSpecialRow(const std::string &name) const
{
	Row row;
	row.name = name;
	if (name == "@quality" || name == "@shadows") {
		const bool quality = name == "@quality";
		if (!quality) {
			IrrlichtDevice *device = RenderingEngine::get_raw_device();
			if (!ShadowRenderer::isSupported(device->getVideoDriver()))
				return row;
			// Пока тени выключены, набора нет: за это отвечает переключатель
			// ниже, а в списке игрок не увидел бы, что предлагается.
			if (!g_settings->getBool("enable_dynamic_shadows"))
				return row;
		}
		const PresetGroup &group = quality ? qualityPresets() : shadowPresets();
		const int current = group.detect();
		std::vector<std::string> labels;
		for (const std::string &label : group.labels)
			labels.push_back(strgettext(label));
		// «Свои» показываются, только когда они и есть: иначе это пункт,
		// который нечего выбирать.
		if ((size_t)current + 1 != labels.size())
			labels.pop_back();
		row.kind = "preset";
		row.label = quality ? strgettext("Quality preset") : strgettext("Shadows");
		row.value = labels[current];
		row.widget = makeStepper(labels, current);
		return row;
	}
	if (name == "@crosshair") {
		row.kind = "crosshair";
		row.label = strgettext("Crosshair");
		row.widget = makeCrosshairWidget();
		return row;
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

	// Виджет, который сам прислал новое значение, уже его показывает;
	// пересобирать его разметку значило бы уронить фокус из-под пальцев.
	const std::string widget = row.widget;
	refreshAll();
	if (!refresh_widget)
		row.widget = widget;
}

void SettingsScreen::refreshRow(Row &row)
{
	const int index = row.index;
	if (!row.name.empty() && row.name[0] == '@')
		row = makeSpecialRow(row.name);
	else if (const SettingDef *def = m_catalog.find(row.name))
		row = makeRow(*def);
	row.index = index;
}

// Одна настройка меняет другие строки: наборы качества сравнивают себя с
// десятками значений, перекрестье рисуется из шести настроек.
void SettingsScreen::refreshAll()
{
	for (Row &row : m_rows) {
		if (row.kind != "heading")
			refreshRow(row);
	}
	focus(m_focus);
}

void SettingsScreen::changed(int index, Rml::Event &event)
{
	if (!m_armed || index < 0 || (size_t)index >= m_rows.size())
		return;
	Row &row = m_rows[index];
	Rml::Element *target = event.GetTargetElement();
	if (!target)
		return;
	std::string value(trim(event.GetParameter<Rml::String>("value", "")));

	if (row.kind == "crosshair") {
		if (event.GetParameter<bool>("linebreak", false))
			crosshairChanged(target, value);
		return;
	}

	const SettingDef *def = m_catalog.find(row.name);
	if (!def)
		return;
	const std::string current = readSetting(*def);
	const std::string type = target->GetAttribute<Rml::String>("type", "");

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

	if (def->kind == SettingDef::Kind::Enum) {
		if (std::find(def->values.begin(), def->values.end(), value) == def->values.end())
			return;
	} else if (def->kind == SettingDef::Kind::Int || def->kind == SettingDef::Kind::Float) {
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
		if (Rml::Element *box = target->GetParentNode()) {
			for (int i = 0; i < box->GetNumChildren(); i++) {
				Rml::Element *sibling = box->GetChild(i);
				if (sibling != target && sibling->GetTagName() == "input")
					sibling->SetAttribute("value", value);
			}
		}
		if (submitted && !from_slider)
			target->SetAttribute("value", value);
	}

	if (value == current)
		return;
	write(row, value, false);
}

void SettingsScreen::clicked(int index, Rml::Event &event)
{
	if (index < 0 || (size_t)index >= m_rows.size())
		return;
	Rml::Element *target = event.GetTargetElement();
	if (!target)
		return;
	Row &row = m_rows[index];

	if (row.kind == "crosshair") {
		crosshairClicked(target);
		return;
	}
	const int direction = target->IsClassSet("prev") ? -1 : target->IsClassSet("next") ? 1 : 0;
	if (direction == 0)
		return;
	if (row.kind == "preset") {
		stepSpecial(row, direction);
		return;
	}
	if (const SettingDef *def = m_catalog.find(row.name))
		step(row, *def, direction);
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

void SettingsScreen::stepSpecial(Row &row, int direction)
{
	const PresetGroup &group = row.name == "@quality" ? qualityPresets() : shadowPresets();
	const int count = (int)group.presets.size();
	int pos = group.detect();
	// От «своих» стрелка ведёт к крайнему набору, а между наборами — по кругу.
	if (pos >= count)
		pos = direction > 0 ? 0 : count - 1;
	else
		pos = ((pos + direction) % count + count) % count;
	group.apply(pos);
	rebuild();
}

void SettingsScreen::crosshairChanged(Rml::Element *target, const std::string &value)
{
	const std::string setting = target->GetAttribute<Rml::String>("name", "");
	unsigned r, g, b;
	if (setting.empty() || !parseHex(value, r, g, b))
		return;
	char buf[32];
	snprintf(buf, sizeof(buf), "(%u,%u,%u)", r, g, b);
	g_settings->set(setting, buf);
	refreshAll();
}

void SettingsScreen::crosshairClicked(Rml::Element *target)
{
	IrrlichtDevice *device = RenderingEngine::get_raw_device();
	m_crosshair_status.clear();

	if (target->IsClassSet("copy")) {
		device->getOSOperator()->copyToClipboard(
				CrosshairStyle::fromSettings(g_settings).toCode().c_str());
		m_crosshair_status = strgettext("Copied");
		return;
	}
	if (target->IsClassSet("paste")) {
		// Чужую строку не разбираем на части: либо она целиком наша, либо не
		// применяется вовсе.
		const char *clipboard = device->getOSOperator()->getTextFromClipboard();
		CrosshairStyle style;
		if (!clipboard || !CrosshairStyle::fromCode(std::string(trim(clipboard)), style)) {
			m_crosshair_status = strgettext("No crosshair code in the clipboard");
			return;
		}
		style.toSettings(g_settings);
		refreshAll();
		return;
	}

	const int direction = target->IsClassSet("prev") ? -1 : target->IsClassSet("next") ? 1 : 0;
	if (direction == 0)
		return;
	const std::string code = CrosshairStyle::fromSettings(g_settings).toCode();
	const int count = (int)std::size(CROSSHAIR_PRESETS);
	int pos = crosshairPreset(code) - 1;
	if (pos < 0)
		pos = direction > 0 ? 0 : count - 1;
	else
		pos = ((pos + direction) % count + count) % count;
	CrosshairStyle style;
	if (!CrosshairStyle::fromCode(CROSSHAIR_PRESETS[pos].code + codeColors(code), style))
		return;
	style.toSettings(g_settings);
	refreshAll();
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
	if (def->kind == SettingDef::Kind::Bool)
		rebuild();
	else
		refreshAll();
	focus(index);
}

void SettingsScreen::focus(int index)
{
	m_focus = -1;
	m_focus_label.clear();
	m_focus_help.clear();
	m_focus_load.clear();
	m_focus_load_class.clear();
	m_focus_note.clear();
	if (index < 0 || (size_t)index >= m_rows.size() || m_rows[index].kind == "heading")
		return;

	const Row &row = m_rows[index];
	m_focus = index;
	m_focus_label = row.label;
	if (row.kind == "preset") {
		m_focus_help = row.name == "@quality"
				? strgettext("Sets everything below at once. Changing anything by hand switches this to Custom.")
				: strgettext("(The crate will need to enable shadows as well)");
		return;
	}
	if (row.kind == "crosshair") {
		m_focus_help = strgettext("Shape, size, gap, dot and outline of the crosshair; "
				"presets, colors and a code to share with others.");
		return;
	}

	const SettingDef *def = m_catalog.find(row.name);
	if (!def)
		return;
	m_focus_help = describe(*def);
	if (!def->load.empty()) {
		m_focus_load = strgettext(loadWord(def->load));
		// Подпись переведена с двоеточием — для строки, а не для плашки.
		m_focus_load_label = strgettext("Cost:");
		if (!m_focus_load_label.empty() && m_focus_load_label.back() == ':')
			m_focus_load_label.pop_back();
		m_focus_load_class = def->load;
	}
	if (!def->note.empty())
		m_focus_note = strgettext(def->note);
}

}
