-- Axis
-- SPDX-License-Identifier: LGPL-2.1-or-later
-- Copyright (C) 2026 the Axis contributors

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

-- Опрос живёт, пока экран на виду. Уйти с него можно как угодно — кнопкой
-- режима, вкладкой, «назад» со стартовой страницы, — и ловить каждый способ
-- по отдельности значит однажды пропустить новый. Поэтому признак простой:
-- экран рисуется. Перестал рисоваться — очередь никого не интересует.
local SHOWN_FOR = 3

-- Что мы знаем от Диспетчера в последний раз.
local state = {
	region = 1,      -- какой из входов выбран
	modes = nil,     -- список арен; nil — ещё не спрашивали
	queue = nil,     -- своя очередь: room, waiting, needed, room_state
	pass = nil,      -- пропуск: им доказывается право на своё место в очереди
	status = nil,    -- что пошло не так, если пошло
	polling = false, -- опрос в полёте
	drawn = 0,       -- когда экран рисовали в последний раз
	epoch = 0,       -- ответы прошлых заходов нам не нужны
	bad = {},        -- входы, чей Диспетчер не отозвался
}

local function servers()
	if not serverlistmgr.servers then
		serverlistmgr.sync()
	end
	return serverlistmgr.servers or {}
end

--- Выбор входа --------------------------------------------------------------
-- Вход не спрашивают у игрока. Он и не может это решить: какой узел ближе и
-- через какой вообще дойдёт до матча — вопрос сети, а не вкуса, и ошибка в
-- ответе разводит друзей по разным матчам.
--
-- Решает замер, и меряет он не «дошёл ли пакет». Одиночный пакет — плохой
-- свидетель: там, где фильтруют, первый обмен обычно проходит, а поток за ним
-- гаснет, и путь, не тянущий полноразмерные пакеты, ломается только на
-- настоящих данных. Игра и застревает там же — на приёме содержимого, а не на
-- рукопожатии. Поэтому клиент просит очередь полноразмерных пакетов и считает,
-- сколько дошло. Годным считается вход, добравший очередь почти целиком;
-- из годных берётся самый быстрый.
--
-- Стоит это двух обменов и полусотни килобайт на вход — раз на открытие
-- экрана.

local BURST = 48        -- пакетов в очереди, по 1200 байт
local PROBE_TIMEOUT = 3000
local DELIVERED = 0.9   -- меньше — путь рвётся, играть на нём нечего
-- Сколько замер считается свежим. Сеть за полминуты не переделывается, а
-- Диспетчер отсекает слишком частые просьбы — и отказ выглядел бы как обрыв.
local PROBE_FRESH = 30

-- Что показала проверка каждого входа: { ping, got, want }.
local link = {}
local probed_at = 0
local probing = false

local function delivery(i)
	local r = link[i]
	if not r or not r.want or r.want == 0 then
		return nil
	end
	return (r.got or 0) / r.want
end

-- Годится ли вход: очередь дошла, и его Диспетчер нас не подводил.
local function usable(i, server)
	local part = delivery(i)
	return server.dispatch and part and part >= DELIVERED
		and not state.bad[server.dispatch]
end

local function pick_entry()
	-- Пока ждём набора, вход не меняем ни при каких замерах: очередь стоит
	-- у него, и переезд посреди ожидания — это выход из очереди.
	if state.queue then
		return
	end
	local best
	for i, server in ipairs(servers()) do
		if usable(i, server) and
				(not best or (link[i].ping or 0) < (link[best].ping or 0)) then
			best = i
		end
	end
	if not best then
		-- Ни один вход не прошёл проверку целиком. Отказывать игроку из-за
		-- этого нельзя — берём тот, где потери меньше, а если не мерили ещё,
		-- то первый годный, чтобы экран не пустовал.
		local most
		for i, server in ipairs(servers()) do
			if server.dispatch and not state.bad[server.dispatch] then
				local part = delivery(i)
				if part and (not most or part > delivery(most)) then
					most = i
				end
				best = best or i
			end
		end
		best = most or best
	end
	state.region = best or 1
end

-- Известно ли уже, каким входом идти. Пока нет — арены не показываются и
-- список их не спрашивается: нажатие ушло бы к тому Диспетчеру, который мы
-- как раз собираемся забраковать, и игрок оказался бы в матче не там, где
-- оказался бы через секунду.
local function decided()
	local own = core.settings:get("matchmaking_url")
	if own and own ~= "" then
		return true -- свой Диспетчер, выбирать не из чего
	end
	if not core.probe_link then
		return true -- проверять нечем
	end
	-- Список приходит от лаунчера и приходит не мгновенно. Пока его нет,
	-- решать не из чего: показать «арен нет» в эту секунду значит соврать —
	-- через мгновение вход появится, и арены вместе с ним.
	return not probing and probed_at > 0
end

---Проверить входы. Замер идёт в стороне: главное не имеет права стоять.
local function probe_entries()
	if probing or not core.probe_link then
		return
	end
	if probed_at > 0 and os.time() - probed_at < PROBE_FRESH then
		return
	end
	local targets = {}
	for i, server in ipairs(servers()) do
		if server.dispatch and server.probe_port then
			targets[#targets + 1] = {
				i = i, address = server.address, port = server.probe_port,
			}
		end
	end
	if #targets == 0 then
		-- Мерить нечего — но и решить нечего. Отметить это как сделанный замер
		-- значит объявить решённым выбор из пустоты: список ещё едет от
		-- лаунчера, и через мгновение входы появятся.
		return
	end

	probing = true
	core.handle_async(function(p)
		local out = {}
		for _, t in ipairs(p.targets) do
			out[t.i] = core.probe_link(t.address, t.port, p.burst, p.wait)
		end
		return out
	end, { targets = targets, burst = BURST, wait = PROBE_TIMEOUT }, function(out)
		probing = false
		link = out or {}
		probed_at = os.time()
		pick_entry()

		-- В журнал — чтобы на вопрос «почему меня увело в Германию» был
		-- ответ, а не догадка.
		for i, server in ipairs(servers()) do
			local r = link[i]
			core.log("action", ("[matchmaking] %s: %s%s"):format(
				server.name or server.address,
				r and ("дошло %d из %d, отклик %d мс")
					:format(r.got or 0, r.want or 0, r.ping or 0)
					or "не отвечает",
				i == state.region and " — выбран" or ""))
		end
		core.event_handler("Refresh")
	end)
end

local function entry()
	return servers()[state.region]
end

-- Настройка сильнее списка: пустой она бывает у всех, а заполняют её те, кто
-- поднял своего Диспетчера или проверяет его на стенде, — и им нужен именно он,
-- а не официальный вход.
-- На какой сервер выписывать билет. Имя берётся у выбранного входа, а не
-- вписывается сюда: входов два, сервер за ними один, и знать это должен список
-- серверов, а не подбор матча.
local function server_id()
	local server = entry()
	return server and server.id
end


local function dispatch_url()
	local set = core.settings:get("matchmaking_url")
	if set and set ~= "" then
		return set
	end
	local server = entry()
	return server and server.dispatch
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
		-- Отказ уходит тем же путём, что и настоящий ответ, а не сразу.
		-- Ответить сразу значит ответить раньше, чем спрашивавший договорил:
		-- зовут это и с ещё не построенного экрана, а обновлять там нечего.
		core.handle_async(function() return { error = "no server" } end, {}, callback)
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
		return nil, "unreachable"
	end
	local body = core.parse_json(res.data or "")
	if res.code == 404 then
		-- «Тебя тут нет» — обычный ответ, а не поломка: очередь могла
		-- разойтись, пока мы отворачивались.
		return nil, "gone"
	end
	if type(body) ~= "table" then
		return nil, "unreachable"
	end
	if res.code ~= 200 then
		return nil, "unreachable"
	end
	return body
end

local function on_screen()
	return os.time() - state.drawn <= SHOWN_FOR
end

-- Список арен спрашивают снова и снова, пока экран на виду: на плитках
-- написано, сколько человек уже ждёт, и цифра эта должна быть живой — иначе
-- встаёшь в очередь к другу, а на кнопке ноль. Диспетчер придерживает
-- повторный ответ на секунду, поэтому круг идёт сам собой и никого не молотит.
local function refresh_modes(again)
	if state.asking and not again then
		return
	end
	state.asking = true
	local asked = dispatch_url()
	request("/v1/modes" .. (again and "?wait=1" or ""), nil, function(res)
		local body = decode(res)
		if body then
			state.modes = body.modes or {}
			state.status = nil
		else
			-- Замер говорил, что сюда дойдёт, а Диспетчер молчит. Больше
			-- этот вход не предлагаем и сразу пробуем следующий: игроку
			-- незачем знать, что один из узлов сегодня не в духе.
			if asked and not state.queue then
				state.bad[asked] = true
				pick_entry()
			end
			if dispatch_url() and dispatch_url() ~= asked then
				state.asking = false
				refresh_modes()
				return
			end
			state.modes = {}
			state.status = fgettext("Matchmaking is unavailable")
		end
		core.event_handler("Refresh")

		-- Пока ждём набора, список не на виду, а с экрана могли и уйти.
		if on_screen() and not state.queue then
			refresh_modes(true)
		else
			state.asking = false
		end
	end)
end

-- Диспетчер назвал адрес — дальше обычный вход на сервер, только адрес не
-- набран руками, а получен. Возвращает true, если ушли в игру.
local function enter(body)
	if not (body.address and body.port and body.port > 0) then
		return false
	end
	state.queue = nil
	-- Экрана уже нет: бросать игрока в игру мимо его воли нельзя.
	if not on_screen() then
		matchmaking.stop()
		return true
	end
	gamedata.mode = "join"
	-- На какой сервер идём. Адрес у матча свой и живёт полчаса, а билет
	-- выписывается на сервер целиком — на лобби и на все его матчи.
	gamedata.server_id = server_id()
	gamedata.address = body.address
	gamedata.port = body.port
	-- Имя не проставляется здесь: его выдаёт билет, и подставит его тот, кто
	-- билет получил (core.start). Записать сюда своё значило бы дать игроку
	-- войти под именем, которого в билете нет, — а сервер такого не пустит.
	gamedata.password = ""
	gamedata.selected_world = 0
	core.start()
	return true
end

-- Опрос очереди. Диспетчер придерживает ответ на секунду, поэтому цикл идёт
-- сам собой и никого не молотит.
local function poll()
	if state.polling or not state.queue or not on_screen() then
		return
	end
	state.polling = true
	local epoch = state.epoch

	-- Имя здесь больше не посылается: чьё это ожидание, Диспетчер знает по
	-- пропуску. Раньше хватало чужого имени, чтобы подглядеть в чужую очередь.
	request("/v1/queue", core.write_json({
		pass = state.pass,
		region = player_region(),
	}), function(res)
		state.polling = false
		-- Ответ из прошлой жизни: экран с тех пор закрыли или очередь сменили.
		if epoch ~= state.epoch then
			return
		end
		local body, why = decode(res)

		if not body then
			state.queue = nil
			-- Пропавшая очередь — не беда: за ней никто уже не стоит.
			-- А вот молчащий Диспетчер — повод сказать об этом.
			state.status = why ~= "gone" and fgettext("Matchmaking is unavailable") or nil
			core.event_handler("Refresh")
			return
		end

		state.queue = body
		if enter(body) then
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
		-- Свой пропуск, если он уже есть: смена режима не должна выглядеть
		-- как новый игрок и не должна выдавать второй пропуск.
		pass = state.pass,
	}), function(res)
		local body = decode(res)
		if not body then
			state.status = fgettext("Matchmaking is unavailable")
			core.event_handler("Refresh")
			return
		end
		-- Пропуск — единственное, чем потом доказывается право на своё место
		-- в очереди. Живёт только в памяти меню: на диск ему незачем.
		state.pass = body.pass or state.pass
		-- Диспетчер мог и не ставить в очередь: если на этом режиме уже
		-- играют и место есть, он сразу называет адрес.
		state.queue = body
		state.status = nil
		if enter(body) then
			return
		end
		core.event_handler("Refresh")
		poll()
	end)
end

local function leave()
	matchmaking.stop()
	refresh_modes()
	core.event_handler("Refresh")
end

--- Окно ---------------------------------------------------------------------

-- Левая карточка: кто и откуда играет. Вход не выбирают — его показывают:
-- решение уже принято замером, а игроку важно видеть, куда он попадёт и
-- насколько это близко.
local function side_card(ox, oy, h)
	local x, w = ox + 0.375, LEFT_W - 0.75
	local fs = {
		menu_style.surface(ox, oy, LEFT_W, h),
		menu_style.heading(x, oy + 0.175, w, 0.6, fgettext("Connection")),
	}

	local y = oy + 0.85
	local own = core.settings:get("matchmaking_url")
	local server = entry()
	if own and own ~= "" then
		-- Свой Диспетчер: замеры и списки тут ни при чём, показываем как есть.
		fs[#fs + 1] = menu_style.body(x, y, w, 0.6, ESC(own))
		y = y + 1.15
	elseif server then
		-- Пока идёт проверка, вход не называется. Назвать его заранее — значит
		-- сказать «пойдёшь отсюда» до того, как это решено: сменится он через
		-- секунду или нет, игрок уже прочитал не то.
		local part = delivery(state.region)
		if probing and not part then
			fs[#fs + 1] = menu_style.body(x, y, w, 0.6, fgettext("Checking..."))
		else
			fs[#fs + 1] = menu_style.body(x, y, w, 0.6,
				ESC(server.name or server.address))
			local note
			if not part then
				note = probed_at > 0 and fgettext("No answer") or nil
			elseif part < DELIVERED then
				-- Путь отвечает, но теряет: играть на нём можно, только зная,
				-- что рвётся не игра, а дорога до неё.
				note = fgettext("Unstable: $1% delivered",
					tostring(math.floor(part * 100)))
			else
				note = fgettext("$1 ms", tostring(math.floor(link[state.region].ping or 0)))
			end
			if note then
				fs[#fs + 1] = menu_style.caption(x, y + 0.6, w, 0.5, note)
			end
		end
		y = y + 1.15
	else
		fs[#fs + 1] = menu_style.caption(x, y, w, 0.6, fgettext("No servers"))
		y = y + 1.15
	end

	-- Поля имени здесь нет: имя приходит вместе с билетом, и сервер сверяет
	-- его с тем, что в билете записано. Дать игроку строку, куда можно печатать
	-- что угодно без всякого последствия, — обещание, которого клиент не
	-- держит.

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

	-- Про готовящуюся арену говорим, только когда комната и правда полна:
	-- греть её начинают раньше, и «все в сборе» при трёх из десяти — неправда.
	local line
	if q.room_state == "warming" and (q.waiting or 0) >= (q.needed or 0) then
		line = fgettext("Everyone is here. Preparing the arena")
	else
		line = ("%s  %d / %d"):format(fgettext("Waiting for players"),
			q.waiting or 0, q.needed or 0)
	end
	fs[#fs + 1] = menu_style.body(cx, y + h * 0.28 + 1.0, w - 1, 0.6, ESC(line))

	-- Считаем, пока Диспетчер называет секунды, и не смотрим на состояние
	-- комнаты: арену он греет заранее, а уходит она всё равно по сроку.
	-- Сказать «отправляемся» и продержать полминуты — соврать.
	local left = q.starts_in or 0
	local below
	if left > 0 then
		below = fgettext("Starts in $1 s", tostring(left))
	elseif (q.waiting or 0) < (q.min or 1) then
		below = fgettext("Waiting for more players")
	else
		below = fgettext("Starting")
	end
	if below then
		fs[#fs + 1] = menu_style.caption(cx, y + h * 0.28 + 1.7, w - 1, 0.6,
			ESC(below))
	end

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

	if not decided() then
		-- Кроме этой строки на карточке ничего нет, поэтому и стоит она
		-- посреди неё, а не в углу под заголовком.
		fs[#fs + 1] = menu_style.body(x + 0.375, y + h / 2 - 0.3, w - 0.75, 0.6,
			fgettext("Checking connection..."), "center")
		return table.concat(fs)
	end

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

---Уйти с экрана: снять себя с очереди и забыть ответы, которые ещё в пути.
function matchmaking.stop()
	state.epoch = state.epoch + 1
	if state.queue then
		state.queue = nil
		-- Снимает с очереди пропуск, а не имя: иначе уйти можно было бы за
		-- любого, назвав его ник.
		request("/v1/leave", core.write_json({ pass = state.pass }), function() end)
		state.pass = nil
	end
	state.status = nil
end

---Нарисовать подбор матча в прямоугольнике (x, y, w, h).
function matchmaking.get_formspec(x, y, w, h)
	state.drawn = os.time()
	-- Список приходит от лаунчера позже, чем открывается экран, поэтому замер
	-- заводится и отсюда: на входе в экран мерить было нечего. Сам он дешёвый —
	-- пока замер свеж или идёт, вызов ничего не делает.
	probe_entries()
	-- Замер приходит не сразу, поэтому вход пересматриваем на каждом
	-- рисовании: как только стало известно, кто ближе, экран сам переедет.
	local was = dispatch_url()
	pick_entry()
	if decided() and (not state.modes or dispatch_url() ~= was) then
		state.modes = nil
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
	if fields.retry then
		-- Ручная попытка прощает всё: если вход отвалился по случайности,
		-- второй заход по кнопке должен его вернуть.
		state.bad = {}
		state.modes = nil
		pick_entry()
		refresh_modes()
		return true
	end

	if fields.leave then
		leave()
		return true
	end

	for i, mode in ipairs(state.modes or {}) do
		if fields["mode_" .. i] then
			if decided() then
				join(mode.id)
			end
			return true
		end
	end
	return false
end

---Экран открыли: список арен мог измениться, пока нас не было.
function matchmaking.on_enter()
	state.drawn = os.time()
	state.modes = nil
	-- Замер за прошлый заход мог устареть, а вход, объявленный плохим, —
	-- починиться: сеть меняется чаще, чем открывают меню.
	state.bad = {}
	serverlistmgr.sync()
	probe_entries()
	pick_entry()
	if decided() then
		refresh_modes()
	end
end

return matchmaking
