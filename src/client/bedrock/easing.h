// Copyright (C) 2026 the-axis
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "irrTypes.h"
#include <string>
#include <vector>

/*
 * Смягчение перехода между ключами анимации.
 *
 * Blockbench пишет имя кривой в самом ключе, и означает оно переход не «от
 * этого ключа к следующему», а «от предыдущего ключа к этому» — так его читает
 * и Minecraft. Кривых три десятка, но все они сложены из семи функций тремя
 * способами: в начале (in), в конце (out) и симметрично (inOut).
 *
 * Формулы повторены за GeckoLib (core/animation/EasingType.java) намеренно
 * дословно, включая её собственные странности: анимации подбирают на глаз, и
 * кривая, отличающаяся от той, что видел автор в Blockbench, — это уже другая
 * анимация.
 */

namespace bedrock
{

/// Кривая перехода. Разбирается из имени; неизвестное имя — линейная.
struct Easing
{
	enum class Shape : u8 {
		LINEAR, STEP, SINE, QUADRATIC, CUBIC, POW, EXP, CIRCLE,
		BACK, ELASTIC, BOUNCE,
	};
	enum class Mode : u8 { IN, OUT, IN_OUT };

	Shape shape = Shape::LINEAR;
	Mode mode = Mode::IN;
	/// Показатель степени для POW; для BACK/ELASTIC/BOUNCE — их «сила».
	f32 param = 0.0f;
	/// Аргументы из easingArgs, если анимация их задала.
	f32 arg = 0.0f;
	bool has_arg = false;

	/// Разобрать имя вида "easeInOutSine". Регистр не важен, как и в Bedrock.
	static Easing parse(const std::string &name);

	/// Доля пройденного пути для доли пройденного времени t из [0, 1].
	f32 apply(f32 t) const;

	/// Линейная кривая проходится точно и не нуждается в промежуточных ключах.
	bool isLinear() const { return shape == Shape::LINEAR; }
};

} // namespace bedrock
