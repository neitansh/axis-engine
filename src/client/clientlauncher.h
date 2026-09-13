// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2010-2013 celeron55, Perttu Ahola <celeron55@gmail.com>

#pragma once

#include <memory>
#include <string>

class RenderingEngine;
class Settings;
class MyEventReceiver;
class InputHandler;
struct GameParams;
struct GameStartData;
struct MainMenuData;

namespace menu
{
class LoadScreen;
}
namespace ui
{
class Host;
}

class ClientLauncher
{
public:
	ClientLauncher();

	~ClientLauncher();

	bool run(const GameParams &game_params, const Settings &cmd_args);

private:
	void init_args(GameStartData &start_data, const Settings &cmd_args);
	void init_engine();
	void init_input();

	static void setting_changed_callback(const std::string &name, void *data);
	static void language_changed_callback(const std::string &name, void *data);
	void config_guienv();

	bool launch_game(GameErrorData &errordata,
		GameStartData &start_data, const Settings &cmd_args);

	void main_menu(MainMenuData *menudata);

	bool skip_main_menu = false;
	bool random_input = false;
	RenderingEngine *m_rendering_engine = nullptr;
	InputHandler *input = nullptr;
	MyEventReceiver *receiver = nullptr;
	// Интерфейс на RmlUi живёт весь запуск клиента: меню, экран загрузки и
	// всё, что покажут в игре, делят один хост.
	std::unique_ptr<ui::Host> m_ui;
	std::unique_ptr<menu::LoadScreen> m_load_screen;
	// Экран меню, на который возвращаются после игры или неудачного входа.
	std::string m_menu_screen;
};
