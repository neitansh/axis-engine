// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2010-2013 celeron55, Perttu Ahola <celeron55@gmail.com>

#include "game_formspec.h"

#include "gettext.h"
#include "nodemetadata.h"
#include "renderingengine.h"
#include "client.h"
#include "scripting_client.h"
#include "cpp_api/s_client_common.h"
#include "clientmap.h"
#include "gui/guiFormSpecMenu.h"
#include "gui/mainmenumanager.h"
#include "gui/touchcontrols.h"
#include "gui/touchscreeneditor.h"
#include "gui/guiPasswordChange.h"
#include "gui/guiPasswordChange.h"
#include "gui/guiOpenURL.h"
#include "gui/guiVolumeChange.h"
#include "localplayer.h"

/*
	Text input system
*/

struct TextDestNodeMetadata : public TextDest
{
	TextDestNodeMetadata(v3s16 p, Client *client)
	{
		m_p = p;
		m_client = client;
	}
	void gotText(const StringMap &fields)
	{
		m_client->sendNodemetaFields(m_p, "", fields);
	}

	v3s16 m_p;
	Client *m_client;
};

struct TextDestPlayerInventory : public TextDest
{
	TextDestPlayerInventory(Client *client)
	{
		m_client = client;
		m_formname.clear();
	}
	TextDestPlayerInventory(Client *client, const std::string &formname)
	{
		m_client = client;
		m_formname = formname;
	}
	void gotText(const StringMap &fields)
	{
		m_client->sendInventoryFields(m_formname, fields);
	}

	Client *m_client;
};

struct LocalScriptingFormspecHandler : public TextDest
{
	LocalScriptingFormspecHandler(const std::string &formname, ScriptApiClientCommon *script)
	{
		m_formname = formname;
		m_script = script;
	}

	void gotText(const StringMap &fields)
	{
		m_script->on_formspec_input(m_formname, fields);
	}

	ScriptApiClientCommon *m_script = nullptr;
};

struct HardcodedPauseFormspecHandler : public TextDest
{
	HardcodedPauseFormspecHandler()
	{
		m_formname = "MT_PAUSE_MENU";
	}

	void gotText(const StringMap &fields)
	{
		if (fields.find("btn_settings") != fields.end()) {
			g_gamecallback->openSettings();
			return;
		}

		if (fields.find("btn_sound") != fields.end()) {
			g_gamecallback->changeVolume();
			return;
		}

		if (fields.find("btn_exit_menu") != fields.end()) {
			g_gamecallback->disconnect();
			return;
		}

		if (fields.find("btn_exit_os") != fields.end()) {
			g_gamecallback->exitToOS();
#ifndef __ANDROID__
			RenderingEngine::get_raw_device()->closeDevice();
#endif
			return;
		}

		if (fields.find("btn_change_password") != fields.end()) {
			g_gamecallback->changePassword();
			return;
		}
	}
};

struct LegacyDeathFormspecHandler : public TextDest
{
	LegacyDeathFormspecHandler(Client *client)
	{
		m_formname = "MT_DEATH_SCREEN";
		m_client = client;
	}

	void gotText(const StringMap &fields)
	{
		if (fields.find("quit") != fields.end())
			m_client->sendRespawnLegacy();
	}

	Client *m_client = nullptr;
};

/* Form update callback */

class NodeMetadataFormSource: public IFormSource
{
public:
	NodeMetadataFormSource(ClientMap *map, v3s16 p):
		m_map(map),
		m_p(p)
	{
	}
	const std::string &getForm() const
	{
		static const std::string empty_string = "";
		NodeMetadata *meta = m_map->getNodeMetadata(m_p);

		if (!meta)
			return empty_string;

		return meta->getString("formspec");
	}

	virtual std::string resolveText(const std::string &str)
	{
		NodeMetadata *meta = m_map->getNodeMetadata(m_p);

		if (!meta)
			return str;

		return meta->resolveString(str);
	}

	ClientMap *m_map;
	v3s16 m_p;
};

class PlayerInventoryFormSource: public IFormSource
{
public:
	PlayerInventoryFormSource(Client *client):
		m_client(client)
	{
	}

	const std::string &getForm() const
	{
		LocalPlayer *player = m_client->getEnv().getLocalPlayer();

		if (!player->inventory_formspec_override.empty())
			return player->inventory_formspec_override;

		return player->inventory_formspec;
	}

	Client *m_client;
};


//// GameFormSpec

void GameFormSpec::init(Client *client, RenderingEngine *rendering_engine, InputHandler *input)
{
	m_client = client;
	m_rendering_engine = rendering_engine;
	m_input = input;
	m_pause_script = std::make_unique<PauseMenuScripting>(client);
	m_pause_script->loadBuiltin();

	// Make sure any remaining game callback requests are cleared out.
	*g_gamecallback = MainGameCallback();
}

void GameFormSpec::deleteFormspec()
{
	if (m_formspec) {
		m_formspec->drop();
		m_formspec = nullptr;
	}
}

void GameFormSpec::reset()
{
	if (m_formspec)
		m_formspec->quitMenu();
	deleteFormspec();
}

bool GameFormSpec::handleEmptyFormspec(const std::string &formspec, const std::string &formname)
{
	if (formspec.empty()) {
		GUIModalMenu *menu = g_menumgr.tryGetTopMenu();
		if (menu && (formname.empty() || formname == menu->getName())) {
			// `m_formspec` will be fixed up in `GameFormSpec::update()`
			menu->quitMenu();
		}
		return true;
	}
	return false;
}

void GameFormSpec::showFormSpec(const std::string &formspec, const std::string &formname)
{
	if (handleEmptyFormspec(formspec, formname))
		return;

	FormspecFormSource *fs_src =
		new FormspecFormSource(formspec);
	TextDestPlayerInventory *txt_dst =
		new TextDestPlayerInventory(m_client, formname);

	// Replace the currently open formspec
	GUIFormSpecMenu::create(m_formspec, m_client, m_rendering_engine->get_gui_env(),
		fs_src, txt_dst, m_client->getFormspecPrepend(),
		m_client->getSoundManager());
	m_formspec->setName(formname);
}

void GameFormSpec::showCSMFormSpec(const std::string &formspec, const std::string &formname)
{
	if (handleEmptyFormspec(formspec, formname))
		return;

	FormspecFormSource *fs_src = new FormspecFormSource(formspec);
	LocalScriptingFormspecHandler *txt_dst =
		new LocalScriptingFormspecHandler(formname, m_client->getScript());

	GUIFormSpecMenu::create(m_formspec, m_client, m_rendering_engine->get_gui_env(),
			fs_src, txt_dst, m_client->getFormspecPrepend(),
			m_client->getSoundManager());
	m_formspec->setName(formname);
}

void GameFormSpec::showPauseMenuFormSpec(const std::string &formspec, const std::string &formname)
{
	// The pause menu env is a trusted context like the mainmenu env and provides
	// the in-game settings formspec.
	// Neither CSM nor the server must be allowed to mess with it.

	// If we send updated formspec contents, we can either (1) recycle the old
	// GUIFormSpecMenu or (2) close the old and open a new one. This is option 2.
	(void)handleEmptyFormspec("", formname);
	if (formspec.empty())
		return;

	FormspecFormSource *fs_src = new FormspecFormSource(formspec);
	LocalScriptingFormspecHandler *txt_dst =
		new LocalScriptingFormspecHandler(formname, m_pause_script.get());

	GUIFormSpecMenu *fs = nullptr;
	GUIFormSpecMenu::create(fs, m_client, m_rendering_engine->get_gui_env(),
			// Ignore formspec prepend.
			fs_src, txt_dst, "",
			m_client->getSoundManager());

	fs->setName(formname);
	fs->doPause = true;
	fs->drop(); // 1 reference held by `g_menumgr`
}

void GameFormSpec::showNodeFormspec(const std::string &formspec, const v3s16 &nodepos)
{
	infostream << "Launching custom inventory view" << std::endl;

	InventoryLocation inventoryloc;
	inventoryloc.setNodeMeta(nodepos);

	NodeMetadataFormSource *fs_src = new NodeMetadataFormSource(
		&m_client->getEnv().getClientMap(), nodepos);
	TextDest *txt_dst = new TextDestNodeMetadata(nodepos, m_client);

	GUIFormSpecMenu::create(m_formspec, m_client, m_rendering_engine->get_gui_env(),
		fs_src, txt_dst, m_client->getFormspecPrepend(),
		m_client->getSoundManager());

	m_formspec->setFormSpec(formspec, inventoryloc);
}

void GameFormSpec::showPlayerInventory(const std::string *fs_override)
{
	/*
	 * Don't permit to open inventory is CAO or player doesn't exists.
	 * This prevent showing an empty inventory at player load
	 */

	LocalPlayer *player = m_client->getEnv().getLocalPlayer();
	if (!player || !player->getCAO())
		return;

	infostream << "Game: Launching inventory" << std::endl;

	auto fs_src = std::make_unique<PlayerInventoryFormSource>(m_client);

	InventoryLocation inventoryloc;
	inventoryloc.setCurrentPlayer();

	if (fs_override) {
		// Temporary overwrite for this specific formspec.
		player->inventory_formspec_override = *fs_override;
	} else {
		// Show the regular inventory formspec
		player->inventory_formspec_override.clear();
	}

	// If prevented by Client-Side Mods
	if (m_client->modsLoaded() && m_client->getScript()->on_inventory_open(m_client->getInventory(inventoryloc)))
		return;

	// Empty formspec -> do not show.
	if (fs_src->getForm().empty())
		return;

	TextDest *txt_dst = new TextDestPlayerInventory(m_client);

	GUIFormSpecMenu::create(m_formspec, m_client, m_rendering_engine->get_gui_env(),
		fs_src.get(), txt_dst, m_client->getFormspecPrepend(),
		m_client->getSoundManager());

	m_formspec->setFormSpec(fs_src->getForm(), inventoryloc);
	fs_src.release(); // owned by GUIFormSpecMenu
}

#define SIZE_TAG "size[11,5.5,true]" // Fixed size (ignored in touchscreen mode)

// Shared look of every menu, mirroring builtin/common/menu_style.lua. That file
// is the source of truth for the palette and the nine-slice art; this menu is
// built in C++ and cannot read it, so the few values it needs are repeated here.
// The textures live in the base pack, which the client's texture source already
// serves to formspecs by plain name.
namespace {
	const char *MENU_TEXT_COLOR = "#E8EAED";
	// Corner size of the button art, in source pixels.
	const int MENU_BUTTON_MIDDLE = 2;
}

void GameFormSpec::showPauseMenu()
{
	std::string control_text;

	if (g_touchcontrols) {
		control_text = strgettext("Controls:\n"
			"No menu open:\n"
			"- slide finger: look around\n"
			"- tap: place/punch/use (default)\n"
			"- long tap: dig/use (default)\n"
			"Menu/inventory open:\n"
			"- double tap (outside):\n"
			" --> close\n"
			"- touch stack, touch slot:\n"
			" --> move stack\n"
			"- touch&drag, tap 2nd finger\n"
			" --> place single item to slot\n"
			);
	}

	auto simple_singleplayer_mode = m_client->m_simple_singleplayer_mode;

	// One column, laid out from the top down. Sizes are named so the menu can
	// gain or lose a button without every following number having to move.
	const float width = 16.0f;
	const float panel_w = 6.4f;
	const float panel_x = (width - panel_w) / 2.0f;
	const float margin = 0.6f;
	const float button_w = panel_w - 2.0f * margin;
	const float button_h = 0.85f;
	const float gap = 0.2f;

	// The wordmark is 1344x144 in source, kept at its own aspect ratio so it
	// does not smear.
	const float logo_w = 15.0f;
	const float logo_h = logo_w * 144.0f / 1344.0f;
	const float logo_x = (width - logo_w) / 2.0f;

	int buttons = 4; // continue, settings, exit to menu, exit to OS
#if !defined(__ANDROID__) && USE_SOUND
	buttons++;
#endif
	if (!simple_singleplayer_mode)
		buttons++; // change password

	float y = 0.5f;
	const float logo_y = y;
	y += logo_h + 1.2f;
	const float title_y = y;
	y += 0.5f + 0.3f;
	const float first_button_y = y;
	float height = first_button_y + buttons * button_h + (buttons - 1) * gap + margin;

	if (!control_text.empty()) {
		height += 4.0f + gap;
	}

	const float panel_y = title_y - margin;
	const float panel_h = height - panel_y;

	std::ostringstream os;
	os << "formspec_version[6]"
		<< "size[" << width << "," << height << ",true]"
		<< "bgcolor[#00000000;false]"
		<< "background9[" << panel_x << "," << panel_y << ";" << panel_w << "," << panel_h
			<< ";axis_panel.png;false;2]"
		<< "style_type[button,image_button;bgimg=axis_button.png"
			";bgimg_hovered=axis_button_hover.png"
			";bgimg_pressed=axis_button_pressed.png"
			";bgimg_middle=" << MENU_BUTTON_MIDDLE
			<< ";border=false;textcolor=" << MENU_TEXT_COLOR << "]"
		// An area label is the only kind that can be centred, and centring is
		// what the heading was missing.
		<< "style_type[label;textcolor=" << MENU_TEXT_COLOR << ";halign=center]"
		<< "image[" << logo_x << "," << logo_y << ";"
			<< logo_w << "," << logo_h << ";menu_header.png]"
		<< "label[" << panel_x + margin << "," << title_y << ";" << button_w << ",0.5;"
			<< strgettext("Game paused") << "]";

	auto button = [&](const char *tag, const std::string &label, bool exits) {
		os << (exits ? "button_exit[" : "button[")
			<< panel_x + margin << "," << y << ";" << button_w << "," << button_h << ";"
			<< tag << ";" << label << "]";
		y += button_h + gap;
	};

	// TRANSLATORS: Pause menu button, try to keep the translation short
	button("btn_continue", strgettext("Continue"), true);

	if (!simple_singleplayer_mode) {
		// TRANSLATORS: Pause menu button, try to keep the translation short
		button("btn_change_password", strgettext("Change Password"), false);
	}

	// TRANSLATORS: Try to keep the translation short
	button("btn_settings", strgettext("Settings"), false);

#if !defined(__ANDROID__) && USE_SOUND
	// TRANSLATORS: Pause menu button, try to keep the translation short
	button("btn_sound", strgettext("Sound Volume"), false);
#endif

	// TRANSLATORS: Pause menu button, try to keep the translation short
	button("btn_exit_menu", strgettext("Exit to Menu"), true);
	// TRANSLATORS: Pause menu button, try to keep the translation short (OS = Operating System)
	button("btn_exit_os", strgettext("Exit to OS"), true);

	// The version and server details that used to sit here were dropped: they
	// were unreadable over the world and belong in the About screen anyway.
	// Touch controls stay, because there they are the only place to read them.
	if (!control_text.empty()) {
		os << "textarea[" << panel_x + margin << "," << y << ";" << button_w << ",4;;"
			<< control_text << ";]";
	}

	/* Create menu */
	/* Note: FormspecFormSource and LocalFormspecHandler  *
	 * are deleted by guiFormSpecMenu                     */
	FormspecFormSource *fs_src = new FormspecFormSource(os.str());
	HardcodedPauseFormspecHandler *txt_dst = new HardcodedPauseFormspecHandler();

	GUIFormSpecMenu::create(m_formspec, m_client, m_rendering_engine->get_gui_env(),
			fs_src, txt_dst, m_client->getFormspecPrepend(),
			m_client->getSoundManager());
	m_formspec->setFocus("btn_continue");
	// game will be paused in next step, if in singleplayer (see Game::m_is_paused)
	m_formspec->doPause = true;
}

void GameFormSpec::showDeathFormspecLegacy()
{
	static std::string formspec_str =
		std::string("formspec_version[1]") +
		SIZE_TAG
		"bgcolor[#320000b4;true]"
		"label[4.85,1.35;" + gettext("You died") + "]"
		"button_exit[4,3;3,0.5;btn_respawn;" + gettext("Respawn") + "]"
		;

	/* Create menu */
	/* Note: FormspecFormSource and LocalFormspecHandler  *
	 * are deleted by guiFormSpecMenu                     */
	FormspecFormSource *fs_src = new FormspecFormSource(formspec_str);
	LegacyDeathFormspecHandler *txt_dst = new LegacyDeathFormspecHandler(m_client);

	GUIFormSpecMenu::create(m_formspec, m_client, m_rendering_engine->get_gui_env(),
		fs_src, txt_dst, m_client->getFormspecPrepend(),
		m_client->getSoundManager());
	m_formspec->setFocus("btn_respawn");
}

void GameFormSpec::update()
{
	/*
	   make sure menu is on top
	   1. Delete formspec menu reference if menu was removed
	   2. Else, make sure formspec menu is on top
	*/
	if (!m_formspec)
		return;

	if (m_formspec->getReferenceCount() == 1) {
		// See GUIFormSpecMenu::create what refcnt = 1 means
		this->deleteFormspec();
		return;
	}

	auto &loc = m_formspec->getFormspecLocation();
	if (loc.type == InventoryLocation::NODEMETA) {
		NodeMetadata *meta = m_client->getEnv().getClientMap().getNodeMetadata(loc.p);
		if (!meta || meta->getString("formspec").empty()) {
			m_formspec->quitMenu();
			return;
		}
	}

	if (isMenuActive())
		guiroot->bringToFront(m_formspec);
}

void GameFormSpec::disableDebugView()
{
	if (m_formspec) {
		m_formspec->setDebugView(false);
	}
}

/* returns false if game should exit, otherwise true
 */
bool GameFormSpec::handleCallbacks()
{
	auto texture_src = m_client->getTextureSource();

	if (g_gamecallback->disconnect_requested) {
		g_gamecallback->disconnect_requested = false;
		return false;
	}

	if (g_gamecallback->settings_requested) {
		m_pause_script->open_settings();
		g_gamecallback->settings_requested = false;
	}

	if (g_gamecallback->changepassword_requested) {
		(void)make_irr<GUIPasswordChange>(guienv, guiroot, -1,
				       &g_menumgr, m_client, texture_src);
		g_gamecallback->changepassword_requested = false;
	}

	if (g_gamecallback->changevolume_requested) {
		(void)make_irr<GUIVolumeChange>(guienv, guiroot, -1,
				     &g_menumgr, texture_src);
		g_gamecallback->changevolume_requested = false;
	}

	if (g_gamecallback->touchscreenlayout_requested) {
		(new GUITouchscreenLayout(guienv, guiroot, -1,
				     &g_menumgr, texture_src))->drop();
		g_gamecallback->touchscreenlayout_requested = false;
	}

	if (!g_gamecallback->show_open_url_dialog.empty()) {
		(void)make_irr<GUIOpenURLMenu>(guienv, guiroot, -1,
				 &g_menumgr, texture_src, g_gamecallback->show_open_url_dialog);
		g_gamecallback->show_open_url_dialog.clear();
	}

	return true;
}

#ifdef __ANDROID__
bool GameFormSpec::handleAndroidUIInput()
{
	// FIXME: m_formspec and this value are not in sync at all times.
	GUIModalMenu *menu = g_menumgr.tryGetTopMenu();
	if (menu) {
		menu->getAndroidUIInput();
		return true;
	}
	return false;
}
#endif
