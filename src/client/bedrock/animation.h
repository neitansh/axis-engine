// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#pragma once

#include "SkinnedMesh.h"
#include <string>

/*
 * Загрузчик анимаций Bedrock (*.animation.json) — выгрузка Blockbench
 * «Bedrock Animation», та же, что читает GeckoLib.
 *
 * Анимации живут отдельным файлом от геометрии, поэтому добавляются к уже
 * собранной модели: одна модель может набрать их из нескольких файлов, и
 * каждая становится обычной именованной дорожкой движка. Дальше ими
 * распоряжается тот же самый Lua-вызов, что и дорожками из .b3d или .gltf:
 *
 *     obj:play_animation("reload", {loop = false, speed = 1})
 *
 * Время дорожки считается в секундах — так же, как оно записано в файле.
 * Поэтому speed = 1 означает «как задумал автор», speed = 2 — вдвое быстрее,
 * и переводить кадры в секунды в игре не приходится.
 */

namespace bedrock
{

/// Расширение, по которому опознаются анимации.
constexpr const char *ANIMATION_EXT = ".animation.json";

/// Добавить в модель дорожки из файла анимаций.
/// @return сколько дорожек добавлено
u32 loadAnimations(scene::SkinnedMesh *mesh, const std::string &json,
		const std::string &name);

} // namespace bedrock
