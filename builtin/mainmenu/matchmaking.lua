-- Axis
-- SPDX-License-Identifier: LGPL-2.1-or-later

-- Подбор матча: выбор арены и ожидание набора.
--
-- Живёт внутри экрана «Подключиться к игре» отдельным режимом рядом со
-- списком серверов: это две дороги в одну и ту же игру, и разводить их по
-- разным углам меню незачем.
--
-- Ждут здесь же, в меню, и это осознанно. Отдельный мир-лобби пришлось бы
-- поднимать, поддерживать и переезжать из него на матч — ради экрана, на
-- котором нечего делать, кроме как смотреть на счётчик. Счётчик рисуется и
-- тут, а в игру игрок попадает сразу на готовый матч.
--
-- Меню ничего не решает само: куда подключаться, говорит Диспетчер. Пока
-- режим один и сервер один, ответ всегда одинаковый, но в тот день, когда
-- их станет много, поменяется ответ, а не клиент.

matchmaking = {}

local ESC = core.formspec_escape

local LEFT_W = 4.5
local GAP = 0.375

-- Что мы знаем от Диспетчера в последний раз.
local state = {
	region = 1,      -- какой из входов выбран
	modes = nil,     -- список арен; nil — ещё не спрашивали
	queue = nil,     -- своя очередь: room, waiting, needed, room_state
	status = nil,    -- что пошло не так, если пошло
	polling = false, -- опрос в полёте
}

local function servers()
	if not serverlistmgr.servers then
		serverlistmgr.sync()
	end
	return serverlistmgr.servers or {}
end

local function entry()
	return servers()[state.region]
end

local function dispatch_url()
	local server = entry()
	return server and server.dispatch or core.settings:get("matchmaking_url")
end

local function player_region()
	local server = entry()
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
		callback({ error = "no server" })
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
		local body = decode(res)
		if body then
			state.modes = body.modes or {}
			state.status = nil
		else
			state.modes = {}
			state.status = fgettext("Matchmaking is unavailable")
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
		local body = decode(res)

		if not body then
			-- Пропавшая очередь — не беда: Диспетчер мог перезапуститься,
			-- и тогда стоять в ней больше негде.
			state.queue = nil
			state.status = fgettext("Matchmaking is unavailable")
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
		core.event_handler("Refresh")
		return
	end
	request("/v1/join", core.write_json({
		player = name,
		mode = mode,
		region = player_region(),
	}), function(res)
		local body = decode(res)
		if not body then
			state.status = fgettext("Matchmaking is unavailable")
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

-- Левая карточка: кто и откуда играет.
local function side_card(ox, oy, h)
	local x, w = ox + 0.375, LEFT_W - 0.75
	local fs = {
		menu_style.surface(ox, oy, LEFT_W, h),
		menu_style.heading(x, oy + 0.175, w, 0.6, fgettext("Region")),
	}

	local names = {}
	for _, server in ipairs(servers()) do
		local label = server.name or server.address
		if server.ping then
			label = ("%s   %d ms"):format(label, math.floor(server.ping * 1000))
		end
		names[#names + 1] = ESC(label)
	end

	local y = oy + 0.85
	if #names > 0 then
		fs[#fs + 1] = ("dropdown[%f,%f;%f,0.8;region;%s;%d;true]")
			:format(x, y, w, table.concat(names, ","), state.region)
	else
		fs[#fs + 1] = menu_style.caption(x, y, w, 0.6, fgettext("No servers"))
	end
	y = y + 1.15

	fs[#fs + 1] = menu_style.divider(x, y, w)
	y = y + menu_style.SPACE.lg

	fs[#fs + 1] = menu_style.heading(x, y, w, 0.6, fgettext("Name"))
	fs[#fs + 1] = menu_style.inset(x, y + 0.7, w, 0.8)
	fs[#fs + 1] = ("field[%f,%f;%f,0.8;name;;%s]")
		:format(x + 0.1, y + 0.7, w - 0.2, ESC(player_name()))
	fs[#fs + 1] = "field_close_on_enter[name;false]"

	return table.concat(fs)
end

-- Правая карточка, пока ждём набора.
local function waiting_card(x, y, w, h)
	local q = state.queue
	local cx = x + 0.5
	local fs = {
		menu_style.surface(x, y, w, h),
		menu_style.title(cx, y + h * 0.28, w - 1, 0.9,
			ESC(q.title or q.mode or "")),
	}

	local line
	if q.room_state == "warming" then
		line = fgettext("Everyone is here. Preparing the arena")
	else
		line = ("%s  %d / %d"):format(fgettext("Waiting for players"),
			q.waiting or 0, q.needed or 0)
	end
	fs[#fs + 1] = menu_style.body(cx, y + h * 0.28 + 1.0, w - 1, 0.6, ESC(line))

	fs[#fs + 1] = ("button[%f,%f;3.2,0.8;leave;%s]")
		:format(cx, y + h - 1.4, fgettext("Cancel"))
	return table.concat(fs)
end

-- Правая карточка, когда выбирают арену.
local function arenas_card(x, y, w, h)
	local fs = {
		menu_style.surface(x, y, w, h),
		menu_style.heading(x + 0.375, y + 0.175, w - 0.75, 0.6, fgettext("Arenas")),
	}

	if not state.modes then
		fs[#fs + 1] = menu_style.caption(x + 0.375, y + 1.0, w - 0.75, 0.6,
			fgettext("Loading..."))
		return table.concat(fs)
	end

	if #state.modes == 0 then
		fs[#fs + 1] = menu_style.body(x + 0.375, y + 1.0, w - 0.75, 0.6,
			ESC(state.status or fgettext("No arenas available")))
		fs[#fs + 1] = ("button[%f,%f;3.2,0.8;retry;%s]")
			:format(x + 0.375, y + 1.9, fgettext("Try again"))
		return table.concat(fs)
	end

	-- Арены плитками: их немного, а кнопка во всю ширину читается как строка
	-- списка настроек, а не как выбор карты.
	local cols = 3
	local bw = (w - 0.75 - 0.3 * (cols - 1)) / cols
	local bh = 1.5
	local ox, oy = x + 0.375, y + 0.95

	for i, mode in ipairs(state.modes) do
		local col = (i - 1) % cols
		local row = math.floor((i - 1) / cols)
		local bx, by = ox + col * (bw + 0.3), oy + row * (bh + 0.3)
		if i == 1 then
			fs[#fs + 1] = menu_style.accent("mode_1")
		end
		-- Счётчик — второй строкой самой кнопки, а не подписью рядом: у
		-- акцентной плитки своя заливка, и приглушённый текст на ней тонет.
		fs[#fs + 1] = ("button[%f,%f;%f,%f;mode_%d;%s]")
			:format(bx, by, bw, bh, i, ESC(("%s\n%d / %d")
				:format(mode.title, mode.waiting or 0, mode.players)))
	end

	if state.status then
		fs[#fs + 1] = menu_style.caption(x + 0.375, y + h - 0.75, w - 0.75, 0.5,
			ESC(state.status))
	end
	return table.concat(fs)
end

---Нарисовать подбор матча в прямоугольнике (x, y, w, h).
function matchmaking.get_formspec(x, y, w, h)
	-- Первый показ: списка арен ещё нет и никто его не просил.
	if not state.modes and not state.asked then
		state.asked = true
		refresh_modes()
	end
	local rx = x + LEFT_W + GAP
	local rw = w - LEFT_W - GAP

	local fs = { side_card(x, y, h) }
	if state.queue then
		fs[#fs + 1] = waiting_card(rx, y, rw, h)
	else
		fs[#fs + 1] = arenas_card(rx, y, rw, h)
	end
	return table.concat(fs)
end

---Разобрать нажатия. true — своё, разобрали.
function matchmaking.handle(fields)
	if fields.name then
		core.settings:set("name", fields.name)
	end

	-- Выпадающий список приходит с каждым событием, не только со своим:
	-- проверять его надо на смену, иначе он съедает нажатия на арены.
	local picked = tonumber(fields.region)
	if picked and picked ~= state.region and servers()[picked] then
		state.region = picked
		state.modes = nil
		state.asked = true
		refresh_modes()
		return true
	end

	if fields.retry then
		state.modes = nil
		state.asked = true
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

---Экран открыли: список арен мог измениться, пока нас не было.
function matchmaking.on_enter()
	state.modes = nil
	state.asked = true
	refresh_modes()
end

return matchmaking
