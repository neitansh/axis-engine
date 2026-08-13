// Copyright (C) 2026 the-axis
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "IMeshLoader.h"
#include "SkinnedMesh.h"
#include <string>

/*
 * Загрузчик геометрии Bedrock (*.geo.json) — того самого формата, который
 * Blockbench экспортирует как «Bedrock Geometry», а Minecraft читает через
 * GeckoLib. Файл берётся как есть, без промежуточной конвертации.
 *
 * Что из него получается: обычный скелетный меш движка. Кости становятся
 * суставами, кубы — вершинами, привязанными к своей кости целиком. Дальше эта
 * модель ничем не отличается от .b3d или .gltf: её так же двигают анимации,
 * к её костям так же цепляются другие объекты, её так же рисует общий
 * рендер — отдельной ветки для «геколибовских» моделей нет нигде.
 *
 * Анимации лежат в отдельных файлах (*.animation.json) и добавляются к готовой
 * модели: см. bedrock/animation.h.
 */

namespace bedrock
{

/// Расширение, по которому опознаётся геометрия.
constexpr const char *GEOMETRY_EXT = ".geo.json";

class GeometryLoader final : public scene::IMeshLoader
{
public:
	bool isALoadableFileExtension(const io::path &filename) const override;

	scene::IAnimatedMesh *createMesh(io::IReadFile *file) override;

	/// Собрать модель из уже прочитанного текста. Отдельно от createMesh,
	/// потому что клиент держит медиа в памяти, а не файлами на диске.
	/// @return готовый меш или nullptr; о причине сообщается в журнал.
	static scene::SkinnedMesh *build(const std::string &json,
			const std::string &name);
};

} // namespace bedrock
