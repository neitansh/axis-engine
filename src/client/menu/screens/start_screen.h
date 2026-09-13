// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#pragma once

#include "client/menu/main_menu.h"

namespace menu
{

// Стартовый экран: вход в остальные экраны и выход с подтверждением.
class StartScreen final : public Screen
{
public:
	explicit StartScreen(MainMenu &menu) : Screen(menu, "start") {}

	void refresh() override;
	void entered() override;
	void left() override;
	bool onEvent(const SEvent &event) override;
	std::vector<KeyHint> keys() const override;

	// Следующий показ начнётся из черноты: заставка кончается чёрным, и
	// стартовый экран проявляется из него, а не вспыхивает небом.
	void fadeInOnce() { m_fade_in = true; }

protected:
	void bind(Rml::DataModelConstructor &model) override;

private:
	void askToQuit();
	void closeQuitDialog();

	bool m_confirm_exit = false;
	bool m_ask_always = true;
	bool m_fade_in = false;
};

}
