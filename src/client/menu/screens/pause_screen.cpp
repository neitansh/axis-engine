// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#include "pause_screen.h"

#include "client/menu/game_menu.h"
#include "gettext.h"
#include "settings.h"

namespace menu
{

PauseScreen::PauseScreen(GameMenu &menu) : Screen(menu, "pause"), m_game(menu)
{
}

void PauseScreen::bind(Rml::DataModelConstructor &model)
{
	model.Bind("singleplayer", &m_singleplayer);
	model.Bind("confirm_exit", &m_confirm_exit);

	model.BindEventCallback("resume",
			[this](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
				m_game.close();
			});
	model.BindEventCallback("leave",
			[this](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
				m_game.disconnect();
			});
	model.BindEventCallback("quit_begin",
			[this](Rml::DataModelHandle handle, Rml::Event &, const Rml::VariantList &) {
				askToQuit();
				handle.DirtyAllVariables();
			});
	model.BindEventCallback("quit_cancel",
			[this](Rml::DataModelHandle handle, Rml::Event &, const Rml::VariantList &) {
				m_confirm_exit = false;
				refocus();
				handle.DirtyAllVariables();
			});
	model.BindEventCallback("quit_confirm",
			[this](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
				m_game.quit();
			});
}

// Как на стартовом экране: подтверждение можно выключить настройкой, тогда
// выход сразу.
void PauseScreen::askToQuit()
{
	if (!g_settings->getBool("enable_esc_dialog")) {
		m_game.quit();
		return;
	}
	m_confirm_exit = true;
	refocus();
}

void PauseScreen::refresh()
{
	m_singleplayer = m_game.singleplayer();
	m_confirm_exit = false;
	model().DirtyAllVariables();
}

bool PauseScreen::onEvent(const SEvent &event)
{
	if (event.EventType != EET_KEY_INPUT_EVENT || !event.KeyInput.PressedDown
			|| event.KeyInput.Key != KEY_ESCAPE)
		return false;
	if (m_confirm_exit) {
		m_confirm_exit = false;
		refocus();
		model().DirtyAllVariables();
	} else {
		m_game.close();
	}
	return true;
}

std::vector<Screen::KeyHint> PauseScreen::keys() const
{
	return {{"↑↓", strgettext("Choose")}, {"Enter", strgettext("Open")},
			{"Esc", strgettext("Continue")}};
}

}
