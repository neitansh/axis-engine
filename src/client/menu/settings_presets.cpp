// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#include "settings_presets.h"

#include "gettext.h"
#include "settings.h"
#include "util/string.h"

namespace menu
{

int PresetGroup::detect() const
{
	for (size_t i = 0; i < presets.size(); i++) {
		bool match = true;
		for (const auto &[name, want] : presets[i].values) {
			std::string have;
			g_settings->getNoEx(name, have);
			// Логические сравниваются по смыслу: «true» и «1» — одно и то же.
			const bool boolean = want == "true" || want == "false";
			if (boolean ? is_yes(have) != is_yes(want) : have != want) {
				match = false;
				break;
			}
		}
		if (match)
			return (int)i;
	}
	return (int)labels.size() - 1;
}

void PresetGroup::apply(int index) const
{
	if (index < 0 || (size_t)index >= presets.size())
		return;
	for (const auto &[name, value] : presets[index].values)
		g_settings->set(name, value);
}

const PresetGroup &qualityPresets()
{
	// Что стоит в наборах, взято из замеров на арене матча: листва и тени —
	// самое дорогое в кадре, остальное решает картинку.
	static const PresetGroup group = {
		{N_("Low"), N_("Standard"), N_("High"), N_("Ultra"), N_("Custom")},
		{
			{{{"leaves_style", "simple"}, {"leaves_detail_range", "16"},
				{"foliage_range", "48"}, {"smooth_lighting", "false"},
				{"enable_dynamic_shadows", "false"}, {"enable_post_processing", "false"},
				{"enable_bloom", "false"}, {"enable_volumetric_lighting", "false"},
				{"enable_auto_exposure", "false"}, {"enable_waving_leaves", "false"},
				{"enable_waving_plants", "false"}, {"enable_waving_water", "false"},
				{"enable_water_reflections", "false"}, {"enable_translucent_foliage", "false"},
				{"enable_3d_clouds", "false"}, {"fxaa", "false"}, {"antialiasing", "none"}}},
			{{{"leaves_style", "fancy"}, {"leaves_detail_range", "32"},
				{"foliage_range", "128"}, {"smooth_lighting", "true"},
				{"enable_dynamic_shadows", "true"}, {"shadow_map_texture_size", "1024"},
				{"shadow_map_max_distance", "80"}, {"enable_post_processing", "true"},
				{"enable_bloom", "true"}, {"enable_volumetric_lighting", "false"},
				{"enable_auto_exposure", "false"}, {"enable_waving_leaves", "true"},
				{"enable_waving_plants", "true"}, {"enable_waving_water", "true"},
				{"enable_water_reflections", "false"}, {"enable_translucent_foliage", "false"},
				{"enable_3d_clouds", "true"}, {"fxaa", "true"}, {"antialiasing", "none"}}},
			{{{"leaves_style", "fancy"}, {"leaves_detail_range", "64"},
				{"foliage_range", "192"}, {"smooth_lighting", "true"},
				{"enable_dynamic_shadows", "true"}, {"shadow_map_texture_size", "2048"},
				{"shadow_map_max_distance", "140"}, {"enable_post_processing", "true"},
				{"enable_bloom", "true"}, {"enable_volumetric_lighting", "true"},
				{"enable_auto_exposure", "true"}, {"enable_waving_leaves", "true"},
				{"enable_waving_plants", "true"}, {"enable_waving_water", "true"},
				{"enable_water_reflections", "true"}, {"enable_translucent_foliage", "true"},
				{"enable_3d_clouds", "true"}, {"fxaa", "true"}, {"antialiasing", "none"}}},
			{{{"leaves_style", "fancy"}, {"leaves_detail_range", "0"},
				{"foliage_range", "0"}, {"smooth_lighting", "true"},
				{"enable_dynamic_shadows", "true"}, {"shadow_map_texture_size", "4096"},
				{"shadow_map_max_distance", "210"}, {"enable_post_processing", "true"},
				{"enable_bloom", "true"}, {"enable_volumetric_lighting", "true"},
				{"enable_auto_exposure", "true"}, {"enable_waving_leaves", "true"},
				{"enable_waving_plants", "true"}, {"enable_waving_water", "true"},
				{"enable_water_reflections", "true"}, {"enable_translucent_foliage", "true"},
				{"enable_3d_clouds", "true"}, {"fxaa", "true"}, {"antialiasing", "none"}}},
		},
	};
	return group;
}

const PresetGroup &shadowPresets()
{
	// Край тени везде чёткий (shadow_filters = 0): у мира из кубов тень куба
	// должна быть кубом. Наборы отличаются тем, сколько пикселей карты
	// приходится на узел мира.
	auto preset = [](const char *distance, const char *size, const char *color) {
		return PresetGroup::Preset{{{"enable_dynamic_shadows", "true"},
				{"shadow_map_max_distance", distance}, {"shadow_map_texture_size", size},
				{"shadow_map_texture_32bit", "true"}, {"shadow_filters", "0"},
				{"shadow_map_color", color}}};
	};
	static const PresetGroup group = {
		{N_("Very Low"), N_("Low"), N_("Medium"), N_("High"), N_("Very High"), N_("Custom")},
		{preset("62", "512", "false"), preset("93", "1024", "false"),
			preset("140", "2048", "false"), preset("210", "4096", "true"),
			preset("300", "8192", "true")},
	};
	return group;
}

}
