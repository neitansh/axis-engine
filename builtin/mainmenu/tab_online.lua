-- Luanti
-- Copyright (C) 2014 sapier
-- SPDX-License-Identifier: LGPL-2.1-or-later

-- Кого вообще можно отметить любимым: чужой сервер — да, наш — нет.
--
-- Правило записано здесь одно на всех: по нему и рисуется звёздочка, и
-- раскладывается список. Разъедься они — игрок увидел бы кнопку, которая
-- ничего не меняет.
local function can_be_favorite(server)
	return server ~= nil and not server.official
end

-- Список делится на три части, и деление это не косметика.
--
-- Официальные — наши: их поднимает Axis, в них играют в то, что мы делаем.
-- Они всегда наверху, и отмечать их любимыми нечего — искать их не по чему.
--
-- Сообщество — чужие: их поднимают сами игроки и вписывают в реестр. Их будет
-- много, и вот среди них любимое теряется — потому звёздочка есть только тут.
--
-- Избранное — то, что игрок отметил сам, и оно выше всего остального.
local function get_sorted_servers()
	local servers = {
		fav = {},
		official = {},
		community = {},
		incompatible = {}
	}

	local result = menudata.search_result or serverlistmgr.shown
	for _, server in ipairs(result) do
		server.is_compatible = is_server_protocol_compat(server.proto_min, server.proto_max)
		server.is_favorite = can_be_favorite(server) and
			serverlistmgr.is_favorite(server.id)

		if not server.is_compatible then
			table.insert(servers.incompatible, server)
		elseif server.is_favorite then
			table.insert(servers.fav, server)
		elseif server.official then
			table.insert(servers.official, server)
		else
			table.insert(servers.community, server)
		end
	end

	return servers
end

-- Persists the selected server in the "address" and "remote_port" settings

local function set_selected_server(server)
	if server == nil then -- reset selection
		core.settings:remove("address")
		core.settings:remove("remote_port")
		return
	end
	local address = server.address
	local port    = server.port

	if address and port then
		core.settings:set("address", address)
		core.settings:set("remote_port", port)
	end
end

local function find_selected_server()
	local address = core.settings:get("address")
	local port = tonumber(core.settings:get("remote_port"))
	for _, server in ipairs(serverlistmgr.shown) do
		if server.address == address and server.port == port then
			return server
		end
	end
end

-- Экран ведёт в игру двумя дорогами: подбором матча и списком серверов.
-- Переключают их закладки над карточкой — те самые, что торчат из книги.
-- Рисуются они в отрицательных координатах, то есть выше верхнего края
-- карточки: место над ней занимает заголовок меню, и закладке там просторно.
-- Внутри карточки их не рисуют намеренно — полоска кнопок съедала строку
-- списка и читалась как часть содержимого, а не как переключатель страниц.
local TAB_W = 2.8
local TAB_GAP = 0.2
local TAB_X = 0.25
-- Насколько закладка торчит над карточкой. Открытая выше закрытых — этим она
-- и видна, не считая цвета.
local TAB_UP = 0.7
local TAB_UP_IDLE = 0.55
-- Первое, что видно на открытой закладке, — полоска цветом игры по верхнему
-- краю. «Вы здесь» так понятно, не читая.
local TAB_EDGE = 0.06
-- Насколько закладка заходит под карточку. Кнопка рисует подложку чуть уже
-- своего места, и между закладкой и карточкой оставалась щель — сквозь неё
-- виден мир, и книга разваливалась на две части.
local TAB_INTO = 0.04

-- Содержимое начинается сразу под верхним краем карточки: закладки места
-- внутри неё больше не занимают.
local CONTENT_TOP = 0.35

local function mode_strip(tabdata)
	local fs = {}
	local modes = {
		{ id = "matches", label = fgettext("Matches") },
		{ id = "servers", label = fgettext("Server List") },
	}
	for i, mode in ipairs(modes) do
		local x = TAB_X + (i - 1) * (TAB_W + TAB_GAP)
		local open = tabdata.mode == mode.id
		local up = open and TAB_UP or TAB_UP_IDLE
		fs[#fs + 1] = menu_style.tab("mode_" .. mode.id, open)
		fs[#fs + 1] = ("button[%f,%f;%f,%f;mode_%s;%s]")
			:format(x, -up, TAB_W, up + TAB_INTO, mode.id, mode.label)
		if open then
			fs[#fs + 1] = ("box[%f,%f;%f,%f;%s]")
				:format(x, -up, TAB_W, TAB_EDGE, menu_style.ACCENT)
		end
	end
	return table.concat(fs)
end

local function get_formspec(tabview, name, tabdata)
	-- Update the cached supported proto info,
	-- it may have changed after a change by the settings menu.
	common_update_cached_supp_proto()

	if not tabdata.mode then
		tabdata.mode = "matches"
	end

	if tabdata.mode == "matches" then
		return mode_strip(tabdata) ..
			matchmaking.get_formspec(0.25, CONTENT_TOP, 15, 7.1 - CONTENT_TOP - 0.25)
	end

	if not tabdata.search_for then
		tabdata.search_for = ""
	end

	local retval = mode_strip(tabdata) ..
		("container[0,%f]"):format(CONTENT_TOP) ..
		-- Поиск. Врезка под ним не украшение: без неё поле было прозрачным и
		-- невидимым до первого щелчка — не понять, что туда вообще пишут.
		menu_style.inset(0.25, 0.25, 7, 0.75) ..
		"field[0.35,0.25;6.8,0.75;te_search;;" .. core.formspec_escape(tabdata.search_for) .. "]" ..
		"tooltip[te_search;" .. core.formspec_escape(table.concat({
				fgettext("Possible filters"),
				"place:<name>",
				"mod:<name>",
				"player:<name>",
				"sort:[-](name|relevance|players|mods|uptime|ping|lag)",
		}, "\n")) .. "]" ..
		"field_enter_after_edit[te_search;true]" ..
		"container[7.25,0.25]" ..
		"image_button[0,0;0.75,0.75;" .. core.formspec_escape(defaulttexturedir .. "search.png") .. ";btn_mp_search;]" ..
		"image_button[0.75,0;0.75,0.75;" .. core.formspec_escape(defaulttexturedir .. "clear.png") .. ";btn_mp_clear;]" ..
		"image_button[1.5,0;0.75,0.75;" .. core.formspec_escape(defaulttexturedir .. "refresh.png") .. ";btn_mp_refresh;]" ..
		"tooltip[btn_mp_clear;" .. fgettext("Clear") .. "]" ..
		"tooltip[btn_mp_search;" .. fgettext("Search") .. "]" ..
		-- TRANSLATORS: As in 'reload'/'check again'
		"tooltip[btn_mp_refresh;" .. fgettext("Refresh") .. "]" ..
		"container_end[]" ..

		"container[9.75,0]" ..
		menu_style.surface(0, 0, 5.75, 6.75) ..

		-- Порту нужно место на все пять цифр и поля по краям: в узкой
		-- клетке «30000» упиралось в обе стенки и читалось как обрезанное.
		-- Отобрано у адреса — там запас был.
		--
		-- TRANSLATORS: Network address
		"label[0.25,0.35;" .. fgettext("Address") .. "]" ..
		-- TRANSLATORS: Network port
		"label[3.95,0.35;" .. fgettext("Port") .. "]" ..
		menu_style.inset(0.25, 0.5, 3.6, 0.75) ..
		"field[0.35,0.5;3.4,0.75;te_address;;" ..
			core.formspec_escape(core.settings:get("address")) .. "]" ..
		menu_style.inset(3.95, 0.5, 1.55, 0.75) ..
		"field[4.10,0.5;1.25,0.75;te_port;;" ..
			core.formspec_escape(core.settings:get("remote_port")) .. "]" ..

		-- Description Background
		"label[0.25,1.6;" .. fgettext("Server Description") .. "]" ..
		menu_style.inset(0.25, 1.85, 5.25, 3.3)

	-- Ни имени, ни пароля здесь больше нет: игрок доказывает, кто он, билетом,
	-- а билет выписывает axis-auth по сессии лаунчера. Имя приходит вместе с
	-- билетом — см. builtin/mainmenu/init.lua.
	--
	-- TRANSLATORS: Join a server
	-- Пока билет в пути, кнопка говорит об этом сама: нажатие уже принято, и
	-- второе ничего не ускорит (см. core.waiting_for_ticket).
	if core.waiting_for_ticket() then
		retval = retval ..
				"button[3,5.65;2.5,0.75;btn_mp_waiting;" .. fgettext("Connecting…") .. "]"
	else
		retval = retval .. menu_style.accent("btn_mp_login") ..
				"button[3,5.65;2.5,0.75;btn_mp_login;" .. fgettext("Join") .. "]"
	end

	local selected_server = find_selected_server()

	if selected_server then
		if selected_server.description then
			retval = retval .. "textarea[0.25,1.85;5.25,3.3;;;" ..
				core.formspec_escape(selected_server.description) .. "]"
		end

		-- URL button
		if selected_server.url then
			retval = retval .. "tooltip[btn_server_url;" .. fgettext("Open server website") .. "]"
			retval = retval .. "style[btn_server_url;padding=6]"
			retval = retval .. "image_button[3.5,1.3;0.5,0.5;" ..
				core.formspec_escape(defaulttexturedir .. "server_url.png") .. ";btn_server_url;]"
		else
			retval = retval .. "image[3.6,1.4;0.3,0.3;" .. core.formspec_escape(defaulttexturedir ..
				"server_url_unavailable.png") .. "]"
		end

		-- Mods button
		local mods = selected_server.mods
		if mods and #mods > 0 then
			local tooltip = ""
			if selected_server.placeid then
				tooltip = fgettext("Place: $1", selected_server.placeid) .. "\n"
			end
			tooltip = tooltip .. fgettext("Number of mods: $1", #mods)

			retval = retval ..
				"tooltip[btn_view_mods;" .. tooltip .. "]" ..
				"style[btn_view_mods;padding=6]" ..
				"image_button[4,1.3;0.5,0.5;" .. core.formspec_escape(defaulttexturedir ..
				"server_view_mods.png") .. ";btn_view_mods;]"
		else
			retval = retval .. "image[4.1,1.4;0.3,0.3;" .. core.formspec_escape(defaulttexturedir ..
				"server_view_mods_unavailable.png") .. "]"
		end

		-- Clients list button
		local clients_list = selected_server.clients_list
		local can_view_clients_list = clients_list and #clients_list > 0
		if can_view_clients_list then
			table.sort(clients_list, function(a, b)
				return a:lower() < b:lower()
			end)
			local max_clients = 5
			if #clients_list > max_clients then
				retval = retval .. "tooltip[btn_view_clients;" ..
						-- TRANSLATORS: $1 is a list of players
						fgettext("Players:\n$1", table.concat(clients_list, "\n", 1, max_clients)) .. "\n..." .. "]"
			else
				retval = retval .. "tooltip[btn_view_clients;" ..
						fgettext("Players:\n$1", table.concat(clients_list, "\n")) .. "]"
			end
			retval = retval .. "style[btn_view_clients;padding=6]"
			retval = retval .. "image_button[4.5,1.3;0.5,0.5;" .. core.formspec_escape(defaulttexturedir ..
				"server_view_clients.png") .. ";btn_view_clients;]"
		else
			retval = retval .. "image[4.6,1.4;0.3,0.3;" .. core.formspec_escape(defaulttexturedir ..
				"server_view_clients_unavailable.png") .. "]"
		end

		-- Отметить любимым. У наших серверов кнопки нет вовсе: они и так
		-- наверху отдельным разделом, и отмечать среди них нечего.
		if can_be_favorite(selected_server) then
			if serverlistmgr.is_favorite(selected_server.id) then
				retval = retval .. "tooltip[btn_delete_favorite;" .. fgettext("Remove favorite") .. "]"
				retval = retval .. "style[btn_delete_favorite;padding=6]"
				retval = retval .. "image_button[5,1.3;0.5,0.5;" ..
					core.formspec_escape(defaulttexturedir .. "server_favorite_delete.png") .. ";btn_delete_favorite;]"
			else
				retval = retval .. "tooltip[btn_add_favorite;" .. fgettext("Add favorite") .. "]"
				retval = retval .. "style[btn_add_favorite;padding=6]"
				retval = retval .. "image_button[5,1.3;0.5,0.5;" ..
					core.formspec_escape(defaulttexturedir .. "server_favorite.png") .. ";btn_add_favorite;]"
			end
		end

	end

	retval = retval .. "container_end[]"

	-- Table
	retval = retval .. "tablecolumns[" ..
		-- TRANSLATORS: Also known as "latency"
		"image,tooltip=" .. fgettext("Ping") .. "," ..
		"0=" .. core.formspec_escape(defaulttexturedir .. "blank.png") .. "," ..
		"1=" .. core.formspec_escape(defaulttexturedir .. "server_ping_4.png") .. "," ..
		"2=" .. core.formspec_escape(defaulttexturedir .. "server_ping_3.png") .. "," ..
		"3=" .. core.formspec_escape(defaulttexturedir .. "server_ping_2.png") .. "," ..
		"4=" .. core.formspec_escape(defaulttexturedir .. "server_ping_1.png") .. "," ..
		"5=" .. core.formspec_escape(defaulttexturedir .. "server_favorite.png") .. "," ..
		"6=" .. core.formspec_escape(defaulttexturedir .. "server_official.png") .. "," ..
		"7=" .. core.formspec_escape(defaulttexturedir .. "server_incompatible.png") .. "," ..
		"8=" .. core.formspec_escape(defaulttexturedir .. "server_public.png") .. ";" ..
		"color,span=1;" ..
		"text,align=inline;"..
		"color,span=1;" ..
		"text,align=inline,width=4.25;" ..
		"image,tooltip=" .. fgettext("Creative mode") .. "," ..
		"0=" .. core.formspec_escape(defaulttexturedir .. "blank.png") .. "," ..
		"1=" .. core.formspec_escape(defaulttexturedir .. "server_flags_creative.png") .. "," ..
		"align=inline,padding=0.25,width=1.5;" ..
		-- TRANSLATORS: PvP = Player versus Player
		"image,tooltip=" .. fgettext("Damage / PvP") .. "," ..
		"0=" .. core.formspec_escape(defaulttexturedir .. "blank.png") .. "," ..
		"1=" .. core.formspec_escape(defaulttexturedir .. "server_flags_damage.png") .. "," ..
		"2=" .. core.formspec_escape(defaulttexturedir .. "server_flags_pvp.png") .. "," ..
		"align=inline,padding=0.25,width=1.5;" ..
		"color,align=inline,span=1;" ..
		"text,align=inline,padding=1]" ..
		"table[0.25,1;9.25,5.45;servers;"

	local servers = get_sorted_servers()

	-- Заголовки разделов. Цвет разделяет их с одного взгляда: любимое —
	-- тёплым, наше — цветом игры, чужое и несовместимое — приглушённым.
	local dividers = {
		fav = "5,#F2C14E," .. fgettext("Favorites") .. ",,,0,0,,",
		official = "6," .. menu_style.ACCENT .. "," .. fgettext("Official Servers") .. ",,,0,0,,",
		community = "8,#8E8899," .. fgettext("Community Servers") .. ",,,0,0,,",
		incompatible = "7,"..mt_color_grey.."," .. fgettext("Incompatible Servers") .. ",,,0,0,,"
	}
	local order = {"fav", "official", "community", "incompatible"}

	tabdata.lookup = {} -- maps row number to server
	local rows = {}
	for _, section in ipairs(order) do
		local section_servers = servers[section]
		if next(section_servers) ~= nil then
			rows[#rows + 1] = dividers[section]
			for _, server in ipairs(section_servers) do
				tabdata.lookup[#rows + 1] = server
				rows[#rows + 1] = render_serverlist_row(server)
			end
		end
	end

	retval = retval .. table.concat(rows, ",")

	local selected_row_idx = 0
	if selected_server then
		for i, server in pairs(tabdata.lookup) do
			if selected_server.address == server.address and
					selected_server.port == server.port then
				selected_row_idx = i
				break
			end
		end
	end
	retval = retval .. ";" .. selected_row_idx .. "]"

	return retval .. "container_end[]"
end

--------------------------------------------------------------------------------

local function parse_search_input(input)
	if not input:find("%S") then
		return -- Return nil if nothing to search for
	end

	-- Search is not case sensitive
	input = input:lower()

	local query = {keywords = {}, mods = {}, players = {}}

	-- Process quotation enclosed parts
	input = input:gsub('(%S?)"([^"]*)"(%S?)', function(before, match, after)
		if before == "" and after == "" then -- Also have be separated by spaces
			table.insert(query.keywords, match)
			return " "
		end
		return before..'"'..match..'"'..after
	end)

	-- Separate by space characters and handle special prefixes
	-- (words with special prefixes need an exact match and none of them can contain spaces)
	for word in input:gmatch("%S+") do
		local mod = word:match("^mod:(.*)")
		table.insert(query.mods, mod)
		local player = word:match("^player:(.*)")
		table.insert(query.players, player)
		local place = word:match("^place:(.*)")
		query.place = query.place or place
		local sort = word:match("^sort:(.*)")
		query.sort = query.sort or sort
		if not (mod or player or place or sort) then
			table.insert(query.keywords, word)
		end
	end

	return query
end

-- Prepares the server to be used for searching
local function uncapitalize_server(server)
	local function table_lower(t)
		local r = {}
		for i, s in ipairs(t or {}) do
			r[i] = s:lower()
		end
		return r
	end

	return {
		name = (server.name or ""):lower(),
		description = (server.description or ""):lower(),
		placeid = (server.placeid or ""):lower(),
		mods = table_lower(server.mods),
		clients_list = table_lower(server.clients_list),
	}
end

-- Returns false if the query does not match
-- otherwise returns a number to adjust the sorting priority
local function matches_query(server, query)
	-- Search is not case sensitive
	server = uncapitalize_server(server)

	-- Check if mods found
	for _, mod in ipairs(query.mods) do
		if table.indexof(server.mods, mod) < 0 then
			return false
		end
	end

	-- Check if players found
	for _, player in ipairs(query.players) do
		if table.indexof(server.clients_list, player) < 0 then
			return false
		end
	end

	-- Check if place matches
	if query.place and query.place ~= server.placeid then
		return false
	end

	-- Check if keyword found
	local name_matches = true
	local description_matches = true
	for _, keyword in ipairs(query.keywords) do
		name_matches = name_matches and server.name:find(keyword, 1, true)
		description_matches = description_matches and server.description:find(keyword, 1, true)
	end

	return name_matches and 50 or description_matches and 0
end

-- Sorts the serverlist depending on the query
local function sort_servers(servers, query)
	local sort_by = query.sort or "relevance"

	local reverse = false
	if string.sub(sort_by, 1, 1) == "-" then
		reverse = true
		sort_by = string.sub(sort_by, 2)
	end

	local get_compare_val
	if sort_by == "mods" then
		get_compare_val = function(v)
			return v.mods and #v.mods or 0
		end
	elseif sort_by == "lag" then
		get_compare_val = function(v)
			return v.lag or math.huge
		end
	else
		local sort_indices = {
			players = "clients",
			uptime = "uptime",
			name = "name",
			ping = "ping",
			relevance = "points",
		}
		get_compare_val = function(v)
			return v[sort_indices[sort_by] or "points"]
		end
	end

	-- For those lower is typically better
	local asc = {
		name = true,
		ping = true,
		lag = true,
	}

	table.sort(servers, function(a, b)
		if reverse == (asc[sort_by] or false) then
			return get_compare_val(a) > get_compare_val(b)
		else
			return get_compare_val(a) < get_compare_val(b)
		end
	end)
end

local function search_server_list(input, tabdata)
	menudata.search_result = nil
	if #serverlistmgr.shown < 2 then
		return
	end


	tabdata.pre_search_selection = tabdata.pre_search_selection or find_selected_server()

	-- setup the search query
	local query = parse_search_input(input)
	if not query then
		return
	end

	menudata.search_result = {}

	-- Search the serverlist
	local search_result = {}
	for i, server in ipairs(serverlistmgr.shown) do
		local match = matches_query(server, query)
		if match then
			server.points = #serverlistmgr.shown - i + match
			table.insert(search_result, server)
		end
	end

	if #search_result == 0 then
		return
	end

	local current_server = find_selected_server()

	sort_servers(search_result, query)
	menudata.search_result = search_result

	-- Keep current selection if it's in search results
	if current_server then
	    for _, server in ipairs(search_result) do
			if server.address == current_server.address and
					server.port == current_server.port then
				return
			end
		end
	end

	-- Find first compatible server
	for _, server in ipairs(search_result) do
		if is_server_protocol_compat(server.proto_min, server.proto_max) then
			set_selected_server(server)
			return
		end
	end
	-- If no compatible server found, clear selection
	set_selected_server(nil)
end

local function main_button_handler(tabview, fields, name, tabdata)
	for _, mode in ipairs({ "matches", "servers" }) do
		if fields["mode_" .. mode] then
			tabdata.mode = mode
			if mode == "matches" then
				matchmaking.on_enter()
			else
				matchmaking.stop()
			end
			return true
		end
	end

	if tabdata.mode == "matches" then
		return matchmaking.handle(fields)
	end

	if fields.servers then
		local event = core.explode_table_event(fields.servers)
		local server = tabdata.lookup[event.row]

		if server then
			if event.type == "DCL" then
				if not is_server_protocol_compat_or_error(
							server.proto_min, server.proto_max) then
					return true
				end

				gamedata.mode       = "join"
				gamedata.address    = server.address
				gamedata.port       = server.port
				gamedata.server_id  = server.id
				gamedata.selected_world = 0

				if gamedata.address and gamedata.port then
					set_selected_server(server)
					core.start()
				end
				return true
			end
			if event.type == "CHG" then
				set_selected_server(server)
				tabdata.pre_search_selection = nil
				return true
			end
		end
	end


	if fields.btn_add_favorite or fields.btn_delete_favorite then
		local server = find_selected_server()
		if fields.btn_add_favorite and can_be_favorite(server) then
			serverlistmgr.add_favorite(server.id)
		elseif fields.btn_delete_favorite and server then
			serverlistmgr.delete_favorite(server.id)
		end
		return true
	end

	if fields.btn_server_url then
		core.open_url_dialog(find_selected_server().url)
		return true
	end

	if fields.btn_view_clients then
		local dlg = create_clientslist_dialog(find_selected_server())
		dlg:set_parent(tabview)
		tabview:hide()
		dlg:show()
		return true
	end

	if fields.btn_view_mods then
		local dlg = create_server_list_mods_dialog(find_selected_server())
		dlg:set_parent(tabview)
		tabview:hide()
		dlg:show()
		return true
	end

	if fields.btn_mp_clear then
		tabdata.search_for = ""
		menudata.search_result = nil
		if tabdata.pre_search_selection then
			set_selected_server(tabdata.pre_search_selection)
			tabdata.pre_search_selection = nil
		end
		return true
	end

	if fields.btn_mp_search or fields.key_enter_field == "te_search" then
		tabdata.search_for = fields.te_search
		search_server_list(fields.te_search, tabdata)
		return true
	end

	if fields.btn_mp_refresh then
		serverlistmgr.sync()
		return true
	end

	local host_filled = (fields.te_address ~= "") and fields.te_port:match("^%s*[1-9][0-9]*%s*$")
	local te_port_number = tonumber(fields.te_port)

	if (fields.btn_mp_login or fields.key_enter) and host_filled then
		gamedata.mode       = "join"
		gamedata.address    = fields.te_address
		gamedata.port       = te_port_number
		gamedata.selected_world = 0

		local idx = core.get_table_index("servers")
		local server = idx and tabdata.lookup[idx]

		-- Билет выписывается на сервер из реестра, а не на адрес. Введённый
		-- руками адрес ищется в том же списке: не нашёлся — билета не будет, и
		-- игрок должен узнать об этом здесь, а не получить отказ от сервера.
		gamedata.server_id = serverlistmgr.id_of(gamedata.address, gamedata.port)

		if server and server.address == gamedata.address and
				server.port == gamedata.port then
			if not is_server_protocol_compat_or_error(
						server.proto_min, server.proto_max) then
				return true
			end
		end

		core.settings:set("address",     gamedata.address)
		core.settings:set("remote_port", gamedata.port)

		core.start()
		return true
	end

	return false
end

local function on_change(type)
	if type == "ENTER" then
		mm_place_theme.set_engine()
		serverlistmgr.sync()
		-- Экран открыли заново: список арен мог измениться, пока нас не было.
		matchmaking.on_enter()
	elseif type == "LEAVE" then
		-- Ушли с экрана — значит и ждать матча перестали.
		matchmaking.stop()
	end
end

return {
	name = "online",
	caption = fgettext("Join Place"),
	cbf_formspec = get_formspec,
	cbf_button_handler = main_button_handler,
	on_change = on_change
}
