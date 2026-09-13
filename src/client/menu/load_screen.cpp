// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#include "load_screen.h"

#include "client/clouds.h"
#include "client/inputhandler.h"
#include "filesys.h"
#include "gettext.h"
#include "log.h"
#include "porting.h"
#include "settings.h"
#include "util/string.h"
#include <IGUIEnvironment.h>
#include <IVideoDriver.h>
#include <IrrlichtDevice.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <algorithm>

namespace menu
{

LoadScreen::LoadScreen(RenderingEngine *engine, ui::Host &host, MyEventReceiver *receiver) :
	m_engine(engine),
	m_host(host),
	m_receiver(receiver)
{
	m_title = strgettext("Loading...");
}

LoadScreen::~LoadScreen()
{
	if (m_document)
		m_document->Close();
	if (m_context)
		m_host.removeContext("load");
}

void LoadScreen::load()
{
	if (m_context || !m_host.ok())
		return;
	m_context = m_host.createContext("load");
	if (!m_context)
		return;

	Rml::DataModelConstructor model = m_context->CreateDataModel("loading");
	model.Bind("title", &m_title);
	model.Bind("status", &m_status);
	model.Bind("detail", &m_detail);
	model.Bind("error", &m_error);
	model.Bind("failed", &m_failed);
	model.Bind("fill_left", &m_fill_left);
	model.Bind("fill_width", &m_fill_width);
	model.BindEventCallback("back",
			[this](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
				if (m_failed)
					m_back = true;
				else
					pressEscape();
			});
	m_model = model.GetModelHandle();

	const std::string path = porting::path_share + DIR_DELIM "client" DIR_DELIM "ui"
			DIR_DELIM "menu" DIR_DELIM "loading.rml";
	m_document = m_context->LoadDocument(path);
	if (m_document)
		m_document->Show();
	else
		errorstream << "LoadScreen: " << path << " failed to load" << std::endl;
}

void LoadScreen::setTitle(const std::string &title)
{
	m_title = title;
	if (m_model)
		m_model.DirtyVariable("title");
}

// Стрелка «Назад» во время загрузки нажимает Esc за игрока: подключение
// прерывает тот же код, что и клавиша, и второй дороги ему не нужно.
void LoadScreen::pressEscape()
{
	SEvent event{};
	event.EventType = EET_KEY_INPUT_EVENT;
	event.KeyInput.Key = KEY_ESCAPE;
	event.KeyInput.Char = 0;
	event.KeyInput.PressedDown = true;
	m_injecting = true;
	m_receiver->OnEvent(event);
	event.KeyInput.PressedDown = false;
	m_receiver->OnEvent(event);
	m_injecting = false;
}

bool LoadScreen::OnEvent(const SEvent &event)
{
	if (m_injecting || !m_context)
		return false;
	// Экран загрузки давно не рисовали — значит игра уже идёт, и ввод её.
	if (!m_failed && porting::getTimeMs() - m_drawn_at > 500)
		return false;

	if (event.EventType == EET_KEY_INPUT_EVENT) {
		if (m_failed && event.KeyInput.PressedDown && event.KeyInput.Key == KEY_ESCAPE) {
			m_back = true;
			return true;
		}
		// Клавиши идут дальше: Esc во время подключения ловит сама игра.
		return false;
	}
	if (event.EventType == EET_MOUSE_INPUT_EVENT) {
		m_host.feedEvent(*m_context, event);
		return true;
	}
	return false;
}

void LoadScreen::frame(float dtime)
{
	video::IVideoDriver *driver = m_engine->get_video_driver();
	const video::SColor sky = m_engine->m_menu_sky_color;

	m_host.setPixelRatio(RenderingEngine::getDisplayDensity() *
			g_settings->getFloat("gui_scaling", 0.5f, 20.0f));
	m_host.update(*m_context);

	driver->setFog(sky);
	driver->beginScene(true, true, sky);
	if (g_settings->getBool("menu_clouds") && g_menuclouds) {
		g_menuclouds->step(dtime * 3);
		g_menucloudsmgr->drawAll();
	}
	m_host.render(*m_context);
	driver->endScene();
}

void LoadScreen::draw(const std::wstring &text, ITextureSource *tsrc, float dtime,
		int percent, float *indef_pos, const std::wstring &bottom_text)
{
	load();
	if (!m_context) {
		// RmlUi не поднялся — экран рисует движок, как раньше.
		m_engine->setLoadScreen(nullptr);
		m_engine->draw_load_screen(text, m_engine->get_gui_env(), tsrc, dtime, percent,
				indef_pos, bottom_text);
		m_engine->setLoadScreen(this);
		return;
	}

	m_status = wide_to_utf8(text);
	m_detail = wide_to_utf8(bottom_text);
	m_failed = false;
	m_error.clear();
	if (indef_pos) {
		// Бегунок без конца: отрезок в 40 % едет слева направо и уходит за
		// край, как у встроенного экрана.
		*indef_pos = fmodf(*indef_pos + (dtime * 50.0f), 140.0f);
		const int left = std::max((int)*indef_pos - 40, 0);
		const int right = std::min((int)*indef_pos, 100);
		m_fill_left = std::to_string(left) + "%";
		m_fill_width = std::to_string(std::max(right - left, 0)) + "%";
	} else {
		m_fill_left = "0%";
		m_fill_width = std::to_string(std::clamp(percent, 0, 100)) + "%";
	}
	m_model.DirtyAllVariables();

	m_receiver->setUiReceiver(this);
	m_drawn_at = porting::getTimeMs();
	frame(dtime);
}

void LoadScreen::showError(const std::string &message, volatile std::sig_atomic_t &kill)
{
	load();
	if (!m_context)
		return;

	m_failed = true;
	m_back = false;
	m_error = message;
	m_detail.clear();
	m_fill_left = "0%";
	m_fill_width = "0%";
	m_model.DirtyAllVariables();
	m_receiver->setUiReceiver(this);

	IrrlichtDevice *device = m_engine->get_raw_device();
	if (gui::ICursorControl *cursor = device->getCursorControl()) {
		cursor->setVisible(true);
		cursor->setRelativeMode(false);
	}

	FpsControl fps_control;
	f32 dtime = 0.0f;
	fps_control.reset();
	while (m_engine->run() && !kill && !m_back) {
		fps_control.limit(device, &dtime);
		if (!device->isWindowVisible())
			continue;
		frame(dtime);
	}

	m_receiver->setUiReceiver(nullptr);
	m_failed = false;
	m_error.clear();
}

}
