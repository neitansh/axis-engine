// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#pragma once

#include "client/menu/screen.h"

namespace menu
{

// Стартовый экран: вход в остальные и сообщение о том, чем кончился
// прошлый заход, если он кончился ошибкой.
class StartScreen final : public Screen
{
public:
	StartScreen(MainMenu &menu, const std::string &notice);

	void refresh() override;

protected:
	void bind(Rml::DataModelConstructor &model) override;

private:
	Rml::String m_notice;
};

}
