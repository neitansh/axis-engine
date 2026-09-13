// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#pragma once

#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Input.h>

namespace menu
{

inline Rml::Input::KeyIdentifier keyOf(const Rml::Event &event)
{
	return (Rml::Input::KeyIdentifier)event.GetParameter<int>("key_identifier", 0);
}

// Enter и пробел — «нажать» то, что в фокусе, как у RmlUi.
inline bool isEnter(const Rml::Event &event)
{
	const Rml::Input::KeyIdentifier key = keyOf(event);
	return key == Rml::Input::KI_RETURN || key == Rml::Input::KI_NUMPADENTER
			|| key == Rml::Input::KI_SPACE;
}

}
