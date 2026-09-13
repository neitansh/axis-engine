// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#pragma once

#include "client/menu/screen.h"

namespace menu
{

class GameMenu;

// Пауза поверх игры: продолжить, настройки, выйти в меню, выйти из игры.
class PauseScreen final : public Screen
{
public:
	explicit PauseScreen(GameMenu &menu);

	void refresh() override;
	bool onEvent(const SEvent &event) override;
	std::vector<KeyHint> keys() const override;

protected:
	void bind(Rml::DataModelConstructor &model) override;

private:
	GameMenu &m_game;
	bool m_singleplayer = false;
};

}
