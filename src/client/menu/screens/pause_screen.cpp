// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#include "pause_screen.h"

#include "client/menu/game_menu.h"
#include "gettext.h"

namespace menu
{

PauseScreen::PauseScreen(GameMenu &menu) : Screen(menu, "pause"), m_game(menu)
{
}

void PauseScreen::bind(Rml::DataModelConstructor &model)
{
	model.Bind("singleplayer", &m_singleplayer);

	model.BindEventCallback("resume",
			[this](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
				m_game.close();
			});
	model.BindEventCallback("leave",
			[this](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
				m_game.disconnect();
			});
}

void PauseScreen::refresh()
{
	m_singleplayer = m_game.singleplayer();
	model().DirtyAllVariables();
}

bool PauseScreen::onEvent(const SEvent &event)
{
	if (event.EventType != EET_KEY_INPUT_EVENT || !event.KeyInput.PressedDown
			|| event.KeyInput.Key != KEY_ESCAPE)
		return false;
	m_game.close();
	return true;
}

std::vector<Screen::KeyHint> PauseScreen::keys() const
{
	return {{"↑↓", strgettext("Choose")}, {"Enter", strgettext("Open")},
			{"Esc", strgettext("Continue")}};
}

}
