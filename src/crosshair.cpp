// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "crosshair.h"

#include "settings.h"
#include "util/numeric.h"
#include "util/string.h"
#include "porting.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{

const char *SHAPE_NAMES[CrosshairStyle::SHAPE_COUNT] = {
	"cross", "x", "dot", "circle", "square", "brackets", "none",
};

//! Округление вверх от единицы: линия тоньше пикселя — это отсутствие линии.
inline s32 scaled(s32 value, f32 scale)
{
	if (value <= 0)
		return 0;
	return std::max<s32>(1, (s32)std::lround(value * scale));
}

//! Цвет из строки вида `aarrggbb`.
bool parseHexColor(const std::string &text, video::SColor &out)
{
	if (text.size() != 8)
		return false;
	for (char c : text) {
		if (!isxdigit((unsigned char)c))
			return false;
	}
	const u32 value = (u32)std::stoul(text, nullptr, 16);
	out = video::SColor(value);
	return true;
}

std::string hexColor(video::SColor color)
{
	char buf[16];
	porting::mt_snprintf(buf, sizeof(buf), "%08x", (unsigned)color.color);
	return buf;
}

//! Настройка «цвет» в привычном для движка виде `(r,g,b)` плюс отдельная альфа.
video::SColor readColorSetting(Settings *settings, const std::string &name,
		const std::string &alpha_name, video::SColor def)
{
	video::SColor out = def;
	v3f rgb;
	if (settings->existsLocal(name) || settings->exists(name)) {
		auto parsed = settings->getV3F(name);
		if (parsed.has_value()) {
			rgb = parsed.value();
			out.setRed(rangelim(myround(rgb.X), 0, 255));
			out.setGreen(rangelim(myround(rgb.Y), 0, 255));
			out.setBlue(rangelim(myround(rgb.Z), 0, 255));
		}
	}
	if (!alpha_name.empty() && (settings->existsLocal(alpha_name) || settings->exists(alpha_name)))
		out.setAlpha(rangelim(settings->getS32(alpha_name), 0, 255));
	return out;
}

void writeColorSetting(Settings *settings, const std::string &name,
		const std::string &alpha_name, video::SColor color)
{
	settings->set(name, "(" + itos(color.getRed()) + "," + itos(color.getGreen())
			+ "," + itos(color.getBlue()) + ")");
	if (!alpha_name.empty())
		settings->setS32(alpha_name, color.getAlpha());
}

s32 readLimited(Settings *settings, const std::string &name, s32 def, s32 max)
{
	if (!settings->existsLocal(name) && !settings->exists(name))
		return def;
	return rangelim(settings->getS32(name), 0, max);
}

} // namespace

const char *CrosshairStyle::shapeName(Shape shape)
{
	// Shape is unsigned, so only the upper end can be out of range
	if (shape >= SHAPE_COUNT)
		return SHAPE_NAMES[SHAPE_CROSS];
	return SHAPE_NAMES[shape];
}

CrosshairStyle::Shape CrosshairStyle::shapeByName(const std::string &name)
{
	for (u8 i = 0; i < SHAPE_COUNT; i++) {
		if (name == SHAPE_NAMES[i])
			return (Shape)i;
	}
	return SHAPE_COUNT;
}

std::vector<CrosshairStyle::Piece> CrosshairStyle::pieces(f32 scale) const
{
	std::vector<Piece> out;
	if (shape == SHAPE_NONE && dot <= 0)
		return out;

	const s32 len = scaled(size, scale);
	const s32 th = std::max(1, scaled(thickness, scale));
	const s32 gp = scaled(gap, scale);
	// Толщина растёт в обе стороны от центра линии: у чётной толщины центра
	// нет вовсе, и штрих, положенный «от нуля», уезжает на полпикселя вбок.
	const s32 half = th / 2;

	switch (shape) {
	case SHAPE_CROSS: {
		if (len > 0) {
			// Вверх, вниз, влево, вправо. Просвет отсчитывается от центра, а
			// не от конца штриха: так его видно ровно столько, сколько
			// заказано.
			out.push_back({-half, -gp - len, th, len});
			out.push_back({-half, gp, th, len});
			out.push_back({-gp - len, -half, len, th});
			out.push_back({gp, -half, len, th});
		}
		break;
	}
	case SHAPE_X: {
		// Диагональ — это лесенка из квадратиков: рисовать её отрезками
		// нельзя, у нас только прямоугольники, а лесенка и есть то, как
		// диагональ выглядит в пикселях.
		for (s32 i = 0; i < len; i++) {
			const s32 d = gp + i;
			out.push_back({d, d, th, th});
			out.push_back({-d - th, d, th, th});
			out.push_back({d, -d - th, th, th});
			out.push_back({-d - th, -d - th, th, th});
		}
		break;
	}
	case SHAPE_DOT: {
		/*
		 * Точка меряется тем же «размером», что и штрихи у прочих форм.
		 *
		 * Раньше она бралась из отдельного поля «точка в центре», и получалась
		 * ловушка: выбрал форму «точка» — а на экране пусто, потому что то
		 * поле по умолчанию ноль. Один и тот же ползунок должен управлять
		 * тем, что игрок видит.
		 */
		const s32 ds = std::max(1, len);
		out.push_back({-ds / 2, -ds / 2, ds, ds});
		break;
	}
	case SHAPE_CIRCLE: {
		/*
		 * Кольцо растеризуется по восьмушке и отражается: считать все
		 * четыреста точек незачем, а восьмушка даёт ровно ту же ступенчатую
		 * окружность, что и в пиксельной графике.
		 */
		const s32 r = std::max(1, len);
		for (s32 x = 0; x <= r; x++) {
			const s32 y = (s32)std::lround(std::sqrt((f32)(r * r - x * x)));
			if (x > y)
				break;
			const s32 pts[8][2] = {
				{x, y}, {-x - th, y}, {x, -y - th}, {-x - th, -y - th},
				{y, x}, {-y - th, x}, {y, -x - th}, {-y - th, -x - th},
			};
			for (const auto &p : pts)
				out.push_back({p[0], p[1], th, th});
		}
		break;
	}
	case SHAPE_SQUARE: {
		const s32 r = std::max(1, len);
		const s32 side = r * 2 + th;
		// Не near/far: у Windows это макросы из windef.h.
		const s32 lo = -r - half;
		const s32 hi = r - half;
		out.push_back({lo, lo, side, th});
		out.push_back({lo, hi, side, th});
		out.push_back({lo, lo, th, side});
		out.push_back({hi, lo, th, side});
		break;
	}
	case SHAPE_BRACKETS: {
		/*
		 * Уголки: каждый — два штриха, сходящихся под прямым углом. Цель
		 * оказывается взятой в четыре скобки, и центр остаётся пустым — по
		 * нему и целятся.
		 */
		const s32 arm = std::max(1, len);
		const s32 d = gp;
		const s32 corners[4][2] = {{-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
		for (const auto &c : corners) {
			const s32 cx = c[0] < 0 ? -d - arm : d;
			const s32 cy = c[1] < 0 ? -d - th : d;
			out.push_back({cx, cy, arm, th});
			const s32 vx = c[0] < 0 ? -d - th : d;
			const s32 vy = c[1] < 0 ? -d - arm : d;
			out.push_back({vx, vy, th, arm});
		}
		break;
	}
	default:
		break;
	}

	if (dot > 0) {
		const s32 ds = std::max(1, scaled(dot, scale));
		out.push_back({-ds / 2, -ds / 2, ds, ds});
	}

	return out;
}

std::vector<CrosshairStyle::Piece> CrosshairStyle::outlinePieces(f32 scale) const
{
	std::vector<Piece> out;
	if (outline <= 0)
		return out;

	const s32 o = std::max(1, scaled(outline, scale));
	for (const Piece &p : pieces(scale))
		out.push_back({p.x - o, p.y - o, p.w + o * 2, p.h + o * 2});
	return out;
}

CrosshairStyle CrosshairStyle::fromSettings(Settings *settings)
{
	CrosshairStyle style;
	if (!settings)
		return style;

	const std::string shape_name = settings->exists("crosshair_shape")
			? settings->get("crosshair_shape") : std::string("cross");
	const Shape parsed = shapeByName(shape_name);
	style.shape = parsed == SHAPE_COUNT ? SHAPE_CROSS : parsed;

	style.size = readLimited(settings, "crosshair_size", style.size, MAX_SIZE);
	style.thickness = std::max(1,
			readLimited(settings, "crosshair_thickness", style.thickness, MAX_THICKNESS));
	style.gap = readLimited(settings, "crosshair_gap", style.gap, MAX_GAP);
	style.dot = readLimited(settings, "crosshair_dot", style.dot, MAX_DOT);
	style.outline = readLimited(settings, "crosshair_outline", style.outline, MAX_OUTLINE);

	style.color = readColorSetting(settings, "crosshair_color", "crosshair_alpha",
			style.color);
	style.outline_color = readColorSetting(settings, "crosshair_outline_color",
			"crosshair_outline_alpha", style.outline_color);
	style.object_color = readColorSetting(settings, "crosshair_object_color",
			"crosshair_alpha", style.object_color);
	return style;
}

void CrosshairStyle::toSettings(Settings *settings) const
{
	if (!settings)
		return;

	settings->set("crosshair_shape", shapeName(shape));
	settings->setS32("crosshair_size", size);
	settings->setS32("crosshair_thickness", thickness);
	settings->setS32("crosshair_gap", gap);
	settings->setS32("crosshair_dot", dot);
	settings->setS32("crosshair_outline", outline);

	writeColorSetting(settings, "crosshair_color", "crosshair_alpha", color);
	writeColorSetting(settings, "crosshair_outline_color", "crosshair_outline_alpha",
			outline_color);
	writeColorSetting(settings, "crosshair_object_color", "", object_color);
}

std::string CrosshairStyle::toCode() const
{
	std::string out = "AXCH1-";
	out += shapeName(shape);
	out += "-" + itos(size);
	out += "-" + itos(thickness);
	out += "-" + itos(gap);
	out += "-" + itos(dot);
	out += "-" + itos(outline);
	out += "-" + hexColor(color);
	out += "-" + hexColor(outline_color);
	out += "-" + hexColor(object_color);
	return out;
}

bool CrosshairStyle::fromCode(const std::string &code, CrosshairStyle &out)
{
	// Пробелы по краям — обычная плата за путешествие через чат и буфер
	// обмена; всё остальное должно совпадать до знака.
	std::string trimmed(trim(code));
	std::vector<std::string> parts = str_split(trimmed, '-');
	if (parts.size() != 10 || parts[0] != "AXCH1")
		return false;

	CrosshairStyle style;

	const Shape shape = shapeByName(parts[1]);
	if (shape == SHAPE_COUNT)
		return false;
	style.shape = shape;

	const struct
	{
		s32 *field;
		s32 max;
	} numbers[5] = {
		{&style.size, MAX_SIZE},
		{&style.thickness, MAX_THICKNESS},
		{&style.gap, MAX_GAP},
		{&style.dot, MAX_DOT},
		{&style.outline, MAX_OUTLINE},
	};

	for (size_t i = 0; i < 5; i++) {
		const std::string &text = parts[2 + i];
		if (text.empty() || !is_number(text))
			return false;
		const s32 value = mystoi(text, 0, numbers[i].max);
		if (itos(value) != text)
			return false; // за пределами — чужая строка, а не наша с опечаткой
		*numbers[i].field = value;
	}
	if (style.thickness < 1)
		return false;

	if (!parseHexColor(parts[7], style.color))
		return false;
	if (!parseHexColor(parts[8], style.outline_color))
		return false;
	if (!parseHexColor(parts[9], style.object_color))
		return false;

	out = style;
	return true;
}
