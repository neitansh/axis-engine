// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#pragma once

#include "launcher.h"
#include "matchmaking.h"
#include "net.h"
#include "server_list.h"
#include "ui/host.h"
#include <IEventReceiver.h>
#include <csignal>
#include <memory>
#include <string>
#include <vector>

class MyEventReceiver;
class RenderingEngine;
struct MainMenuData;
struct WorldSpec;

namespace Rml
{
class ElementDocument;
}

namespace menu
{

class Screen;

// Главное меню на RmlUi. Логика — здесь и в экранах, вид — файлы темы:
// движок несёт тему по умолчанию в client/ui/menu, крейт может положить
// свою поверх и перерисовать меню, не трогая код. Экраны ходят к меню за
// навигацией, запуском игры и путями темы.
class MainMenu : private IEventReceiver
{
public:
	MainMenu(RenderingEngine *engine, MyEventReceiver *receiver,
			MainMenuData *data, volatile std::sig_atomic_t &kill);
	~MainMenu();

	// Крутит меню, пока игрок не запустит игру или не выйдет.
	void run();

	Rml::Context &context() { return *m_context; }

	// Путь к файлу темы: первый найденный по списку каталогов темы.
	std::string themeFile(const std::string &name) const;

	void navigate(const std::string &screen);
	void quit();
	void startSingleplayer(const WorldSpec &world);
	// Вход на сервер по адресу. Билет спрашивается у лаунчера здесь же:
	// дорог в игру несколько, а билет им нужен одинаково. match — название
	// матча, если адрес выдал Диспетчер: лаунчер показывает матч и обычный
	// сервер по-разному, а по адресу их не отличить.
	void startJoin(const std::string &address, int port, const std::string &server_id,
			const std::string &match);
	// Пока билет в пути, кнопка входа говорит об этом сама.
	bool joining() const { return m_joining; }
	// Чем кончился последний вход, если не игрой; пусто — всё хорошо.
	const std::string &joinError() const { return m_join_error; }
	void clearJoinError() { m_join_error.clear(); }
	void setJoinError(const std::string &error) { m_join_error = error; }

	Launcher &launcher() { return m_launcher; }
	ServerList &servers() { return m_servers; }
	Matchmaking &matchmaking() { return m_matchmaking; }

private:
	bool OnEvent(const SEvent &event) override;
	void addScreen(std::unique_ptr<Screen> screen);
	Screen *findScreen(const std::string &name);
	void loadChrome();
	void reloadTheme();

	RenderingEngine *m_engine;
	MyEventReceiver *m_receiver;
	MainMenuData *m_data;
	volatile std::sig_atomic_t &m_kill;

	ui::Host m_host;
	Net m_net;
	Launcher m_launcher;
	ServerList m_servers;
	Matchmaking m_matchmaking;
	Rml::Context *m_context = nullptr;
	std::vector<std::string> m_theme_dirs;
	std::vector<std::unique_ptr<Screen>> m_screens;
	Screen *m_current = nullptr;
	// Общая рамка поверх экранов: версия в углу и то, что должно быть видно
	// везде.
	Rml::ElementDocument *m_chrome = nullptr;
	Rml::String m_version;
	bool m_start_game = false;
	bool m_joining = false;
	std::string m_join_error;
};

}
