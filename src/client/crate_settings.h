// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#pragma once

#include <string>
#include <utility>
#include <vector>

// Графика, которую просит крейт (его crate_client.conf, присылает сервер при
// входе): ставится поверх настроек игрока на время игры. Пропускаются только
// имена из белого списка — что и как рисовать; всё, что про машину игрока
// (окно, дальность, антиалиасинг, клавиши, сеть), сервер трогать не вправе.
// Настройка crate_graphics = false оставляет игроку его графику целиком.
void applyCrateClientSettings(
		const std::vector<std::pair<std::string, std::string>> &pairs);
