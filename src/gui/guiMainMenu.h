// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2010-2013 celeron55, Perttu Ahola <celeron55@gmail.com>

#pragma once

#include "gameparams.h"
#include <string>

struct MainMenuData : GameClientData {
	MainMenuData(GameErrorData &errordata) :
		script_data(errordata)
	{}

	// Client options
	std::string port; // TODO combine into GameClientData

	// Whether to reconnect
	bool do_reconnect = false;

	// Server options
	int selected_world = 0;

	// Экран меню, с которого ушли в игру и на который вернутся. Меню
	// строится заново после каждой игры, и без этого игрок, которого не
	// пустило на сервер, оказывался бы на стартовом экране, а не там, где
	// нажимал «Подключиться».
	std::string screen;

	// Data to be passed to the script
	GameErrorData &script_data;
};
