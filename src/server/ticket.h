// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#pragma once

#include "irrlichttypes.h"

#include <string>

/**
 * Tickets: how a server learns who is connecting.
 *
 * A ticket is a short signed string an account service hands to a player, and
 * the player shows it when connecting. It says who they are — an account key
 * that never changes, a login, a display name — and for how long the statement
 * holds.
 *
 * The check lives in the engine rather than in the game on purpose. Games are
 * written by whoever runs the server, and a check that a server owner has to
 * write is a check that will not be there. Here nobody has to write anything:
 * the server refuses an unproven player before the game sees the connection at
 * all, and the game only gets to ask who came in.
 *
 * Nothing here can be turned off from a game or a setting. That is the point.
 */

/// Who the player turned out to be. Filled in only when the ticket held up.
struct TicketIdentity
{
	/// Account key: assigned once and never changed. Everything the game
	/// remembers about a person should hang off this, not off a name.
	std::string uid;
	/// Unique, searchable, and how the player is known to the engine.
	std::string login;
	/// What to call them. Not unique and may change any day.
	std::string display;
	/// When the ticket stops being valid (unix seconds).
	s64 expires = 0;
};

/// Why a ticket was refused. The player is told this in plain words; the
/// distinctions exist so the server log says what actually went wrong.
enum class TicketError
{
	None,
	Malformed,    ///< not a ticket at all
	BadSignature, ///< signed by something other than the account service
	Expired,      ///< it was a ticket, once
	WrongServer,  ///< issued for a different server
	NameMismatch, ///< the ticket names somebody else
};

/**
 * Check a ticket and find out who it belongs to.
 *
 * @param ticket        what the client sent, verbatim
 * @param expect_login  the name the client is connecting under; a ticket for
 *                      another name is refused, otherwise a valid ticket would
 *                      let its holder play as anyone
 * @param server_id     this server's name in the registry; a ticket issued for
 *                      another server is refused, so one that reaches a
 *                      stranger's server is worthless anywhere else
 * @param out           filled in on success
 * @return TicketError::None when the ticket holds up
 */
TicketError checkTicket(const std::string &ticket, const std::string &expect_login,
		const std::string &server_id, TicketIdentity *out);

/// Human-readable reason, for the log and for the player being turned away.
const char *ticketErrorText(TicketError err);

/**
 * Ask the account service about a ticket that already passed the signature
 * check.
 *
 * Two things only the service knows: whether the account has been closed since
 * the ticket was issued, and what the player is called right now — a ticket
 * lives for minutes, and both can change inside them.
 *
 * Only servers holding a service token can ask; a stranger's server has none
 * and does not need one, since the signature already told it who came.
 *
 * @param reason  set to what the service said when it refuses
 * @return false when the service refused the player. A service that cannot be
 *         reached does NOT refuse: the signature is proof enough on its own,
 *         and a network hiccup must not close the door on everybody.
 */
bool askAccountService(const std::string &ticket, const std::string &server_id,
		TicketIdentity *id, std::string *reason);
