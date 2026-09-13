// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#pragma once

#include <RmlUi/Core/Types.h>
#include <string>
#include <vector>

namespace Rml
{
class Context;
}

namespace menu
{

class Screen;
class Sounds;

// Подсказка по клавишам в нижней строке рамки: пара «клавиша — действие».
struct KeyHint
{
	Rml::String key;
	Rml::String label;
};

// То, что экрану нужно от хозяина, у кого бы он ни жил — в главном меню
// или в меню поверх игры: контекст RmlUi, файлы темы, переход между
// экранами, звук и строка подсказок. Экраны, которым нужно больше
// (запуск мира, лаунчер), живут только в главном меню и берут его целиком.
class ScreenHost
{
public:
	virtual ~ScreenHost() = default;

	virtual Rml::Context &context() = 0;
	virtual std::string themeFile(const std::string &name) const = 0;
	virtual void navigate(const std::string &screen) = 0;
	virtual Screen *findScreen(const std::string &name) = 0;
	virtual Sounds &sounds() = 0;
	virtual void showKeys(const std::vector<KeyHint> &keys) = 0;
	// Закрыть игру целиком.
	virtual void quit() = 0;
};

}
