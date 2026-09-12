// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#pragma once

#include "ui/host.h"
#include <IEventReceiver.h>
#include <csignal>
#include <memory>
#include <string>
#include <vector>

class MyEventReceiver;
class RenderingEngine;
struct MainMenuData;
struct WorldSpec;

namespace menu
{

class Screen;

// Главное меню на RmlUi. Логика — здесь и в экранах, вид — файлы темы:
// движок несёт тему по умолчанию в client/ui/menu, крейт может положить
// свою поверх и перерисовать меню, не трогая код. Экраны ходят к меню за
// навигацией, запуском игры и путями темы.
class MainMenu : private IEventReceiver
{
public:
	MainMenu(RenderingEngine *engine, MyEventReceiver *receiver,
			MainMenuData *data, volatile std::sig_atomic_t &kill);
	~MainMenu();

	// Крутит меню, пока игрок не запустит игру или не выйдет.
	void run();

	Rml::Context &context() { return *m_context; }

	// Путь к файлу темы: первый найденный по списку каталогов темы.
	std::string themeFile(const std::string &name) const;

	void navigate(const std::string &screen);
	void quit();
	void startSingleplayer(const WorldSpec &world);

private:
	bool OnEvent(const SEvent &event) override;
	void addScreen(std::unique_ptr<Screen> screen);
	Screen *findScreen(const std::string &name);
	void reloadTheme();

	RenderingEngine *m_engine;
	MyEventReceiver *m_receiver;
	MainMenuData *m_data;
	volatile std::sig_atomic_t &m_kill;

	ui::Host m_host;
	Rml::Context *m_context = nullptr;
	std::vector<std::string> m_theme_dirs;
	std::vector<std::unique_ptr<Screen>> m_screens;
	Screen *m_current = nullptr;
	bool m_start_game = false;
};

}
