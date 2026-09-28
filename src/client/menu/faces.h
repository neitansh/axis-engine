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

// Картинки меню по сети: лица игроков по хэшу облика и обложки крейтов от
// Диспетчера. Скачиваются один раз и лежат в кэше: RmlUi читает картинку
// только с диска.
class Faces
{
public:
	explicit Faces(Net &net) : m_net(net) {}

	// Пусто, пока картинка не скачана или её нет вовсе; скачалась — on_change.
	std::string path(const std::string &skin, const std::string &url);
	// url несёт версию обложки: другая строка — другой файл в кэше.
	std::string cover(const std::string &crate, const std::string &url);

	void setOnChange(std::function<void()> on_change) { m_on_change = std::move(on_change); }

private:
	std::string fetch(const std::string &dir, const std::string &file, const std::string &url);

	Net &m_net;
	std::set<std::string> m_asked;
	std::function<void()> m_on_change;
};

}
