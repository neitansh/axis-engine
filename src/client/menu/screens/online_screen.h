// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#pragma once

#include "client/menu/screen.h"
#include <string>
#include <vector>

namespace menu
{

// Сетевая игра: две дороги в одну и ту же игру — подбор матча и список
// серверов, — переключаются вкладками наверху. Состояние подбора живёт в
// Matchmaking при меню; экран только показывает его и передаёт нажатия.
class OnlineScreen final : public Screen
{
public:
	explicit OnlineScreen(MainMenu &menu);

	void refresh() override;
	void entered() override;
	void left() override;
	void afterUpdate() override;
	bool onEvent(const SEvent &event) override;

protected:
	void bind(Rml::DataModelConstructor &model) override;

private:
	struct ModeEntry
	{
		Rml::String id;
		Rml::String title;
		Rml::String count;
	};

	struct ServerRow
	{
		int index = -1;
		Rml::String kind;
		Rml::String name;
		Rml::String players;
		Rml::String ping;
		Rml::String ping_class;
		bool favorite = false;
		bool official = false;
	};

	void open(const std::string &mode);
	void rebuildMatches();
	void rebuildServers();
	void selectServer(int index);
	void joinSelected();
	void connect();

	Rml::String m_mode = "matches";
	Rml::String m_tab_indicator;
	Rml::String m_title;
	Rml::String m_matches_heading;
	Rml::String m_servers_heading;
	Rml::String m_connection_heading;
	Rml::String m_direct_heading;

	// Матчи
	bool m_decided = false;
	Rml::String m_connection;
	Rml::String m_connection_class;
	bool m_modes_loaded = false;
	std::vector<ModeEntry> m_modes;
	bool m_waiting = false;
	Rml::String m_queue_title;
	Rml::String m_queue_line;
	Rml::String m_queue_below;
	Rml::String m_status;

	// Серверы
	bool m_launcher = false;
	bool m_list_loaded = false;
	std::vector<ServerRow> m_rows;
	int m_selected = -1;
	Rml::String m_selected_name;
	Rml::String m_selected_address;
	bool m_can_favorite = false;
	bool m_is_favorite = false;
	Rml::String m_address;
	Rml::String m_port;
	bool m_joining = false;
	Rml::String m_join_error;
	Rml::String m_hint;

	bool m_dirty = false;
};

}
