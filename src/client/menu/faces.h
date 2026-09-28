// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#pragma once

#include "net.h"
#include <functional>
#include <set>
#include <string>

namespace menu
{

// Лица игроков по хэшу облика: скачиваются один раз и лежат в кэше, RmlUi
// читает картинку только с диска.
class Faces
{
public:
	explicit Faces(Net &net) : m_net(net) {}

	// Пусто, пока лицо не скачано или его нет вовсе; скачалось — on_change.
	std::string path(const std::string &skin, const std::string &url);

	void setOnChange(std::function<void()> on_change) { m_on_change = std::move(on_change); }

private:
	Net &m_net;
	std::set<std::string> m_asked;
	std::function<void()> m_on_change;
};

}
