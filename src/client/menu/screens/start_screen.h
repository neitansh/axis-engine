// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#pragma once

#include "client/menu/screen.h"

namespace menu
{

// Стартовый экран: вход в остальные экраны и выход с подтверждением.
class StartScreen final : public Screen
{
public:
	explicit StartScreen(MainMenu &menu) : Screen(menu, "start") {}

	void refresh() override;
	bool onEvent(const SEvent &event) override;

protected:
	void bind(Rml::DataModelConstructor &model) override;

private:
	void askToQuit();

	bool m_confirm_exit = false;
	bool m_ask_always = true;
};

}
