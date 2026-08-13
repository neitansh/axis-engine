// Copyright (C) 2026 the-axis
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "easing.h"
#include "irrMath.h"
#include <algorithm>
#include <cmath>

namespace bedrock
{

/// Само тело кривой, без учёта того, с какого конца её прикладывают.
static f32 curve(Easing::Shape shape, f32 param, f32 arg, bool has_arg, f32 t)
{
	switch (shape) {
	case Easing::Shape::LINEAR:
		return t;
	case Easing::Shape::STEP:
		return t > 0.0f ? 1.0f : 0.0f;
	case Easing::Shape::SINE:
		return 1.0f - std::cos(t * core::PI / 2.0f);
	case Easing::Shape::QUADRATIC:
		return t * t;
	case Easing::Shape::CUBIC:
		return t * t * t;
	case Easing::Shape::POW:
		return std::pow(t, param);
	case Easing::Shape::EXP:
		return std::pow(2.0f, 10.0f * (t - 1.0f));
	case Easing::Shape::CIRCLE:
		return 1.0f - std::sqrt(std::max(0.0f, 1.0f - t * t));
	case Easing::Shape::BACK: {
		const f32 n = has_arg ? arg * 1.70158f : 1.70158f;
		return t * t * ((n + 1.0f) * t - n);
	}
	case Easing::Shape::ELASTIC: {
		const f32 n = has_arg ? arg : 1.0f;
		return 1.0f - std::pow(std::cos(t * core::PI / 2.0f), 3.0f)
				* std::cos(t * n * core::PI);
	}
	case Easing::Shape::BOUNCE: {
		const f32 n = has_arg ? arg : 0.5f;
		const f32 one = 121.0f / 16.0f * t * t;
		const f32 two = 121.0f / 4.0f * n * std::pow(t - 6.0f / 11.0f, 2.0f) + 1.0f - n;
		const f32 three = 121.0f * n * n * std::pow(t - 9.0f / 11.0f, 2.0f)
				+ 1.0f - n * n;
		const f32 four = 484.0f * n * n * n * std::pow(t - 10.5f / 11.0f, 2.0f)
				+ 1.0f - n * n * n;
		return std::min(std::min(one, two), std::min(three, four));
	}
	}
	return t;
}

f32 Easing::apply(f32 t) const
{
	t = core::clamp(t, 0.0f, 1.0f);
	switch (mode) {
	case Mode::IN:
		return curve(shape, param, arg, has_arg, t);
	case Mode::OUT:
		return 1.0f - curve(shape, param, arg, has_arg, 1.0f - t);
	case Mode::IN_OUT:
		if (t < 0.5f)
			return curve(shape, param, arg, has_arg, t * 2.0f) / 2.0f;
		return 1.0f - curve(shape, param, arg, has_arg, (1.0f - t) * 2.0f) / 2.0f;
	}
	return t;
}

Easing Easing::parse(const std::string &name)
{
	Easing out;

	std::string key;
	key.reserve(name.size());
	for (char c : name) {
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
		key.push_back(c);
	}

	if (key.empty() || key == "linear" || key == "none")
		return out;
	if (key == "step") {
		out.shape = Shape::STEP;
		return out;
	}
	// Catmull-Rom считает по четырём соседним ключам, а не по двум; отдельной
	// поддержки у него пока нет, и линейная кривая — честное к нему
	// приближение, а не подмена.
	if (key == "catmullrom")
		return out;

	std::string rest = key;
	if (rest.rfind("ease", 0) == 0)
		rest = rest.substr(4);

	if (rest.rfind("inout", 0) == 0) {
		out.mode = Mode::IN_OUT;
		rest = rest.substr(5);
	} else if (rest.rfind("in", 0) == 0) {
		out.mode = Mode::IN;
		rest = rest.substr(2);
	} else if (rest.rfind("out", 0) == 0) {
		out.mode = Mode::OUT;
		rest = rest.substr(3);
	}

	if (rest == "sine")            out.shape = Shape::SINE;
	else if (rest == "quad")       out.shape = Shape::QUADRATIC;
	else if (rest == "cubic")      out.shape = Shape::CUBIC;
	else if (rest == "quart")    { out.shape = Shape::POW; out.param = 4.0f; }
	else if (rest == "quint")    { out.shape = Shape::POW; out.param = 5.0f; }
	else if (rest == "expo")       out.shape = Shape::EXP;
	else if (rest == "circ")       out.shape = Shape::CIRCLE;
	else if (rest == "back")       out.shape = Shape::BACK;
	else if (rest == "elastic")    out.shape = Shape::ELASTIC;
	else if (rest == "bounce")     out.shape = Shape::BOUNCE;

	return out;
}

} // namespace bedrock
