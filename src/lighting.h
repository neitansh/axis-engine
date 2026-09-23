// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2021 x2048, Dmitry Kostenko <codeforsmile@gmail.com>

#pragma once
#include "SColor.h"
#include "irr_v3d.h"


/**
 * Parameters for automatic exposure compensation
 *
 * Automatic exposure compensation uses the following equation:
 *
 * wanted_exposure = 2^exposure_correction / clamp(observed_luminance, 2^luminance_min, 2^luminance_max)
 *
 */
struct AutoExposure
{
	/// @brief Minimum boundary for computed luminance
	float luminance_min;
	/// @brief Maximum boundary for computed luminance
	float luminance_max;
	/// @brief Luminance bias. Higher values make the scene darker, can be negative.
	float exposure_correction;
	/// @brief Speed of transition from dark to bright scenes
	float speed_dark_bright;
	/// @brief Speed of transition from bright to dark scenes
	float speed_bright_dark;
	/// @brief Power value for center-weighted metering. Value of 1.0 measures entire screen uniformly
	float center_weight_power;

	constexpr AutoExposure()
		: luminance_min(-3.f),
		luminance_max(-3.f),
		exposure_correction(0.0f),
		speed_dark_bright(1000.f),
		speed_bright_dark(1000.f),
		center_weight_power(1.f)
	{}
};

/// Объектив, всё 0..1.
struct LensParams
{
	float vignette {0.0f};
	float flicker {0.0f};
	float grain {0.0f};
	float chromatic {0.0f};
	float pulse {0.0f};
	float blind {0.0f};

	static constexpr float LensParams::*FIELDS[] = {
		&LensParams::vignette, &LensParams::flicker, &LensParams::grain,
		&LensParams::chromatic, &LensParams::pulse, &LensParams::blind,
	};
};

/** Describes ambient light settings for a player
 */
struct Lighting
{
	AutoExposure exposure;
	float shadow_intensity {0.0f};
	float saturation {1.0f};
	float volumetric_light_strength {0.0f};
	video::SColor shadow_tint {255, 0, 0, 0};
	float bloom_intensity {0.05f};
	float bloom_strength_factor {1.0f};
	float bloom_radius {1.0f};
	v3f shadow_direction;
	// Свет самого светила там, куда оно достаёт по карте теней: цвет и сила
	// (чёрный — нет). Тень сама по себе лишь гасит дневную долю света.
	video::SColor shadow_light {255, 0, 0, 0};
	// Множитель к нему: 1 — как дневной свет на той же поверхности, больше
	// — ярче (луна в тёмной комнате должна читаться, а не теряться в дереве).
	float shadow_light_strength {1.0f};
	// Цвет теней и светов кадра; альфа — сила, 0 — без оттенка.
	video::SColor grade_shadows {0, 0, 0, 0};
	video::SColor grade_highlights {0, 0, 0, 0};
	LensParams lens;
	// За сколько секунд клиент ведёт кадр от прежнего `lens` к этому. Не
	// настройка, а свойство одного вызова: следующий вызов без него — сразу.
	float lens_fade {0.0f};
};
