// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#pragma once

#include "util/string.h"
#include <cwctype>
#include <string>

namespace menu
{

// Заголовки разделов стоят капителью; text-transform у RmlUi знает только
// латиницу, поэтому регистр меняется до того, как текст попадёт в документ.
inline std::string uppercase(const std::string &s)
{
	std::wstring wide = utf8_to_wide(s);
	for (wchar_t &c : wide)
		c = std::towupper(c);
	return wide_to_utf8(wide);
}

}
