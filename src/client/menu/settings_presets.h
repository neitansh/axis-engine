// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#pragma once

#include <string>
#include <vector>

namespace menu
{

// Набор — несколько настроек одним выбором. Последняя подпись всегда
// «свои»: она не набор, а признак, что значения ни с одним не совпали.
struct PresetGroup
{
	std::vector<std::string> labels;
	// Какой из наборов стоит сейчас; labels.size() - 1 — свои.
	int detect() const;
	// Выставить набор с этим номером; для «своих» ничего не делает.
	void apply(int index) const;

	struct Preset
	{
		std::vector<std::pair<std::string, std::string>> values;
	};
	std::vector<Preset> presets;
};

// Наборы качества картинки (quality_component.lua) и теней
// (shadows_component.lua) — те же значения, что в меню на Lua.
const PresetGroup &qualityPresets();
const PresetGroup &shadowPresets();

}
