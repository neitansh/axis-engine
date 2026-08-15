// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#include "ed25519.h"

#include "log.h"
#include "porting.h"

#include <cstdlib>
#include <vector>

extern "C" {
#include "tweetnacl.h"
}

extern "C" {

/**
 * TweetNaCl asks for this whenever something is signed or encrypted. The
 * engine does neither: it only checks signatures made elsewhere. If this is
 * ever called, someone started signing here — and that is a decision to be
 * made deliberately, not discovered at runtime with a weak random source.
 */
void randombytes(unsigned char *, unsigned long long)
{
	errorstream << "randombytes() called: the engine does not sign anything, "
		"and has no business generating keys" << std::endl;
	abort();
}

} // extern "C"

namespace ed25519
{

bool verify(std::string_view message, std::string_view signature,
		std::string_view public_key)
{
	if (signature.size() != SIGNATURE_SIZE || public_key.size() != PUBLIC_KEY_SIZE)
		return false;

	// TweetNaCl checks a "signed message": the signature followed by the
	// message itself, and it hands back the message it recovered. We have the
	// two apart, so they are glued together here.
	std::vector<unsigned char> signed_message;
	signed_message.reserve(signature.size() + message.size());
	signed_message.insert(signed_message.end(), signature.begin(), signature.end());
	signed_message.insert(signed_message.end(), message.begin(), message.end());

	// The recovered message goes into a buffer of the same size; we do not
	// need it — we already know what was signed — but the interface wants
	// somewhere to put it.
	std::vector<unsigned char> recovered(signed_message.size());
	unsigned long long recovered_len = 0;

	const int result = crypto_sign_open(recovered.data(), &recovered_len,
			signed_message.data(), signed_message.size(),
			reinterpret_cast<const unsigned char *>(public_key.data()));

	return result == 0;
}

} // namespace ed25519
