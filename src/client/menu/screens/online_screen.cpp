// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#include "online_screen.h"

#include "client/menu/main_menu.h"
#include "gettext.h"
#include "settings.h"
#include "util/string.h"

namespace menu
{

OnlineScreen::OnlineScreen(MainMenu &menu) : Screen(menu, "online")
{
	m_title = strgettext("Multiplayer");
	m_matches_heading = strgettext("Arenas");
	m_servers_heading = strgettext("Server List");
	m_connection_heading = strgettext("Connection");
	m_direct_heading = strgettext("Address");
	m_tab_indicator = "translateX(0dp)";

	Matchmaking &mm = this->menu().matchmaking();
	mm.setOnChange([this]() { m_dirty = true; });
	this->menu().servers().setOnChange([this]() { m_dirty = true; });
}

void OnlineScreen::bind(Rml::DataModelConstructor &model)
{
	if (auto entry = model.RegisterStruct<ModeEntry>()) {
		entry.RegisterMember("id", &ModeEntry::id);
		entry.RegisterMember("title", &ModeEntry::title);
		entry.RegisterMember("count", &ModeEntry::count);
	}
	if (auto row = model.RegisterStruct<ServerRow>()) {
		row.RegisterMember("index", &ServerRow::index);
		row.RegisterMember("kind", &ServerRow::kind);
		row.RegisterMember("name", &ServerRow::name);
		row.RegisterMember("players", &ServerRow::players);
		row.RegisterMember("ping", &ServerRow::ping);
		row.RegisterMember("ping_class", &ServerRow::ping_class);
		row.RegisterMember("favorite", &ServerRow::favorite);
		row.RegisterMember("official", &ServerRow::official);
	}
	model.RegisterArray<std::vector<ModeEntry>>();
	model.RegisterArray<std::vector<ServerRow>>();

	model.Bind("mode", &m_mode);
	model.Bind("tab_indicator", &m_tab_indicator);
	model.Bind("title", &m_title);
	model.Bind("matches_heading", &m_matches_heading);
	model.Bind("servers_heading", &m_servers_heading);
	model.Bind("connection_heading", &m_connection_heading);
	model.Bind("direct_heading", &m_direct_heading);

	model.Bind("decided", &m_decided);
	model.Bind("connection", &m_connection);
	model.Bind("connection_class", &m_connection_class);
	model.Bind("modes_loaded", &m_modes_loaded);
	model.Bind("modes", &m_modes);
	model.Bind("waiting", &m_waiting);
	model.Bind("queue_title", &m_queue_title);
	model.Bind("queue_line", &m_queue_line);
	model.Bind("queue_below", &m_queue_below);
	model.Bind("status", &m_status);

	model.Bind("launcher", &m_launcher);
	model.Bind("list_loaded", &m_list_loaded);
	model.Bind("servers", &m_rows);
	model.Bind("selected", &m_selected);
	model.Bind("selected_name", &m_selected_name);
	model.Bind("selected_address", &m_selected_address);
	model.Bind("can_favorite", &m_can_favorite);
	model.Bind("is_favorite", &m_is_favorite);
	model.Bind("address", &m_address);
	model.Bind("port", &m_port);
	model.Bind("joining", &m_joining);
	model.Bind("join_error", &m_join_error);
	model.Bind("hint", &m_hint);

	auto index_arg = [](const Rml::VariantList &args) {
		return args.empty() ? -1 : args[0].Get<int>(-1);
	};

	model.BindEventCallback("open",
			[this](Rml::DataModelHandle handle, Rml::Event &, const Rml::VariantList &args) {
				if (!args.empty())
					open(args[0].Get<Rml::String>());
				handle.DirtyAllVariables();
			});
	model.BindEventCallback("join_mode",
			[this](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args) {
				if (!args.empty())
					menu().matchmaking().join(args[0].Get<Rml::String>());
			});
	model.BindEventCallback("cancel",
			[this](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
				menu().matchmaking().cancel();
			});
	model.BindEventCallback("retry",
			[this](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
				menu().matchmaking().retry();
			});
	model.BindEventCallback("select",
			[this, index_arg](Rml::DataModelHandle handle, Rml::Event &, const Rml::VariantList &args) {
				selectServer(index_arg(args));
				handle.DirtyAllVariables();
			});
	model.BindEventCallback("play",
			[this, index_arg](Rml::DataModelHandle handle, Rml::Event &, const Rml::VariantList &args) {
				selectServer(index_arg(args));
				joinSelected();
				handle.DirtyAllVariables();
			});
	model.BindEventCallback("join_selected",
			[this](Rml::DataModelHandle handle, Rml::Event &, const Rml::VariantList &) {
				joinSelected();
				handle.DirtyAllVariables();
			});
	model.BindEventCallback("favorite",
			[this](Rml::DataModelHandle handle, Rml::Event &, const Rml::VariantList &) {
				const std::vector<ServerEntry> &shown = menu().servers().shown();
				if (m_selected >= 0 && (size_t)m_selected < shown.size()) {
					const ServerEntry &server = shown[m_selected];
					if (!server.official)
						menu().servers().setFavorite(server.id, !menu().servers().isFavorite(server.id));
				}
				rebuildServers();
				handle.DirtyAllVariables();
			});
	model.BindEventCallback("refresh",
			[this](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
				menu().servers().sync();
			});
	model.BindEventCallback("connect",
			[this](Rml::DataModelHandle handle, Rml::Event &, const Rml::VariantList &) {
				connect();
				handle.DirtyAllVariables();
			});
}

void OnlineScreen::entered()
{
	m_address = g_settings->get("address");
	m_port = g_settings->get("remote_port");
	menu().clearJoinError();
	open(m_mode);
}

void OnlineScreen::left()
{
	menu().matchmaking().leave();
}

void OnlineScreen::open(const std::string &mode)
{
	Matchmaking &mm = menu().matchmaking();
	if (mode == "matches") {
		if (m_mode != mode || !mm.shown())
			mm.enter();
	} else {
		mm.leave();
		menu().servers().sync();
	}
	m_mode = mode;
	m_tab_indicator = mode == "matches" ? "translateX(0dp)" : "translateX(100%)";
	refresh();
}

void OnlineScreen::afterUpdate()
{
	Matchmaking &mm = menu().matchmaking();
	if (m_mode == "matches")
		mm.update();
	if (m_dirty) {
		m_dirty = false;
		refresh();
	}
}

bool OnlineScreen::onEvent(const SEvent &event)
{
	if (event.EventType != EET_KEY_INPUT_EVENT || !event.KeyInput.PressedDown
			|| event.KeyInput.Key != KEY_ESCAPE)
		return false;
	if (m_waiting)
		menu().matchmaking().cancel();
	else
		menu().navigate("start");
	return true;
}

void OnlineScreen::refresh()
{
	if (!model())
		return;
	m_joining = menu().joining();
	m_join_error = menu().joinError();
	m_launcher = menu().launcher().available();
	rebuildMatches();
	rebuildServers();
	model().DirtyAllVariables();
}

void OnlineScreen::rebuildMatches()
{
	Matchmaking &mm = menu().matchmaking();
	m_decided = mm.decided();

	// Вход называется тот, что выбрал лаунчер; пока Диспетчер не отозвался,
	// вход не называется — не ответит, уйдём к следующему, а игрок уже
	// прочитал не то.
	const std::string own = mm.ownDispatch();
	const ServerEntry *entry = mm.entry();
	if (!own.empty()) {
		m_connection = own;
		m_connection_class = "named";
	} else if (entry && mm.answered()) {
		m_connection = entry->name;
		m_connection_class = "named";
	} else if (entry) {
		m_connection = strgettext("Checking...");
		m_connection_class = "checking";
	} else if (!m_launcher) {
		m_connection = strgettext("Start the game through the launcher to play online");
		m_connection_class = "none";
	} else {
		m_connection = strgettext("No servers");
		m_connection_class = "none";
	}

	m_modes.clear();
	m_modes_loaded = mm.modes().has_value();
	if (mm.modes()) {
		for (const Matchmaking::Mode &mode : *mm.modes()) {
			ModeEntry entry;
			entry.id = mode.id;
			entry.title = mode.title;
			entry.count = std::to_string(mode.waiting) + " / " + std::to_string(mode.players);
			m_modes.push_back(entry);
		}
	}

	m_waiting = mm.queue().has_value();
	if (mm.queue()) {
		const Matchmaking::Queue &q = *mm.queue();
		m_queue_title = q.title;
		// Про готовящуюся арену говорим, только когда комната и правда
		// полна: греть её начинают раньше.
		if (q.room_state == "warming" && q.waiting >= q.needed)
			m_queue_line = strgettext("Everyone is here. Preparing the arena");
		else
			m_queue_line = strgettext("Waiting for players") + "  " + std::to_string(q.waiting)
					+ " / " + std::to_string(q.needed);
		// Когда счёт кончился, комната ещё не ушла: сервер матча строит
		// арену, и это полминуты, а не миг.
		if (q.starts_in > 0)
			m_queue_below = fmtgettext("Starts in %d s", q.starts_in);
		else if (q.waiting < q.min)
			m_queue_below = strgettext("Waiting for more players");
		else if (q.room_state == "warming")
			m_queue_below = strgettext("Preparing the arena");
		else
			m_queue_below = strgettext("Starting");
	}

	m_status = mm.status();
	if (m_status.empty() && !m_join_error.empty() && m_mode == "matches")
		m_status = m_join_error;
}

void OnlineScreen::rebuildServers()
{
	ServerList &list = menu().servers();
	m_list_loaded = list.loaded();
	const std::vector<ServerEntry> &shown = list.shown();

	// Три раздела, и деление это не косметика: любимое — что игрок отметил
	// сам, наши — их поднимает Axis, сообщество — чужие, и среди них
	// любимое теряется, потому звёздочка есть только у них.
	struct Section
	{
		const char *title;
		std::vector<int> rows;
	};
	Section sections[3] = {{N_("Favorites"), {}}, {N_("Official Servers"), {}},
			{N_("Community Servers"), {}}};
	for (size_t i = 0; i < shown.size(); i++) {
		const ServerEntry &server = shown[i];
		if (!server.official && list.isFavorite(server.id))
			sections[0].rows.push_back((int)i);
		else if (server.official)
			sections[1].rows.push_back((int)i);
		else
			sections[2].rows.push_back((int)i);
	}

	m_rows.clear();
	for (const Section &section : sections) {
		if (section.rows.empty())
			continue;
		ServerRow header;
		header.kind = "header";
		header.name = strgettext(section.title);
		m_rows.push_back(header);
		for (int index : section.rows) {
			const ServerEntry &server = shown[index];
			ServerRow row;
			row.index = index;
			row.kind = "row";
			row.name = server.name;
			row.official = server.official;
			row.favorite = !server.official && list.isFavorite(server.id);
			if (server.clients >= 0)
				row.players = std::to_string(server.clients) + " / " + std::to_string(server.clients_max);
			if (server.ping_ms >= 0) {
				row.ping = std::to_string(server.ping_ms) + " ms";
				row.ping_class = server.ping_ms < 80 ? "good" : (server.ping_ms < 200 ? "fair" : "poor");
			} else {
				row.ping_class = "none";
			}
			m_rows.push_back(row);
		}
	}

	if (m_selected < 0 || (size_t)m_selected >= shown.size()) {
		const ServerEntry *last = list.find(g_settings->get("address"),
				stoi(g_settings->get("remote_port")));
		m_selected = -1;
		for (size_t i = 0; last && i < shown.size(); i++) {
			if (&shown[i] == last || (shown[i].address == last->address && shown[i].port == last->port))
				m_selected = (int)i;
		}
	}
	selectServer(m_selected);

	if (!m_launcher)
		m_hint = strgettext("Start the game through the launcher to play online");
	else if (m_list_loaded && shown.empty())
		m_hint = strgettext("No servers");
	else
		m_hint.clear();
}

void OnlineScreen::selectServer(int index)
{
	const std::vector<ServerEntry> &shown = menu().servers().shown();
	m_selected = index >= 0 && (size_t)index < shown.size() ? index : -1;
	if (m_selected < 0) {
		m_selected_name.clear();
		m_selected_address.clear();
		m_can_favorite = false;
		m_is_favorite = false;
		return;
	}
	const ServerEntry &server = shown[m_selected];
	m_selected_name = server.name;
	m_selected_address = server.address + ":" + std::to_string(server.port);
	m_can_favorite = !server.official;
	m_is_favorite = m_can_favorite && menu().servers().isFavorite(server.id);
	g_settings->set("address", server.address);
	g_settings->set("remote_port", std::to_string(server.port));
}

void OnlineScreen::joinSelected()
{
	const std::vector<ServerEntry> &shown = menu().servers().shown();
	if (m_selected < 0 || (size_t)m_selected >= shown.size())
		return;
	const ServerEntry &server = shown[m_selected];
	menu().startJoin(server.address, server.port, server.id, "");
	refresh();
}

// Введённый руками адрес ищется в том же реестре: билет выписывается на
// сервер, а не на адрес.
void OnlineScreen::connect()
{
	const std::string address(trim(m_address));
	const int port = mystoi(std::string(trim(m_port)), 0, 65535);
	if (address.empty() || port <= 0) {
		menu().setJoinError(strgettext("Address and port are required"));
		refresh();
		return;
	}
	g_settings->set("address", address);
	g_settings->set("remote_port", std::to_string(port));
	menu().startJoin(address, port, menu().servers().idOf(address, port), "");
	refresh();
}

}
