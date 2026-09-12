// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#pragma once

#include "files.h"
#include "input.h"
#include "renderer.h"
#include "system.h"
#include <string>

class IrrlichtDevice;

namespace Rml
{
class Context;
}

namespace ui
{

// Хост RmlUi: инициализация библиотеки, её интерфейсы, шрифты, контексты и
// кадр. Один на процесс — RmlUi глобальна. О меню, HUD и прочих экранах
// хост не знает: они создают контексты и грузят документы сами.
class Host
{
public:
	explicit Host(IrrlichtDevice *device);
	~Host();

	Host(const Host &) = delete;
	Host &operator=(const Host &) = delete;

	bool ok() const { return m_ok; }

	// Контекст живёт до removeContext() или до конца хоста.
	Rml::Context *createContext(const std::string &name);
	void removeContext(const std::string &name);

	// Размер окна и масштаб dp подтягиваются перед каждым кадром.
	void setPixelRatio(float ratio);
	void update(Rml::Context &context);
	void render(Rml::Context &context);

	bool feedEvent(Rml::Context &context, const SEvent &event)
	{
		return m_input.feed(context, event);
	}

	void toggleDebugger(Rml::Context &context);

private:
	void loadFonts();
	void syncDimensions(Rml::Context &context);

	IrrlichtDevice *m_device;
	System m_system;
	Files m_files;
	Renderer m_renderer;
	Input m_input;
	float m_pixel_ratio = 1.0f;
	bool m_ok = false;
	bool m_debugger = false;
};

}
