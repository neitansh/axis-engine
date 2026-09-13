// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#include "game_menu.h"

#include "client/inputhandler.h"
#include "filesys.h"
#include "gui/mainmenumanager.h"
#include "log.h"
#include "porting.h"
#include "screens/pause_screen.h"
#include "screens/settings_screen.h"
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>

namespace menu
{

GameMenu::GameMenu(ui::Host &host, MyEventReceiver *receiver, IGameCallback *callback,
		bool singleplayer) :
	m_host(host),
	m_receiver(receiver),
	m_callback(callback),
	m_singleplayer(singleplayer)
{
	m_theme_dirs.push_back(porting::path_share + DIR_DELIM "client" DIR_DELIM "ui"
			DIR_DELIM "menu");
	if (!m_host.ok())
		return;

	m_context = m_host.createContext("game");
	if (!m_context)
		return;
	m_sounds = std::make_unique<Sounds>(m_theme_dirs, false);
	m_sounds->attach(*m_context);

	addScreen(std::make_unique<PauseScreen>(*this));
	auto settings = std::make_unique<SettingsScreen>(*this);
	settings->setBack("pause");
	addScreen(std::move(settings));
	loadChrome();
}

GameMenu::~GameMenu()
{
	close();
	m_screens.clear();
	if (m_chrome)
		m_chrome->Close();
	if (m_context)
		m_host.removeContext("game");
	m_sounds.reset();
}

void GameMenu::open()
{
	if (!m_context || m_open)
		return;
	m_open = true;
	g_rml_menu_open = true;
	m_receiver->setUiReceiver(this);
	navigate("pause");
	if (m_chrome) {
		m_chrome->SetClass("shown", true);
		m_chrome->Show();
	}
}

void GameMenu::close()
{
	if (!m_open)
		return;
	m_open = false;
	g_rml_menu_open = false;
	m_receiver->setUiReceiver(nullptr);
	if (m_current) {
		m_current->hide();
		m_current = nullptr;
	}
	if (m_chrome) {
		m_chrome->SetClass("shown", false);
		m_chrome->Hide();
	}
}

void GameMenu::step(f32 dtime, bool window_active)
{
	if (!m_context)
		return;
	if (m_sounds)
		m_sounds->step(dtime, window_active);
	if (!m_open)
		return;
	m_host.update(*m_context);
	if (m_current)
		m_current->settle();
}

void GameMenu::render()
{
	if (m_context && m_open)
		m_host.render(*m_context);
}

void GameMenu::disconnect()
{
	close();
	m_callback->disconnect();
}

void GameMenu::changePassword()
{
	close();
}

void GameMenu::quit()
{
	close();
	m_callback->exitToOS();
}

std::string GameMenu::themeFile(const std::string &name) const
{
	std::string path;
	for (const std::string &dir : m_theme_dirs) {
		path = dir;
		path.append(DIR_DELIM).append(name);
		if (fs::IsFile(path))
			return path;
	}
	errorstream << "GameMenu: theme file \"" << name << "\" is missing" << std::endl;
	return path;
}

void GameMenu::navigate(const std::string &name)
{
	Screen *next = findScreen(name);
	if (!next) {
		errorstream << "GameMenu: no screen \"" << name << "\"" << std::endl;
		return;
	}
	if (m_current == next)
		return;
	if (m_current)
		m_current->hide();
	m_current = next;
	m_current->show();
	showKeys(m_current->keys());
}

Screen *GameMenu::findScreen(const std::string &name)
{
	for (auto &screen : m_screens) {
		if (screen->name() == name)
			return screen.get();
	}
	return nullptr;
}

void GameMenu::showKeys(const std::vector<KeyHint> &keys)
{
	m_keys = keys;
	if (m_chrome_model)
		m_chrome_model.DirtyVariable("keys");
}

bool GameMenu::OnEvent(const SEvent &event)
{
	if (!m_open)
		return false;
	switch (event.EventType) {
	case EET_MOUSE_INPUT_EVENT:
	case EET_KEY_INPUT_EVENT:
	case EET_STRING_INPUT_EVENT:
		if (m_current && m_current->onEvent(event))
			return true;
		if (!m_host.feedEvent(*m_context, event) && m_current)
			m_current->onUnhandledKey(event);
		// Пока меню открыто, игре ввод не достаётся — как при окне Irrlicht.
		return true;
	default:
		return false;
	}
}

void GameMenu::addScreen(std::unique_ptr<Screen> screen)
{
	m_screens.push_back(std::move(screen));
}

void GameMenu::loadChrome()
{
	Rml::DataModelConstructor model = m_context->CreateDataModel("chrome");
	if (auto hint = model.RegisterStruct<KeyHint>()) {
		hint.RegisterMember("key", &KeyHint::key);
		hint.RegisterMember("label", &KeyHint::label);
	}
	model.RegisterArray<std::vector<KeyHint>>();
	model.Bind("version", &m_version);
	model.Bind("keys", &m_keys);
	m_chrome_model = model.GetModelHandle();
	m_chrome = ui::Host::loadDocument(*m_context, themeFile("chrome.rml"));
}

}
