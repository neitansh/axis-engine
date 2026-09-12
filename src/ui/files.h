// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#pragma once

#include <RmlUi/Core/FileInterface.h>
#include <fstream>
#include <memory>
#include <unordered_map>

namespace ui
{

// Файлы для RmlUi открываются через fs движка: встроенный интерфейс
// библиотеки зовёт fopen, а тот на Windows не понимает путей в UTF-8.
class Files final : public Rml::FileInterface
{
public:
	Rml::FileHandle Open(const Rml::String &path) override;
	void Close(Rml::FileHandle file) override;
	size_t Read(void *buffer, size_t size, Rml::FileHandle file) override;
	bool Seek(Rml::FileHandle file, long offset, int origin) override;
	size_t Tell(Rml::FileHandle file) override;
	size_t Length(Rml::FileHandle file) override;

private:
	std::ifstream &stream(Rml::FileHandle file);

	std::unordered_map<Rml::FileHandle, std::unique_ptr<std::ifstream>> m_open;
	Rml::FileHandle m_next = 1;
};

}
