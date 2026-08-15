// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#include "ticket.h"

#include "log.h"
#include "settings.h"
#include "util/base64.h"
#include "util/ed25519.h"

#include <json/json.h>

#include <ctime>

/*
	A ticket looks like this:

		a1.<payload>.<signature>

	The payload is JSON in base64url, the signature covers the string
	"a1.<payload>" as it stands. Dots separate the parts because a ticket
	travels as one field of the handshake and must not need escaping.

	The payload:

		{"k":1,"uid":"…","login":"neitan","name":"Нейтан",
		 "srv":"salvo-official","iat":…,"exp":…,"jti":"…"}

	Everything in it is readable by anyone holding the ticket, and that is
	fine: there are no secrets inside. What protects it is that the signature
	cannot be forged and that it dies within minutes.
*/

namespace
{

constexpr const char *TICKET_PREFIX = "a1";

/**
 * The account service's public key, built into the engine.
 *
 * Built in rather than configured, and that is deliberate. A key that can be
 * pointed elsewhere by a setting is a key that can be replaced with one's own,
 * and then anybody can write their own tickets and walk in as anyone. The one
 * thing this whole mechanism is for would be gone, quietly, through a config
 * file.
 *
 * Rotating it therefore means shipping a new build. That is a fair price: the
 * key changes about never, and every server in the world has to agree on it.
 */
const char *AUTH_PUBLIC_KEY_BASE64 = "Vsvo4JnNiJZ4CUaU/LJrYFF+C4ekjeG4e8wDCymP8MA=";

/// base64url → bytes. The alphabet differs from plain base64 in two letters
/// and carries no padding, so it survives inside a URL — or, as here, inside a
/// dot-separated string.
std::string base64url_decode(std::string_view in)
{
	std::string plain(in);
	for (char &c : plain) {
		if (c == '-')
			c = '+';
		else if (c == '_')
			c = '/';
	}
	while (plain.size() % 4 != 0)
		plain.push_back('=');
	if (!base64_is_valid(plain))
		return "";
	return base64_decode(plain);
}

const std::string &authPublicKey()
{
	static const std::string key = base64_decode(AUTH_PUBLIC_KEY_BASE64);
	return key;
}

} // namespace

TicketError checkTicket(const std::string &ticket, const std::string &expect_login,
		const std::string &server_id, TicketIdentity *out)
{
	// Split into the three parts. Signature last, everything before the last
	// dot is what was signed.
	const size_t first_dot = ticket.find('.');
	const size_t last_dot = ticket.rfind('.');
	if (first_dot == std::string::npos || first_dot == last_dot)
		return TicketError::Malformed;
	if (ticket.compare(0, first_dot, TICKET_PREFIX) != 0)
		return TicketError::Malformed;

	const std::string signed_part = ticket.substr(0, last_dot);
	const std::string payload_raw = base64url_decode(
			std::string_view(ticket).substr(first_dot + 1, last_dot - first_dot - 1));
	const std::string signature = base64url_decode(
			std::string_view(ticket).substr(last_dot + 1));
	if (payload_raw.empty() || signature.empty())
		return TicketError::Malformed;

	// The signature comes first, before anything inside is believed. Until it
	// checks out the payload is just bytes somebody sent us.
	if (!ed25519::verify(signed_part, signature, authPublicKey()))
		return TicketError::BadSignature;

	Json::Value payload;
	{
		Json::CharReaderBuilder builder;
		std::string errors;
		const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
		if (!reader->parse(payload_raw.data(), payload_raw.data() + payload_raw.size(),
				&payload, &errors))
			return TicketError::Malformed;
	}
	if (!payload.isObject())
		return TicketError::Malformed;

	TicketIdentity id;
	id.uid = payload.get("uid", "").asString();
	id.login = payload.get("login", "").asString();
	id.display = payload.get("name", "").asString();
	id.expires = payload.get("exp", 0).asInt64();
	if (id.uid.empty() || id.login.empty())
		return TicketError::Malformed;
	if (id.display.empty())
		id.display = id.login;

	if (id.expires <= (s64)std::time(nullptr))
		return TicketError::Expired;

	// A ticket may name the server it was issued for. Then it is good there
	// and nowhere else: one that ends up on somebody else's server — and it
	// will, because the client shows it to whoever it connects to — is worth
	// nothing anywhere but there.
	const std::string issued_for = payload.get("srv", "").asString();
	if (!issued_for.empty() && issued_for != server_id)
		return TicketError::WrongServer;

	if (id.login != expect_login)
		return TicketError::NameMismatch;

	*out = id;
	return TicketError::None;
}

const char *ticketErrorText(TicketError err)
{
	switch (err) {
	case TicketError::None: return "";
	case TicketError::Malformed: return "Ticket is malformed";
	case TicketError::BadSignature: return "Ticket signature does not match";
	case TicketError::Expired: return "Ticket has expired";
	case TicketError::WrongServer: return "Ticket was issued for another server";
	case TicketError::NameMismatch: return "Ticket was issued for another name";
	}
	return "Ticket was refused";
}
