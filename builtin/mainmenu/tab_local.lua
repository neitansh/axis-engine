-- Luanti
-- Copyright (C) 2014 sapier
-- SPDX-License-Identifier: LGPL-2.1-or-later


local current_game
local valid_disabled_settings = {
	["enable_damage"]=true,
	["creative_mode"]=true,
	["enable_server"]=true,
}

-- Name and port stored to persist when updating the formspec
local current_name = core.settings:get("name")
local current_port = core.settings:get("port")

-- Currently chosen game in gamebar for theming and filtering
function current_game()
	local gameid = core.settings:get("menu_last_game")
	local game = gameid and pkgmgr.find_by_gameid(gameid)
	-- Fall back to first game installed if one exists.
	if not game and #pkgmgr.games > 0 then
		game = pkgmgr.games[1]
		gameid = game.id
		core.settings:set("menu_last_game", gameid)
	end

	return game
end

-- Apply menu changes from given game
function apply_game(game)
	core.settings:set("menu_last_game", game.id)
	menudata.worldlist:set_filtercriteria(game.id)

	mm_game_theme.set_game(game)

	local index = filterlist.get_current_index(menudata.worldlist,
		tonumber(core.settings:get("mainmenu_last_selected_world")))
	if not index or index < 1 then
		local selected = core.get_textlist_index("sp_worlds")
		if selected ~= nil and selected < #menudata.worldlist:get_list() then
			index = selected
		else
			index = #menudata.worldlist:get_list()
		end
	end
	menu_worldmt_legacy(index)
end

local function get_disabled_settings(game)
	if not game then
		return {}
	end

	local gameconfig = Settings(game.path .. "/game.conf")
	local disabled_settings = {}
	if gameconfig then
		local disabled_settings_str = (gameconfig:get("disabled_settings") or ""):split()
		for _, value in pairs(disabled_settings_str) do
			local state = false
			value = value:trim()
			if string.sub(value, 1, 1) == "!" then
				state = true
				value = string.sub(value, 2)
			end
			if valid_disabled_settings[value] then
				disabled_settings[value] = state
			else
				core.log("error", "Invalid disabled setting in game.conf: "..tostring(value))
			end
		end
	end
	return disabled_settings
end

local function get_formspec(tabview, name, tabdata)

	-- Point the player to ContentDB when no games are found
	if #pkgmgr.games == 0 then
		local W = tabview.width
		local H = tabview.height

		local hypertext = "<global valign=middle halign=center size=18>" ..
				fgettext_ne("Luanti is a game-creation platform that allows you to play many different games.") .. "\n" ..
				fgettext_ne("Luanti doesn't come with a game by default.") .. " " ..
				fgettext_ne("You need to install a game before you can create a world.")

		local button_y = H * 2/3 - 0.6
		return table.concat({
			"hypertext[0.375,0;", W - 2*0.375, ",", button_y, ";ht;", core.formspec_escape(hypertext), "]",
			"button[5.25,", button_y, ";5,1.2;game_open_cdb;", fgettext("Install a game"), "]"})
	end

	local retval = ""

	local index = core.get_textlist_index("sp_worlds") or filterlist.get_current_index(menudata.worldlist,
				tonumber(core.settings:get("mainmenu_last_selected_world"))) or 0

	local list = menudata.worldlist:get_list()
	-- When changing tabs to a world list with fewer entries, the last index is selected (visually).
	-- However, the formspec fields lag behind, thus 'index > #list' can be a valid choice.
	local world = list and list[math.min(index, #list)]
	local game

	if world then
		game = pkgmgr.find_by_gameid(world.gameid)
	else
		game = current_game()
	end
	local disabled_settings = get_disabled_settings(game)

	local creative, damage, host = "", "", ""

	-- Y offsets for game settings checkboxes
	local y = 0.2
	local yo = 0.5625

	if world then
		if disabled_settings["creative_mode"] == nil then
			creative = "checkbox[0,"..y..";cb_creative_mode;".. fgettext("Creative Mode") .. ";" ..
				dump(core.settings:get_bool("creative_mode")) .. "]"
			y = y + yo
		end
		if disabled_settings["enable_damage"] == nil then
			damage = "checkbox[0,"..y..";cb_enable_damage;".. fgettext("Enable Damage") .. ";" ..
				dump(core.settings:get_bool("enable_damage")) .. "]"
			y = y + yo
		end
		if disabled_settings["enable_server"] == nil then
			host = "checkbox[0,"..y..";cb_server;".. fgettext("Host Server") ..";" ..
				dump(core.settings:get_bool("enable_server")) .. "]"
			y = y + yo
		end
	end

	-- Two cards: what the world is set up as, and which world it is
	retval = retval ..
			menu_style.surface(0.375, 0.375, 4.5, tabview.height - 0.75) ..
			menu_style.surface(5.25, 0.375, 9.875, 4.5) ..
			menu_style.heading(0.75, 0.55, 3.9, 0.6, fgettext("Game")) ..
			"container[5.25,4.875]"
	if world then
		retval = retval ..
				"button[0,0;3.225,0.8;world_delete;".. fgettext("Delete") .. "]" ..
				"button[3.325,0;3.225,0.8;world_configure;".. fgettext("Select Mods") .. "]"
	end
	retval = retval ..
			"button[6.65,0;3.225,0.8;world_create;".. fgettext("New") .. "]" ..
			"container_end[]" ..
			"container[0.75,1.15]" ..
			creative ..
			damage ..
			host ..
			"container_end[]" ..
			"container[5.625,0.375]" ..
			menu_style.heading(0, 0.15, 6, 0.6, fgettext("Select World:")) ..
			"textlist[0,0.85;9.125,3.6;sp_worlds;" ..
			menu_render_worldlist() ..
			";" .. index .. "]" ..
			"container_end[]"

	if core.settings:get_bool("enable_server") and disabled_settings["enable_server"] == nil then
		retval = retval ..
				menu_style.accent("play") ..
				"button[10.1875,5.925;4.9375,0.8;play;".. fgettext("Host Game") .. "]" ..
				"container[0.375,0.375]" ..
				"checkbox[0,"..y..";cb_server_announce;" .. fgettext("Announce Server") .. ";" ..
				dump(core.settings:get_bool("server_announce")) .. "]"

		-- Reset y so that the text fields always start at the same position,
		-- regardless of whether some of the checkboxes are hidden.
		y = 0.2 + 4 * yo + 0.35

		retval = retval .. "field[0," .. y .. ";4.5,0.75;te_playername;" .. fgettext("Name") .. ";" ..
				core.formspec_escape(current_name) .. "]"

		y = y + 1.15 + 0.25

		retval = retval .. "pwdfield[0," .. y .. ";4.5,0.75;te_passwd;" .. fgettext("Password") .. "]"

		y = y + 1.15 + 0.25

		local bind_addr = core.settings:get("bind_address")
		if bind_addr ~= nil and bind_addr ~= "" then
			retval = retval ..
				"field[0," .. y .. ";3,0.75;te_serveraddr;" .. fgettext("Bind Address") .. ";" ..
				core.formspec_escape(core.settings:get("bind_address")) .. "]" ..
				-- TRANSLATORS: Network port
				"field[3.25," .. y .. ";1.25,0.75;te_serverport;" .. fgettext("Port") .. ";" ..
				core.formspec_escape(current_port) .. "]"
		else
			retval = retval ..
				"field[0," .. y .. ";4.5,0.75;te_serverport;" .. fgettext("Server Port") .. ";" ..
				core.formspec_escape(current_port) .. "]"
		end

		retval = retval .. "container_end[]"
	elseif world then
		retval = retval ..
				menu_style.accent("play") ..
				"button[10.1875,5.925;4.9375,0.8;play;" .. fgettext("Play Game") .. "]"
	end

	return retval
end

local function main_button_handler(this, fields, name, tabdata)

	assert(name == "local")

	if fields.game_open_cdb then
		local maintab = ui.find_by_name("maintab")
		local dlg = create_contentdb_dlg("game")
		dlg:set_parent(maintab)
		maintab:hide()
		dlg:show()
		return true
	end

	if this.dlg_create_world_closed_at == nil then
		this.dlg_create_world_closed_at = 0
	end

	local world_doubleclick = false

	if fields["te_playername"] then
		current_name = fields["te_playername"]
	end

	if fields["te_serverport"] then
		current_port = fields["te_serverport"]
	end

	if fields["sp_worlds"] ~= nil then
		local event = core.explode_textlist_event(fields["sp_worlds"])
		local selected = core.get_textlist_index("sp_worlds")

		menu_worldmt_legacy(selected)

		if event.type == "DCL" then
			world_doubleclick = true
		end

		if event.type == "CHG" and selected ~= nil then
			core.settings:set("mainmenu_last_selected_world",
				menudata.worldlist:get_raw_index(selected))
			return true
		end
	end

	if menu_handle_key_up_down(fields,"sp_worlds","mainmenu_last_selected_world") then
		return true
	end

	if fields["cb_creative_mode"] then
		core.settings:set("creative_mode", fields["cb_creative_mode"])
		local selected = core.get_textlist_index("sp_worlds")
		menu_worldmt(selected, "creative_mode", fields["cb_creative_mode"])

		return true
	end

	if fields["cb_enable_damage"] then
		core.settings:set("enable_damage", fields["cb_enable_damage"])
		local selected = core.get_textlist_index("sp_worlds")
		menu_worldmt(selected, "enable_damage", fields["cb_enable_damage"])

		return true
	end

	if fields["cb_server"] then
		core.settings:set("enable_server", fields["cb_server"])

		return true
	end

	if fields["cb_server_announce"] then
		core.settings:set("server_announce", fields["cb_server_announce"])
		local selected = core.get_textlist_index("srv_worlds")
		menu_worldmt(selected, "server_announce", fields["cb_server_announce"])

		return true
	end

	if fields["play"] ~= nil or world_doubleclick or fields["key_enter"] then
		local enter_key_duration = core.get_us_time() - this.dlg_create_world_closed_at
		if world_doubleclick and enter_key_duration <= 200000 then -- 200 ms
			this.dlg_create_world_closed_at = 0
			return true
		end

		local selected = core.get_textlist_index("sp_worlds")
		gamedata.selected_world = menudata.worldlist:get_raw_index(selected)

		if selected == nil or gamedata.selected_world == 0 then
			return true
		end

		-- Update last game
		local world = menudata.worldlist:get_raw_element(gamedata.selected_world)
		local game_obj
		if world then
			game_obj = pkgmgr.find_by_gameid(world.gameid)
			core.settings:set("menu_last_game", game_obj.id)
		end

		local disabled_settings = get_disabled_settings(game_obj)
		for k, _ in pairs(valid_disabled_settings) do
			local v = disabled_settings[k]
			if v ~= nil then
				if k == "enable_server" and v == true then
					error("Setting 'enable_server' cannot be force-enabled! The game.conf needs to be fixed.")
				end
				core.settings:set_bool(k, disabled_settings[k])
			end
		end

		if core.settings:get_bool("enable_server") then
			gamedata.mode       = "host"
			gamedata.playername = fields["te_playername"]
			gamedata.password   = fields["te_passwd"]
			gamedata.port       = fields["te_serverport"]
			gamedata.address    = ""

			core.settings:set("port",gamedata.port)
			if fields["te_serveraddr"] ~= nil then
				core.settings:set("bind_address",fields["te_serveraddr"])
			end
		else
			gamedata.mode = "singleplayer"
		end

		core.start()
		return true
	end

	if fields["world_create"] ~= nil then
		this.dlg_create_world_closed_at = 0
		local create_world_dlg = create_create_world_dlg()
		create_world_dlg:set_parent(this)
		this:hide()
		create_world_dlg:show()
		return true
	end

	if fields["world_delete"] ~= nil then
		local selected = core.get_textlist_index("sp_worlds")
		if selected ~= nil and
			selected <= menudata.worldlist:size() then
			local world = menudata.worldlist:get_list()[selected]
			if world ~= nil and
				world.name ~= nil and
				world.name ~= "" then
				local index = menudata.worldlist:get_raw_index(selected)
				local delete_world_dlg = create_delete_world_dlg(world.name,index)
				delete_world_dlg:set_parent(this)
				this:hide()
				delete_world_dlg:show()
			end
		end

		return true
	end

	if fields["world_configure"] ~= nil then
		local selected = core.get_textlist_index("sp_worlds")
		if selected ~= nil then
			local configdialog =
				create_configure_world_dlg(
						menudata.worldlist:get_raw_index(selected))

			if (configdialog ~= nil) then
				configdialog:set_parent(this)
				this:hide()
				configdialog:show()
			end
		end

		return true
	end
end

local function on_change(type)
	if type == "ENTER" then
		local game = current_game()
		if game then
			apply_game(game)
		else
			mm_game_theme.set_engine()
		end

		-- Раньше здесь поднималась панель выбора игры (game_button_bar).
		-- Панель убрана вместе с функцией singleplayer_refresh_gamebar,
		-- которая её собирала: строить её незачем, пока игра в репозитории
		-- одна. Если панель понадобится обратно, код лежит в истории —
		-- искать в builtin/mainmenu/tab_local.lua до этого коммита.
	elseif type == "LEAVE" then
		menudata.worldlist:set_filtercriteria(nil)
	end
end

--------------------------------------------------------------------------------
return {
	name = "local",
	caption = fgettext("Start Game"),
	cbf_formspec = get_formspec,
	cbf_button_handler = main_button_handler,
	on_change = on_change
}
