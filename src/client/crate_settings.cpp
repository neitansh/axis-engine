// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#include "crate_settings.h"
#include "log.h"
#include "settings.h"
#include <unordered_set>

namespace
{

// Что и как рисовать — в том числе всё, что меняют пресеты качества, иначе
// пресет переписывает вид крейта. Не здесь: окно и разрешение, дальность
// видимости, undersampling, всё про ввод, звук, сеть, моды — это машина и
// привычки игрока, а не вид крейта.
const std::unordered_set<std::string> ALLOWED = {
	// Свет
	"display_gamma", "ambient_occlusion_gamma", "smooth_lighting",
	"lighting_alpha", "lighting_beta", "lighting_boost",
	"lighting_boost_center", "lighting_boost_spread",
	// Тени
	"enable_dynamic_shadows", "shadow_strength_gamma",
	"shadow_map_max_distance", "shadow_map_texture_size",
	"shadow_map_texture_32bit", "shadow_map_color",
	"shadow_sky_body_orbit_tilt", "shadow_update_frames",
	"shadow_filters", "shadow_soft_radius", "shadow_poisson_filter",
	"shadow_texel_snap",
	// Пост-обработка
	"enable_post_processing", "post_processing_texture_bits",
	"tone_mapping", "enable_auto_exposure", "exposure_compensation",
	"debanding", "enable_bloom", "enable_volumetric_lighting",
	// Небо и туман
	"enable_fog", "directional_colored_fog", "fog_start",
	"enable_3d_clouds", "soft_clouds", "cloud_radius",
	"enable_volumetric_clouds", "volumetric_clouds_height",
	"volumetric_clouds_density", "volumetric_clouds_speed",
	"volumetric_clouds_distance", "volumetric_clouds_size",
	"volumetric_clouds_pixel", "volumetric_clouds_thickness",
	"volumetric_clouds_roundness",
	// Ноды и вода
	"translucent_liquids", "leaves_style", "connected_glass",
	"enable_waving_leaves", "enable_waving_plants", "enable_waving_water",
	"water_wave_height", "water_wave_length", "water_wave_speed",
	"enable_translucent_foliage", "enable_water_reflections",
	"foliage_range", "leaves_detail_range",
	// Сглаживание
	"antialiasing", "fxaa",
	// Текстуры
	"bilinear_filter", "trilinear_filter", "anisotropic_filter",
	"texture_min_size",
	// Обводка и прицел
	"node_highlighting", "selectionbox_color", "selectionbox_width",
	"selectionbox_alpha", "crosshair_color", "crosshair_alpha",
	"crosshair_shape", "crosshair_size", "crosshair_thickness",
	"crosshair_gap", "crosshair_dot", "crosshair_outline",
	"crosshair_outline_color", "crosshair_outline_alpha",
	"crosshair_object_color",
};

}

void applyCrateClientSettings(
		const std::vector<std::pair<std::string, std::string>> &pairs)
{
	if (!g_settings->getBool("crate_graphics")) {
		infostream << "Crate client settings: kept the player's own graphics"
			<< std::endl;
		return;
	}
	std::vector<std::pair<std::string, std::string>> accepted;
	for (const auto &pair : pairs) {
		if (ALLOWED.count(pair.first)) {
			accepted.push_back(pair);
		} else {
			warningstream << "Crate client settings: \"" << pair.first
				<< "\" is not a graphics setting, ignored" << std::endl;
		}
	}
	infostream << "Crate client settings: applied " << accepted.size()
		<< " of " << pairs.size() << std::endl;
	Settings::setCrateClientOverrides(accepted);
}
