// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#include "host.h"

#include "debug.h"
#include "filesys.h"
#include "log.h"
#include "porting.h"
#include "util/string.h"
#include <IVideoDriver.h>
#include <IrrlichtDevice.h>
#include <RmlUi/Core.h>
#include <RmlUi/Debugger.h>

namespace ui
{

static bool g_host_alive = false;

Host::Host(IrrlichtDevice *device) :
	m_device(device),
	m_system(device),
	m_renderer(device->getVideoDriver())
{
	FATAL_ERROR_IF(g_host_alive, "ui::Host is created twice");
	g_host_alive = true;

	if (!m_renderer) {
		errorstream << "ui::Host: the OpenGL 3 renderer for RmlUi could not "
				"be created; the interface stays off" << std::endl;
		return;
	}

	Rml::SetSystemInterface(&m_system);
	Rml::SetFileInterface(&m_files);
	Rml::SetRenderInterface(&m_renderer);
	if (!Rml::Initialise()) {
		errorstream << "ui::Host: RmlUi failed to initialise" << std::endl;
		return;
	}

	loadFonts();
	m_ok = true;
	infostream << "ui::Host: RmlUi " << Rml::GetVersion() << " is up" << std::endl;
}

Host::~Host()
{
	if (m_ok)
		Rml::Shutdown();
	g_host_alive = false;
}

void Host::loadFonts()
{
	const std::string dir = porting::path_share + DIR_DELIM + "fonts";
	for (const auto &entry : fs::GetDirListing(dir)) {
		if (entry.dir)
			continue;
		const std::string ext = lowercase(fs::GetFilenameFromPath(entry.name.c_str()));
		if (!str_ends_with(ext, ".ttf") && !str_ends_with(ext, ".otf"))
			continue;
		// Шрифт для недостающих глифов: его RmlUi спрашивает последним и
		// для любого семейства, так что CJK и прочее берутся из него.
		const bool fallback = str_starts_with(entry.name, "DroidSansFallback");
		Rml::LoadFontFace(dir + DIR_DELIM + entry.name, fallback);
	}
}

Rml::Context *Host::createContext(const std::string &name)
{
	if (!m_ok)
		return nullptr;
	const core::dimension2du size = m_device->getVideoDriver()->getScreenSize();
	Rml::Context *context = Rml::CreateContext(name, Rml::Vector2i(size.Width, size.Height));
	if (context)
		context->SetDensityIndependentPixelRatio(m_pixel_ratio);
	return context;
}

void Host::removeContext(const std::string &name)
{
	if (m_ok)
		Rml::RemoveContext(name);
}

void Host::setPixelRatio(float ratio)
{
	m_pixel_ratio = ratio;
}

void Host::syncDimensions(Rml::Context &context)
{
	const core::dimension2du size = m_device->getVideoDriver()->getScreenSize();
	const Rml::Vector2i dimensions(size.Width, size.Height);
	if (context.GetDimensions() != dimensions)
		context.SetDimensions(dimensions);
	if (context.GetDensityIndependentPixelRatio() != m_pixel_ratio)
		context.SetDensityIndependentPixelRatio(m_pixel_ratio);
}

void Host::update(Rml::Context &context)
{
	syncDimensions(context);
	context.Update();
}

void Host::render(Rml::Context &context)
{
	const Rml::Vector2i size = context.GetDimensions();
	m_renderer.SetViewport(size.x, size.y);
	m_renderer.beginFrame();
	context.Render();
	m_renderer.endFrame();
}

void Host::toggleDebugger(Rml::Context &context)
{
	if (!m_debugger) {
		Rml::Debugger::Initialise(&context);
		m_debugger = true;
		Rml::Debugger::SetVisible(true);
		return;
	}
	Rml::Debugger::SetContext(&context);
	Rml::Debugger::SetVisible(!Rml::Debugger::IsVisible());
}

}
