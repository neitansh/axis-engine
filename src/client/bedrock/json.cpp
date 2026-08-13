// Copyright (C) 2026 the-axis
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "json.h"
#include "log.h"
#include <memory>

namespace bedrock
{

bool parseJson(const std::string &text, Json::Value &out,
		const std::string &name)
{
	Json::CharReaderBuilder builder;
	builder.settings_["collectComments"] = false;
	const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());

	std::string error;
	if (!reader->parse(text.data(), text.data() + text.size(), &out, &error)) {
		errorstream << "Bedrock: " << name << ": файл не разобран: " << error
				<< std::endl;
		return false;
	}
	return true;
}

} // namespace bedrock
