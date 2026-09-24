// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#pragma once

#include "chat.h"
#include "ui/host.h"
#include <IEventReceiver.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/Types.h>
#include <deque>
#include <string>
#include <vector>

class Client;
class MyEventReceiver;

namespace Rml
{
class Context;
class ElementDocument;
class Element;
}

// Чат в игре на RmlUi: внизу слева, поверх мира, всегда на экране.
//
// Свёрнут — последние реплики, каждая гаснет через несколько секунд;
// открыт (клавиша чата или «/») — история с прокруткой и поле ввода. Вид —
// документ chat.rml в теме меню (те же цвета, что у меню и сайта), логика —
// здесь: что показать, когда погасить, куда деть набранное. Строки берутся у
// ChatBackend по его же правилам (реплики, слово игры, короткие ответы
// команд); консоль F10 остаётся отдельной, для разработки, и не отсюда.
//
// Пока открыт, забирает ввод у игры (g_rml_menu_open, как меню поверх игры),
// но мир не останавливает: напарник пишет — игра идёт.
class ChatOverlay : public IEventReceiver
{
public:
	ChatOverlay(ui::Host &host, MyEventReceiver *receiver, Client *client,
			ChatBackend *backend);
	~ChatOverlay() override;

	ChatOverlay(const ChatOverlay &) = delete;
	ChatOverlay &operator=(const ChatOverlay &) = delete;

	bool ok() const { return m_document != nullptr; }
	bool isOpen() const { return m_open; }

	// Открыть с набранным началом: пусто, «/» для команды, «.» для клиентской.
	void open(const std::wstring &initial);
	void close();

	// Показывать ли вообще (клавиша «спрятать чат»).
	void setVisible(bool visible);

	// Каждый кадр: новые строки, старение, обновление документа.
	void step(f32 dtime);
	void render();

	bool OnEvent(const SEvent &event) override;

private:
	// Строка чата, как её видит документ.
	struct Line
	{
		Rml::String kind;
		Rml::String name;
		Rml::String html;
		int count = 1;
		bool old = false;  // гаснет
		bool gone = false; // погасла: свёрнутым не занимает места
		f32 age = 0.0f;
	};

	// Что можно набрать дальше: команда, подкоманда, аргумент, игрок.
	struct Option
	{
		Rml::String text;
		Rml::String detail;
		bool chosen = false;

		bool operator==(const Option &other) const
		{
			return text == other.text && detail == other.detail &&
					chosen == other.chosen;
		}
	};

	// Клик по ссылке в строке: с Ctrl открывает её в браузере.
	class LinkListener : public Rml::EventListener
	{
	public:
		void ProcessEvent(Rml::Event &event) override;
	};

	std::string themeFile(const std::string &name) const;
	void loadDocument();
	void addLine(const ChatLine &line);
	void send();
	void setDraft(const std::wstring &text);
	void completeDraft();
	std::vector<ChatPrompt::CommandInfo> commandInfos() const;
	bool cursorAtEnd() const;
	void refreshSuggestions();
	void scrollHistory(float rows);
	void scrollToBottom();
	Rml::Element *draftElement() const;
	Rml::Element *historyElement() const;

	ui::Host &m_host;
	MyEventReceiver *m_receiver;
	Client *m_client;
	ChatBackend *m_backend;
	std::vector<std::string> m_theme_dirs;
	Rml::Context *m_context = nullptr;
	Rml::ElementDocument *m_document = nullptr;
	Rml::DataModelHandle m_model;
	LinkListener m_links;

	std::vector<ChatLine> m_incoming;
	std::vector<Line> m_lines;
	Rml::String m_draft;
	Rml::String m_placeholder;
	std::vector<Option> m_options;
	int m_more = 0;
	Rml::String m_ghost;
	Rml::String m_usage;
	Rml::String m_about;
	bool m_ghost_fits = true;
	bool m_open = false;
	bool m_visible = true;
	// Свёрнутым нечего показать: подложка прячется; нет ни строки — и
	// открытым тоже.
	bool m_empty = true;
	bool m_none = true;
	bool m_focus_pending = false;
	bool m_scroll_pending = false;
	bool m_lines_dirty = false;
};
