// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#include "settings_catalog.h"

#include "filesys.h"
#include "log.h"
#include "porting.h"
#include "util/string.h"
#include <algorithm>
#include <sstream>

namespace menu
{

namespace
{

// Страницы собираются из разделов settingtypes.txt («Раздел|Подраздел»);
// перечисленное в basic стоит наверху, остальное из тех же разделов уходит
// под «Расширенные». Список тот же, что у меню на Lua (dlg_settings.lua).
struct PageSpec
{
	const char *id;
	const char *title;
	std::vector<const char *> sources;
	std::vector<const char *> basic;
};

const std::vector<PageSpec> PAGE_SPECS = {
	{"graphics", "Graphics", {"Graphics and Audio|Graphics"},
		{"#Window", "fullscreen", "vsync", "fps_max", "fps_max_unfocused",
			"pause_on_lost_focus",
		"#World", "viewing_range", "fov", "enable_fog", "enable_3d_clouds",
		"#Quality", "smooth_lighting", "leaves_style", "antialiasing", "mip_map",
			"performance_tradeoffs", "undersampling",
		"#Picture", "display_gamma"}},
	{"effects", "Effects", {"Graphics and Audio|Effects"},
		{"#Shadows", "enable_dynamic_shadows", "shadow_map_max_distance",
			"shadow_map_texture_size",
		"#Lighting", "enable_post_processing", "enable_bloom",
			"enable_volumetric_lighting", "enable_auto_exposure",
		"#Water and foliage", "enable_waving_leaves", "enable_waving_plants",
			"enable_waving_water", "translucent_liquids", "enable_water_reflections",
			"connected_glass"}},
	{"audio", "Audio", {"Graphics and Audio|Audio"},
		{"sound_volume", "sound_volume_unfocused", "mute_sound"}},
	{"interface", "Interface", {"Graphics and Audio|User Interfaces"},
		{"#General", "language", "font_size", "gui_scaling", "hud_scaling",
			"menu_theme", "menu_clouds",
		"#Crosshair", "crosshair_shape", "crosshair_size", "crosshair_thickness",
			"crosshair_gap", "crosshair_dot", "crosshair_outline",
		"#Hints", "tooltip_show_delay", "tooltip_append_itemname",
			"show_nametag_backgrounds",
		"#Chat", "chat_font_size", "recent_chat_messages", "console_height",
			"console_alpha",
		"#Debugging", "show_debug"}},
	{"controls", "Controls",
		{"Controls|General", "Controls|Keyboard and Mouse", "Controls|Touchscreen",
			"Controls|Gamepads and Joysticks"},
		{"#Mouse", "mouse_sensitivity", "invert_mouse",
		"#Movement", "autojump", "doubletap_jump", "always_fly_fast", "aux1_descends",
			"toggle_sneak_key", "toggle_aux1_key",
		"#Interaction", "safe_dig_and_place", "enable_build_where_you_stand",
			"repeat_place_time", "repeat_dig_time",
		"#Menus", "enable_esc_dialog"}},
	{"keys", "Keys", {"Controls|Actions and Keybindings"},
		{"#Movement", "keymap_forward", "keymap_backward", "keymap_left",
			"keymap_right", "keymap_jump", "keymap_sneak", "keymap_sprint",
			"keymap_aux1",
		"#Interaction", "keymap_dig", "keymap_place", "keymap_drop",
			"keymap_inventory",
		"#Interface", "keymap_chat", "keymap_cmd", "keymap_zoom", "keymap_pickitem",
			"keymap_help", "keymap_screenshot", "keymap_fullscreen", "keymap_pause"}},
	{"multiplayer", "Game and network",
		{"Client and Server|Client", "Client and Server|Server",
			"Client and Server|Server Security", "Client and Server|Server Gameplay"},
		{"#Hosting", "server_name", "server_description", "max_users", "port",
			"server_announce",
		"#Client", "enable_local_map_saving"}},
	{"worldgen", "World generation",
		{"Mapgen|", "Mapgen|Biome API", "Mapgen|Mapgen V5", "Mapgen|Mapgen V6",
			"Mapgen|Mapgen V7", "Mapgen|Mapgen Carpathian", "Mapgen|Mapgen Flat",
			"Mapgen|Mapgen Fractal", "Mapgen|Mapgen Valleys"},
		{}},
	{"developer", "Developer",
		{"Advanced|", "Advanced|Developer Options", "Advanced|Advanced",
			"Advanced|Hide: Temporary Settings"},
		{}},
};

bool isVariableChar(char c)
{
	return isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.';
}

bool isFlagsChar(char c)
{
	return isVariableChar(c) || c == ',';
}

std::string_view takeWhile(std::string_view &s, bool (*pred)(char))
{
	size_t n = 0;
	while (n < s.size() && pred(s[n]))
		n++;
	std::string_view out = s.substr(0, n);
	s.remove_prefix(n);
	return out;
}

void skipSpace(std::string_view &s)
{
	while (!s.empty() && isspace((unsigned char)s[0]))
		s.remove_prefix(1);
}

std::vector<std::string> splitFlags(std::string_view s)
{
	std::string joined;
	for (char c : s) {
		if (!isspace((unsigned char)c))
			joined += c;
	}
	return str_split(joined, ',');
}

bool parseNumber(std::string_view s, double &out)
{
	if (s.empty())
		return false;
	std::string tmp(s);
	char *end = nullptr;
	out = strtod(tmp.c_str(), &end);
	return end && *end == '\0';
}

// Числа со знаком; для int цифры, для float ещё точка.
bool isNumberChar(char c)
{
	return isdigit((unsigned char)c) || c == '+' || c == '-' || c == '.';
}

}

bool SettingsCatalog::load()
{
	m_defs.clear();
	m_index.clear();
	m_by_source.clear();
	m_pages.clear();

	const std::string builtin = porting::path_share + DIR_DELIM "builtin";
	if (!parseFile(builtin + DIR_DELIM "settingtypes.txt"))
		return false;
	parseDescriptions(builtin + DIR_DELIM "common" DIR_DELIM "settings" DIR_DELIM
			"descriptions.lua");
	annotate();
	buildPages();
	return true;
}

// Пояснения живут в descriptions.lua: их читает и меню на Lua, и по ним
// собираются переводы. Второй экземпляр на C++ разошёлся бы с первым, поэтому
// файл разбирается как есть — строки в нём одного вида:
//   ["name"] = { text = N_("..."), load = "high" },
void SettingsCatalog::parseDescriptions(const std::string &path)
{
	std::string content;
	if (!fs::ReadFile(path, content, true))
		return;

	auto unescape = [](std::string_view raw) {
		std::string out;
		for (size_t i = 0; i < raw.size(); i++) {
			if (raw[i] != '\\' || i + 1 >= raw.size()) {
				out += raw[i];
				continue;
			}
			const char c = raw[++i];
			out += c == 'n' ? '\n' : c;
		}
		return out;
	};

	std::istringstream in(content);
	std::string line;
	while (std::getline(in, line)) {
		std::string_view rest(line);
		skipSpace(rest);
		if (rest.size() < 3 || rest[0] != '[' || rest[1] != '"')
			continue;
		const size_t name_end = rest.find("\"]");
		if (name_end == std::string_view::npos)
			continue;
		const std::string name(rest.substr(2, name_end - 2));
		auto it = m_index.find(name);
		if (it == m_index.end())
			continue;
		SettingDef &def = m_defs[it->second];

		const size_t text_start = rest.find("N_(\"");
		if (text_start == std::string_view::npos)
			continue;
		size_t i = text_start + 4;
		size_t end = i;
		while (end < rest.size() && !(rest[end] == '"' && rest[end - 1] != '\\'))
			end++;
		def.description = unescape(rest.substr(i, end - i));

		const size_t load = rest.find("load = \"", end);
		if (load != std::string_view::npos) {
			const size_t load_end = rest.find('"', load + 8);
			if (load_end != std::string_view::npos)
				def.load = std::string(rest.substr(load + 8, load_end - load - 8));
		}
	}
}

// То, что меню на Lua дописывает к разобранному файлу (dlg_settings.lua):
// подписи языков и сенсорных настроек, примечания про крейт.
void SettingsCatalog::annotate()
{
	auto set_labels = [this](const char *name, std::map<std::string, std::string> labels) {
		auto it = m_index.find(name);
		if (it != m_index.end())
			m_defs[it->second].option_labels = std::move(labels);
	};
	auto set_note = [this](const char *name, const char *note) {
		auto it = m_index.find(name);
		if (it != m_index.end())
			m_defs[it->second].note = note;
	};

	// Названия языков не переводятся: каждое на своём языке. Список держится
	// вровень с src/unsupported_language_list.txt.
	set_labels("language", {
		{"", "(Use system language)"},
		{"be", "Беларуская [be]"}, {"bg", "Български [bg]"}, {"ca", "Català [ca]"},
		{"cs", "Česky [cs]"}, {"cy", "Cymraeg [cy]"}, {"da", "Dansk [da]"},
		{"de", "Deutsch [de]"}, {"el", "Ελληνικά [el]"}, {"en", "English [en]"},
		{"eo", "Esperanto [eo]"}, {"es", "Español [es]"}, {"et", "Eesti [et]"},
		{"eu", "Euskara [eu]"}, {"fi", "Suomi [fi]"}, {"fil", "Wikang Filipino [fil]"},
		{"fr", "Français [fr]"}, {"gd", "Gàidhlig [gd]"}, {"gl", "Galego [gl]"},
		{"hu", "Magyar [hu]"}, {"id", "Bahasa Indonesia [id]"}, {"it", "Italiano [it]"},
		{"ja", "日本語 [ja]"}, {"jbo", "Lojban [jbo]"}, {"kk", "Қазақша [kk]"},
		{"ko", "한국어 [ko]"}, {"ky", "Kırgızca / Кыргызча [ky]"}, {"lt", "Lietuvių [lt]"},
		{"lv", "Latviešu [lv]"}, {"mn", "Монгол [mn]"}, {"mr", "मराठी [mr]"},
		{"ms", "Bahasa Melayu [ms]"}, {"nb", "Norsk Bokmål [nb]"}, {"nl", "Nederlands [nl]"},
		{"nn", "Norsk Nynorsk [nn]"}, {"oc", "Occitan [oc]"}, {"pl", "Polski [pl]"},
		{"pt", "Português [pt]"}, {"pt_BR", "Português do Brasil [pt_BR]"},
		{"ro", "Română [ro]"}, {"ru", "Русский [ru]"}, {"sk", "Slovenčina [sk]"},
		{"sl", "Slovenščina [sl]"}, {"sr_Cyrl", "Српски [sr_Cyrl]"},
		{"sr_Latn", "Srpski (Latinica) [sr_Latn]"}, {"sv", "Svenska [sv]"},
		{"sw", "Kiswahili [sw]"}, {"tr", "Türkçe [tr]"}, {"tt", "Tatarça [tt]"},
		{"uk", "Українська [uk]"}, {"vi", "Tiếng Việt [vi]"},
		{"zh_CN", "中文 (简体) [zh_CN]"}, {"zh_TW", "正體中文 (繁體) [zh_TW]"},
	});
	set_labels("touch_controls", {{"auto", "Auto"}, {"true", "Enabled"}, {"false", "Disabled"}});
	set_labels("touch_interaction_style", {{"tap", "Tap"}, {"tap_crosshair", "Tap with crosshair"},
			{"buttons_crosshair", "Buttons with crosshair"}});
	set_labels("touch_punch_gesture", {{"short_tap", "Short tap"}, {"long_tap", "Long tap"}});

	set_note("enable_auto_exposure", "(The crate will need to enable automatic exposure as well)");
	set_note("enable_bloom", "(The crate will need to enable bloom as well)");
	set_note("enable_volumetric_lighting", "(The crate will need to enable volumetric lighting as well)");
	set_note("enable_dynamic_shadows", "(The crate will need to enable shadows as well)");
}

const SettingDef *SettingsCatalog::find(const std::string &name) const
{
	auto it = m_index.find(name);
	return it == m_index.end() ? nullptr : &m_defs[it->second];
}

bool SettingsCatalog::parseFile(const std::string &path)
{
	std::string content;
	if (!fs::ReadFile(path, content, true))
		return false;

	std::vector<std::string> comment;
	std::string section, subsection;
	std::string section_context;
	int section_context_level = -1;

	std::istringstream in(content);
	std::string raw;
	while (std::getline(in, raw)) {
		if (!raw.empty() && raw.back() == '\r')
			raw.pop_back();
		std::string_view line(raw);

		if (!line.empty() && line[0] == '#') {
			line.remove_prefix(1);
			skipSpace(line);
			comment.emplace_back(line);
			continue;
		}

		std::vector<std::string> own_comment;
		own_comment.swap(comment);

		std::string_view rest(line);
		skipSpace(rest);
		if (rest.empty())
			continue;

		if (rest[0] == '[') {
			const size_t close = rest.find(']');
			if (close == std::string_view::npos)
				continue;
			std::string_view inner = rest.substr(1, close - 1);
			int level = 0;
			while (!inner.empty() && inner[0] == '*') {
				level++;
				inner.remove_prefix(1);
			}
			std::string_view tail = rest.substr(close + 1);
			skipSpace(tail);
			std::string category_context;
			if (!tail.empty() && tail[0] == '[' && tail.back() == ']')
				category_context = std::string(tail.substr(1, tail.size() - 2));

			if (section_context_level >= 0 && level <= section_context_level) {
				section_context.clear();
				section_context_level = -1;
			}
			if (!category_context.empty()) {
				section_context = category_context;
				section_context_level = level;
			}

			if (level == 0) {
				section = std::string(inner);
				subsection.clear();
			} else if (level == 1) {
				subsection = std::string(inner);
			} else if (!section.empty()) {
				m_by_source[section + "|" + subsection].push_back(
						{std::string(inner), ""});
			}
			continue;
		}

		SettingDef def;
		std::string_view name = takeWhile(rest, isVariableChar);
		skipSpace(rest);
		if (name.empty() || rest.empty() || rest[0] != '(')
			continue;
		const size_t close = rest.find(')');
		if (close == std::string_view::npos)
			continue;
		def.name = std::string(name);
		def.readable = std::string(rest.substr(1, close - 1));
		rest.remove_prefix(close + 1);
		skipSpace(rest);
		if (!rest.empty() && rest[0] == '[') {
			const size_t ctx_close = rest.find(']');
			if (ctx_close == std::string_view::npos)
				continue;
			def.context = std::string(rest.substr(1, ctx_close - 1));
			rest.remove_prefix(ctx_close + 1);
			skipSpace(rest);
		} else {
			def.context = section_context;
		}
		const std::string type(takeWhile(rest, isVariableChar));
		skipSpace(rest);

		if (!own_comment.empty()) {
			std::string last = std::string(trim(own_comment.back()));
			if (lowercase(last).rfind("requires:", 0) == 0) {
				own_comment.pop_back();
				for (std::string part : str_split(last.substr(9), ',')) {
					part = std::string(trim(part));
					bool value = true;
					if (!part.empty() && part[0] == '!') {
						value = false;
						part = std::string(trim(part.substr(1)));
					}
					if (!part.empty())
						def.requires[part] = value;
				}
			}
		}
		for (size_t i = 0; i < own_comment.size(); i++) {
			if (i > 0)
				def.comment += '\n';
			def.comment += own_comment[i];
		}
		def.comment = std::string(trim(def.comment));

		if (type == "int" || type == "float") {
			def.kind = type == "int" ? SettingDef::Kind::Int : SettingDef::Kind::Float;
			std::string_view a = takeWhile(rest, isNumberChar);
			skipSpace(rest);
			std::string_view b = takeWhile(rest, isNumberChar);
			skipSpace(rest);
			std::string_view c = takeWhile(rest, isNumberChar);
			double v = 0;
			if (!parseNumber(a, v))
				continue;
			def.default_value = std::string(a);
			if (parseNumber(b, v))
				def.min = v;
			if (parseNumber(c, v))
				def.max = v;
		} else if (type == "bool") {
			if (rest != "true" && rest != "false")
				continue;
			def.kind = SettingDef::Kind::Bool;
			def.default_value = std::string(rest);
		} else if (type == "enum") {
			def.kind = SettingDef::Kind::Enum;
			std::string_view first = takeWhile(rest, isVariableChar);
			skipSpace(rest);
			std::string_view second = takeWhile(rest, isFlagsChar);
			if (second.empty()) {
				// Один список без умолчания: по умолчанию пусто.
				def.values = splitFlags(first);
			} else {
				def.default_value = std::string(first);
				def.values = splitFlags(second);
			}
			if (def.values.empty())
				continue;
		} else if (type == "flags") {
			def.kind = SettingDef::Kind::Flags;
			std::string_view first = takeWhile(rest, isFlagsChar);
			skipSpace(rest);
			std::string_view second = takeWhile(rest, isFlagsChar);
			if (second.empty()) {
				def.values = splitFlags(first);
			} else {
				def.default_value = std::string(first);
				def.values = splitFlags(second);
			}
		} else if (type == "string") {
			def.kind = SettingDef::Kind::String;
			def.default_value = std::string(rest);
		} else if (type == "key") {
			def.kind = SettingDef::Kind::Key;
			def.default_value = std::string(rest);
			def.requires["keyboard_mouse"] = true;
		} else if (type == "v3f") {
			def.kind = SettingDef::Kind::V3f;
			def.default_value = std::string(rest);
		} else if (type == "path") {
			def.kind = SettingDef::Kind::Path;
			def.default_value = std::string(rest);
		} else if (type == "filepath") {
			def.kind = SettingDef::Kind::FilePath;
			def.default_value = std::string(rest);
		} else if (type == "noise_params_2d" || type == "noise_params_3d") {
			def.kind = SettingDef::Kind::NoiseParams;
			def.default_value = std::string(rest);
		} else {
			warningstream << "settingtypes.txt: unknown type \"" << type
					<< "\" of " << def.name << std::endl;
			continue;
		}

		if (m_index.count(def.name))
			continue;
		m_index[def.name] = m_defs.size();
		if (!section.empty())
			m_by_source[section + "|" + subsection].push_back({"", def.name});
		m_defs.push_back(std::move(def));
	}
	return true;
}

void SettingsCatalog::buildPages()
{
	// Имя игрока приходит от служб платформы, крутить его в меню нечего.
	std::map<std::string, bool> claimed = {{"name", true}};
	for (const PageSpec &spec : PAGE_SPECS) {
		for (const char *item : spec.basic) {
			if (item[0] != '#')
				claimed[item] = true;
		}
	}

	for (const PageSpec &spec : PAGE_SPECS) {
		SettingsPage page;
		page.id = spec.id;
		page.title = spec.title;
		for (const char *item : spec.basic) {
			if (item[0] == '#') {
				page.basic.push_back({item + 1, ""});
			} else if (find(item)) {
				page.basic.push_back({"", item});
			} else {
				warningstream << "Settings page " << spec.id
						<< " lists unknown setting " << item << std::endl;
			}
		}
		for (const char *source : spec.sources) {
			auto it = m_by_source.find(source);
			if (it == m_by_source.end())
				continue;
			const SettingsPage::Item *pending_heading = nullptr;
			for (const SettingsPage::Item &item : it->second) {
				if (!item.heading.empty()) {
					pending_heading = &item;
				} else if (!claimed.count(item.setting)) {
					if (pending_heading) {
						page.advanced.push_back(*pending_heading);
						pending_heading = nullptr;
					}
					page.advanced.push_back(item);
				}
			}
		}
		m_pages.push_back(std::move(page));
	}

	// Свои строки, которых нет в settingtypes.txt: наборы качества и теней,
	// перекрестье с предпросмотром. Имя с «@» экран разбирает сам.
	auto insert_before = [this](const char *page_id, const char *before, const char *item) {
		for (SettingsPage &page : m_pages) {
			if (page.id != page_id)
				continue;
			auto it = before ? std::find_if(page.basic.begin(), page.basic.end(),
					[before](const SettingsPage::Item &i) { return i.setting == before; })
					: page.basic.begin();
			page.basic.insert(it, {"", item});
		}
	};
	insert_before("graphics", nullptr, "@quality");
	insert_before("effects", "enable_dynamic_shadows", "@shadows");
	insert_before("interface", "crosshair_shape", "@crosshair");
}

}
