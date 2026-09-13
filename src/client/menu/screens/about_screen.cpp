// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#include "about_screen.h"

#include "client/menu/main_menu.h"
#include "client/renderingengine.h"
#include "config.h"
#include "filesys.h"
#include "gettext.h"
#include "porting.h"
#include "util/string.h"
#include "version.h"
#include <IOSOperator.h>
#include <IVideoDriver.h>
#include <IrrlichtDevice.h>
#include <fstream>
#include <json/json.h>

namespace menu
{

namespace
{

const char *deviceName(IrrlichtDevice *device)
{
	switch (device->getType()) {
	case EIDT_WIN32: return "Win32";
	case EIDT_X11: return "X11";
	case EIDT_OSX: return "macOS";
	case EIDT_SDL: return "SDL";
	case EIDT_ANDROID: return "Android";
	default: return "?";
	}
}

}

AboutScreen::AboutScreen(MainMenu &menu) : Screen(menu, "about")
{
	m_version = std::string(PROJECT_NAME_C) + " " + g_version_hash;
	m_build = g_build_info;
	m_user_path = porting::path_user;

	IrrlichtDevice *device = RenderingEngine::get_raw_device();
	std::string renderer = RenderingEngine::get_video_driver()->getName();
	m_renderer = renderer + " · " + deviceName(device);
	if (const std::string version = device->getVersionString(); !version.empty())
		m_renderer += " " + version;
	m_platform = porting::get_sysinfo();

	loadCredits();
}

// Списки — авторы Luanti, движка, из которого Axis вырос. Без заголовка об
// этом их читают как авторов Axis, а это присвоение чужой работы.
void AboutScreen::loadCredits()
{
	m_credits.clear();
	const std::string path = porting::path_share + DIR_DELIM "builtin" DIR_DELIM "mainmenu"
			DIR_DELIM "credits.json";
	std::ifstream in(path);
	Json::Value credits;
	Json::CharReaderBuilder builder;
	std::string errors;
	if (!in.good() || !Json::parseFromStream(builder, in, &credits, &errors))
		return;

	const std::pair<const char *, const char *> sections[] = {
		{"core_developers", N_("Core Developers")},
		{"core_team", N_("Core Team")},
		{"contributors", N_("Active Contributors")},
		{"previous_core_developers", N_("Previous Core Developers")},
		{"previous_contributors", N_("Previous Contributors")},
	};
	for (const auto &[key, title] : sections) {
		const Json::Value &list = credits[key];
		if (!list.isArray() || list.empty())
			continue;
		m_credits.push_back({"heading", strgettext(title), ""});
		for (const Json::Value &entry : list) {
			if (!entry.isString())
				continue;
			// «Имя (ник) <почта> [роль]»: роль — приглушённой пометкой, почта
			// на экране не нужна.
			std::string line = entry.asString();
			std::string note;
			const size_t bracket = line.find('[');
			if (bracket != std::string::npos) {
				const size_t close = line.find(']', bracket);
				note = line.substr(bracket + 1, close == std::string::npos ? std::string::npos
						: close - bracket - 1);
				line.erase(bracket);
			}
			const size_t mail = line.find('<');
			if (mail != std::string::npos) {
				const size_t close = line.find('>', mail);
				line.erase(mail, close == std::string::npos ? std::string::npos : close - mail + 1);
			}
			m_credits.push_back({"name", std::string(trim(line)), note});
		}
	}
}

// Что вставить в сообщение об ошибке: версия, сборка, рендер, система.
std::string AboutScreen::report() const
{
	return m_version + "\n" + m_build + "\n" + m_renderer + "\n" + m_platform + "\n";
}

void AboutScreen::bind(Rml::DataModelConstructor &model)
{
	if (auto line = model.RegisterStruct<CreditLine>()) {
		line.RegisterMember("kind", &CreditLine::kind);
		line.RegisterMember("name", &CreditLine::name);
		line.RegisterMember("note", &CreditLine::note);
	}
	model.RegisterArray<std::vector<CreditLine>>();

	model.Bind("version", &m_version);
	model.Bind("build", &m_build);
	model.Bind("renderer", &m_renderer);
	model.Bind("platform", &m_platform);
	model.Bind("user_path", &m_user_path);
	model.Bind("copied", &m_copied);
	model.Bind("credits", &m_credits);

	model.BindEventCallback("open_data",
			[](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
				porting::open_directory(porting::path_user);
			});
	model.BindEventCallback("copy",
			[this](Rml::DataModelHandle handle, Rml::Event &, const Rml::VariantList &) {
				RenderingEngine::get_raw_device()->getOSOperator()->copyToClipboard(
						report().c_str());
				m_copied = strgettext("Copied");
				handle.DirtyVariable("copied");
			});
	model.BindEventCallback("open_url",
			[](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args) {
				if (!args.empty())
					porting::open_url(args[0].Get<Rml::String>());
			});
}

void AboutScreen::refresh()
{
	m_copied.clear();
	model().DirtyAllVariables();
}

bool AboutScreen::onEvent(const SEvent &event)
{
	if (event.EventType != EET_KEY_INPUT_EVENT || !event.KeyInput.PressedDown
			|| event.KeyInput.Key != KEY_ESCAPE)
		return false;
	menu().navigate("start");
	return true;
}

}
