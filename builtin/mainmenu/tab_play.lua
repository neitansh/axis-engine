-- Luanti
-- SPDX-License-Identifier: LGPL-2.1-or-later

-- Вкладка «Играть»: выбор арены и ожидание набора.
--
-- Ждут здесь же, в меню, и это осознанно. Отдельный мир-лобби пришлось бы
-- поднимать, поддерживать и переезжать из него на матч — ради экрана, на
-- котором нечего делать, кроме как смотреть на счётчик. Счётчик рисуется и
-- тут, а в игру игрок попадает сразу на готовый матч.
--
-- Меню ничего не решает само: куда подключаться, говорит Диспетчер. Пока
-- режим один и сервер один, ответ всегда одинаковый, но в тот день, когда
-- их станет много, поменяется ответ, а не клиент.

local ESC = core.formspec_escape

-- Что мы знаем от Диспетчера в последний раз.
local state = {
	region = 1,      -- какой из официальных серверов выбран
	modes = nil,     -- список арен; nil — ещё не спрашивали
	queue = nil,     -- своя очередь: room, waiting, needed, room_state
	status = nil,    -- строка для показа: ошибка или пояснение
	polling = false, -- опрос в полёте
}

local function servers()
	return serverlistmgr.servers or {}
end

local function region()
	return servers()[state.region]
end

local function dispatch_url()
	local server = region()
	return server and server.dispatch or core.settings:get("matchmaking_url")
end

local function player_region()
	local server = region()
	return server and server.region or ""
end

local function player_name()
	local name = core.settings:get("name") or ""
	return (name:gsub("^%s*(.-)%s*$", "%1"))
end

--- Запросы ------------------------------------------------------------------
-- HTTP уходит в асинхронное состояние: главное не имеет права стоять, пока
-- ответа нет, — меню перестанет рисоваться.

local function request(path, body, callback)
	local url = dispatch_url()
	if not url or url == "" then
		callback({ error = fgettext("No matchmaking server configured.") })
		return
	end

	core.handle_async(function(p)
		local http = core.get_http_api()
		if not http then
			return { error = "no http" }
		end
		local res = http.fetch_sync({
			url = p.url,
			method = p.body and "POST" or "GET",
			data = p.body,
			extra_headers = { "Content-Type: application/json" },
			timeout = 20,
		})
		if not res.succeeded then
			return { error = "unreachable" }
		end
		return { code = res.code, data = res.data }
	end, { url = url .. path, body = body }, callback)
end

local function decode(res)
	if not res or res.error then
		return nil, res and res.error or "unreachable"
	end
	local body = core.parse_json(res.data or "")
	if type(body) ~= "table" then
		return nil, "bad answer"
	end
	if res.code ~= 200 then
		return nil, body.error or ("code " .. tostring(res.code))
	end
	return body
end

local function refresh_modes()
	request("/v1/modes", nil, function(res)
		local body, err = decode(res)
		if body then
			state.modes = body.modes or {}
			state.status = nil
		else
			state.modes = {}
			state.status = fgettext("Matchmaking server did not answer") .. " (" .. err .. ")"
		end
		core.event_handler("Refresh")
	end)
end

-- Опрос очереди. Диспетчер придерживает ответ на секунду, поэтому цикл идёт
-- сам собой и никого не молотит.
local function poll()
	if state.polling or not state.queue then
		return
	end
	state.polling = true

	request("/v1/queue", core.write_json({
		player = player_name(),
		region = player_region(),
	}), function(res)
		state.polling = false
		local body, err = decode(res)

		if not body then
			-- Пропавшая очередь — не беда: Диспетчер мог перезапуститься,
			-- и тогда стоять в ней больше негде.
			state.queue = nil
			state.status = fgettext("Left the queue") .. " (" .. err .. ")"
			core.event_handler("Refresh")
			return
		end

		state.queue = body

		if body.address and body.port and body.port > 0 then
			-- Матч готов. Дальше обычный вход на сервер, только адрес не
			-- набран руками, а получен.
			state.queue = nil
			gamedata.mode = "join"
			gamedata.address = body.address
			gamedata.port = body.port
			gamedata.playername = player_name()
			gamedata.password = ""
			gamedata.selected_world = 0
			core.start()
			return
		end

		core.event_handler("Refresh")
		poll()
	end)
end

local function join(mode)
	local name = player_name()
	if name == "" then
		state.status = fgettext("Enter a name first")
		return
	end
	request("/v1/join", core.write_json({
		player = name,
		mode = mode,
		region = player_region(),
	}), function(res)
		local body, err = decode(res)
		if not body then
			state.status = fgettext("Could not join the queue") .. " (" .. err .. ")"
			core.event_handler("Refresh")
			return
		end
		state.queue = body
		state.status = nil
		core.event_handler("Refresh")
		poll()
	end)
end

local function leave()
	local name = player_name()
	state.queue = nil
	request("/v1/leave", core.write_json({ player = name }), function()
		core.event_handler("Refresh")
	end)
end

--- Окно ---------------------------------------------------------------------

local function region_dropdown(x, y, w)
	local names = {}
	for _, server in ipairs(servers()) do
		names[#names + 1] = ESC(server.name or server.address)
	end
	if #names == 0 then
		return ""
	end
	return ("dropdown[%s,%s;%s,0.8;region;%s;%d;true]")
		:format(x, y, w, table.concat(names, ","), state.region)
end

local function get_formspec(tabview, name, tabdata)
	local w = tabview.width or 12
	local fs = {
		("field[0.4,0.7;%s,0.8;name;%s;%s]"):format(w * 0.45, fgettext("Name"),
			ESC(player_name())),
		region_dropdown(w * 0.5 + 0.6, 0.7, w * 0.45 - 0.6),
		("box[0.4,1.7;%s,0.05;#ffffff22]"):format(w - 0.8),
	}

	if state.queue then
		local q = state.queue
		local line
		if q.room_state == "warming" then
			line = fgettext("Everyone is here. Preparing the arena…")
		else
			line = ("%s — %d / %d"):format(ESC(q.title or q.mode),
				q.waiting or 0, q.needed or 0)
		end
		fs[#fs + 1] = ("label[0.4,2.4;%s]"):format(ESC(fgettext("Waiting for players")))
		fs[#fs + 1] = ("label[0.4,3.0;%s]"):format(ESC(line))
		fs[#fs + 1] = ("button[0.4,3.6;3,0.8;leave;%s]"):format(fgettext("Cancel"))
		return table.concat(fs)
	end

	if not state.modes then
		fs[#fs + 1] = ("label[0.4,2.4;%s]"):format(ESC(fgettext("Loading…")))
		return table.concat(fs)
	end

	if #state.modes == 0 then
		fs[#fs + 1] = ("label[0.4,2.4;%s]"):format(
			ESC(state.status or fgettext("No arenas available")))
		fs[#fs + 1] = ("button[0.4,3.0;3,0.8;retry;%s]"):format(fgettext("Try again"))
		return table.concat(fs)
	end

	-- Арены в два столбца: их немного, а кнопка во всю ширину выглядит как
	-- список настроек, а не как выбор карты.
	local cols = 2
	local bw = (w - 0.8 - 0.3) / cols
	local y = 2.2
	for i, mode in ipairs(state.modes) do
		local col = (i - 1) % cols
		local x = 0.4 + col * (bw + 0.3)
		local caption = ("%s\n%d / %d"):format(mode.title, mode.waiting or 0, mode.players)
		fs[#fs + 1] = ("button[%s,%s;%s,1.4;mode_%d;%s]")
			:format(x, y, bw, i, ESC(caption))
		if col == cols - 1 then
			y = y + 1.6
		end
	end

	if state.status then
		fs[#fs + 1] = ("label[0.4,%s;%s]"):format(y + 1.8, ESC(state.status))
	end
	return table.concat(fs)
end

local function button_handler(tabview, fields, name, tabdata)
	if fields.name then
		core.settings:set("name", fields.name)
	end

	if fields.region then
		for i, server in ipairs(servers()) do
			if (server.name or server.address) == fields.region then
				state.region = i
				refresh_modes()
				break
			end
		end
		return true
	end

	if fields.retry then
		refresh_modes()
		return true
	end

	if fields.leave then
		leave()
		return true
	end

	for i, mode in ipairs(state.modes or {}) do
		if fields["mode_" .. i] then
			join(mode.id)
			return true
		end
	end
	return false
end

local function on_change(type)
	if type == "ENTER" then
		mm_game_theme.set_engine()
		refresh_modes()
	end
end

--------------------------------------------------------------------------------
return {
	name = "play",
	caption = fgettext("Play"),
	cbf_formspec = get_formspec,
	cbf_button_handler = button_handler,
	on_change = on_change,
}
