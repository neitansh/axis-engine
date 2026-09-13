// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#pragma once

#include "client/renderingengine.h"
#include "ui/host.h"
#include <IEventReceiver.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <csignal>
#include <string>

class MyEventReceiver;

namespace Rml
{
class Context;
class ElementDocument;
}

namespace menu
{

// Экран загрузки на RmlUi: подключение, содержимое сервера, текстуры — всё,
// что движок показывает между меню и игрой. Он же показывает, чем кончилась
// неудачная попытка: ошибка стоит на том же экране, а «Назад» и Esc возвращают
// в меню на тот экран, откуда игрок уходил. Пока идёт загрузка, стрелка
// «Назад» делает то же, что Esc, — прерывает подключение.
class LoadScreen final : public RenderingEngine::LoadScreen, private IEventReceiver
{
public:
	LoadScreen(RenderingEngine *engine, ui::Host &host, MyEventReceiver *receiver);
	~LoadScreen() override;

	// Имя страницы в шапке на время следующей загрузки.
	void setTitle(const std::string &title);

	void draw(const std::wstring &text, ITextureSource *tsrc, float dtime, int percent,
			float *indef_pos, const std::wstring &bottom_text) override;

	// Показывает ошибку и ждёт, пока игрок нажмёт «Назад» или Esc.
	void showError(const std::string &message, volatile std::sig_atomic_t &kill);

private:
	bool OnEvent(const SEvent &event) override;
	void load();
	void frame(float dtime);
	void pressEscape();

	RenderingEngine *m_engine;
	ui::Host &m_host;
	MyEventReceiver *m_receiver;
	Rml::Context *m_context = nullptr;
	Rml::ElementDocument *m_document = nullptr;
	Rml::DataModelHandle m_model;

	Rml::String m_title;
	Rml::String m_status;
	Rml::String m_detail;
	Rml::String m_error;
	Rml::String m_fill_left = "0%";
	Rml::String m_fill_width = "0%";
	bool m_failed = false;
	bool m_back = false;
	bool m_injecting = false;
	// Последний кадр загрузки: события мыши идут сюда только пока экран на виду.
	u64 m_drawn_at = 0;
	float m_indef = 0;
};

}
