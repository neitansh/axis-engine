// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2013 sapier
// Copyright (C) 2025 grorp

#include "l_menu_common.h"

#include "client/renderingengine.h"
#include "crosshair.h"
#include "porting.h"
#include "settings.h"
#include "IrrlichtDevice.h"
#include "IGUIEnvironment.h"
#include "IOSOperator.h"
#include "client/shadows/dynamicshadowsrender.h"
#include "gettext.h"
#include "lua_api/l_internal.h"


int ModApiMenuCommon::l_gettext(lua_State *L)
{
	const char *srctext = luaL_checkstring(L, 1);
	const char *text = *srctext ? gettext(srctext) : "";
	lua_pushstring(L, text);

	return 1;
}


int ModApiMenuCommon::l_get_active_driver(lua_State *L)
{
	auto drivertype = RenderingEngine::get_video_driver()->getDriverType();
	lua_pushstring(L, RenderingEngine::getVideoDriverInfo(drivertype).name.c_str());
	return 1;
}


int ModApiMenuCommon::l_driver_supports_shadows(lua_State *L)
{
	auto *device = RenderingEngine::get_raw_device();
	lua_pushboolean(L, ShadowRenderer::isSupported(device->getVideoDriver()));
	return 1;
}


int ModApiMenuCommon::l_irrlicht_device_supports_touch(lua_State *L)
{
	lua_pushboolean(L, RenderingEngine::get_raw_device()->supportsTouchEvents());
	return 1;
}


int ModApiMenuCommon::l_normalize_keycode(lua_State *L)
{
	auto keystr = luaL_checkstring(L, 1);
	lua_pushstring(L, KeyPress(keystr).sym().c_str());
	return 1;
}


int ModApiMenuCommon::l_get_key_description(lua_State *L)
{
	const char *keystr = luaL_checkstring(L, 1);
	KeyPress kp(keystr);
	std::string name = kp.name();
	lua_pushstring(L, name.c_str());
	return 1;
}


/*
 * Буфер обмена берётся у оболочки напрямую, а не через движок меню: то же
 * самое окно обслуживает и стартовое меню, и меню паузы, а вот объект
 * GUIEngine есть только у первого.
 */
static gui::IGUIEnvironment *getMenuGuiEnv()
{
	auto *device = RenderingEngine::get_raw_device();
	return device ? device->getGUIEnvironment() : nullptr;
}

int ModApiMenuCommon::l_copy_to_clipboard(lua_State *L)
{
	const char *text = luaL_checkstring(L, 1);

	auto *env = getMenuGuiEnv();
	if (env)
		env->getOSOperator()->copyToClipboard(text);
	return 0;
}

int ModApiMenuCommon::l_paste_from_clipboard(lua_State *L)
{
	auto *env = getMenuGuiEnv();
	const c8 *text = env ? env->getOSOperator()->getTextFromClipboard() : nullptr;

	lua_pushstring(L, text ? text : "");
	return 1;
}

/*
 * Перекрестье для меню настроек.
 *
 * Меню показывает предпросмотр и рисует его теми же прямоугольниками, из
 * которых перекрестье собирается на экране (crosshair.h). Считать геометрию
 * второй раз, на Lua, значило бы завести два источника правды — и они
 * разошлись бы в первый же день.
 */
int ModApiMenuCommon::l_get_crosshair(lua_State *L)
{
	const f32 scale = lua_isnumber(L, 1) ? (f32)lua_tonumber(L, 1) : 1.0f;

	const CrosshairStyle style = CrosshairStyle::fromSettings(g_settings);

	auto push_pieces = [L](const std::vector<CrosshairStyle::Piece> &pieces) {
		lua_createtable(L, pieces.size(), 0);
		int i = 1;
		for (const auto &p : pieces) {
			lua_createtable(L, 0, 4);
			lua_pushinteger(L, p.x); lua_setfield(L, -2, "x");
			lua_pushinteger(L, p.y); lua_setfield(L, -2, "y");
			lua_pushinteger(L, p.w); lua_setfield(L, -2, "w");
			lua_pushinteger(L, p.h); lua_setfield(L, -2, "h");
			lua_rawseti(L, -2, i++);
		}
	};

	auto push_color = [L](const char *name, video::SColor color) {
		char buf[16];
		porting::mt_snprintf(buf, sizeof(buf), "#%08x", (unsigned)color.color);
		lua_pushstring(L, buf);
		lua_setfield(L, -2, name);
	};

	lua_createtable(L, 0, 7);

	push_pieces(style.pieces(scale));
	lua_setfield(L, -2, "pieces");
	push_pieces(style.outlinePieces(scale));
	lua_setfield(L, -2, "outline_pieces");

	push_color("color", style.color);
	push_color("outline_color", style.outline_color);
	push_color("object_color", style.object_color);

	lua_pushstring(L, CrosshairStyle::shapeName(style.shape));
	lua_setfield(L, -2, "shape");
	lua_pushstring(L, style.toCode().c_str());
	lua_setfield(L, -2, "code");
	return 1;
}

int ModApiMenuCommon::l_set_crosshair_code(lua_State *L)
{
	const std::string code = luaL_checkstring(L, 1);

	CrosshairStyle style;
	if (!CrosshairStyle::fromCode(code, style)) {
		lua_pushboolean(L, false);
		return 1;
	}

	style.toSettings(g_settings);
	lua_pushboolean(L, true);
	return 1;
}

void ModApiMenuCommon::Initialize(lua_State *L, int top)
{
	API_FCT(gettext);
	API_FCT(get_active_driver);
	API_FCT(driver_supports_shadows);
	API_FCT(irrlicht_device_supports_touch);
	API_FCT(normalize_keycode);
	API_FCT(get_key_description);
	API_FCT(copy_to_clipboard);
	API_FCT(paste_from_clipboard);
	API_FCT(get_crosshair);
	API_FCT(set_crosshair_code);
}


void ModApiMenuCommon::InitializeAsync(lua_State *L, int top)
{
	API_FCT(gettext);
}
