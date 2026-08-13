// Copyright (C) 2026 the-axis
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "irrTypes.h"
#include "irr_v3d.h"
#include "quaternion.h"
#include "constants.h"
#include <cmath>

/*
 * Перевод из системы Bedrock/Blockbench в систему движка. Всё, что касается
 * осей, единиц и углов, живёт здесь одной кучкой — именно затем, чтобы нигде
 * дальше не появилось ни одного «плюс полблока, и вроде встало на место».
 *
 * Единицы. Blockbench считает в пикселях, шестнадцать на блок. Движок считает
 * в своих единицах, BS на ноду. Отсюда PIXEL: один пиксель модели — это
 * BS/16 единиц, и модель, занимающая в Blockbench куб 16×16×16, занимает
 * ровно одну ноду при visual_size = 1. Другого масштабирования не требуется:
 * автору не нужно знать ни про BS, ни про пиксели.
 *
 * Оси. Обе системы держат Y вверх, а расходятся в двух других:
 *
 *   Blockbench/Bedrock  X на восток, Z на юг, отсчёт правой рукой
 *   движок (Irrlicht)   X вправо,    Z вперёд, отсчёт левой рукой
 *
 * Разница снимается поворотом на 180° вокруг Y, то есть сменой знака у X и Z.
 * Знак у X берётся из того, как эти модели читает сам Minecraft: Blockbench
 * пишет их зеркально тому, как их рисуют, и зеркало по X — часть формата, а не
 * наша поправка (см. GeckoLib, BakedModelFactory.constructCube). Знак у Z —
 * это уже переход к левой тройке осей, тот самый, что делает загрузчик glTF
 * (CGLTFMeshFileLoader, «Notes on the coordinate system»).
 *
 * Оба зеркала вместе дают поворот, а не отражение: определитель равен единице,
 * поэтому обход вершин и нормали остаются как есть, разворачивать их не нужно.
 * Модель, смотрящая в Blockbench на север, смотрит в движке вдоль +Z — туда
 * же, куда смотрит сущность с нулевым поворотом.
 *
 * Углы. В Bedrock они в градусах и применяются в порядке X, Y, Z (матрицей —
 * Rz·Ry·Rx). После поворота на 180° вокруг Y углы вокруг X и Y меняли бы знак
 * дважды — сперва при зеркале по X, как в Minecraft, затем при зеркале по Z, —
 * поэтому здесь они берутся ровно такими, как записаны в файле.
 */

namespace bedrock
{

/// Сколько единиц движка в одном пикселе Blockbench.
constexpr f32 PIXEL = BS / 16.0f;

/// Точка или смещение модели: пиксели Bedrock — в единицы движка.
inline v3f toEngine(const v3f &pixels)
{
	return v3f(-pixels.X * PIXEL, pixels.Y * PIXEL, -pixels.Z * PIXEL);
}

/// То же для направления, у которого нет единиц (нормаль).
inline v3f toEngineDir(const v3f &dir)
{
	return v3f(-dir.X, dir.Y, -dir.Z);
}

/// Обратный ход: направление движка — в оси Bedrock. Нужен там, где поворот
/// считается в исходных координатах, а вектор пришёл из наших.
inline v3f toBedrockDir(const v3f &dir)
{
	// Преобразование — поворот на 180°, оно обратно самому себе.
	return v3f(-dir.X, dir.Y, -dir.Z);
}

/// Углы Bedrock (градусы, порядок X→Y→Z) — в поворот движка.
inline core::quaternion toEngineRotation(const v3f &degrees);

/// Обратный ход: поворот движка — в те самые углы Bedrock.
///
/// Нужен потому, что анимация в Bedrock прибавляется к повороту кости
/// углами, а не поворотами: чтобы сложить их правильно, надо сперва достать
/// углы обратно. Разложение — строго обратное toEngineRotation, а не общая
/// эйлерова раскладка: у той свой порядок осей, и совпадать они не обязаны.
inline v3f fromEngineRotation(const core::quaternion &rotation)
{
	// Матрицу собираем из образов базисных векторов: так не приходится
	// гадать, как именно уложены числа внутри matrix4.
	core::quaternion q = rotation;
	const v3f col0 = q * v3f(1, 0, 0);
	const v3f col1 = q * v3f(0, 1, 0);
	const v3f col2 = q * v3f(0, 0, 1);

	// Для M = Rz·Ry·Rx третья строка даёт Y и X, первый столбец — Z.
	const f32 sin_y = core::clamp(-col0.Z, -1.0f, 1.0f);
	const f32 y = std::asin(sin_y);
	f32 x, z;
	if (std::abs(sin_y) > 0.99999f) {
		// Оси сложились: угол вокруг X неотличим от угла вокруг Z, и любой
		// их размен даёт тот же поворот. Берём весь угол на Z.
		x = 0.0f;
		z = std::atan2(-col1.X, col1.Y);
	} else {
		x = std::atan2(col1.Z, col2.Z);
		z = std::atan2(col0.Y, col0.X);
	}
	return v3f(x, y, z) * core::RADTODEG;
}

inline core::quaternion toEngineRotation(const v3f &degrees)
{
	const f32 rad = core::DEGTORAD;
	// Порядок именно такой: сперва X, затем Y, затем Z. Обратный порядок даёт
	// ту же тройку углов, но другую позу, и расхождение видно сразу — на
	// костях, повёрнутых больше чем по одной оси.
	core::quaternion qx(degrees.X * rad, 0, 0);
	core::quaternion qy(0, degrees.Y * rad, 0);
	core::quaternion qz(0, 0, degrees.Z * rad);
	return qz * qy * qx;
}

} // namespace bedrock
