// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2010-2013 celeron55, Perttu Ahola <celeron55@gmail.com>

#pragma once

#include "irrlichttypes.h"
#include "content/subgames.h"
#include "log.h" // errorstream

// Information provided from "main"
// Start information for server or client
struct GameParams
{
	GameParams() = default;

	u16 socket_port;
	std::string world_path;
	SubgameSpec game_spec;
	bool is_dedicated_server;
};

enum class ELoginRegister {
	Any = 0,
	Login,
	Register
};

struct GameClientData
{
	bool isSinglePlayer() const { return mode == GM_SINGLEPLAYER; }
	bool isAnyServer() const
	{ return mode == GM_HOST_AND_JOIN || mode == GM_SINGLEPLAYER; }

	std::string name;
	std::string password;
	std::string address; //< non-empty when joining a server

	// Signed proof of who the player is, issued by an account service and
	// handed to the server on connect. Empty when nobody vouched for this
	// player: the server then decides on its own whether to let them in.
	//
	// This is not a password and never travels as one. A password proves a
	// secret the server already knows; a ticket carries a name the server can
	// check against a public key without knowing anything in advance. Servers
	// run by other people are the reason for the difference: they must be able
	// to tell who is connecting without being trusted with any secret of ours.
	std::string ticket;

	enum Mode {
		GM_SINGLEPLAYER,
		GM_HOST_AND_JOIN,
		GM_JOIN,
		GM_Undefined
	} mode = GM_Undefined;

	ELoginRegister allow_login_or_register = ELoginRegister::Any;
};

// Information provided by ClientLauncher
// Client-only data (joining or hosting server)
struct GameStartData : GameParams, GameClientData
{
	GameStartData() = default;

	// "world_path" must be kept in sync!
	WorldSpec world_spec;
};

/// Data provided to Lua for error reporting by the main menu
struct GameErrorData
{
	GameErrorData() = default;

	/// Raise and log an error
	/// @param msg Empty string means no error
	void setError(const std::string &msg, bool reconnect = false)
	{
		message = msg;
		reconnect_requested = reconnect;
		errorstream << msg << std::endl;
	}

	// Whether the server has requested a reconnect
	bool reconnect_requested = false;
	std::string message;
};
