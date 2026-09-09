-- Luanti
-- Copyright (C) 2014 sapier
-- SPDX-License-Identifier: LGPL-2.1-or-later


local current_crate
local valid_disabled_settings = {
	["enable_damage"]=true,
	["creative_mode"]=true,
}

-- Своего сервера здесь нет, и это не упущение.
--
-- Сервер, поднятый из меню, открывает порт наружу, а значит обязан знать, кто
-- к нему стучится. Знает это тот, кто записан в реестре: билет выписывается на
-- конкретный сервер и на другом не работает. Записи у сервера из меню нет и
-- взяться ей неоткуда, поэтому он не пустил бы никого — включая хозяина.
--
-- Пускать без билета движок не умеет намеренно: это ровно та дыра, ради
-- которой вся проверка и заведена. Поэтому галка «Разместить сервер» убрана
-- целиком, а не оставлена нажимаемой: кнопка, которая не может сработать,
-- хуже её отсутствия.
--
-- Здесь остаётся своя игра — там сервер живёт внутри клиента и второго игрока
-- туда не пускает сам движок, проверять некого. Сервер для друзей поднимается
-- отдельно и заводится в реестре; когда это смогут делать игроки, вкладка
-- вернётся вместе с записью.

-- Currently chosen crate for theming and filtering
function current_crate()
	local crateid = core.settings:get("menu_last_crate")
	local crate = crateid and pkgmgr.find_by_crateid(crateid)
	-- Fall back to first crate installed if one exists.
	if not crate and #pkgmgr.crates > 0 then
		crate = pkgmgr.crates[1]
		crateid = crate.id
		core.settings:set("menu_last_crate", crateid)
	end

	return crate
end

-- Apply menu changes from given crate
function apply_crate(crate)
	core.settings:set("menu_last_crate", crate.id)
	menudata.worldlist:set_filtercriteria(crate.id)

	mm_crate_theme.set_crate(crate)

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

local function get_disabled_settings(crate)
	if not crate then
		return {}
	end

	local crateconfig = Settings(crate.path .. "/crate.conf")
	local disabled_settings = {}
	if crateconfig then
		local disabled_settings_str = (crateconfig:get("disabled_settings") or ""):split()
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
				core.log("error", "Invalid disabled setting in crate.conf: "..tostring(value))
			end
		end
	end
	return disabled_settings
end

local function get_formspec(tabview, name, tabdata)

	-- Point the player to ContentDB when no crates are found
	if #pkgmgr.crates == 0 then
		local W = tabview.width
		local H = tabview.height

		local hypertext = "<global valign=middle halign=center size=18>" ..
				fgettext_ne("Axis is a crate engine: it runs crates, and does not come with one.") .. "\n" ..
				fgettext_ne("You need to install a crate before you can create a world.")

		local button_y = H * 2/3 - 0.6
		return table.concat({
			"hypertext[0.375,0;", W - 2*0.375, ",", button_y, ";ht;", core.formspec_escape(hypertext), "]",
			"button[5.25,", button_y, ";5,1.2;crate_open_cdb;", fgettext("Install a crate"), "]"})
	end

	local retval = ""

	local index = core.get_textlist_index("sp_worlds") or filterlist.get_current_index(menudata.worldlist,
				tonumber(core.settings:get("mainmenu_last_selected_world"))) or 0

	local list = menudata.worldlist:get_list()
	-- When changing tabs to a world list with fewer entries, the last index is selected (visually).
	-- However, the formspec fields lag behind, thus 'index > #list' can be a valid choice.
	local world = list and list[math.min(index, #list)]
	local crate

	if world then
		crate = pkgmgr.find_by_crateid(world.crateid)
	else
		crate = current_crate()
	end
	local disabled_settings = get_disabled_settings(crate)

	-- Crate settings the world offers as checkboxes. Each one that survives the
	-- crate's disabled list takes the next slot down, so the box's own place is
	-- decided by how many stand above it, not by a running offset.
	local FIRST_Y = 0.2
	local ROW_HEIGHT = 0.5625

	local boxes = {}
	local function checkbox(setting, label)
		if disabled_settings[setting] ~= nil then
			return
		end
		local y = FIRST_Y + #boxes * ROW_HEIGHT
		boxes[#boxes + 1] = "checkbox[0," .. y .. ";cb_" .. setting .. ";" ..
			label .. ";" .. dump(core.settings:get_bool(setting)) .. "]"
	end

	if world then
		checkbox("creative_mode", fgettext("Creative Mode"))
		checkbox("enable_damage", fgettext("Enable Damage"))
	end

	-- Two cards: what the world is set up as, and which world it is
	retval = retval ..
			menu_style.surface(0.375, 0.375, 4.5, tabview.height - 0.75) ..
			menu_style.surface(5.25, 0.375, 9.875, 4.5) ..
			menu_style.heading(0.75, 0.55, 3.9, 0.6, fgettext("Crate")) ..
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
			table.concat(boxes) ..
			"container_end[]" ..
			"container[5.625,0.375]" ..
			menu_style.heading(0, 0.15, 6, 0.6, fgettext("Select World:")) ..
			"textlist[0,0.85;9.125,3.6;sp_worlds;" ..
			menu_render_worldlist() ..
			";" .. index .. "]" ..
			"container_end[]"

	if world then
		retval = retval ..
				menu_style.accent("play") ..
				"button[10.1875,5.925;4.9375,0.8;play;" .. fgettext("Play Crate") .. "]"
	end

	return retval
end

local function main_button_handler(this, fields, name, tabdata)

	assert(name == "local")

	if fields.crate_open_cdb then
		local maintab = ui.find_by_name("maintab")
		local dlg = create_contentdb_dlg("crate")
		dlg:set_parent(maintab)
		maintab:hide()
		dlg:show()
		return true
	end

	if this.dlg_create_world_closed_at == nil then
		this.dlg_create_world_closed_at = 0
	end

	local world_doubleclick = false

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

		-- Update last crate
		local world = menudata.worldlist:get_raw_element(gamedata.selected_world)
		local crate_obj
		if world then
			crate_obj = pkgmgr.find_by_crateid(world.crateid)
			core.settings:set("menu_last_crate", crate_obj.id)
		end

		local disabled_settings = get_disabled_settings(crate_obj)
		for k, _ in pairs(valid_disabled_settings) do
			local v = disabled_settings[k]
			if v ~= nil then
				core.settings:set_bool(k, disabled_settings[k])
			end
		end

		gamedata.mode = "singleplayer"

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
		local crate = current_crate()
		if crate then
			apply_crate(crate)
		else
			mm_crate_theme.set_engine()
		end
	elseif type == "LEAVE" then
		menudata.worldlist:set_filtercriteria(nil)
	end
end

--------------------------------------------------------------------------------
return {
	name = "local",
	caption = "Singleplayer",
	cbf_formspec = get_formspec,
	cbf_button_handler = main_button_handler,
	on_change = on_change
}
