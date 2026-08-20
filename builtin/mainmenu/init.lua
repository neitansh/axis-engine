-- Luanti
-- Copyright (C) 2014 sapier
-- SPDX-License-Identifier: LGPL-2.1-or-later

MAIN_TAB_W = 15.5
MAIN_TAB_H = 7.1
TABHEADER_H = 0.85
GAMEBAR_H = 1.25
FOOTER_H = 1.0
GAMEBAR_OFFSET_DESKTOP = 0.375
GAMEBAR_OFFSET_TOUCH = 0.15

local menupath = core.get_mainmenu_path()
local basepath = core.get_builtin_path()
defaulttexturedir = core.get_texturepath_share() .. DIR_DELIM .. "base" .. DIR_DELIM .. "pack" .. DIR_DELIM

dofile(basepath .. "common" .. DIR_DELIM .. "menu_style.lua")
dofile(basepath .. "common" .. DIR_DELIM .. "menu.lua")
dofile(basepath .. "common" .. DIR_DELIM .. "filterlist.lua")
dofile(basepath .. "fstk" .. DIR_DELIM .. "buttonbar.lua")
dofile(basepath .. "fstk" .. DIR_DELIM .. "dialog.lua")
dofile(basepath .. "fstk" .. DIR_DELIM .. "tabview.lua")
dofile(basepath .. "fstk" .. DIR_DELIM .. "ui.lua")
dofile(menupath .. DIR_DELIM .. "async_event.lua")
dofile(menupath .. DIR_DELIM .. "common.lua")
dofile(menupath .. DIR_DELIM .. "serverlistmgr.lua")
dofile(menupath .. DIR_DELIM .. "presence.lua")
dofile(menupath .. DIR_DELIM .. "matchmaking.lua")
dofile(menupath .. DIR_DELIM .. "place_theme.lua")
dofile(menupath .. DIR_DELIM .. "content" .. DIR_DELIM .. "init.lua")

dofile(menupath .. DIR_DELIM .. "dlg_config_world.lua")
dofile(basepath .. "common" .. DIR_DELIM .. "settings" .. DIR_DELIM .. "init.lua")
dofile(menupath .. DIR_DELIM .. "dlg_confirm_exit.lua")
dofile(menupath .. DIR_DELIM .. "dlg_create_world.lua")
dofile(menupath .. DIR_DELIM .. "dlg_delete_content.lua")
dofile(menupath .. DIR_DELIM .. "dlg_delete_world.lua")
dofile(menupath .. DIR_DELIM .. "dlg_rename_modpack.lua")
dofile(menupath .. DIR_DELIM .. "dlg_reinstall_mtg.lua")
dofile(menupath .. DIR_DELIM .. "dlg_rebind_keys.lua")
dofile(menupath .. DIR_DELIM .. "dlg_clients_list.lua")
dofile(menupath .. DIR_DELIM .. "dlg_server_list_mods.lua")
dofile(menupath .. DIR_DELIM .. "dlg_start.lua")

local tabs = {
	content = dofile(menupath .. DIR_DELIM .. "tab_content.lua"),
	about = dofile(menupath .. DIR_DELIM .. "tab_about.lua"),
	local_place = dofile(menupath .. DIR_DELIM .. "tab_local.lua"),
	play_online = dofile(menupath .. DIR_DELIM .. "tab_online.lua"),
}

local start_page

local function main_event_handler(tabview, event)
	if event == "MenuQuit" then
		-- Step back to the landing page instead of leaving the menu
		tabview:hide()
		start_page:show()
		return true
	end
	return true
end

local function init_globals()
	-- Permanent warning if on an unoptimized debug build
	if core.is_debug_build() then
		local set_topleft_text = core.set_topleft_text
		core.set_topleft_text = function(s)
			s = (s or "") .. "\n"
			s = s .. core.colorize("#f22", core.gettext("Debug build, expect worse performance"))
			set_topleft_text(s)
		end
	end

	-- Init gamedata
	gamedata.worldindex = 0

	menudata.worldlist = filterlist.create(
		core.get_worlds,
		compare_worlds,
		-- Unique id comparison function
		function(element, uid)
			return element.name == uid
		end,
		-- Filter function
		function(element, placeid)
			-- Keep in sync with the logic in pkgmgr.find_by_placeid
			local el_placeid = pkgmgr.normalize_place_id(element.placeid)
			if el_placeid == placeid then
				return true
			end
			local place = pkgmgr.find_by_placeid(el_placeid)
			if (not place or place.id ~= el_placeid) and pkgmgr.find_by_placeid(placeid).aliases[el_placeid] then
				return true
			end
			return false
		end
	)

	menudata.worldlist:add_sort_mechanism("alphabetic", sort_worlds_alphabetic)
	menudata.worldlist:set_sortmode("alphabetic")

	mm_place_theme.init()
	mm_place_theme.set_engine() -- This is just a fallback.

	-- Create main tabview
	local tv_main = tabview_create("maintab", { x = MAIN_TAB_W, y = MAIN_TAB_H }, { x = 0, y = 0 })

	tv_main:set_sidebar({
		width = 3.4,
		gap = 0.35,

		actions = {
			{
				name = "open_settings",
				label = fgettext("Settings"),
				on_click = function(tabview)
					local dlg = create_settings_dlg()
					dlg:set_parent(tabview)
					tabview:hide()
					dlg:show()
					return true
				end,
			},
			{
				name = "quit",
				label = fgettext("Exit"),
				on_click = function()
					core.close()
					return true
				end,
			},
		},
	})

	tv_main:set_autosave_tab(true)
	tv_main:add(tabs.local_place)
	tv_main:add(tabs.play_online)
	tv_main:add(tabs.content)

	tabs.about.sidebar = false
	tv_main:add(tabs.about)

	tv_main:set_global_event_handler(main_event_handler)
	tv_main:set_fixed_size(false)

	local last_tab = core.settings:get("maintab_LAST")
	if last_tab and tv_main.current_tab ~= last_tab then
		tv_main:set_tab(last_tab)
	end

	start_page = create_start_page(tv_main)

	-- Открыться сразу на нужном экране, минуя стартовую страницу. Пусто —
	-- обычный вход; заполняют это стенды, которым надо снять экран, и некому
	-- нажать за них кнопку.
	local straight = core.settings:get("mainmenu_start_tab")
	local skip_start = straight and straight ~= "" and tv_main:set_tab(straight)

	local parent
	if skip_start then
		ui.set_default("maintab")
		tv_main:show()
		ui.update()
		parent = tv_main
	else
		ui.set_default("mainmenu_start")
		start_page:show()
		ui.update()
		parent = start_page
	end

	-- synchronous, chain parents to only show one at a time
	parent = migrate_keybindings(parent)
	check_reinstall_mtg(parent)
end

-- Спросить билет у лаунчера и, когда он придёт, запустить игру.
--
-- Запрос уходит в асинхронное состояние: главный поток стоять не имеет права,
-- пока ответа нет, — меню перестанет рисоваться. Отсюда и устройство: core.start
-- не запускает игру сразу, а уходит за билетом и запускает её в ответе.
--
-- В асинхронном состоянии живёт только то, что туда передали: ни fgettext_ne,
-- ни gamedata там нет. Поэтому оттуда возвращаются короткие пометки, а
-- человеческие слова подбираются уже здесь.
local asking = false

-- Ждём ли сейчас билет. Спрашивают вкладки: пока билет в пути, кнопка входа
-- меняет подпись и перестаёт нажиматься.
function core.waiting_for_ticket()
	return asking
end

-- Под каким именем игрок вошёл.
--
-- Спрашивается отдельно от билета, потому что нужно раньше него: очередь
-- подбора матча ведётся по имени — им Диспетчер отличает игроков и не даёт
-- одному занять два места, — а билет берётся уже на выходе из очереди, на
-- найденный матч.
--
-- Прав это имя не даёт никаких, и подменить им ничего нельзя: правами
-- распоряжается билет, а сервер сверяет имя с тем, что в билете записано. Взять
-- его самому клиенту неоткуда — сессия есть только у лаунчера, и логин могли
-- сменить с другой машины.
local function ask_for_login()
	local url = core.settings:get("axis_ticket_url") or ""
	local key = core.settings:get("axis_ticket_key") or ""
	if url == "" or key == "" then
		-- Без лаунчера имени нет, и придумывать его за игрока незачем: без
		-- билета его всё равно никуда не пустят.
		return
	end

	core.handle_async(function(p)
		local http = core.get_http_api()
		if not http then
			return nil
		end
		local res = http.fetch_sync({
			url = p.url .. "/login",
			method = "GET",
			timeout = 10,
			extra_headers = { "Authorization: Bearer " .. p.key },
		})
		if not res.succeeded then
			return nil
		end
		local body = res.data and core.parse_json(res.data) or nil
		if res.code ~= 200 or type(body) ~= "table" then
			return nil
		end
		return body.login
	end, { url = url, key = key }, function(login)
		if type(login) == "string" and login ~= "" then
			core.settings:set("name", login)
		end
	end)
end

local function ask_for_ticket(url, key, server_id, done)
	core.handle_async(function(p)
		local http = core.get_http_api()
		if not http then
			return { trouble = "no_http" }
		end
		local res = http.fetch_sync({
			url = p.url .. "/ticket",
			method = "POST",
			timeout = 10,
			extra_headers = {
				"Authorization: Bearer " .. p.key,
				"Content-Type: application/json",
			},
			data = p.body,
		})
		if not res.succeeded then
			return { trouble = "silent" }
		end
		local body = res.data and core.parse_json(res.data) or nil
		if res.code ~= 200 or not body or not body.ticket then
			return { trouble = "refused", said = body and body.message or nil }
		end
		return { ticket = body.ticket, login = body.login }
	end, {
		url = url,
		key = key,
		body = core.write_json({ server = server_id }),
	}, done)
end

-- Имя места из списка. Не нашлось — пусто: лучше общее «на сервере», чем адрес
-- в чужом профиле.
local function place_name()
	for _, server in ipairs(serverlistmgr.servers or {}) do
		local same = gamedata.server_id and gamedata.server_id ~= "" and
			server.id == gamedata.server_id
		if same or (server.address == gamedata.address and server.port == gamedata.port) then
			return server.place or ""
		end
	end
	return ""
end

-- Куда игрок пошёл — лаунчеру, чтобы он показал это в профиле.
--
-- Здесь, а не в каждой вкладке: дорог в игру три, и рассказывать о себе они
-- должны одинаково. Отсюда же видно, чем они отличаются, — по адресу матч от
-- обычного места не отличишь, он один и тот же.
--
-- Адрес наружу не уходит никогда: место называется своим именем из реестра, а
-- не найденное там — просто «на сервере». Введённый руками адрес чужого
-- сервера — не то, что стоит показывать всем друзьям игрока.
local function tell_where_we_are_going()
	local match = gamedata.match
	-- Таблица между заходами не чистится, и пометка прошлого матча иначе
	-- сделала бы матчем следующее обычное место.
	gamedata.match = nil

	if match then
		presence.playing({ where = "match", place = place_name(), mode = match })
	elseif gamedata.mode == "join" then
		presence.playing({ where = "place", name = place_name() })
	else
		presence.playing({ where = "solo" })
	end
end

-- Билет к запуску игры.
--
-- Билет — короткая подписанная строка, которой игрок доказывает серверу, что он
-- тот, за кого себя выдаёт. Выписывает её служба аккаунтов, и **на конкретный
-- сервер**: на другом он не работает.
--
-- Отсюда порядок. Билет берётся не при запуске клиента, а здесь — когда уже
-- известно, куда игрок идёт. Раньше лаунчер выписывал билет вперёд, на
-- вписанный в него сервер, и это разваливалось на втором: на любой другой
-- уезжал бы билет, выписанный не ему.
--
-- Просить билет ходим к лаунчеру: сессия игрока есть только у него, и клиенту
-- её не дают. Адрес его дверцы и ключ этого запуска приходят в командной строке
-- (--ticket-url, --ticket-key) и лежат в настройках.
--
-- Одно место на все пути к серверу — подбор матча, список серверов, прямой
-- адрес: дорога у них разная, а спрашивать билет надо одинаково.
--
-- Билет одноразовый: сервер гасит его при входе, и второй раз он не сработает.
-- А в gamedata он остаётся с прошлого захода — таблица между входами не
-- чистится, — поэтому старый выбрасывается здесь же. Иначе второй вход за
-- запуск предъявлял бы погашенный билет и получал отказ, а выглядело бы это
-- поломкой входа.
local start_place = core.start
function core.start()
	if not gamedata then
		return start_place()
	end
	gamedata.ticket = nil
	tell_where_we_are_going()

	local url = core.settings:get("axis_ticket_url") or ""
	local key = core.settings:get("axis_ticket_key") or ""
	if url == "" or key == "" then
		-- Клиент запустили без лаунчера. Билета не будет, и это не наша беда:
		-- сервер, который его спрашивает, откажет и скажет почему. Своя игра и
		-- свой сервер при этом работают как работали.
		gamedata.ticket = ""
		return start_place()
	end

	if not gamedata.server_id or gamedata.server_id == "" then
		gamedata.errormessage =
			fgettext_ne("This server is not in the registry, so no ticket can be issued for it.")
		ui.update()
		return
	end

	-- Второе нажатие, пока билет в пути, ни к чему: получилось бы два билета и
	-- два запуска.
	if asking then
		return
	end
	asking = true
	-- Билет идёт к лаунчеру и обратно к службе, и это не мгновенно. Пока он в
	-- пути, экран обязан показать, что нажатие услышано: иначе игрок жмёт ещё
	-- раз, решив, что кнопка не сработала.
	ui.update()

	ask_for_ticket(url, key, gamedata.server_id, function(answer)
		asking = false
		ui.update()
		if answer.trouble == "refused" then
			-- Лаунчер уже перевёл отказ службы на человеческий; показываем его
			-- как есть, а не своё «не удалось подключиться».
			gamedata.errormessage = answer.said or
				fgettext_ne("The launcher refused to give a ticket.")
		elseif answer.trouble then
			gamedata.errormessage =
				fgettext_ne("The launcher is not answering. Is it still running?")
		end
		if answer.trouble then
			ui.update()
			return
		end
		-- Имя приходит вместе с билетом, а не задаётся игроком: сервер сверяет
		-- его с тем, что записано в билете, и не сойдётся — не пустит. Логин
		-- могли сменить с другой машины, поэтому спрашиваем его каждый раз, а
		-- не помним однажды записанное.
		if answer.login and answer.login ~= "" then
			gamedata.playername = answer.login
			core.settings:set("name", answer.login)
		end

		gamedata.ticket = answer.ticket
		start_place()
	end)
end

assert(os.execute == nil)
init_globals()

-- Имя спрашивается один раз за запуск и после того, как меню построено: ответ
-- приходит асинхронно и только записывает настройку, так что ждать его некому.
ask_for_login()

-- Меню открыто — значит игра запущена, а игрок ещё никуда не пошёл. Сюда же
-- возвращаются из игры: меню строится заново каждый раз.
presence.in_menu()
