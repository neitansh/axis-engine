// Copyright (C) 2026 the-axis
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <json/json.h>
#include <string>

namespace bedrock
{

/// Разобрать файл модели или анимации. Ошибка попадает в журнал вместе с
/// именем файла: разбираться с выгрузкой из Blockbench иначе невозможно.
bool parseJson(const std::string &text, Json::Value &out,
		const std::string &name);

} // namespace bedrock
