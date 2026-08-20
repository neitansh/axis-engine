-- Luanti
-- Copyright (C) 2020 rubenwardy
-- SPDX-License-Identifier: LGPL-2.1-or-later

serverlistmgr = {
	-- continent code we detected for ourselves
	my_continent = nil,

	-- Все входы, какие есть: по ним ходит подбор матча и по ним ищется
	-- вписанный руками адрес.
	servers = nil,
	-- Что лаунчер намерил при запуске: каким входом он ходит сам («ru», «eu»)
	-- и какие входы вообще годны. Забракованный вход отвечает не отказом, а
	-- молчанием до срока — ходить туда клиенту незачем.
	entry = nil,
	usable = nil,

	-- Те из них, что показываются во вкладке серверов. Официальный Salvo сюда
	-- не попадает: он лобби для подбора матча, и попадают туда через «Матчи»,
	-- получив адрес конкретного матча. Строка в списке звала бы игрока в
	-- лобби, где играть не во что.
	shown = nil,
}

do
	if check_cache_age("geoip_last_checked", 3600) then
		local tmp = cache_settings:get("geoip") or ""
		if tmp:match("^[A-Z][A-Z]$") then
			serverlistmgr.my_continent = tmp
		end
	end
end

-- The public Luanti list is not used: Axis only talks to its own servers.
--
-- Список приходит из реестра, и приходит **через лаунчер**, а не напрямую.
-- Причин две, и обе не про удобство. До реестра надо дойти тем входом, который
-- лаунчер выбрал замером, — клиент про входы ничего не знает и знать не может,
-- пока не получил список. И адрес реестра клиенту неоткуда взять: раз его нет
-- в настройках, его нельзя и подменить настройкой.
--
-- Дверца лаунчера отдаёт список по тому же ключу, что и билеты. Ключ приходит
-- окружением на один запуск; адрес дверцы — в командной строке.
local ping_in_flight = false
local list_in_flight = false

-- Запросить список у дверцы. Всё, что нужно ответу, передаётся внутрь: в
-- асинхронном состоянии нет ни настроек, ни gettext.
local function ask_for_servers(url, key, done)
	core.handle_async(function(p)
		local http = core.get_http_api()
		if not http then
			return { trouble = "no_http" }
		end
		local res = http.fetch_sync({
			url = p.url .. "/servers",
			method = "GET",
			timeout = 10,
			extra_headers = { "Authorization: Bearer " .. p.key },
		})
		if not res.succeeded then
			return { trouble = "silent" }
		end
		local body = res.data and core.parse_json(res.data) or nil
		if res.code ~= 200 or type(body) ~= "table" or type(body.servers) ~= "table" then
			return { trouble = "refused" }
		end
		-- Вместе со списком лаунчер сообщает, каким входом он сам ходит: он
		-- померил их при запуске, и мерить те же адреса ещё раз незачем.
		return { servers = body.servers, entry = body.entry, usable = body.usable }
	end, { url = url, key = key }, done)
end

-- Развернуть записи реестра в строки списка.
--
-- В реестре запись — это сервер, а входов у него несколько: Россия и Европа —
-- две дороги к одному и тому же `salvo-official`, а не два сервера. В списке
-- же строка — это вход: игрок выбирает, какой дорогой идти, а подбор матча
-- ходит к Диспетчеру именно этого входа.
--
-- Поэтому `id` у строк одного сервера общий: билет выписывается на сервер, и
-- взятый через одну дорогу годен на другой.
local function unfold(servers)
	local rows = {}
	for _, server in ipairs(servers) do
		if type(server) == "table" and type(server.entrances) == "table" then
			for _, entrance in ipairs(server.entrances) do
				-- Запись без входа никуда не ведёт: строка в списке была бы, а
				-- нажатие на неё не значило бы ничего. Реестр такого не
				-- выписывает, но список приходит снаружи, и верить ему на слово
				-- незачем.
				if type(entrance) == "table" and entrance.address and
						tonumber(entrance.port) then
					rows[#rows + 1] = {
						id = server.id,
						name = ("%s · %s"):format(server.name or "", entrance.name or ""),
						-- Имя самого места, без входа: в списке нужна строка
						-- целиком, а тем, кто показывает место наружу, — только
						-- оно. Склеенное обратно не разберёшь: точка с
						-- пробелами бывает и в самом имени.
						place = server.name or "",
						address = entrance.address,
						port = tonumber(entrance.port),
						-- Куда вкладка «Играть» ходит за очередями и адресами
						-- матчей и каким входом отсюда виден матч: до Германии
						-- из России доходит не всё, и путь туда идёт через свой
						-- узел.
						dispatch = entrance.dispatch,
						region = entrance.region,
						-- На этом порту Диспетчер отвечает на игровой запрос
						-- сведений. Порт из того же проброшенного диапазона,
						-- что и матчи, поэтому ответ отсюда означает, что и до
						-- матча этой дорогой дойдёт.
						probe_port = tonumber(entrance.probe_port),
						-- Показывать ли эту строку во вкладке серверов.
						-- Умолчание — не показывать: реестр говорит об этом
						-- прямо, и молчание значит «нет».
						listed = server.listed == true,
						-- Чем сервер занят: лобби подбора матчей или обычный
						-- игровой. Реестр говорит это прямо, и гадать по
						-- наличию Диспетчера больше не нужно — раньше из-за
						-- такого гадания обычный сервер попадал в матчи.
						role = server.role or "server",
					}
				end
			end
		end
	end
	return rows
end

-- Round trip time to each official server, measured off the main thread so the
-- menu keeps drawing while the packets are out.
local function measure_pings()
	if ping_in_flight or not core.handle_async or not core.ping_server then
		return
	end
	ping_in_flight = true

	-- Меряются только показываемые: отклик нужен строке списка, а у скрытого
	-- сервера строки нет. Стучаться к нему значило бы слать пакеты ради числа,
	-- которого никто не увидит.
	--
	-- Чей это замер. Ответы приходят по номеру строки, а список за это время
	-- мог смениться — тогда отклик Европы лёг бы в строку России. Список
	-- меняется целиком, новой таблицей, поэтому достаточно сверить её.
	local measured = serverlistmgr.shown or {}

	local targets = {}
	for i, server in ipairs(measured) do
		targets[i] = { address = server.address, port = server.probe_port or server.port }
	end

	core.handle_async(function(list)
		local result = {}
		if not core.ping_server then
			return result
		end

		for i, target in ipairs(list) do
			result[i] = core.ping_server(target.address, target.port, 2000)
		end
		return result
	end, targets, function(result)
		ping_in_flight = false
		if not result or serverlistmgr.shown ~= measured then
			return
		end

		for i, info in pairs(result) do
			local server = measured[i]
			if server and type(info) == "table" then
				-- The list wants seconds, the engine reports milliseconds
				server.ping = info.ping and info.ping / 1000 or nil
				server.clients = info.clients
				server.clients_max = info.clients_max
			end
		end
		core.event_handler("Refresh")
	end)
end

function serverlistmgr.sync()
	local url = core.settings:get("axis_ticket_url") or ""
	local key = core.settings:get("axis_ticket_key") or ""
	if url == "" or key == "" then
		-- Клиент запустили без лаунчера. Список остаётся пустым, и это не
		-- поломка: билета без лаунчера тоже не будет, а сервер, который его
		-- спрашивает, никого без билета не пустит. Показать такую строку значит
		-- позвать игрока туда, откуда его выставят. Своя игра и прямой адрес
		-- работают как работали — им реестр не нужен.
		serverlistmgr.servers = {}
		serverlistmgr.shown = {}
		return
	end

	-- Список уже спрошен: второй запрос дал бы второй ответ, и какой из них
	-- ляжет последним, решала бы сеть.
	if list_in_flight then
		return
	end
	list_in_flight = true

	-- До ответа список остаётся тем, что был: подменять его пустым значит
	-- моргнуть пустой вкладкой на каждое обновление.
	serverlistmgr.servers = serverlistmgr.servers or {}
	serverlistmgr.shown = serverlistmgr.shown or {}

	ask_for_servers(url, key, function(answer)
		list_in_flight = false
		if answer.trouble then
			-- Молчание лаунчера не повод стирать список: он мог быть верным.
			-- А если списка ещё не было, пустая вкладка и есть правда — идти
			-- некуда, пока реестр не ответит.
			core.log("warning", "[serverlist] лаунчер не дал список: " .. answer.trouble)
			core.event_handler("Refresh")
			return
		end

		serverlistmgr.servers = unfold(answer.servers)
		serverlistmgr.entry = answer.entry
		serverlistmgr.usable = answer.usable

		-- В список серверов идёт только то, что им и является. Лобби подбора
		-- матчей туда не попадает ни при каких настройках: строка в списке
		-- звала бы игрока туда, где играть не во что, — играют на арене, а
		-- адрес её выдаёт Диспетчер.
		local shown = {}
		for _, server in ipairs(serverlistmgr.servers) do
			if server.listed and server.role ~= "matches" then
				shown[#shown + 1] = server
			end
		end
		serverlistmgr.shown = shown

		measure_pings()
		core.event_handler("Refresh")
	end)
end

-- Избранного здесь нет. Список приходит из реестра, и все серверы в нём наши;
-- отмечать среди своих же серверов любимые незачем, а хранить их отдельным
-- файлом — тем более. Вместе с избранным ушёл и разбор старого списка Luanti.
--------------------------------------------------------------------------------
--- На какой сервер реестра ведёт этот адрес.
---
--- Билет выписывается на сервер из реестра, а не на адрес: адресов у одного
--- сервера бывает несколько — Россия и Европа это две дороги к одному и тому
--- же. Введённый руками адрес ищется здесь; не нашёлся — билета не будет, и
--- игрок узнаёт об этом в меню, а не получает невнятный отказ сервера.
---
--- Ищется только среди показываемых. Скрытый сервер скрыт не наполовину: он
--- лобби для подбора матча, и попасть туда, вписав его адрес руками, — обход
--- того самого подбора. Строки в списке нет, и билета по адресу тоже нет.
function serverlistmgr.id_of(address, port)
	port = tonumber(port)
	for _, server in ipairs(serverlistmgr.shown or {}) do
		if server.address == address and tonumber(server.port) == port then
			return server.id
		end
	end
	return nil
end
