// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#pragma once

#include <string>
#include <string_view>

/**
 * Ed25519 signature checking.
 *
 * One operation and nothing else: given a message, a signature and a public
 * key, say whether they belong together. The engine never signs anything —
 * signing requires a private key, and a private key handed to every player is
 * not a secret at all.
 *
 * This is what makes a ticket worth anything. Whoever issued it kept the
 * signing half of the key; everyone else, this engine included, gets only the
 * half that checks. A server can therefore tell who is connecting without
 * being trusted with the means to invent that answer.
 */
namespace ed25519
{

constexpr size_t PUBLIC_KEY_SIZE = 32;
constexpr size_t SIGNATURE_SIZE = 64;

/**
 * Does this signature belong to this message and key?
 *
 * Returns false on anything unexpected — wrong sizes, garbage, a signature
 * made with another key. There is deliberately no error code: for the caller
 * there is only one useful distinction, and reasons why a forgery is a forgery
 * are not among the things to act upon.
 */
bool verify(std::string_view message, std::string_view signature,
		std::string_view public_key);

} // namespace ed25519
