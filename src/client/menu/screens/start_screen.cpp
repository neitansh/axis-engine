// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#include "start_screen.h"

#include "client/menu/main_menu.h"
#include "settings.h"

namespace menu
{

void StartScreen::bind(Rml::DataModelConstructor &model)
{
	model.Bind("confirm_exit", &m_confirm_exit);
	model.Bind("ask_always", &m_ask_always);

	model.BindEventCallback("quit_begin",
			[this](Rml::DataModelHandle handle, Rml::Event &, const Rml::VariantList &) {
				askToQuit();
				handle.DirtyAllVariables();
			});
	model.BindEventCallback("quit_cancel",
			[this](Rml::DataModelHandle handle, Rml::Event &, const Rml::VariantList &) {
				m_confirm_exit = false;
				handle.DirtyAllVariables();
			});
	model.BindEventCallback("quit_confirm",
			[this](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
				g_settings->setBool("enable_esc_dialog", m_ask_always);
				menu().quit();
			});
	model.BindEventCallback("ask_toggle",
			[this](Rml::DataModelHandle handle, Rml::Event &, const Rml::VariantList &) {
				g_settings->setBool("enable_esc_dialog", m_ask_always);
				handle.DirtyVariable("ask_always");
			});
	model.BindEventCallback("ask_flip",
			[this](Rml::DataModelHandle handle, Rml::Event &, const Rml::VariantList &) {
				m_ask_always = !m_ask_always;
				g_settings->setBool("enable_esc_dialog", m_ask_always);
				handle.DirtyVariable("ask_always");
			});
}

void StartScreen::refresh()
{
	m_confirm_exit = false;
	m_ask_always = g_settings->getBool("enable_esc_dialog");
	model().DirtyAllVariables();
}

// Подтверждение можно выключить галкой в нём же или в настройках — тогда
// «Выйти» и Esc закрывают игру сразу.
void StartScreen::askToQuit()
{
	if (!g_settings->getBool("enable_esc_dialog")) {
		menu().quit();
		return;
	}
	m_ask_always = true;
	m_confirm_exit = true;
}

bool StartScreen::onEvent(const SEvent &event)
{
	if (event.EventType != EET_KEY_INPUT_EVENT || !event.KeyInput.PressedDown)
		return false;
	if (event.KeyInput.Key == KEY_ESCAPE) {
		if (m_confirm_exit)
			m_confirm_exit = false;
		else
			askToQuit();
		model().DirtyAllVariables();
		return true;
	}
	if (m_confirm_exit && event.KeyInput.Key == KEY_RETURN) {
		g_settings->setBool("enable_esc_dialog", m_ask_always);
		menu().quit();
		return true;
	}
	return false;
}

}
