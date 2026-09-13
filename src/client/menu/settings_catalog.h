// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace menu
{

// Одна настройка из settingtypes.txt: что это, как называется для игрока,
// какие значения допустимы. Разбор повторяет builtin/common/settings/
// settingtypes.lua — формат один на оба меню.
struct SettingDef
{
	enum class Kind
	{
		Bool,
		Int,
		Float,
		Enum,
		String,
		Path,
		FilePath,
		Key,
		Flags,
		V3f,
		NoiseParams,
	};

	std::string name;
	std::string readable;
	std::string comment;
	Kind kind = Kind::String;
	std::string default_value;
	std::optional<double> min;
	std::optional<double> max;
	std::vector<std::string> values;
	// Не «requires»: в C++20 это ключевое слово.
	std::map<std::string, bool> needs;
	std::string context;

	// Пояснение игроку и цена (high, medium, low или пусто) из
	// builtin/common/settings/descriptions.lua; без него — comment.
	std::string description;
	std::string load;
	// Подписи вариантов перечисления, если сырые значения игроку ни о чём.
	std::map<std::string, std::string> option_labels;
	// Примечание под описанием: что крейт должен включить со своей стороны.
	std::string note;
};

// Страница настроек: избранное сверху, всё остальное из её разделов — под
// «Расширенными». Заголовки внутри списка — строки без настройки.
struct SettingsPage
{
	struct Item
	{
		std::string heading;
		std::string setting;
	};

	std::string id;
	std::string title;
	std::vector<Item> basic;
	std::vector<Item> advanced;
};

class SettingsCatalog
{
public:
	// Читает settingtypes.txt движка и раскладывает по страницам.
	bool load();

	const std::vector<SettingsPage> &pages() const { return m_pages; }
	const SettingDef *find(const std::string &name) const;

private:
	bool parseFile(const std::string &path);
	void parseDescriptions(const std::string &path);
	void annotate();
	void buildPages();

	std::vector<SettingDef> m_defs;
	std::map<std::string, size_t> m_index;
	// Раздел|Подраздел → настройки и заголовки третьего уровня по порядку.
	std::map<std::string, std::vector<SettingsPage::Item>> m_by_source;
	std::vector<SettingsPage> m_pages;
};

}
