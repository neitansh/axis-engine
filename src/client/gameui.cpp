// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2010-2013 celeron55, Perttu Ahola <celeron55@gmail.com>
// Copyright (C) 2018 nerzhul, Loic Blot <loic.blot@unix-experience.fr>

#include "gameui.h"
#include <irrlicht_changes/static_text.h>
#include <gettext.h>
#include "gui/mainmenumanager.h"
#include "gui/guiChatConsole.h"
#include "gui/statusTextHelper.h"
#include "gui/touchcontrols.h"
#include "util/enriched_string.h"
#include "util/pointedthing.h"
#include "client.h"
#include "clientmap.h"
#include "fontengine.h"
#include "hud_element.h" // HUD_FLAG_*
#include "nodedef.h"
#include "localplayer.h"
#include "profiler.h"
#include "renderingengine.h"
#include "version.h"
#include "itemdef.h"
#include "util/numeric.h"
#include "porting.h"
#include <IGUIFont.h>
#include <IGUIStaticText.h>
#include <algorithm>
#include <cmath>
#include <locale>

inline static const char *yawToDirectionString(int yaw)
{
	static const char *direction[4] =
		{"North +Z", "West -X", "South -Z", "East +X"};

	yaw = wrapDegrees_0_360(yaw);
	yaw = (yaw + 45) % 360 / 90;

	return direction[yaw];
}

/*
	The debug screen is read by ordinary players, not only by engine
	developers: it is the one place that answers "where am I", "is it dark
	enough for monsters here" and "what am I standing on". So it names blocks
	the way the game names them, spells the numbers out, and keeps anything
	that gives an unfair advantage behind a privilege.
*/

/// Which way the player faces, in words rather than in axis names.
inline static const char *yawToWords(int yaw)
{
	static const char *direction[4] = {
		N_("north"), N_("west"), N_("south"), N_("east")};

	yaw = wrapDegrees_0_360(yaw);

	return direction[(yaw + 45) % 360 / 90];
}

/// The axis the player faces, for those who build by coordinates.
inline static const char *yawToAxis(int yaw)
{
	static const char *axis[4] = {"+Z", "-X", "-Z", "+X"};

	yaw = wrapDegrees_0_360(yaw);

	return axis[(yaw + 45) % 360 / 90];
}

/// The name of a block as the player knows it, not its technical id.
static std::string nodeLabel(Client *client, const MapNode &n)
{
	const ContentFeatures &f = client->getNodeDefManager()->get(n);

	if (f.name == "air")
		return gettext("nothing");
	if (f.name == "unknown")
		return gettext("unknown block");

	const ItemDefinition &item = client->getItemDefManager()->get(f.name);

	std::string label = item.short_description.empty()
		? item.description : item.short_description;

	// A description may run over several lines; the first one names the thing
	const size_t line_end = label.find('\n');
	if (line_end != std::string::npos)
		label.resize(line_end);

	return label.empty() ? f.name : label;
}

/// Time of day as a clock reading plus the part of the day it belongs to.
static std::string timeOfDayText(u32 tod)
{
	const u32 hours = tod / 1000;
	const u32 minutes = (tod % 1000) * 60 / 1000;

	const char *part;
	if (hours < 5)
		part = N_("night");
	else if (hours < 11)
		part = N_("morning");
	else if (hours < 17)
		part = N_("daytime");
	else if (hours < 20)
		part = N_("evening");
	else
		part = N_("night");

	char buf[16];
	porting::mt_snprintf(buf, sizeof(buf), "%02u:%02u", hours, minutes);

	return std::string(buf) + ", " + gettext(part);
}

/// Shrinks a debug panel to the text it holds and puts a dim panel behind it,
/// so the lines stay readable over bright sky as well as over dark caves.
static void fitDebugPanel(gui::IGUIStaticText *text, s32 top, const v2u32 &screensize)
{
	const s32 width = std::min<s32>(text->getTextWidth() + 12,
			(s32)screensize.X - 10);
	const s32 height = text->getTextHeight() + 8;

	text->setRelativePosition(core::rect<s32>(5, top, 5 + width, top + height));
	text->setBackgroundColor(video::SColor(140, 0, 0, 0));
	text->setDrawBackground(true);
	text->setTextAlignment(gui::EGUIA_UPPERLEFT, gui::EGUIA_CENTER);
}

void GameUI::init()
{
	// First line of debug text
	m_guitext = gui::StaticText::add(guienv, utf8_to_wide(PROJECT_NAME_C).c_str(),
		core::recti(), false, true, guiroot);

	// Second line of debug text
	m_guitext2 = gui::StaticText::add(guienv, L"", core::recti(), false,
		true, guiroot);

	// Chat text
	m_guitext_chat = gui::StaticText::add(guienv, L"", core::recti(),
		false, true, guiroot);
	u16 chat_font_size = g_settings->getU16("chat_font_size");
	if (chat_font_size != 0) {
		m_guitext_chat->setOverrideFont(g_fontengine->getFont(
			rangelim(chat_font_size, 5, 72), FM_Unspecified));
	}


	// Infotext of nodes and objects.
	// If in debug mode, object debug infos shown here, too.
	// Located on the left on the screen, below chat.
	u32 chat_font_height = m_guitext_chat->getActiveFont()->getDimension(L"Ay").Height;
	m_guitext_info = gui::StaticText::add(guienv, L"",
		// Size is limited; text will be truncated after 6 lines.
		core::rect<s32>(0, 0, 400, g_fontengine->getTextHeight() * 6) +
			v2s32(100, chat_font_height *
			(g_settings->getU16("recent_chat_messages") + 3)),
			false, true, guiroot);

	// Status message for in-game notifications (fly/fast mode, volume changes, etc.)
	m_status_text = std::make_unique<StatusTextHelper>(guienv, guiroot);
	m_status_text->setGameStyle();

	// Profiler text (size is updated when text is updated)
	m_guitext_profiler = gui::StaticText::add(guienv, L"<Profiler>",
		core::recti(), false, false, guiroot);
	m_guitext_profiler->setOverrideFont(g_fontengine->getFont(
		g_fontengine->getDefaultFontSize() * 0.9f, FM_Mono));
	m_guitext_profiler->setVisible(false);
}

void GameUI::update(const RunStats &stats, Client *client, MapDrawControl *draw_control,
	const CameraOrientation &cam, const PointedThing &pointed_old,
	const GUIChatConsole *chat_console, float dtime)
{
	v2u32 screensize = RenderingEngine::getWindowSize();

	LocalPlayer *player = client->getEnv().getLocalPlayer();

	s32 minimal_debug_height = 0;

	// Minimal debug text must only contain info that can't give a gameplay advantage
	if (m_flags.show_minimal_debug) {
		const u16 fps = 1.0f / stats.dtime_jitter.avg;
		m_drawtime_avg *= 0.95f;
		m_drawtime_avg += 0.05f * (stats.drawtime / 1000);

		std::ostringstream os(std::ios_base::binary);
		os.imbue(std::locale::classic());
		os << std::fixed
			<< PROJECT_NAME_C " " << g_version_hash << "\n"
			<< gettext("Frames per second") << ": " << fps
			<< std::setprecision(m_drawtime_avg < 10 ? 1 : 0)
			<< "   " << gettext("Drawing") << ": " << m_drawtime_avg << " "
			<< gettext("ms")
			<< std::setprecision(1)
			<< "   " << gettext("Smoothness") << ": "
			<< (100.0f - stats.dtime_jitter.max_fraction * 100.0f) << "%"
			<< "   " << gettext("Seen distance") << ": "
			<< (draw_control->range_all ? std::string(gettext("no limit"))
				: (itos(draw_control->wanted_range) + " " + gettext("blocks")))
			<< std::setprecision(0)
			<< "   " << gettext("Ping to server") << ": "
			<< (client->getRTT() * 1000.0f) << " " << gettext("ms");

		// Text over open sky or snow is unreadable without something behind
		// it. The panel is fitted to the text, so it never covers more of the
		// view than the lines actually need.
		m_guitext->setRelativePosition(core::rect<s32>(5, 5, screensize.X, screensize.Y));

		setStaticText(m_guitext, utf8_to_wide(os.str()));

		fitDebugPanel(m_guitext, 5, screensize);

		minimal_debug_height = m_guitext->getRelativePosition().getHeight() + 2;
	}

	// Finally set the guitext visible depending on the flag
	m_guitext->setVisible(m_flags.show_minimal_debug);

	// Basic debug text also shows info that might give a gameplay advantage
	if (m_flags.show_basic_debug) {
		ClientMap &map = client->getEnv().getClientMap();
		const NodeDefManager *nodedef = client->getNodeDefManager();

		const v3f player_position = player->getPosition();
		const v3s16 player_node = floatToInt(player_position, BS);

		std::ostringstream os(std::ios_base::binary);
		os.imbue(std::locale::classic());
		os << std::setprecision(1) << std::fixed;

		// Where the player stands, and which way they look
		os << gettext("You are at") << ": "
			<< (player_position.X / BS) << ", "
			<< (player_position.Y / BS) << ", "
			<< (player_position.Z / BS)
			<< "   " << gettext("Facing") << ": "
			<< gettext(yawToWords(cam.camera_yaw))
			<< " (" << yawToAxis(cam.camera_yaw) << ", "
			<< (wrapDegrees_0_360(cam.camera_yaw)) << "°, "
			<< gettext("tilt") << " " << (-wrapDegrees_180(cam.camera_pitch)) << "°)";

		// How fast, and how fast up or down: falling and flying both read here
		const v3f speed = player->getSpeed() / BS;
		const float ground_speed = v2f(speed.X, speed.Z).getLength();

		os << "\n" << gettext("Speed") << ": " << ground_speed << " "
			<< gettext("blocks per second");

		if (std::fabs(speed.Y) >= 0.1f) {
			os << " (" << (speed.Y > 0 ? gettext("going up") : gettext("going down")) << " "
				<< std::fabs(speed.Y) << ")";
		}

		os << "   " << gettext("Time") << ": "
			<< timeOfDayText(client->getEnv().getTimeOfDay());

		// Light decides whether monsters can appear, so it is spelled out
		{
			const MapNode here = map.getNode(player_node);

			if (here.getContent() != CONTENT_IGNORE) {
				const ContentLightingFlags f = nodedef->getLightingFlags(here);
				const u8 day = here.getLight(LIGHTBANK_DAY, f);
				const u8 night = here.getLight(LIGHTBANK_NIGHT, f);
				const u8 now = here.getLightBlend(
					client->getEnv().getDayNightRatio(), f);

				os << "\n" << gettext("Light here") << ": " << (u32)now
					<< "/15 ("
					<< gettext("from the sky") << " " << (u32)day << ", "
					<< gettext("from lamps") << " " << (u32)night << ")";
			}
		}

		// What holds the player up
		{
			const MapNode below = map.getNode(player_node + v3s16(0, -1, 0));

			if (below.getContent() != CONTENT_IGNORE)
				os << "   " << gettext("Standing on") << ": "
					<< nodeLabel(client, below);
		}

		// What the crosshair rests on
		if (pointed_old.type == POINTEDTHING_NODE) {
			const v3s16 at = pointed_old.node_undersurface;
			const MapNode n = map.getNode(at);

			if (n.getContent() != CONTENT_IGNORE) {
				os << "\n" << gettext("Looking at") << ": "
					<< nodeLabel(client, n)
					<< " (" << at.X << ", " << at.Y << ", " << at.Z << ")";

				// Technical details are for those who work on the game
				if (client->checkPrivilege("debug")) {
					os << "   " << nodedef->get(n).name
						<< ", param2: " << (u32)n.getParam2();
				}
			}
		}

		// The world seed lets anyone look up where every structure and ore
		// vein sits. It stays hidden unless the server hands out the right.
		if (client->checkPrivilege("seed")) {
			os << "\n" << gettext("World seed") << ": "
				<< ((u64)client->getMapSeed());
		}

		m_guitext2->setRelativePosition(core::rect<s32>(5, 5 + minimal_debug_height,
				screensize.X, screensize.Y));

		setStaticText(m_guitext2, utf8_to_wide(os.str()).c_str());

		fitDebugPanel(m_guitext2, 5 + minimal_debug_height, screensize);
	}

	m_guitext2->setVisible(m_flags.show_basic_debug);

	setStaticText(m_guitext_info, m_infotext.c_str());
	m_guitext_info->setVisible(m_flags.show_hud && g_menumgr.menuCount() == 0);

	// Update status message element
	if (m_status_text) {
		// Handle touch control override if needed
		bool overridden = g_touchcontrols && g_touchcontrols->isStatusTextOverridden();
		if (overridden) {
			m_status_text->setVisible(false);
			if (g_touchcontrols)
				g_touchcontrols->getStatusText()->setVisible(true);
		} else {
			if (g_touchcontrols)
				g_touchcontrols->getStatusText()->setVisible(false);
			m_status_text->update(dtime);
		}
	}

	// Hide chat when disabled by server or when console is visible
	m_guitext_chat->setVisible(isChatVisible() && !chat_console->isVisible() && (player->hud_flags & HUD_FLAG_CHAT_VISIBLE));
}

void GameUI::initFlags()
{
	m_flags = GameUI::Flags();
}

void GameUI::showTranslatedStatusText(const char *str)
{
	showStatusText(wstrgettext(str));
}

void GameUI::setChatText(const EnrichedString &chat_text, u32 recent_chat_count)
{
	setStaticText(m_guitext_chat, chat_text);

	m_recent_chat_count = recent_chat_count;
}

void GameUI::updateChatSize()
{
	// Update gui element size and position
	s32 chat_y = 5;

	// The debug panels carry padding around their text, so the chat has to
	// step around the panel, not around the bare lines.
	if (m_flags.show_minimal_debug)
		chat_y += m_guitext->getRelativePosition().getHeight() + 2;
	if (m_flags.show_basic_debug)
		chat_y += m_guitext2->getRelativePosition().getHeight() + 2;

	const v2u32 window_size = RenderingEngine::getWindowSize();

	core::rect<s32> chat_size(10, chat_y, window_size.X - 20, 0);
	chat_size.LowerRightCorner.Y = std::min((s32)window_size.Y,
			m_guitext_chat->getTextHeight() + chat_y);

	if (chat_size == m_current_chat_size)
		return;
	m_current_chat_size = chat_size;

	m_guitext_chat->setRelativePosition(chat_size);
}

void GameUI::updateProfiler()
{
	m_guitext_profiler->setVisible(m_profiler_current_page != 0);
	if (m_profiler_current_page == 0)
		return;

	std::ostringstream oss(std::ios_base::binary);
	oss << "Profiler page " << (int)m_profiler_current_page
		<< "/" << (int)m_profiler_max_page
		<< ", elapsed: " << g_profiler->getElapsedMs() << " ms" << std::endl;
	g_profiler->print(oss, m_profiler_current_page, m_profiler_max_page);

	EnrichedString str(utf8_to_wide(oss.str()));
	str.setBackground(video::SColor(120, 0, 0, 0));
	setStaticText(m_guitext_profiler, str);

	v2s32 upper_left(5, 10);
	if (m_flags.show_minimal_debug)
		upper_left.Y += m_guitext->getTextHeight();
	if (m_flags.show_basic_debug)
		upper_left.Y += m_guitext2->getTextHeight();

	v2s32 lower_right = upper_left;
	lower_right.X += m_guitext_profiler->getTextWidth() + 5;
	lower_right.Y += m_guitext_profiler->getTextHeight();

	m_guitext_profiler->setRelativePosition(core::recti(upper_left, lower_right));

	// Really dumb heuristic (we have a fixed number of pages, not a fixed page size)
	const v2u32 window_size = RenderingEngine::getWindowSize();
	if (upper_left.Y + m_guitext_profiler->getTextHeight()
		> window_size.Y * 0.7f) {
		if (m_profiler_max_page < 5) {
			m_profiler_max_page++;
			updateProfiler(); // do it again
		}
	}
}

void GameUI::toggleChat(Client *client)
{
	if (client->getEnv().getLocalPlayer()->hud_flags & HUD_FLAG_CHAT_VISIBLE) {
		m_flags.show_chat = !m_flags.show_chat;
		if (m_flags.show_chat)
			showTranslatedStatusText("Chat shown");
		else
			showTranslatedStatusText("Chat hidden");
	} else {
		showTranslatedStatusText("Chat currently disabled by game or mod");
	}

}

void GameUI::toggleHud()
{
	m_flags.show_hud = !m_flags.show_hud;
	if (m_flags.show_hud)
		showTranslatedStatusText("HUD shown");
	else
		showTranslatedStatusText("HUD hidden");
}

void GameUI::toggleProfiler()
{
	m_profiler_current_page = (m_profiler_current_page + 1) % (m_profiler_max_page + 1);

	// FIXME: This updates the profiler with incomplete values
	updateProfiler();

	if (m_profiler_current_page != 0) {
		std::wstring msg = fwgettext("Profiler shown (page %d of %d)",
				m_profiler_current_page, m_profiler_max_page);
		showStatusText(msg);
	} else {
		showTranslatedStatusText("Profiler hidden");
	}
}

void GameUI::clearText()
{
	if (m_guitext_chat) {
		m_guitext_chat->remove();
		m_guitext_chat = nullptr;
	}

	if (m_guitext) {
		m_guitext->remove();
		m_guitext = nullptr;
	}

	if (m_guitext2) {
		m_guitext2->remove();
		m_guitext2 = nullptr;
	}

	if (m_guitext_info) {
		m_guitext_info->remove();
		m_guitext_info = nullptr;
	}

	m_status_text.reset();

	if (m_guitext_profiler) {
		m_guitext_profiler->remove();
		m_guitext_profiler = nullptr;
	}
}
