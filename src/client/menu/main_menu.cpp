// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#include "main_menu.h"

#include "client/clouds.h"
#include "client/inputhandler.h"
#include "client/renderingengine.h"
#include "content/crates.h"
#include "filesys.h"
#include "gui/guiMainMenu.h"
#include "log.h"
#include "porting.h"
#include "gettext.h"
#include "screens/about_screen.h"
#include "screens/online_screen.h"
#include "screens/settings_screen.h"
#include "screens/start_screen.h"
#include "screens/worlds_screen.h"
#include "settings.h"
#include "version.h"
#include <IVideoDriver.h>
#include <IrrlichtDevice.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Factory.h>

namespace menu
{

MainMenu::MainMenu(RenderingEngine *engine, ui::Host &host, MyEventReceiver *receiver,
		MainMenuData *data, volatile std::sig_atomic_t &kill) :
	m_engine(engine),
	m_receiver(receiver),
	m_data(data),
	m_kill(kill),
	m_host(host),
	m_launcher(m_net),
	m_servers(m_launcher),
	m_matchmaking(m_net, m_launcher, m_servers)
{
	m_theme_dirs.push_back(porting::path_share + DIR_DELIM "client" DIR_DELIM "ui"
			DIR_DELIM "menu");

	if (!m_host.ok())
		return;

	m_context = m_host.createContext("menu");
	m_receiver->setUiReceiver(this);

	addScreen(std::make_unique<StartScreen>(*this));
	addScreen(std::make_unique<WorldsScreen>(*this));
	addScreen(std::make_unique<OnlineScreen>(*this));
	addScreen(std::make_unique<SettingsScreen>(*this));
	addScreen(std::make_unique<AboutScreen>(*this));
	loadChrome();
	navigate(findScreen(m_data->screen) ? m_data->screen : "start");
	m_data->screen.clear();

	m_matchmaking.setOnMatch([this](const std::string &address, int port,
			const std::string &server_id, const std::string &match) {
		startJoin(address, port, server_id, match);
	});

	// Меню открыто — значит игра запущена, а игрок ещё никуда не пошёл.
	m_launcher.inMenu();
	// Имя приходит от лаунчера: сессия есть только у него, и логин могли
	// сменить с другой машины. Прав оно не даёт — ими распоряжается билет.
	m_launcher.login([](const std::string &login) {
		if (!login.empty())
			g_settings->set("name", login);
	});
	// Позвали из Discord — открываемся сразу на матчах. Спрашивается здесь,
	// а не на экране матчей: лаунчер поднимает игру по приглашению, и
	// человек видит стартовую страницу, ничего не понимая.
	m_launcher.invite([this](const std::string &room) {
		if (room.empty())
			return;
		m_matchmaking.invited(room);
		navigate("online");
	});
}

MainMenu::~MainMenu()
{
	m_receiver->setUiReceiver(nullptr);
	m_screens.clear();
	if (m_chrome)
		m_chrome->Close();
	if (m_context)
		m_host.removeContext("menu");
}

void MainMenu::run()
{
	if (!m_context) {
		m_data->script_data.setError("The main menu could not start: RmlUi is unavailable.");
		m_kill = 1;
		return;
	}

	IrrlichtDevice *device = m_engine->get_raw_device();
	video::IVideoDriver *driver = device->getVideoDriver();

	// Небо и облака под меню — те же, что на экране загрузки; цвета по
	// menu_theme, как у прежнего меню.
	const bool dark = g_settings->get("menu_theme") == "dark";
	const video::SColor sky = dark ? video::SColor(255, 0x09, 0x0b, 0x1a)
			: video::SColor(255, 0x8c, 0xba, 0xfa);
	const video::SColor clouds = dark ? video::SColor(255, 0x1c, 0x2a, 0x47)
			: video::SColor(255, 0xf0, 0xf0, 0xff);
	m_engine->m_menu_sky_color = sky;
	m_engine->m_menu_clouds_color = clouds;
	const bool draw_clouds = g_settings->getBool("menu_clouds") && g_menuclouds;

	FpsControl fps_control;
	f32 dtime = 0.0f;
	fps_control.reset();

	while (m_engine->run() && !m_start_game && !m_kill) {
		fps_control.limit(device, &dtime);
		if (!device->isWindowVisible())
			continue;

		m_net.poll();
		if (m_servers.poll() && m_current)
			m_current->refresh();

		m_host.setPixelRatio(RenderingEngine::getDisplayDensity() *
				g_settings->getFloat("gui_scaling", 0.5f, 20.0f));
		m_host.update(*m_context);
		if (m_current)
			m_current->afterUpdate();

		driver->setFog(sky);
		driver->beginScene(true, true, sky);
		if (draw_clouds) {
			g_menuclouds->update(v3f(0, 0, 0), clouds);
			g_menuclouds->step(dtime * 3);
			g_menucloudsmgr->drawAll();
		}
		m_host.render(*m_context);
		driver->endScene();
	}
}

std::string MainMenu::themeFile(const std::string &name) const
{
	std::string path;
	for (const std::string &dir : m_theme_dirs) {
		path = dir;
		path.append(DIR_DELIM).append(name);
		if (fs::IsFile(path))
			return path;
	}
	errorstream << "MainMenu: theme file \"" << name << "\" is missing" << std::endl;
	return path;
}

void MainMenu::navigate(const std::string &name)
{
	Screen *next = findScreen(name);
	if (!next) {
		errorstream << "MainMenu: no screen \"" << name << "\"" << std::endl;
		return;
	}
	if (m_current == next)
		return;
	if (m_current)
		m_current->hide();
	m_current = next;
	m_current->show();
}

void MainMenu::quit()
{
	m_kill = 1;
}

void MainMenu::startSingleplayer(const WorldSpec &world)
{
	const std::vector<WorldSpec> worlds = getAvailableWorlds();
	for (size_t i = 0; i < worlds.size(); i++) {
		if (worlds[i].path != world.path)
			continue;
		m_data->selected_world = (int)i;
		m_data->mode = GameClientData::GM_SINGLEPLAYER;
		m_data->address.clear();
		m_data->ticket.clear();
		m_data->server_id.clear();
		m_data->script_data.message.clear();
		m_data->screen = "worlds";
		m_launcher.playingSolo();
		m_start_game = true;
		return;
	}
	errorstream << "MainMenu: world \"" << world.path << "\" is gone" << std::endl;
}

// Билет — короткая подписанная строка, которой игрок доказывает серверу, кто
// он; выписывается на конкретный сервер и гасится при входе, поэтому берётся
// здесь, когда уже известно, куда идём, и каждый раз заново.
void MainMenu::startJoin(const std::string &address, int port, const std::string &server_id,
		const std::string &match)
{
	if (m_joining)
		return;
	m_join_error.clear();

	const std::string server_name = m_servers.nameOf(server_id, address, port);
	if (!match.empty())
		m_launcher.playingMatch(server_name, match);
	else
		m_launcher.playingServer(server_name);

	m_data->mode = GameClientData::GM_JOIN;
	m_data->address = address;
	m_data->port = std::to_string(port);
	m_data->server_id = server_id;
	m_data->ticket.clear();
	m_data->password.clear();
	m_data->selected_world = -1;
	m_data->name = g_settings->get("name");
	m_data->script_data.message.clear();
	m_data->screen = "online";

	if (!m_launcher.available()) {
		// Клиент запустили без лаунчера. Билета не будет, и это не наша
		// беда: сервер, который его спрашивает, откажет и скажет почему.
		m_start_game = true;
		return;
	}
	// Введённый руками адрес ищется в реестре: не нашёлся — билета не будет,
	// и игрок должен узнать об этом здесь, а не получить отказ от сервера.
	if (server_id.empty()) {
		m_join_error = strgettext("This server is not in the registry, so no ticket can be issued for it.");
		if (m_current)
			m_current->refresh();
		return;
	}

	m_joining = true;
	if (m_current)
		m_current->refresh();
	m_launcher.ticket(server_id, [this](const Launcher::Ticket &answer) {
		m_joining = false;
		if (answer.trouble == "refused") {
			// Лаунчер уже перевёл отказ службы на человеческий.
			m_join_error = answer.said.empty()
					? strgettext("The launcher refused to give a ticket.") : answer.said;
		} else if (!answer.trouble.empty()) {
			m_join_error = strgettext("The launcher is not answering. Is it still running?");
		}
		if (!m_join_error.empty()) {
			m_launcher.inMenu();
			if (m_current)
				m_current->refresh();
			return;
		}
		// Имя приходит вместе с билетом: сервер сверяет его с тем, что в
		// билете записано, и не сойдётся — не пустит.
		if (!answer.login.empty()) {
			m_data->name = answer.login;
			g_settings->set("name", answer.login);
		}
		m_data->ticket = answer.ticket;
		m_start_game = true;
	});
}

bool MainMenu::OnEvent(const SEvent &event)
{
	if (event.EventType == EET_KEY_INPUT_EVENT && event.KeyInput.PressedDown) {
		if (event.KeyInput.Key == KEY_F8) {
			m_host.toggleDebugger(*m_context);
			return true;
		}
		if (event.KeyInput.Key == KEY_F5) {
			reloadTheme();
			return true;
		}
	}

	switch (event.EventType) {
	case EET_MOUSE_INPUT_EVENT:
	case EET_KEY_INPUT_EVENT:
	case EET_STRING_INPUT_EVENT:
		if (m_current && m_current->onEvent(event))
			return true;
		m_host.feedEvent(*m_context, event);
		// Под меню нет ни игры, ни другого интерфейса, которым этот ввод
		// мог бы пригодиться.
		return true;
	default:
		return false;
	}
}

void MainMenu::addScreen(std::unique_ptr<Screen> screen)
{
	m_screens.push_back(std::move(screen));
}

Screen *MainMenu::findScreen(const std::string &name)
{
	for (auto &screen : m_screens) {
		if (screen->name() == name)
			return screen.get();
	}
	return nullptr;
}

void MainMenu::loadChrome()
{
	if (m_chrome) {
		m_chrome->Close();
		m_chrome = nullptr;
		m_context->RemoveDataModel("chrome");
	}
	Rml::DataModelConstructor model = m_context->CreateDataModel("chrome");
	m_version = g_version_hash;
	model.Bind("version", &m_version);
	m_chrome = m_context->LoadDocument(themeFile("chrome.rml"));
	if (m_chrome)
		m_chrome->Show();
}

void MainMenu::reloadTheme()
{
	// Стили и шаблоны RmlUi держит в кэше по пути: без сброса документ
	// перечитается, а таблица стилей — нет.
	Rml::Factory::ClearStyleSheetCache();
	Rml::Factory::ClearTemplateCache();
	for (auto &screen : m_screens)
		screen->reload();
	loadChrome();
}

}
